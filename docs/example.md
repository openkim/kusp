# Rapid Lennard-Jones deployment walkthrough
The `example/lennard_jones` directory in the repository contains a full, reproducible
workflow that exercises every KUSP feature: decorating a Python potential, hot-reload serving,
LAMMPS/ASE integration through the bundled driver, and packaging a deployable KIM item.

## 1. Decorate a Python potential
`example/lennard_jones/lj.py` defines a Lennard-Jones potential using the `@kusp_model`
decorator. Only three inputs—species, positions, contributing mask—are required:

```python
from kusp import kusp_model

@kusp_model(influence_distance=2.6, species=("He",))
class LJ:
    def __call__(self, species, positions, contributing):
        ...
        return energy, forces
```

The same file can be imported directly into Python for quick unit tests or served through KUSP.

## 2. Install the shim artifacts
Before simulators can talk to the TCP server, install the portable model and the C++
driver that lives in `kusp/KUSP__MD_000000000000_000/`:

```bash
kusp install model
kusp install driver
```

Both commands accept `--installer`/`--collection` flags to target specific KIM setups.

## 3. Serve with hot reload
Launch the TCP server and point it at `lj.py`:

```bash
kusp serve example/lennard_jones/lj.py --kusp-config example/kusp_config.yaml
```

The terminal now shows `[KUSP] TCP server listening ...`. Edit `lj.py`, save, and press `Ctrl+C`
once to hot reload. Press `Ctrl+C` twice within two seconds to shut the server down. The generated
config file (printed when the server starts) must be exported as `KUSP_CONFIG` for simulators.

## 4. Talk to KIM-enabled simulators
With the server running and `KUSP_CONFIG` exported, any simulator that understands KIM-API can
consume the portable `KUSP__MO...` model. The example folder contains:

- `example/lennard_jones/test_lj.py` – runs ASE + `KIM("KUSP_lj__MO_111111111111_000")`.
- `example/lennard_jones/lmp_lj.in` – a ready-to-run LAMMPS script.

Both illustrate that the `KUSP__MD...` driver forwards calls to the Python server.

## 5. Package for redistribution
Once satisfied with the model, create a distributable portable model directory:

```bash
kusp deploy example/lennard_jones/lj.py \
    -n KUSP_lj__MO_111111111111_000 \
    --env ast
```

The command copies the hashed Python module, every `--resource`, an environment description, and a
`CMakeLists.txt` into `example/lennard_jones/KUSP_lj__MO_111111111111_000/`. You can zip that folder
or install it with `kim-api-collections-management install`.

## Running KIM tests in KDP
To run KIM verification tests in KDP, synchronize the generated model directory into the
`/home/openkim/models` tree, edit `kimspec.edn` to list the supported species, and use `kimitems`
to install the desired tests. Finally run `pipeline-run-pair <TEST> KUSP__MO_000000000000_000 -v`
to exercise the server-backed model under the pipeline tooling.
