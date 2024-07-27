# NequIP Server: A KUSP Wrapper for NequIP Potential

This markdown file explains a Python script that creates a KUSP (KIM-based Universal Simulation Package) wrapper around the NequIP (Neural Equivariant Interatomic Potential) model. 

## Table of Contents

1. [Introduction](#introduction)
2. [Imports and Dependencies](#imports-and-dependencies)
3. [Helper Function: neighbor_list_and_relative_vec](#helper-function-neighbor_list_and_relative_vec)
4. [NequIPServer Class](#nequipserver-class)
5. [Main Execution](#main-execution)

## Introduction

The script creates a server that wraps the NequIP potential, allowing it to be used within the KUSP framework. This implementation has some specific considerations:

1. The model computes gradients in the second-to-last layer, which can cause issues with double partial derivatives in PyTorch.
2. Due to partial model evaluation, manual application of scaling and shift may be necessary.
3. The energy of a reference configuration needs to be subtracted.

To run the server:

```bash
python NequIPServer.py /path/to/model.pt /path/to/config.yml
```

## Imports and Dependencies

```python
from kusp import KUSPServer
import numpy as np
import torch
import ase
import ase.neighborlist
```

These imports bring in the necessary libraries:
- `kusp`: For the KUSP server implementation
- `numpy`: For numerical computations
- `torch`: For deep learning operations
- `ase` and `ase.neighborlist`: For atomic simulation environment and neighbor list generation

## Helper Function: `neighbor_list_and_relative_vec`

```python
def neighbor_list_and_relative_vec(pos, r_max, self_interaction=False, 
                                   strict_self_interaction=True):
    # ... (function implementation)
```

This function generates the neighbor list and relative vectors for a given atomic configuration. It uses ASE (Atomic Simulation Environment) function `ase.neighborlist.primitive_neighbor_list` to compute the neighbor list and handles periodic boundary conditions. It uses the argument `ijS` to get the neighbor list and shift vectors that NequIP requires.

```python
first_idex, second_idex, shifts = ase.neighborlist.primitive_neighbor_list(
            "ijS", pbc, cell, pos, cutoff=r_max, self_interaction=strict_self_interaction, 
            use_scaled_positions=False,
)
```

It was taken from the NequIP repository implementation and adapted for this script.

Key steps:
1. Set up a cell large enough to contain all atoms and their potential neighbors
2. Use ASE's `primitive_neighbor_list` to get initial neighbor information
3. Eliminate self-interactions if not desired
4. Return the edge index, shifts, and cell information

## NequIPServer Class

```python
class NequIPServer(KUSPServer):
    def __init__(self, model, server_config):
        # ... (initialization code)

    def prepare_model_inputs(self, atomic_numbers, positions, contributing_atoms):
        # ... (input preparation code)

    def execute_model(self, input_dict):
        # ... (model execution code)

    def prepare_model_outputs(self, e_and_f):
        # ... (output preparation code)
```

The `NequIPServer` class is the core of this script. It inherits from `KUSPServer` and implements the necessary methods to integrate the NequIP model with KUSP.

### Initialization

The `__init__` method sets up the server with the following key steps:
1. Modify the model to evaluate only up to the second-to-last layer
2. Set up device (CPU or CUDA) for computations
3. Initialize cutoff distance and species information
4. Set up scaling factor and reference energy for Silicon

The default NequIP model evaluates i) total energy, ii) atomwise energy, and iii) forces. Once the model computes the gradients, the graph is discarded and you will get error:
```text
RuntimeError: Trying to backward through the graph a second time (or directly access saved tensors after they have already been freed). Saved intermediate values of the graph are freed when you call .backward() or autograd.grad(). Specify retain_graph=True if you need to backward through the graph a second time or if you need to access saved tensors after calling backward.
```
To avoid it we will need to modify the NequIP source code so that it retains graph.

Alternative would be to use the children of the model and evaluate them selectively. In the provided model, the first children it the, `GradientOutput` module,
```
RecursiveScriptModule(
    original_name=GradientOutput
```
which has the actual model as a children
```
RecursiveScriptModule(
  original_name=SequentialGraphNetwork
```
Hence, `list(list(model.children())[0].children())[0]` will return the actual `forward` model, without the gradient evaluation. Unfortunately, this is not a general solution, as the model structure may change. So, users should be aware of the model structure and modify this code accordingly.
Secondly, the gradient evaluation also applies the scaling and shift to the energy, which is not necessary for the forces. Hence, we will need to manually apply the scaling and shift to the energy. The scaling constant can be found in the `model` object as `model.scale_by`.

```python

### Input Preparation

The `prepare_model_inputs` method converts the input atomic configuration into the format required by NequIP:
1. Generate neighbor list and relative vectors
2. Create a dictionary with position, cell, atom types, edge index, and edge cell shift information

The input dict has the following structure,
```python
{
    "pos": PositionTensor,
    "cell": CellTensor,
    "species": SpeciesTensor,
    "edge_index": EdgeIndexTensor,
    "edge_attr": ShiftTensor,
    "_contributing_atoms": ContributingAtomsTensor,
}
```

`_contributing_atoms` is a tensor that contains the indices of atoms that contribute to the energy and is not a standard NequIP requirement, but is used to get the correct energy and forces for the `KUSP` framework.

### Model Execution

The `execute_model` method:
1. Runs the modified NequIP model
2. Applies scaling and shift to the energy
3. Computes forces using automatic differentiation
4. Returns energy and forces

The energy is only summed for the contributing atoms, and the forces are computed using PyTorch's automatic differentiation. Here, the per-atom reference energy is subtracted from the computed energy, as the model was trained on the GAP dataset, which has a reference energy of -157.7272 eV for an isolated Si atom.

```python
energy = ((output['atomic_energy'].squeeze() * self.scale_by 
                - self.Si_REF) * input_dict["_contributing_atoms"]).sum()
```

This energy can that be used to compute the forces, on both contributing and non-contributing (ghost) atoms. 

### Output Preparation

The `prepare_model_outputs` method simply returns the energy and forces without modification.

## Main Execution

```python
if __name__ == "__main__":
    # ... (argument parsing and server initialization)
```

The main execution block:
1. Parses command-line arguments for model and config paths
2. Loads the NequIP model
3. Initializes the NequIPServer
4. Starts the server

This setup allows the script to be run as a standalone program, initializing and starting the NequIPServer with the provided model and configuration.

Once the server is running, you can compare its performance against the original NequIP model by running the following scripts:

```bash
# launch the NequIP server
python NequIPServer.py deployed_nequip.py kusp_config.yaml
```

in a separate terminal, run the following script to test the server,

```bash
# native NequIP model
python native_nequip.py

# KUSP nequip server

```bash
python eval_torch_nequip_kusp.py
```
