import os
import subprocess

from loguru import logger

from .kim_utils import (
    KIM_COLLECTIONS_TOOL,
    KIM_ITEMS_TOOL,
    KUSP_DRIVER_ARTIFACT,
    KUSP_MODEL_ARTIFACT,
    check_if_driver_installed,
    check_if_model_installed,
)


def install_kim_model(
    collection: str = "user",
    installer: str = KIM_COLLECTIONS_TOOL,
) -> bool:
    """Install the bundled Python KIM model if needed."""
    if check_if_model_installed():
        logger.info("KUSP model already installed")
        return True

    logger.info(f"Installing KUSP model via {installer}")
    kusp_base_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir)
    )
    os.chdir(kusp_base_path)

    if installer == KIM_COLLECTIONS_TOOL:
        command = [
            installer,
            "install",
            collection,
            KUSP_MODEL_ARTIFACT,
        ]
    elif installer == KIM_ITEMS_TOOL:
        command = [
            installer,
            "install",
            KUSP_MODEL_ARTIFACT,
        ]
    else:
        raise ValueError(f"Installer {installer!r} not recognized")

    logger.debug(f"Running: {' '.join(command)}")
    subprocess.run(command, check=True)
    logger.info("KUSP model installed")
    return True


def install_kim_driver(
    collection: str = "user",
    installer: str = KIM_COLLECTIONS_TOOL,
) -> bool:
    """Install the bundled KIM driver if needed."""
    if check_if_driver_installed():
        logger.info("KUSP driver already installed")
        return True

    logger.info(f"Installing KUSP driver via {installer}")
    kusp_base_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir)
    )
    os.chdir(kusp_base_path)

    if installer == KIM_COLLECTIONS_TOOL:
        command = [
            installer,
            "install",
            collection,
            KUSP_DRIVER_ARTIFACT,
        ]
    elif installer == KIM_ITEMS_TOOL:
        command = [
            installer,
            "install",
            KUSP_DRIVER_ARTIFACT,
        ]
    else:
        raise ValueError(f"Installer {installer!r} not recognized")

    logger.debug(f"Running: {' '.join(command)}")
    subprocess.run(command, check=True)
    logger.info("KUSP driver installed")
    return True
