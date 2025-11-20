from importlib import metadata

try:
    __version__ = metadata.version("kusp")
except metadata.PackageNotFoundError:
    __version__ = "2.x" # default version

from .kim import install_kim_model, install_kim_driver
from .kusp import kusp_model

# TODO: use setuptools-scm for version. and remove this dual file/pyproject dependency
__all__ = [
    "__version__",
    "install_kim_model",
    "install_kim_driver",
    "kusp_model",
]
