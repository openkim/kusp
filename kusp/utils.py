import ast
import importlib.util
import inspect
import os
import socket
import subprocess
import sys
import tempfile
import time
from pathlib import Path
from typing import Any, Callable, Dict, Optional, Tuple, Union

import numpy as np
import pkg_resources
import yaml
from loguru import logger


def recv_exact(sock: socket.socket, size: int) -> bytes:
    """Receive an exact number of bytes from a socket.

    Args:
        sock: Connected socket.
        size: Number of bytes expected.

    Returns:
        Raw bytes received from the peer.

    Raises:
        ConnectionError: If the peer closes or times out before sending all bytes.
    """
    chunks = []
    remaining = size
    while remaining > 0:
        try:
            chunk = sock.recv(remaining)
        except socket.timeout as exc:
            raise ConnectionError("recv timeout") from exc
        except OSError as exc:
            raise ConnectionError(f"recv error: {exc}") from exc
        if not chunk:
            raise ConnectionError("peer closed connection")
        chunks.append(chunk)
        remaining -= len(chunk)
    return b"".join(chunks)


def ensure_array(
    x: Union[np.ndarray, float, int], *, shape: tuple[int, ...]
) -> np.ndarray:
    """Cast to float64, reshape, and return a C-contiguous array.

    Args:
        x: Input array-like object.
        shape: Desired array shape.

    Returns:
        Reshaped contiguous float64 numpy array.

    Raises:
        ValueError: If the array cannot be reshaped accordingly.
    """
    arr = np.asarray(x, dtype=np.float64)
    try:
        arr = arr.reshape(shape)
    except ValueError as exc:
        raise ValueError(f"Cannot reshape {arr.shape} to {shape}") from exc
    return np.ascontiguousarray(arr)


CHEMICAL_SPECIES = [
    # 0
    "X",
    # 1
    "H",
    "He",
    # 2
    "Li",
    "Be",
    "B",
    "C",
    "N",
    "O",
    "F",
    "Ne",
    # 3
    "Na",
    "Mg",
    "Al",
    "Si",
    "P",
    "S",
    "Cl",
    "Ar",
    # 4
    "K",
    "Ca",
    "Sc",
    "Ti",
    "V",
    "Cr",
    "Mn",
    "Fe",
    "Co",
    "Ni",
    "Cu",
    "Zn",
    "Ga",
    "Ge",
    "As",
    "Se",
    "Br",
    "Kr",
    # 5
    "Rb",
    "Sr",
    "Y",
    "Zr",
    "Nb",
    "Mo",
    "Tc",
    "Ru",
    "Rh",
    "Pd",
    "Ag",
    "Cd",
    "In",
    "Sn",
    "Sb",
    "Te",
    "I",
    "Xe",
    # 6
    "Cs",
    "Ba",
    "La",
    "Ce",
    "Pr",
    "Nd",
    "Pm",
    "Sm",
    "Eu",
    "Gd",
    "Tb",
    "Dy",
    "Ho",
    "Er",
    "Tm",
    "Yb",
    "Lu",
    "Hf",
    "Ta",
    "W",
    "Re",
    "Os",
    "Ir",
    "Pt",
    "Au",
    "Hg",
    "Tl",
    "Pb",
    "Bi",
    "Po",
    "At",
    "Rn",
    # 7
    "Fr",
    "Ra",
    "Ac",
    "Th",
    "Pa",
    "U",
    "Np",
    "Pu",
    "Am",
    "Cm",
    "Bk",
    "Cf",
    "Es",
    "Fm",
    "Md",
    "No",
    "Lr",
    "Rf",
    "Db",
    "Sg",
    "Bh",
    "Hs",
    "Mt",
    "Ds",
    "Rg",
    "Cn",
    "Nh",
    "Fl",
    "Mc",
    "Lv",
    "Ts",
    "Og",
]


ATOMIC_NUMBERS = dict()
ATOMIC_SPECIES = dict()
for i, species in enumerate(CHEMICAL_SPECIES):
    ATOMIC_NUMBERS[species] = i
    ATOMIC_SPECIES[i] = species


def load_kusp_callable(
    path: str,
    init_kwargs: Optional[dict] = None,
) -> Callable[
    [np.ndarray, np.ndarray, Optional[np.ndarray]],
    Tuple[np.ndarray, np.ndarray],
]:
    """Load and instantiate the callable exported via `@kusp_model`.

    Args:
        path: Filesystem path to the python module.
        init_kwargs: Extra kwargs supplied if the export is a class.

    Returns:
        Callable that computes `(energy, forces)`.

    Raises:
        ImportError: If the python module cannot be loaded.
        ValueError: If multiple or zero decorated exports are found.
        TypeError: If the exported object (or instance) is not callable.
    """
    spec = importlib.util.spec_from_file_location(
        f"kusp_model_{time.time_ns()}", path
    )
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load module spec for {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)

    exported = [
        obj
        for obj in module.__dict__.values()
        if getattr(obj, "__kusp_model__", False)
    ]
    if len(exported) != 1:
        names = [getattr(o, "__name__", type(o).__name__) for o in exported]
        raise ValueError(
            f"Expected exactly one @kusp_model export in {path}, found {len(exported)}: {names or '[]'}."
        )

    obj = exported[0]

    if isinstance(obj, type):
        instance = obj(**(init_kwargs or {}))
        if not callable(instance):
            raise TypeError(
                f"Exported class {obj.__name__} is not callable (missing __call__)."
            )
        return instance

    if not callable(obj):
        raise TypeError("Exported object is not callable.")
    return obj


