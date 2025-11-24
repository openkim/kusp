# JAX based models

JAX models are notoriously difficult to deploy with C++.
`example/jax_example` demonstrates how to wrap a JAX MD model and expose it to KIM-API simulators through KUSP. The workflow matches
the Lennard-Jones and NequIP tutorials, so you can swap components without relearning new tooling.

## 1. Decorate the JAX model

- `JAXSiSW.py` contains a `@kusp_model` entry point that wires up the JAX MD potential, handles the
  JIT-compiled energy/force function, and returns NumPy arrays.
- Adjust cutoffs, species, or model weights there if you change the physics.

The wrapper is intentionally tiny, most of the file is standard JAX MD setup:

```python
import jax
import jax.numpy as jnp
import numpy as np
from kusp import kusp_model


@kusp_model(influence_distance=3.2, species=("Si",))
class JAXSiSW:
    def __init__(self):
        self.energy_force = jax.jit(self._build_energy_force())

    def __call__(self, species, positions, contributing):
        energy, forces = self.energy_force(
            jnp.array(positions), jnp.array(contributing, dtype=jnp.bool_)
        )
        return np.asarray(energy), np.asarray(forces)
```

Everything outside the decorator/block focuses on defining `_build_energy_force` with JAX MD primitives.
No simulator-specific logic is required.

## 2. Serve with hot reload

```bash
kusp serve example/jax_example/JAXSiSW.py \
    --kusp-config example/jax_example/kusp_config.yaml
```

- The generated config path is printed once; export it via
  `export KUSP_CONFIG=$PWD/example/jax_example/kusp_config.yaml`.
- Save `JAXSiSW.py` and press `Ctrl+C` once to reload JIT-compiled functions without restarting.
- Press `Ctrl+C` twice quickly to stop the server.

## 3. Validate the wrapper

- `eval_jax_md.py` compares energies/forces from the native JAX MD call to those returned by the TCP
  server, so you can confirm the decorated model stays bitwise consistent.
- `Si.xyz` provides the sample configurations used by the script, but you can replace it with your
  own trajectories.

## 4. Package for redistribution

```bash
kusp export example/jax_example/JAXSiSW.py \
    -n KUSP_JAXSiSW__MO_111111111111_000 \
    --resource example/jax_example/Si.xyz \
    --env pip
```

The output directory contains the hashed Python module, optional resources, an environment manifest,
and the `CMakeLists.txt` pointing at `KUSP__MD_000000000000_000`. Install it directly with
`kim-api-collections-management install` or share the folder as-is.
