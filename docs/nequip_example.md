# Torch based models

This guide mirrors the Lennard-Jones tutorial but swaps in the heavier
`example/nequip_example` directory to show how graph neural network potentials can be
prototyped and deployed with the same KUSP workflow.

## 1. Wrap the NequIP checkpoint

`example/nequip_example/NequIPServer.py` is the entry point. It loads a TorchScript checkpoint
(`deployed_nequip.pt`), builds neighbor lists via ASE, applies the NequIP scaling/shift, and exposes
the model through `@kusp_model`. Update the script if you have a different checkpoint or input
format. The only contract is that calling the decorated object returns `(energy, forces)` NumPy arrays.

The actual server class is intentionally minimal:

```python
import numpy as np
import torch
from kusp import kusp_model


@kusp_model(influence_distance=5.0, species=("Si",), strict_arg_check=True)
class NequIPServer:
    def __init__(self, ckpt_path: str):
        self.model = torch.jit.load(ckpt_path).eval()

    def __call__(self, species, positions, contributing):
        inputs = self._prepare_inputs(species, positions, contributing)
        outputs = self.model(**inputs)
        energy = self._postprocess_energy(outputs, contributing)
        forces = self._postprocess_forces(outputs)
        return np.asarray(energy, dtype=np.float64), np.asarray(forces, dtype=np.float64)
```

All of the heavy lifting (`_prepare_inputs`, `_postprocess_*`) is regular Python; wrapping the checkpoint
only requires the decorator plus a single `__call__` method.

## 2. Serve with hot reload

Launch the TCP server and point it to the NequIP entry script. Keep this terminal open so you can hit
`Ctrl+C` once to reload after editing the Python code:

```bash
kusp serve NequIPServer.py 
```

The server prints the config path; export it as `KUSP_CONFIG` for any simulator shells. Pressing
`Ctrl+C` twice within ~2 seconds shuts it down completely.

## 3. Exercise from Python or simulators

The folder contains helper scripts:

- `native_nequip.py`: evaluates the checkpoint directly with PyTorch.
- `eval_torch_nequip_kusp.py`: talks to the running KUSP server and compares predictions.

You can also drive the server from ASE (`from ase.calculators.kim import KIM`) or LAMMPS by simply
selecting the portable model name (`KUSP__MO_000000000000_000` or whatever you deploy later) once
`KUSP_CONFIG` points to the generated YAML.

## 4. Package the model for redistribution

Once the NequIP wrapper behaves as expected, snapshot it into a portable KIM model directory:

```bash
kusp export NequIPServer.py \
    -n KUSP_nequip__MO_111111111111_000 \
    --resource deployed_nequip.pt \
    --env pip
```

`--resource` command is needed to save the model weights for inference.

The command copies a hashed Python module, the Torch checkpoint, any extra resources, an environment
description, and a `CMakeLists.txt` that references the bundled driver. Zip it or install it directly
with `kim-api-collections-management install`.

## Example outputs

### Basic installation of server model and model driver

```shell
$ kusp install driver
[KUSP] [CLI] Installed KUSP KIM driver.

$ kusp install model
[KUSP] [CLI] Installed KUSP KIM model.
```

### Launching server

```shell
$ kusp serve NequIPServer.py 
[KUSP] [CLI] Config written to /tmp/kusp_config_12345_3451662_1763976067.yaml. Export KUSP_CONFIG to point simulators at this server.
[KUSP] [SERVER] TCP server listening on 127.0.0.1:12345   NequIPServer.py              Si.xyz                       SW__MD_335816936951_005.txz                                                        
```

### Calculating energy and forces via the TCP server using ASE 

```python
...
model = KIM("KUSP__MO_000000000000_000")
config.calc = model
energy = config.get_potential_energy()
...
```
Set the `KUSP_CONFIG` and evaluate:
```shell
$ export KUSP_CONFIG=/tmp/kusp_config_12345_3451662_1763976067.yaml

$ python eval_torch_nequip_kusp.py                                 
Forces: [[ 8.47311530e-05  8.47311530e-05  8.47311530e-05]
 [ 1.12424352e-04  1.12424352e-04  8.47309372e-05]
 [ 8.47309372e-05  1.12424352e-04  1.12424352e-04]
 [ 1.12424352e-04  8.47309373e-05  1.12424352e-04]
 [-1.12424295e-04 -1.12424295e-04 -1.12424295e-04]
 [-8.47310680e-05 -8.47310680e-05 -1.12424363e-04]
 [-1.12424363e-04 -8.47310680e-05 -8.47310680e-05]
 [-8.47310680e-05 -1.12424363e-04 -8.47310680e-05]]
Energy: -39.81101851034376
```

