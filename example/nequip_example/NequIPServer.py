"""
KUSP wrapper around the as-exported NequIP potential.

Run the server as:
python NequIPServer.py /path/to/model.pt /path/to/config.yml
"""

from kusp import KUSPServer
import numpy as np
import torch
import ase
import ase.neighborlist


def neighbor_list_and_relative_vec( pos, r_max, self_interaction=False, 
                                   strict_self_interaction=True):
    """
    Generate the neighbor list and relative vectors for the given configuration,
    taken from nequip github repository
    """
    # ASE dependent part
    cell_max_mag = np.max(pos) - np.min(pos) + 2 * r_max
    cell = np.eye(3) * cell_max_mag
    pbc = np.array([True, True, True])

    first_idex, second_idex, shifts = ase.neighborlist.primitive_neighbor_list(
            "ijS", pbc, cell, pos, cutoff=r_max, self_interaction=strict_self_interaction, 
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



class NequIPServer(KUSPServer):
    def __init__(self, model, server_config):
        # This particular nequip model has two special points to consider:
        # 1. The model computes the gradients in the second last layer. This is an issue as 
        #    we cannot compute partial derivatives twice in torch as it discards the grad info. 
        #    To avoid that we will only evaluate the model till second last layer
        # 2. As we hare partially evaluating the model, we need to manually apply scaling and shift
        #    if the nequip model does not have these, we can skip this step 
        # 3. need to subtract the energy of the reference configuration

        device = torch.device("cuda" if torch.cuda.is_available() else "cpu")    
        # Change tge deployed model to remove the last layer
        # Last layer computed the gradients against the total energy
        # We want the gradients against the atomic energies.
        # Once the gradients are evaluated, torch will discard the grad info
        # Hence we stop the model evaluation at the second last layer   
        model_to_eval = list(list(model.children())[0].children())[0]
        model_to_eval = model_to_eval.double()
        model_to_eval = model_to_eval.to(device)

        super().__init__(model_to_eval, server_config)
        # cutoff is 1/3 of the influence distance, KIM API influence distance = n_conv * cutoff
        self.cutoff = self.global_information['influence_distance']/3
        self.species = self.global_information['elements']

        # Initialize the graph generator
        self.device = device
        # As the model is evaluated till the second last layer, we need to manually apply the 
        # scaling and shift which is done in the last layer
        self.scale_by = model.scale_by

        # Reference energy for Si, model was trained on GAP Si data, and it does not subtract 
        # the reference energy value is taken from the GAP Si dataset
        self.Si_REF = torch.tensor(-157.7272, device=device, dtype=torch.float64)

    def prepare_model_inputs(self, atomic_numbers, positions, contributing_atoms):
        # Convert the unwarapped configuration to a kliff config
        # inputs: cell: np.ndarray, species: List[str], coords: np.ndarray, PBC: List[bool],

        edge_index,shifts ,cell = neighbor_list_and_relative_vec(pos=positions, r_max=self.cutoff, 
                                                         self_interaction=False, strict_self_interaction=True)
        # NequIP input dictionary
        # required inputs: "pos" "edge_index" "edge_cell_shift" "cell" "atom_types"
        input_dict = {
            "pos": torch.tensor(positions, dtype=torch.float64, requires_grad=True, device=self.device),
            "cell": torch.tensor(cell, dtype=torch.float64, device=self.device),
            "atom_types": torch.tensor(atomic_numbers, dtype=torch.long, device=self.device),
            "edge_index": torch.tensor(edge_index, dtype=torch.long, device=self.device),
            "edge_cell_shift": torch.tensor(shifts, dtype=torch.float64, device=self.device),
            "_contributing_atoms": torch.tensor(contributing_atoms, dtype=torch.float64, device=self.device), # for later use
        }
        return {"input_dict": input_dict}
    
    def execute_model(self, input_dict):
        # Execute the model
        # The model is partially evaluated till the second last layer
        # We will manually apply the scaling and shift if needed
        output = self.exec_func(input_dict)

        energy = ((output['atomic_energy'].squeeze() * self.scale_by - self.Si_REF) * 
                  input_dict["_contributing_atoms"]).sum()
        energy.backward()
        # Extract the gradients
        forces = -input_dict["pos"].grad

        return {"energy": energy.detach().cpu().numpy(), 
                "forces": forces.detach().cpu().numpy()}
    
    def prepare_model_outputs(self, e_and_f):
        return e_and_f        


if __name__ == "__main__":
    import argparse
    import torch

    parser = argparse.ArgumentParser(description="NequIP server")
    parser.add_argument("model", type=str, help="Path to the model")
    parser.add_argument("config", type=str, help="Path to the config file")
    args = parser.parse_args()

    model = torch.jit.load(args.model)

    # set precision to single for consistency
    model = model.to(dtype=torch.float64)

    server = NequIPServer(model, args.config)

    # Start the server
    server.serve()