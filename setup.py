from setuptools import find_packages, setup

from kusp import __version__

setup(
    name="kusp",
    version=__version__,
    packages=find_packages(),
    package_data={
        "kusp": [
            "KUSP__MO_000000000000_000/**/*",
            "KUSP__MO_000000000000_000/*",
            "KUSP__MO_000000000000_000/**/*.cpp",
            "KUSP__MO_000000000000_000/**/*.h",
        ]
    },
    include_package_data=True,
    install_requires=[
        "pyyaml",
        "loguru",
    ],
    author="Amit Gupta",
    author_email="gupta839@umn.edu",
    description="kusp: KIM Utility for Serving Potentials",
    long_description=open(".pypi_readme.md").read(),
    long_description_content_type="text/markdown",
)
