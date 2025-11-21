import hashlib
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Iterable, List, Optional, Tuple

from loguru import logger

from ..utils import (
    generate_ast_env_yaml,
    generate_conda_env_yaml,
    generate_pip_requirements,
)

# TODO change the driver names etc
KUSP_MODEL_ARTIFACT = "KUSP__MO_000000000000_000"
KUSP_DRIVER_ARTIFACT = "KUSP__MD_000000000000_000"
KUSP_MODEL_PREFIX = "KUSP__MO"
KUSP_DRIVER_PREFIX = "KUSP__MD"
KIM_COLLECTIONS_TOOL = "kim-api-collections-management"
KIM_ITEMS_TOOL = "kimitems"


@dataclass(frozen=True)
class DeploymentPackage:
    """One stop information about the generated deployment package."""

    model_name: str
    target_dir: Path
    env_file: Path
    cmake_file: Path
    files_written: Tuple[str, ...]


def _list_kim_items(tool: str = KIM_COLLECTIONS_TOOL) -> str:
    """Return the textual output of the KIM list command."""
    try:
        proc = subprocess.run(
            [tool, "list"],
            capture_output=True,
            text=True,
            check=True,
        )
    except (OSError, subprocess.CalledProcessError) as exc:
        logger.error(f"Failed to query installed KIM items via {tool}: {exc}")
        raise
    return proc.stdout


def _artifact_present(prefix: str, *, tool: str = KIM_COLLECTIONS_TOOL) -> bool:
    """Check whether a given artifact prefix is present in the installed list."""
    listing = _list_kim_items(tool=tool)
    return any(prefix in line for line in listing.splitlines())


def check_if_model_installed() -> bool:
    """Return True if the bundled Python model is already installed."""
    return _artifact_present(KUSP_MODEL_PREFIX)


def check_if_driver_installed() -> bool:
    """Return True if the bundled driver is already installed."""
    return _artifact_present(KUSP_DRIVER_PREFIX)


def package_model_for_deployment(
    model_file: Path,
    resources: Iterable[Path] = (),
    *,
    name: Optional[str] = None,
    env_mode: str,
) -> DeploymentPackage:
    """Bundle a decorated model and auxiliary files into a deployable package.

    Args:
        model_file: Path to a Python module containing a ``@kusp_model`` entry.
        resources: Extra files copied verbatim into the package directory.
        name: Optional explicit KIM-compliant model name.
        env_mode: ``ast``, ``pip``, or ``conda`` env description strategy.

    Returns:
        DeploymentPackage with paths to all generated artifacts.
    """
    resources = tuple(resources or ())
    resolved_name = name or f"KUSP_{model_file.stem}__MO_111111111111_000"
    target_dir = Path(resolved_name)
    target_dir.mkdir(exist_ok=False)

    model_hash = hashlib.blake2b(
        model_file.read_bytes(), digest_size=8
    ).hexdigest()
    model_target = target_dir / f"@kusp_model_{model_hash}_{resolved_name}.py"
    shutil.copy2(model_file, model_target)

    env_mode = env_mode.lower()
    files_to_write: List[str] = [model_target.name]

    if env_mode == "ast":
        env_text = generate_ast_env_yaml(model_target, env_name=resolved_name)
        env_file = target_dir / "kusp_env.ast.env"
    elif env_mode == "pip":
        env_text = generate_pip_requirements()
        env_file = target_dir / "kusp_env.pip.txt"
    elif env_mode == "conda":
        env_text = generate_conda_env_yaml(env_name=resolved_name)
        env_file = target_dir / "kusp_env.conda.yml"
    else:
        raise ValueError(f"Unsupported env resolver: {env_mode}")

    env_file.write_text(env_text)
    files_to_write.append(env_file.name)

    for resource in resources:
        destination = target_dir / Path(resource).name
        shutil.copy2(resource, destination)
        files_to_write.append(destination.name)

    files_to_write_str = " ".join(f'"{name}"' for name in files_to_write)
    cmake_template = f"""
cmake_minimum_required(VERSION 3.10)

list(APPEND CMAKE_PREFIX_PATH $ENV{{KIM_API_CMAKE_PREFIX_DIR}})
find_package(KIM-API-ITEMS 2.2 REQUIRED CONFIG)

kim_api_items_setup_before_project(ITEM_TYPE "portableModel")
project({resolved_name} LANGUAGES CXX)
kim_api_items_setup_after_project(ITEM_TYPE "portableModel")

add_kim_api_model_library(
  NAME                    "${{PROJECT_NAME}}"
  DRIVER_NAME             "{KUSP_DRIVER_ARTIFACT}"
  PARAMETER_FILES         {files_to_write_str}
)
"""
    cmake_file = target_dir / "CMakeLists.txt"
    cmake_file.write_text(cmake_template.strip() + "\n")

    return DeploymentPackage(
        model_name=resolved_name,
        target_dir=target_dir,
        env_file=env_file,
        cmake_file=cmake_file,
        files_written=tuple(files_to_write),
    )
