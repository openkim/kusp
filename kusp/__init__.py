from importlib import metadata

try:
    __version__ = metadata.version("kusp")
except metadata.PackageNotFoundError:
    __version__ = "2.x" # backup


from .kim import install_kim_model, install_kim_driver
from .kusp import kusp_model


__all__ = [
    "__version__",
    "kusp_model", # only this is needed now.
]
