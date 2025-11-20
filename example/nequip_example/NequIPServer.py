"""
KUSP wrapper around the as-exported NequIP potential.

Run the server as:
python NequIPServer.py /path/to/model.pt /path/to/config.yml
"""

from __future__ import annotations

import argparse
from typing import Any, Dict

import ase
import ase.neighborlist
import numpy as np
import torch

from kusp import KUSP, Properties, Structure


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


class NequIP(KUSP):
    def __init__(self, model: torch.jit.ScriptModule, server_config: str | Dict[str, Any]):
        # This particular NequIP model has two special points to consider:
        # 1. The model computes the gradients in the second last layer, so we stop there to avoid
        #    torch throwing away gradient information before we differentiate.
        # 2. Because we short-circuit the final layer, we apply the scaling and reference shift
        #    manually after running the truncated network.

        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")

        model_to_eval = list(list(model.children())[0].children())[0]
        model_to_eval = model_to_eval.to(device=device, dtype=torch.float64)

        super().__init__(model_to_eval, server_config)

        config = self.protocol_configuration
        global_info = config.get("global", {})
        if not global_info:
            raise ValueError("Global configuration block missing required parameters.")

        influence_distance = global_info.get("influence_distance")
        if influence_distance is None:
            raise ValueError("Global configuration must define 'influence_distance'.")

        self.cutoff = influence_distance / 3.0
        self.species = global_info.get("elements", [])

        self.device = device
        self.scale_by = getattr(model, "scale_by", 1.0)
        self.SI_REF = torch.tensor(-157.7272, device=device, dtype=torch.float64)

    def prepare_model_inputs(self, structure: Structure) -> Dict[str, Any]:
        positions = np.asarray(structure.positions, dtype=np.float64)
        atomic_numbers = np.asarray(structure.atomic_numbers, dtype=np.int64)
        contributing_atoms = structure.contributing
        if contributing_atoms is None:
            contributing_atoms = np.ones(len(atomic_numbers), dtype=np.float64)
        else:
            contributing_atoms = np.asarray(contributing_atoms, dtype=np.float64)

        edge_index, shifts, cell = neighbor_list_and_relative_vec(
            pos=positions,
            r_max=self.cutoff,
            self_interaction=False,
            strict_self_interaction=True,
        )

        input_dict = {
            "pos": torch.tensor(
                positions, dtype=torch.float64, requires_grad=True, device=self.device
            ),
            "cell": torch.tensor(cell, dtype=torch.float64, device=self.device),
            "atom_types": torch.tensor(atomic_numbers, dtype=torch.long, device=self.device),
            "edge_index": torch.tensor(edge_index, dtype=torch.long, device=self.device),
            "edge_cell_shift": torch.tensor(shifts, dtype=torch.float64, device=self.device),
            "_contributing_atoms": torch.tensor(
                contributing_atoms, dtype=torch.float64, device=self.device
            ),
        }
        return {"input_dict": input_dict}

    def execute_model(self, **kwargs: Any) -> Dict[str, Any]:
        input_dict = kwargs["input_dict"]
        output = self.exec_func(input_dict)

        energy = (
            (output["atomic_energy"].squeeze() * self.scale_by - self.SI_REF)
            * input_dict["_contributing_atoms"]
        ).sum()
        energy.backward()

        forces = -input_dict["pos"].grad
        return {
            "energy": energy.detach().cpu().numpy(),
            "forces": forces.detach().cpu().numpy(),
        }

    def prepare_model_outputs(self, results: Dict[str, Any]) -> Properties:
        energy = np.atleast_1d(np.asarray(results["energy"], dtype=np.float64))
        forces = np.asarray(results["forces"], dtype=np.float64)
        return Properties(energy=energy, forces=forces)

def main() -> None:
    parser = argparse.ArgumentParser(description="NequIP server")
    parser.add_argument("model", type=str, help="Path to the model")
    parser.add_argument("config", type=str, help="Path to the config file")
    args = parser.parse_args()

    model = torch.jit.load(args.model).to(dtype=torch.float64)
    server = NequIP(model, args.config)
    server.serve()


if __name__ == "__main__":
    main()
