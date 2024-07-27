import os
import subprocess

from loguru import logger


def check_if_model_installed():
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


def install_kim_model(collection:str = "user", installer: str = "kim-api-collections-management"):
    """
    Install the KUSP model.

    Args:
        collection (str): The collection to install the model to. Default is "user".
        installer (str): The installation mode. Default is "kim-api-collections-management",
            another option is "kimitems", for installing the model in KDP for testing.
    """
    if check_if_model_installed():
        logger.info("KUSP model already installed")
        return True

    logger.info("Installing KUSP model")
    kusp_base_path = os.path.dirname(os.path.realpath(__file__))
    os.chdir(kusp_base_path)

    if installer == "kim-api-collections-management":
        subprocess.run(
            [
                "kim-api-collections-management",
                "install",
                collection,
                "KUSP__MO_000000000000_000",
            ],
            check=True,
        )
    elif installer == "kimitems":
        subprocess.run(
            [
                "kimitems",
                "install",
                "KUSP__MO_000000000000_000",
            ],
            check=True,
        )
    else:
        raise ValueError(f"Installer {installer} not recognized")
    
    logger.info("KUSP model installed")
    return True
