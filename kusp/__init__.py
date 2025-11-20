from .__version__ import __version__
from .install_kim_model import install_kim_model, install_kim_driver
from .kusp import kusp_model


# TODO: use setuptools-scm for version. and remove this dual file/pyproject dependency
__all__ = [
    "__version__",
    "install_kim_model",
    "install_kim_driver",
    "kusp_model",
]
