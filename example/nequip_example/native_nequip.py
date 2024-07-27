import torch
import ase.io
import numpy as np
import ase

def neighbor_list_and_relative_vec(
    pos,
    r_max,
    self_interaction=False,
    strict_self_interaction=True,
    cell=None,
    pbc=False,
):
    # ASE dependent part
    cell = ase.geometry.complete_cell(cell)
    first_idex, second_idex, shifts = ase.neighborlist.primitive_neighbor_list(
            "ijS",
            pbc,
            cell,
            pos,
            cutoff=r_max,
            self_interaction=strict_self_interaction,  # we want edges from atom to itself in different periodic images!
            use_scaled_positions=False,
        )

    # Eliminate true self-edges that don't cross periodic boundaries
    if not self_interaction:
        bad_edge = first_idex == second_idex
        bad_edge &= np.all(shifts == 0, axis=1)
        keep_edge = ~bad_edge
        first_idex = first_idex[keep_edge]
        second_idex = second_idex[keep_edge]
        shifts = shifts[keep_edge]

    # Build output:
    edge_index = np.vstack((first_idex, second_idex))

    return edge_index, shifts, cell


if __name__ == "__main__":
    import torch 
    import numpy as np
    model = torch.jit.load("deployed_nequip.pt")
    atoms = ase.io.read("Si.xyz")
    edge_index, cell_shifts, cell = neighbor_list_and_relative_vec(atoms.get_positions(), 4.0, cell=atoms.get_cell(), pbc=atoms.get_pbc())
    species = np.array([0 for _ in range(len(atoms.get_atomic_numbers()))])

    inputs = {
        "pos": torch.tensor(atoms.get_positions(), dtype=torch.float64, requires_grad=True),
        "cell": torch.tensor(cell, dtype=torch.float64),
        "atom_types": torch.tensor(species, dtype=torch.long),
        "edge_index": torch.tensor(edge_index, dtype=torch.long),
        "edge_cell_shift": torch.tensor(cell_shifts, dtype=torch.float64),
    }

    output = model(inputs)
    print(output["total_energy"], output["forces"])
