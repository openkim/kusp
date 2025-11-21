import os
import sys
from pathlib import Path
from typing import Optional, Tuple

import click
import yaml
from loguru import logger

from kusp.io import IPProtocol
from .kim import (
    install_kim_driver,
    install_kim_model,
    package_model_for_deployment,
    remove_kim_driver,
    remove_kim_model,
)
from .utils import (
    resolve_config_path,
    write_or_update_config,
)


def _cli_message(message: str, *, fg: str = "cyan", bold: bool = False) -> None:
    """Emit a consistently formatted CLI message. Impportant stuff only"""
    click.secho(f"[KUSP] [CLI] {message}", fg=fg, bold=bold)


@click.group(
    help="KUSP utilities.",
    context_settings={"ignore_unknown_options": True},
)
@click.option("-v", count=True, help="Increase verbosity (-v, -vv).")
def cli(v: int):
    """Entry point for the `kusp` CLI.

    Args:
        v: Verbosity flag count.
    """
    if v >= 2:
        logger.remove()
        logger.add(sys.stderr, level="DEBUG")
    elif v == 1:
        logger.remove()
        logger.add(sys.stderr, level="INFO")
    else:
        logger.remove()
        logger.add(sys.stderr, level="WARNING")


@cli.command("install", help="Install the bundled KUSP KIM model or driver.")
@click.argument("item", type=click.Choice(["model", "driver"], case_sensitive=False))
@click.option("--collection", default="user", show_default=True)
@click.option(
    "--installer",
    default="kim-api-collections-management",
    show_default=True,
)
def cmd_install(item: str, collection: str, installer: str) -> None:
    """Install the embedded KUSP KIM model or driver.

    Args:
        item: What to install: `model` or `driver`.
              The model is used as a client in the KUSP server implementation,
              while the driver uses the C++ implementation.
        collection: KIM collection destination.
        installer: Installer executable to invoke, usually
                   `kim-api-collections-management` or `kimitems`.
    """
    item = item.lower()

    if item == "model":
        logger.info("Installing KUSP model.")
        install_kim_model(collection=collection, installer=installer)
        _cli_message("Installed KUSP KIM model.", fg="green", bold=True)
    elif item == "driver":
        logger.info("Installing KUSP driver.")
        install_kim_driver(collection=collection, installer=installer)
        _cli_message("Installed KUSP KIM driver.", fg="green", bold=True)
    else:
        logger.error("Unknown installation type.")
        raise TypeError(f"Unknown installation type: {item}")


@cli.command("remove", help="Remove the bundled KUSP KIM model or driver.")
@click.argument("item", type=click.Choice(["model", "driver"], case_sensitive=False))
@click.option(
    "--installer",
    default="kim-api-collections-management",
    show_default=True,
)
def cmd_remove(item: str, installer: str) -> None:
    """Install the embedded KUSP KIM model or driver.

    Args:
        item: What to install: `model` or `driver`.
              The model is used as a client in the KUSP server implementation,
              while the driver uses the C++ implementation.
        installer: Installer executable to invoke, usually
                   `kim-api-collections-management` or `kimitems`.
    """
    item = item.lower()

    if item == "model":
        logger.info("Removing KUSP model.")
        remove_kim_model(installer=installer)
        _cli_message("Removed KUSP KIM model.", fg="yellow")
    elif item == "driver":
        logger.info("Removing KUSP driver.")
        remove_kim_driver(installer=installer)
        _cli_message("Removed KUSP KIM driver.", fg="yellow")
    else:
        logger.error("Unknown installation type.")
        raise TypeError(f"Unknown installation type: {item}")