### Expoting the model for simulators
```shell
$ kusp export NequIPServer.py --resource deployed_nequip.pt --name KUSP_nequip__MO_111111111111_000
[KUSP] [CLI] Preparing export package for NequIPServer.py
[KUSP] [CLI] Including resources: deployed_nequip.pt                                                           
[KUSP] [CLI] Generating environment description using mode: 'ast'
[KUSP] [CLI] Exporting NequIPServer.py as KUSP_nequip__MO_111111111111_000
[KUSP] [CLI] Wrote environment description: kusp_env.ast.env
[KUSP] [CLI] Model KUSP_nequip__MO_111111111111_000 written in directory: KUSP_nequip__MO_111111111111_000

$ kim-api-collections-management install CWD KUSP_nequip__MO_111111111111_000 
Found local item named: KUSP_nequip__MO_111111111111_000.
In source directory: /home/amit/Projects/FERMAT/KLIFF_Serve/kliff_serve/example/nequip_example/KUSP_nequip__MO_111111111111_000.
   (If you are trying to install an item from openkim.org
    rerun this command from a different working directory,
    or rename the source directory mentioned above.)

Found installed driver... KUSP__MD_000000000000_000
[100%] Built target KUSP_nequip__MO_111111111111_000
Install the project...
-- Install configuration: "Debug"
-- Installing: /home/amit/Projects/FERMAT/KLIFF_Serve/kliff_serve/example/nequip_example/KUSP_nequip__MO_111111111111_000/libkim-api-portable-model.so
-- Set runtime path of "/home/amit/Projects/FERMAT/KLIFF_Serve/kliff_serve/example/nequip_example/KUSP_nequip__MO_111111111111_000/libkim-api-portable-model.so" to ""

Success!
```

### Executing LAMMPS

Use it as any other KIM-API potential:
```
kim init KUSP_nequip__MO_111111111111_000  metal
...
kim interactions Si
```

Output:
```
LAMMPS (2 Aug 2023 - Update 1)
OMP_NUM_THREADS environment is not set. Defaulting to 1 thread. (src/comm.cpp:98)
  using 1 OpenMP thread(s) per MPI task
Lattice spacing in x,y,z = 5.46 5.46 5.46
Created orthogonal box = (0 0 0) to (21.84 21.84 21.84)
  1 by 1 by 1 MPI processor grid
Created 256 atoms
  using lattice units in orthogonal box = (0 0 0) to (21.84 21.84 21.84)
  create_atoms CPU = 0.000 seconds
WARNING: KIM Model does not provide 'partialParticleEnergy'; energy per atom will be zero (src/KIM/pair_kim.cpp:1122)
WARNING: KIM Model does not provide 'partialParticleVirial'; virial per atom will be zero (src/KIM/pair_kim.cpp:1127)
...
Setting up Verlet run ...
  Unit style    : metal
  Current step  : 0
  Time step     : 0.001
Per MPI rank memory allocation (min/avg/max) = 4.479 | 4.479 | 4.479 Mbytes
   Step          Temp          E_pair         E_mol          TotEng         Press     
         0   300           -4443.6196      0             -4364.2409     -73484.473    
         1   301.1582      -4443.9257      0             -4364.2406     -73499.335    
         2   304.65092     -4444.8491      0             -4364.2399     -73544.169    
         3   310.53234     -4446.4043      0             -4364.2388     -73619.901    
         4   318.89247     -4448.6154      0             -4364.2378     -73728.227    
         5   329.85704     -4451.5161      0             -4364.2375     -73871.683    
Loop time of 51.0709 on 1 procs for 5 steps with 2048 atoms

Performance: 0.008 ns/day, 2837.270 hours/ns, 0.098 timesteps/s, 200.506 atom-step/s
55.5% CPU use with 1 MPI tasks x 1 OpenMP threads

MPI task timing breakdown:
Section |  min time  |  avg time  |  max time  |%varavg| %total
---------------------------------------------------------------
Pair    | 50.971     | 50.971     | 50.971     |   0.0 | 99.80
Neigh   | 0.097892   | 0.097892   | 0.097892   |   0.0 |  0.19
Comm    | 0.001269   | 0.001269   | 0.001269   |   0.0 |  0.00
Output  | 0.00027448 | 0.00027448 | 0.00027448 |   0.0 |  0.00
Modify  | 0.00041147 | 0.00041147 | 0.00041147 |   0.0 |  0.00
Other   |            | 0.0002116  |            |       |  0.00

Nlocal:           2048 ave        2048 max        2048 min
Histogram: 1 0 0 0 0 0 0 0 0 0
Nghost:           5765 ave        5765 max        5765 min
Histogram: 1 0 0 0 0 0 0 0 0 0
Neighs:              0 ave           0 max           0 min
Histogram: 1 0 0 0 0 0 0 0 0 0
FullNghs:       458752 ave      458752 max      458752 min
Histogram: 1 0 0 0 0 0 0 0 0 0

Total # of neighbors = 458752
Ave neighs/atom = 224
Neighbor list builds = 5
Dangerous builds not checked
Total wall time: 0:01:03
```

## TODO: Direct Pytorch Model demo

```python
import torch
from kusp import kusp_model

@kusp_model(influence_distance=3.0, species=("Si", "C"),)
class Model(torch.nn.Module):
    pass
    def __call__(self, species, positions, contributing):
        ...
        inputs = preprocess(species, positions ,contributing)
        energy = self.forward(inputs)
        forces = torch.gradient(energy, inputs)
        return energy.numpy(), forces.numpy()
```
