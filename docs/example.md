# Lennard-Jones
## KUSP as rapid prototyping system
The `example/lennard_jones` directory in the repository contains a full, reproducible
workflow that exercises every KUSP feature: decorating a Python potential, hot-reload serving,
LAMMPS/ASE integration through the bundled driver, and packaging a deployable KIM item.

## 1. Decorate a Python potential
`example/lennard_jones/lj.py` defines a Lennard-Jones potential using the `@kusp_model`
decorator. Only three inputs are require: species, positions, contributing mask.

```python
import numpy as np
from kusp import kusp_model


@kusp_model(influence_distance=2.6, species=("He",), strict_arg_check=True)
class LJ:
    def __init__(self, epsilon: float = 0.00088, sigma: float = 2.551):
        self.epsilon = float(epsilon)
        self.sigma = float(sigma)

    def __call__(
        self,
        species: np.ndarray,
        positions: np.ndarray,
        contributing: np.ndarray,
    ) -> tuple[np.ndarray, np.ndarray]:
        energy = np.zeros(1, dtype=np.float64)
        forces = np.zeros_like(positions, dtype=np.float64)
        return energy, forces
```

The same file can be imported directly into Python for quick unit tests or served through KUSP.

## 2. Serve with hot reload
Launch the TCP server and point it at `lj.py`:

```bash
cd example/lennard_jones
kusp serve lj.py
```

The terminal should now show `[KUSP] TCP server listening ...`. Edit `lj.py`, save, and press `Ctrl+C`
once to hot reload. Press `Ctrl+C` twice within two seconds to shut the server down. The generated
config file (printed when the server starts) must be exported as `KUSP_CONFIG` for simulators.

## 3. Talk to KIM-enabled simulators
With the server running and `KUSP_CONFIG` exported, any simulator that understands KIM-API can
consume the portable `KUSP__MO...` model. The example folder contains:

- `example/lennard_jones/test_lj.py`: runs ASE + `KIM("KUSP_lj__MO_111111111111_000")`.
- `example/lennard_jones/lmp_lj.in`: a ready-to-run LAMMPS script.

Both illustrate that the `KUSP__MD...` driver forwards calls to the Python server.

## 4. Package for redistribution
Once satisfied with the model, create a distributable portable model directory:

```bash
kusp export lj.py \
    -n KUSP_lj__MO_111111111111_000
```

The command copies the hashed Python module, every `--resource`, an environment description, and a
`CMakeLists.txt` into `example/lennard_jones/KUSP_lj__MO_111111111111_000/`. You can zip that folder
or install it with `kim-api-collections-management install`.

## Running KIM tests in KDP
To run KIM verification tests in KDP, synchronize the generated model directory into the
`/home/openkim/models` tree, add `kimspec.edn` to list the supported species, and use `kimitems`
to install the desired tests. Finally run `pipeline-run-pair <TEST> KUSP__MO_000000000000_000 -v`
to exercise the server-backed model under the pipeline tooling.

## Adding Numba to the mix
To highlight the fact that KUSP can work with any kind of python libraries, `lj_numba.py`
demonstrates a faster jitted Numba function used for the same Lennard-jones potential.
It can work exactly like the `lj.py`.