@cli.command("serve", help="Run a TCP KUSP server from a decorated model file.")
@click.argument(
    "file", type=click.Path(exists=True, dir_okay=False, path_type=Path)
)
@click.option("--host", default="127.0.0.1", show_default=True)
@click.option("--port", default=12345, show_default=True, type=int)
@click.option("--max-connections", default=1, show_default=True, type=int)
@click.option("--recv-timeout", default=15.0, show_default=True, type=float)
@click.option("--send-timeout", default=15.0, show_default=True, type=float)
@click.option(
    "--kusp-config",
    "kusp_config",
    default=None,
    type=click.Path(dir_okay=False, path_type=Path),
    help="Path to write the YAML config. Defaults to temp file. Priority order:"
    "1. --kusp-config > 2. explicit --host/port > 3. ENV[KUSP_CONFIG] > 4. defaults",
)
def cmd_serve(
    file: Path,
    host: str,
    port: int,
    max_connections: int,
    recv_timeout: float,
    send_timeout: float,
    kusp_config: Optional[Path],
):
    """Serve a decorated model over TCP.

    Args:
        file: Path to the python module containing the model.
        host: Interface to bind.
        port: Port to bind.
        max_connections: Maximum pending connections.
        recv_timeout: Socket receive timeout in seconds.
        send_timeout: Socket send timeout in seconds.
        kusp_config: Optional explicit config path.
    """
    if (
        kusp_config is None
        and host == "127.0.0.1"
        and port == 12345
        and os.environ.get("KUSP_CONFIG", False)
    ):
        kusp_config = Path(os.environ["KUSP_CONFIG"])
        logger.info(
            f"Loading config file provided at env var KUSP_CONFIG: {kusp_config}"
        )
        try:
            config = yaml.safe_load(kusp_config.read_text())
            host = config.get("server", {}).get("host", "127.0.0.1")
            port = config.get("server", {}).get("port", 12345)
        except (FileNotFoundError, KeyError):
            logger.error(
                f"Failed to read config file defined at env var KUSP_CONFIG: {kusp_config}"
            )
        logger.debug(f"Using {host}:{port} for connection.")

    cfg_path = resolve_config_path(
        str(kusp_config) if kusp_config else None, host, port
    )
    cfg_path = write_or_update_config(
        config_path=cfg_path, host=host, port=port, model_file=str(file)
    )
    _cli_message(
        f"Config written to {cfg_path}. Export KUSP_CONFIG to point simulators at this server.",
        fg="green",
        bold=True,
    )
    server = IPProtocol(
        host=host,
        port=port,
        max_connections=max_connections,
        recv_timeout_s=recv_timeout,
        send_timeout_s=send_timeout,
        model_file=str(file),
    )

    server.start()
    try:
        server.serve()
    finally:
        server.stop()


@cli.command(
    "deploy",
    help="Package a KUSP model and its auxiliary files for deployment.",
)
@click.argument(
    "model_file", type=click.Path(exists=True, dir_okay=False, path_type=Path)
)
@click.option(
    "--resource",
    "--resources",
    "-r",
    "resources",
    multiple=True,
    type=click.Path(exists=True, dir_okay=False, path_type=Path),
    help="Additional resource files required by the model (e.g. weights, config).",
)
@click.option(
    "--name",
    "-n",
    default=None,
    type=str,
    help="Name of the model. Default is KUSP_<FileName>__MO_*",
)
@click.option(
    "--env",
    "env_mode",
    type=click.Choice(["ast", "pip", "conda"], case_sensitive=False),
    default="ast",
    show_default=True,
    help="How to generate environment description: 'ast' (imports-based minimal env), 'pip' (pip freeze), 'conda' (conda env export).",
)
def cmd_deploy(
    model_file: Path,
    resources: Tuple[Path, ...],
    name: Optional[str],
    env_mode: str,
):
    _cli_message(f"Preparing deployment package for {model_file}", fg="cyan")
    if resources:
        res_str = " ".join(str(f) for f in resources)
        _cli_message(f"Including resources: {res_str}", fg="cyan")

    env_mode = env_mode.lower()
    _cli_message(
        f"Generating environment description using mode: {env_mode!r}", fg="cyan"
    )

    package = package_model_for_deployment(
        model_file=model_file,
        resources=resources,
        name=name,
        env_mode=env_mode,
    )

    _cli_message(f"Deploying {model_file} as {package.model_name}", fg="green")
    _cli_message(
        f"Wrote environment description: {package.env_file.name}", fg="green"
    )
    _cli_message(
        f"Model {package.model_name} written in directory: {package.target_dir}",
        fg="green",
        bold=True,
    )


def main(argv: Optional[list[str]] = None) -> int:
    """Execute the CLI and return an exit code.

    Args:
        argv: Optional argument vector override.

    Returns:
        Process exit code provided by Click.
    """
    try:
        cli.main(args=argv, prog_name="kusp", standalone_mode=False)
        return 0
    except SystemExit as e:
        return int(e.code)


if __name__ == "__main__":
    sys.exit(main())
