KUSP: KIM Utility for Serving Potentials
=========================================
[![Documentation Status](https://readthedocs.org/projects/kusp/badge/?version=latest)](https://kusp.readthedocs.io/en/latest/?badge=latest)

<img src="kusp.png" width="200px">

This utility package provides an easy and quick way to deploy any potential, ML
or otherwise to the KIM API. It is designed to be used as a quick prototyping and benchmarking
tool against OpenKIM tests and verification checks.
This ensures that any valid ML model is on equal footing with OpenKIM supported interatomic
potentials, and can directly guide uses in improving their models or datasets etc.

> Please note that KUSP is supplementary tool for model validation and development,
> and for high performance production runs, you are advised to use the KIM model drivers.

## Installation
> `pip install kusp`

## Difference between KUSP and TorchML model driver
KUSP used Python interpreter to run any ML model in its native environment, and then
converts the output to KIM API compatible format. KIM API communicates with the model using
the KUSP Python server, which uses sockets to communicate.

<img src="KUSP_mech.png" width='600px'>

While this is easy and quick, it is  not the most efficient way to run ML models. Besides
the overhead of Python interpreter, you also have the communication overhead between
Python and C++ code, and overhead of the data transfer process, which is usually 100-400ms
per call. This is not a problem for very large models, but for small models, this overhead
can be significant.

<img src="transport.png" width="400px">

Also, KUSP is not designed to be portable from user to user, while with KIM API
you can install any model as simply as `kim-api-collections-management install user <model>`.
Hence, while KUSP is good for prototyping, users are encouraged to covert their models
to KIM API compatible models, and use the KIM model drivers for production runs.

## KUSP Protocol
KUSP uses a simple protocol to communicate with the KIM API. The protocol expects the
configuration information in following order,

```plaintext
    [Necessary] First 4 bytes: size of integer on the system (int_width), 32 bit integer
    [Necessary] Next int_width bytes: number of atoms (n_atoms), default int_width integer
    [Necessary] Next int_width x n_atoms bytes: atomic numbers
    [Necessary] Next 8 x 3 x n_atoms bytes: positions of atoms (x, y, z), double precision
    [Optional] Next int_width x n_atoms bytes: Which atoms to compute energy for (contributing atoms)
```

## Dependencies
KUSP is a minimal dependency package, but it requires KIM API to serve potentials in simulator
agnostic framework. Without KIM API user would need to implement own simulator interface.
To install KIM API, just use the `conda` package manager,

```bash
conda install -c conda-forge kim-api=2.3
```

## Environment Variables
To tell the KUSP KIM API client where to find the KUSP server, you need to set the
environment variable `KUSP_SERVER_CONFIG` to the path of the configuration file.

```bash
export KUSP_SERVER_CONFIG=/path/to/kusp_server_config.yaml
```

## Citation
If you use KUSP in your research, or find it useful, please cite the following paper, [accessible here](https://openreview.net/forum?id=lQAnpCF7nq).

```bibtex
@inproceedings{gupta2024kusp,
  title={KUSP: Python server for deploying ML interatomic potentials},
  author={Gupta, Amit and Tadmor, Ellad B and Martiniani, Stefano},
  booktitle={AI for Accelerated Materials Design-Vienna 2024}
}
```