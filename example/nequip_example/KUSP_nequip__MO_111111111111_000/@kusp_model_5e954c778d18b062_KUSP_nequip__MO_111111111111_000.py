"""
KUSP wrapper around the as-exported NequIP potential.

Run the server as:
python NequIPServer.py /path/to/model.pt /path/to/config.yml
"""

import argparse
from typing import Any, Dict, Tuple

import ase
import ase.neighborlist
import numpy as np
import torch

from kusp import kusp_model 


def neighbor_list_and_relative_vec(
    pos: np.ndarray,
    r_max: float,
    self_interaction: bool = False,
    strict_self_interaction: bool = True,
):
    """Generate neighbor list and relative vectors for the configuration."""
    cell_max_mag = np.max(pos) - np.min(pos) + 2 * r_max
    cell = np.eye(3) * cell_max_mag
    pbc = np.array([True, True, True])

    first_idx, second_idx, shifts = ase.neighborlist.primitive_neighbor_list(
        "ijS",
        pbc,
        cell,
        pos,
        cutoff=r_max,
        self_interaction=strict_self_interaction,
        use_scaled_positions=False,
    )

    if not self_interaction:
        bad_edge = first_idx == second_idx
        bad_edge &= np.all(shifts == 0, axis=1)
        keep_edge = ~bad_edge
        first_idx = first_idx[keep_edge]
        second_idx = second_idx[keep_edge]
        shifts = shifts[keep_edge]

    edge_index = np.vstack((first_idx, second_idx))
    return edge_index, shifts, cell


@kusp_model(influence_distance=12.0, species=("Si",))
class NequIP:
    def __init__(self, model: str = "./deployed_nequip.pt"): # <- default args if possible

        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

        model = torch.jit.load(model, map_location=device)
        model_to_eval = list(list(model.children())[0].children())[0]
        model_to_eval = model_to_eval.to(device=device, dtype=torch.float64)

        self.model = model_to_eval
        self.cutoff = 12.0 / 3.0
        self.device = device
        self.scale_by = getattr(model, "scale_by", 1.0)
        self.SI_REF = torch.tensor(-157.7272, device=device, dtype=torch.float64)

    def __call__(self, species: np.ndarray, positions:np.ndarray, contributing:np.ndarray)-> Tuple[np.ndarray, np.ndarray]: # match the input signature
        edge_index, shifts, cell = neighbor_list_and_relative_vec(
            pos=positions,
            r_max=self.cutoff,
            self_interaction=False,
            strict_self_interaction=True,
        )

        pos = torch.tensor(
            positions, dtype=torch.float64, requires_grad=True, device=self.device
        )
        cell = torch.tensor(cell, dtype=torch.float64, device=self.device)
        atom_types = torch.tensor(species, dtype=torch.long, device=self.device)
        edge_index = torch.tensor(edge_index, dtype=torch.long, device=self.device)
        edge_cell_shift = torch.tensor(shifts, dtype=torch.float64, device=self.device)
        contributing_atoms = torch.tensor(
            contributing, dtype=torch.float64, device=self.device
        )
        input_dict = {
                "pos": pos,
                "cell": cell,
                "atom_types": atom_types,
                "edge_index": edge_index,
                "edge_cell_shift": edge_cell_shift,
                }
        output = self.model(input_dict)

        energy = (
            (output["atomic_energy"].squeeze() * self.scale_by - self.SI_REF)
            * contributing_atoms
        ).sum()
        energy.backward()

        forces = -pos.grad
        energy = energy.detach().cpu().numpy()
        forces = forces.detach().cpu().numpy()

        forces = np.asarray(forces, dtype=np.float64)
        return energy, forces

