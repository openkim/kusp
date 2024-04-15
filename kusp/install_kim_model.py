import os
import subprocess

from loguru import logger


def get_if_model_installed():
    """
    Check if the model is installed
    """
    model_list = subprocess.run(
        ["kim-api-collections-management", "list"], capture_output=True, text=True
    )
    for line in model_list.stdout.split("\n"):
        if "KUSP" in line:
            return True
    return False


def install_kim_model(collection="user"):
    """
    Install the KUSP model
    """
    if get_if_model_installed():
        logger.info("KUSP model already installed")
        return True

    logger.info("Installing KUSP model")
    kusp_base_path = os.path.dirname(os.path.realpath(__file__))
    os.chdir(kusp_base_path)

    subprocess.run(
        [
            "kim-api-collections-management",
            "install",
            collection,
            "KUSP__MO_000000000000_000",
        ],
        check=True,
    )
    logger.info("KUSP model installed")
    return True
