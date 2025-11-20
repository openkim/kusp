import signal
import socket
import struct
import time
from typing import Callable, Optional, Tuple

import click
import numpy as np
from loguru import logger

from .utils import load_kusp_callable, recv_exact


class IPProtocol:
    """Serve KUSP models over a TCP socket."""

    _sigint_window_sec = 2.0

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 12345,
        max_connections: int = 1,
        reuse_address: bool = True,
        recv_timeout_s: float = 15.0,
        send_timeout_s: float = 15.0,
        max_atoms: int = 1_000_000_000,
        *,
        model_file: Optional[str] = None,
        init_kwargs: Optional[dict] = None,
    ) -> None:
        """Configure protocol behavior.

        Args:
            host: Interface to bind.
            port: TCP port to bind.
            max_connections: Maximum simultaneous backlog.
            reuse_address: Whether to reuse a recently closed port.
            recv_timeout_s: Socket timeout while receiving payloads.
            send_timeout_s: Socket timeout while sending responses.
            max_atoms: Hard upper bound on atoms accepted from clients.
            model_file: Optional path to a decorated KUSP model.
            init_kwargs: Keyword arguments forwarded to the model constructor.
        """
        self.host = host
        self.port = port
        self.max_connections = max_connections
        self.reuse_address = reuse_address
        self.recv_timeout_s = recv_timeout_s
        self.send_timeout_s = send_timeout_s
        self.max_atoms = max_atoms

        self.server_socket: Optional[socket.socket] = None
        self._running = False

        self._reload_requested = False
        self._shutdown_requested = False
        self._last_sigint_ts: float = 0.0

        self._handler: Optional[
            Callable[
                [np.ndarray, np.ndarray, Optional[np.ndarray]],
                Tuple[np.ndarray, np.ndarray],
            ]
        ] = None

        self._model_file = model_file
        self._init_kwargs = dict(init_kwargs or {})

    def _install_sigint_handler(self) -> None:
        """Register Ctrl-C handling for reload/shutdown semantics."""

        def _on_sigint(_signum, _frame):
            now = time.monotonic()
            if now - self._last_sigint_ts <= self._sigint_window_sec:
                self._shutdown_requested = True
                click.echo(
                    click.style(
                        f"[KUSP] Two Ctrl-C within {self._sigint_window_sec:.1f}s; SHUTTING DOWN.",
                        fg="red",
                        bold=True,
                    )
                )
            else:
                self._reload_requested = True
                click.echo(
                    click.style(
                        f"[KUSP] Reloading the model ... (to shutdown press Ctrl-C twice within {self._sigint_window_sec:.1f} sec).",
                        fg="green",
                        bold=True,
                    )
                )
            self._last_sigint_ts = now

        signal.signal(signal.SIGINT, _on_sigint)

    def _maybe_reload(self) -> None:
        """Reload the configured model if a reload was requested."""
        if not self._reload_requested:
            return
        self._reload_requested = False
        if not self._model_file:
            logger.warning(
                "Reload requested but no model file configured; ignoring."
            )
            return
        try:
            self._handler = load_kusp_callable(
                self._model_file, init_kwargs=self._init_kwargs
            )
            click.secho(f"[KUSP] {self._model_file} reloaded", fg="green", bold=True)
            logger.info(f"Reload successful from {self._model_file}")
        except Exception as exc:
            logger.exception(
                f"Reload failed; keeping previous handler. Error: {exc}"
            )
            click.secho(f"[KUSP] Failed to reload {self._model_file}", fg="red", bold=True)

    def start(self, on_ready: Optional[Callable[[], None]] = None) -> None:
        """Bind the listening socket and start accepting clients.

        Args:
            on_ready: Optional callback invoked once the server is bound.
        """
        if self.server_socket is not None:
            return

        server_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        if self.reuse_address:
            server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        server_socket.settimeout(0.5)  # poll reload/shutdown between accepts
        server_socket.bind((self.host, self.port))
        server_socket.listen(self.max_connections)

        self.server_socket = server_socket
        self._running = True
        logger.debug(f"KUSP TCP server listening on {self.host}:{self.port}")
        click.echo(
            click.style(
                f"[KUSP] TCP server listening on {self.host}:{self.port}",
                bold=True,
                italic=True,
                fg="green",
            )
        )

        self._install_sigint_handler()

        if on_ready is not None:
            on_ready()

    def stop(self) -> None:
        """Shut down the server socket and restore the default SIGINT handler."""
        self._running = False
        if self.server_socket is not None:
            try:
                self.server_socket.close()
                logger.info("KUSP TCP server stopped")
            finally:
                self.server_socket = None
        try:
            signal.signal(signal.SIGINT, signal.default_int_handler)
        except Exception:
            pass

    def serve(self, handler: Optional[Callable] = None) -> None:
        """Run the main accept/response loop.

        Args:
            handler: Optional callable overriding the configured model.

        Raises:
            RuntimeError: If `start` has not been called or no handler exists.
        """
        if self.server_socket is None:
            raise RuntimeError("IPProtocol.start must be called before serve.")

        if handler is not None:
            self._handler = handler
        elif self._model_file:
            self._handler = load_kusp_callable(
                self._model_file, init_kwargs=self._init_kwargs
            )
        else:
            raise RuntimeError(
                "No handler provided and no model_file configured."
            )

        fmt_map = {4: ("i", np.int32), 8: ("q", np.int64)}

        try:
            while self._running and not self._shutdown_requested:
                self._maybe_reload()

                try:
                    client_socket, client_address = self.server_socket.accept()
                except socket.timeout:
                    continue
                except OSError:
                    if not self._running or self._shutdown_requested:
                        break
                    raise

                with client_socket:
                    client_socket.settimeout(self.recv_timeout_s)
                    logger.info(f"Client connected from {client_address}")

                    while self._running and not self._shutdown_requested:
                        self._maybe_reload()

                        try:
                            header = recv_exact(client_socket, 4)
                            logger.debug(f"Received header: {header}")
                        except ConnectionError:
                            logger.debug(
                                f"Client {client_address} disconnected (no header)."
                            )
                            break

                        try:
                            int_width = struct.unpack("i", header)[0]
                            logger.debug(f"Received int_width: {int_width}")
                        except struct.error as exc:
                            logger.warning(
                                f"Malformed int-width header from {client_address}: {exc}"
                            )
                            break

                        if int_width not in fmt_map:
                            logger.warning(
                                f"Unsupported integer width {int_width} from {client_address}"
                            )
                            break

                        int_fmt, int_type = fmt_map[int_width]

                        try:
                            n_atoms_bytes = recv_exact(client_socket, int_width)
                            n_atoms = struct.unpack(int_fmt, n_atoms_bytes)[0]
                            logger.debug(f"Received n_atoms: {n_atoms}")
                        except (ConnectionError, struct.error) as exc:
                            logger.warning(
                                f"Failed reading n_atoms from {client_address}: {exc}"
                            )
                            break

                        if n_atoms <= 0 or n_atoms > self.max_atoms:
                            logger.warning(
                                f"Invalid n_atoms={n_atoms} from {client_address}; closing."
                            )
                            break

                        numbers_nbytes = int_width * n_atoms
                        coords_nbytes = 8 * 3 * n_atoms
                        contrib_nbytes = int_width * n_atoms

                        try:
                            numbers_bytes = recv_exact(
                                client_socket, numbers_nbytes
                            )
                            coords_bytes = recv_exact(
                                client_socket, coords_nbytes
                            )
                            contributing_bytes = recv_exact(
                                client_socket, contrib_nbytes
                            )
                        except ConnectionError as exc:
                            logger.warning(
                                f"Client {client_address} disconnected mid-payload: {exc}"
                            )
                            break

                        try:
                            Z = np.frombuffer(
                                numbers_bytes, dtype=int_type
                            ).copy()
                            R = (
                                np.frombuffer(coords_bytes, dtype=np.float64)
                                .copy()
                                .reshape((n_atoms, 3))
                            )
                            M = np.frombuffer(
                                contributing_bytes, dtype=int_type
                            ).copy()
                            logger.debug(f"Received Arrays:\n{Z}\n{R}\n{M}")
                        except ValueError as exc:
                            logger.warning(
                                f"Shape/dtype error from {client_address}: {exc}"
                            )
                            break

                        logger.debug(
                            f"Received arrays for species, positions, contributing particles:\n{Z}\n{R}\n{M}"
                        )

                        t0 = time.perf_counter()
                        try:
                            current = self._handler
                            if current is None:
                                raise RuntimeError(
                                    "No active handler available"
                                )
                            energy, forces = current(Z, R, M)
                            logger.debug(f"Energy: {energy}, Forces: {forces}")
                        except Exception as exc:
                            logger.exception(
                                f"KUSP handler raised an exception: {exc}"
                            )
                            break
                        t1 = time.perf_counter()
                        elapsed_ms = (t1 - t0) * 1000.0

                        try:
                            client_socket.settimeout(self.send_timeout_s)
                            client_socket.sendall(energy.tobytes())
                            client_socket.sendall(forces.tobytes())
                        except (socket.timeout, OSError) as exc:
                            logger.warning(
                                f"Send failed to {client_address}: {exc}"
                            )
                            break

                        logger.info(
                            f"Evaluated N={n_atoms} in {elapsed_ms:.2f} ms"
                        )

                logger.debug(f"Connection from {client_address} closed.")

        finally:
            self.stop()
