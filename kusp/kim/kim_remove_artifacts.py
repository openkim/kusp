import os
import subprocess

from loguru import logger

from .kim_utils import (
    KIM_COLLECTIONS_TOOL,
    KIM_ITEMS_TOOL,
    KUSP_DRIVER_ARTIFACT,
    KUSP_MODEL_ARTIFACT,
)


def remove_kim_model(installer: str = KIM_COLLECTIONS_TOOL) -> bool:
    """Remove the previously installed portable Python model.

    Args:
        installer: ``kim-api-collections-management`` or ``kimitems`` selector.

    Returns:
        True after the removal command finishes.
    """
    logger.info(f"Removing KUSP model via {installer}")

    kusp_base_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir)
    )
    os.chdir(kusp_base_path)

    if installer in (KIM_COLLECTIONS_TOOL, KIM_ITEMS_TOOL):
        command = [installer, "remove", KUSP_MODEL_ARTIFACT]
    else:
        raise ValueError(f"Installer {installer!r} not recognized")

    logger.debug(f"Running: {' '.join(command)}")
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError:
        logger.warning(
            "Subprocess returned error; most likely the model is not currently installed;"
            "use the commandline kim-api-collections-management if that is not the case."
        )
    logger.info("KUSP model removed")
    return True


def remove_kim_driver(installer: str = KIM_COLLECTIONS_TOOL) -> bool:
    """Remove the compiled KIM bridge driver.

    Args:
        installer: ``kim-api-collections-management`` or ``kimitems`` selector.

    Returns:
        True after the removal command finishes.
    """
    logger.info(f"Removing KUSP driver via {installer}")

    kusp_base_path = os.path.abspath(
        os.path.join(os.path.dirname(__file__), os.pardir)
    )
    os.chdir(kusp_base_path)

    if installer in (KIM_COLLECTIONS_TOOL, KIM_ITEMS_TOOL):
        command = [installer, "remove", KUSP_DRIVER_ARTIFACT]
    else:
        raise ValueError(f"Installer {installer!r} not recognized")

    logger.debug(f"Running: {' '.join(command)}")
    try:
        subprocess.run(command, check=True)
    except subprocess.CalledProcessError:
        logger.warning(
            "Subprocess returned error; most likely the driver is not currently installed;"
            "use the commandline kim-api-collections-management if that is not the case."
        )
    logger.info("KUSP driver removed")
    return True
