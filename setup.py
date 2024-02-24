from setuptools import find_packages, setup

setup(
    name="kusp",
    version="0.0.1",
    packages=find_packages(),
    package_data={"kusp": ["KUSPPortableModel/**/*"]},
    install_requires=[
        "pyyaml",
        "loguru",
    ],
    author="Amit Gupta",
    author_email="gupta839@umn.edu",
    description="kusp",
)