def load_kusp_symbol(path: str):
    """Return the decorated KUSP symbol without instantiating it.

    Args:
        path: Filesystem path to the python module.

    Returns:
        Decorated function or class.

    Raises:
        ImportError: If the module cannot be imported.
        ValueError: If the module exports not exactly one decorated symbol.
    """
    spec = importlib.util.spec_from_file_location(
        f"kusp_model_{time.time_ns()}", path
    )
    if spec is None or spec.loader is None:
        raise ImportError(f"Cannot load module spec for {path}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    exported = [
        obj
        for obj in module.__dict__.values()
        if getattr(obj, "__kusp_model__", False)
    ]
    if len(exported) != 1:
        names = [getattr(o, "__name__", type(o).__name__) for o in exported]
        raise ValueError(
            f"Expected exactly one @kusp_model export, found {names or '[]'}."
        )
    return exported[0]


def resolve_config_path(cli_path: Optional[str], host: str, port: int) -> str:
    """Compute the configuration path respected by the CLI.

    Args:
        cli_path: CLI argument path if provided.
        host: Host portion used for generating filenames.
        port: Port portion used for generating filenames.

    Returns:
        Absolute file path for the YAML config.
    """
    if cli_path:
        return cli_path
    ts = int(time.time())
    return str(
        Path(tempfile.gettempdir())
        / f"kusp_config_{port}_{os.getpid()}_{ts}.yaml"
    )


def write_or_update_config(
    *,
    config_path: str,
    host: str,
    port: int,
    model_file: Optional[str],
) -> str:
    """Write a KUSP YAML configuration file.

    Args:
        config_path: Destination config file path.
        host: Bound host.
        port: Bound port.
        model_file: Optional decorated model file used to populate metadata.

    Returns:
        Path to the written config file.
    """
    species = []
    influence = 0.0
    if model_file:
        try:
            sym = load_kusp_symbol(model_file)  # no instantiation
            species = list(getattr(sym, "__kusp_species__", []))
            influence = float(getattr(sym, "__kusp_influence_distance__", 0.0))
            logger.info(
                f"Inspecting model for config: {getattr(sym, '__name__', type(sym).__name__)} "
                f"(species={species}, influence_distance={influence})"
            )
        except Exception as exc:
            logger.warning(f"Model inspection failed (using defaults): {exc}")

    data: Dict[str, Any] = {
        "kusp_version": "2.0.0",
        "protocol": "ip",
        "protocol_version": 1,
        "server": {"host": host, "port": port},
        "model": {"species": species, "influence_distance": influence},
        "meta": {"pid": os.getpid(), "timestamp": int(time.time())},
    }

    path = Path(config_path)
    path.parent.mkdir(parents=True, exist_ok=True)

    tmp = path.with_suffix(path.suffix + ".tmp")
    with tmp.open("w", encoding="utf-8") as f:
        yaml.safe_dump(data, f, sort_keys=False)
    tmp.replace(path)

    logger.info(f"KUSP config written: {path}")
    return str(path)


def extract_dependencies_from_ast(py_file: Path) -> list[str]:
    tree = ast.parse(py_file.read_text())
    modules = set()

    for node in ast.walk(tree):
        if isinstance(node, ast.Import):
            for n in node.names:
                modules.add(n.name.split(".")[0])
        elif isinstance(node, ast.ImportFrom):
            if node.module:
                modules.add(node.module.split(".")[0])

    stdlib_like = {
        "os", "sys", "typing", "pathlib", "math", "time", "json",
        "re", "logging", "functools", "itertools", "collections",
    } # weed out common dependencies

    return sorted(m for m in modules if m not in stdlib_like)


def resolve_versions_for_imports(imports: list[str]) -> dict[str, str]:
    installed = {d.key: d.version for d in pkg_resources.working_set}
    versions: dict[str, str] = {}
    for mod in imports:
        key = mod.lower()
        if key in installed:
            versions[mod] = installed[key]
    return versions


def generate_ast_env_yaml(model_file: Path, env_name: str) -> str:
    imports = extract_dependencies_from_ast(model_file)
    versions = resolve_versions_for_imports(imports)

    env = {
        "name": env_name,
        "channels": ["conda-forge"],
        "dependencies": [
            f"python={sys.version_info.major}.{sys.version_info.minor}",
        ],
    }

    pip_pkgs = [f"{name}=={ver}" for name, ver in sorted(versions.items())]
    if pip_pkgs:
        env["dependencies"].append({"pip": pip_pkgs})

    return yaml.safe_dump(env, sort_keys=False)


def generate_pip_requirements() -> str:
    try:
        proc = subprocess.run(
            [sys.executable, "-m", "pip", "freeze"],
            check=True,
            capture_output=True,
            text=True,
        )
        return proc.stdout.strip() + "\n"
    except Exception as exc:
        return f"# Failed to run pip freeze: {exc}\n"


def generate_conda_env_yaml(env_name: str) -> str:
    try:
        proc = subprocess.run(
            ["conda", "env", "export"],
            check=True,
            capture_output=True,
            text=True,
        )
        text = proc.stdout
        if env_name:
            lines = text.splitlines()
            out_lines = []
            replaced = False
            for line in lines:
                if line.startswith("name: "):
                    out_lines.append(f"name: {env_name}")
                    replaced = True
                else:
                    out_lines.append(line)
            if not replaced:
                out_lines.insert(0, f"name: {env_name}")
            return "\n".join(out_lines) + "\n"
        return text
    except Exception as exc:
        fallback = {
            "name": env_name,
            "channels": ["conda-forge"],
            "dependencies": [
                f"python={sys.version_info.major}.{sys.version_info.minor}",
            ],
            "comment": f"conda env export failed: {exc}",
        }
        return yaml.safe_dump(fallback, sort_keys=False)
