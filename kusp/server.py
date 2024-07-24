import socket
import struct
import time
from enum import Enum
from typing import Any, Callable

import numpy as np
import yaml
from loguru import logger


class Modes(Enum):
    IP_SOCKETS = 0
    UNIX_SOCKETS = 1
    SHARED_MEM = 2

    @staticmethod
    def from_str(mode: str):
        if mode == "IP":
            return Modes.IP_SOCKETS
        elif mode == "UNIX":
            return Modes.UNIX_SOCKETS
        elif mode == "SHMEM":
            return Modes.SHARED_MEM
        else:
            raise ValueError("Invalid mode")

    @staticmethod
    def to_socket_family(mode: "Modes"):
        if mode == Modes.UNIX_SOCKETS:
            return socket.AF_UNIX
        else:
            return socket.AF_INET


class Server:
    """
    Base server class to handle different modes of communication. This would only read
    the server block from the yaml file and initialize the server. The server would be
    inherited by three different classes for each mode of communication.
    1. IP_SOCKETS
    2. UNIX_SOCKETS
    3. SHARED_MEM
    """

    def __init__(
        self,
        host: str = "127.0.0.1",
        port: int = 12345,
        mode: Modes = Modes.IP_SOCKETS,
        max_connections: int = 1,
        buffer_size: int = 1024,
    ):
        self.host = host
        self.port = port
        self.mode = mode
        self.max_connections = max_connections
        self.socket = None
        self.connection = None
        self.address = None
        self.data = None
        self._callbacks = {}
        self._running = False
        self.buffer_size = buffer_size
        self.server_socket = None
        self.configuration = {}
        logger.add("kusp.log", level="INFO", colorize=False)

    def start(self):
        """
        Start the server
        """
        self.server_socket = socket.socket(
            Modes.to_socket_family(self.mode), socket.SOCK_STREAM
        )
        self.server_socket.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        self.server_socket.bind((self.host, self.port))
        self.server_socket.listen()
        self._running = True
        logger.info(f"KIM server listening on {self.host}:{self.port}")

    def stop(self):
        """
        Stop the server
        """
        self.server_socket.close()
        self.server_socket = None
        self._running = False
        logger.info(f"KIM server stopped")

    def configuration_from_yaml(self, file_path: str):
        """
        Read the server block from the yaml file and initialize the server
        """
        with open(file_path, "r") as file:
            config = yaml.safe_load(file)

        server_block = config.get("server")
        if not server_block:
            raise ValueError("Server block not found in the yaml file")

        optional_args = server_block.get("optional")

        if optional_args:
            if optional_args.get("mode"):
                optional_args["mode"] = Modes.from_str(optional_args["mode"])
        else:
            optional_args = {}

        self.host = server_block.get("host", self.host)
        self.port = server_block.get("port", self.port)
        self.mode = server_block.get("mode", self.mode)
        self.max_connections = server_block.get("max_connections", self.max_connections)
        self.buffer_size = server_block.get("buffer_size", self.buffer_size)
        self.configuration = config

    def serve(self):
        """
        Serve the client
        """
        raise NotImplementedError

    def prepare_model_inputs(
        self, atomic_numbers, positions, contributing_atoms, **kwargs
    ):
        """
        Prepare the model inputs
        """
        raise NotImplementedError

    def execute_model(self, **kwargs):
        """
        Execute the model
        """
        raise NotImplementedError

    def prepare_model_outputs(self, energy, forces, **kwargs):
        """
        Prepare the model outputs
        """
        raise NotImplementedError
