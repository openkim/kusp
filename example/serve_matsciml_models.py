from ase import Atoms
from KLIFFServe import KLIFFServe
import numpy as np

from matsciml.datasets.utils import element_types
from matsciml.lightning.data_utils import MatSciMLDataModule
from matsciml.models import M3GNet
from matsciml.models.base import ScalarRegressionTask
from pymatgen.io.ase import AseAtomsAdaptor
import dgl

import torch


#########################################################################
#### Utils
#########################################################################

def raw_data_to_atoms(species, pos, contributing, cell, elem_map):
    contributing = contributing.astype(int)
    pos_contributing = pos[contributing==1]
    species = np.array(list(map(lambda x: elem_map[x], species)))
    species = species[contributing==1]
    atoms = Atoms(species, positions=pos_contributing, cell=cell, pbc=[1,1,1])
    return atoms

def dgl_from_coords(conf:Atoms, cutoff=6.0):
    pymat_conf = AseAtomsAdaptor.get_structure(conf)
    pos = torch.as_tensor(conf.get_positions())
    pos.requires_grad_(True)
    cell = conf.get_cell()[:]
    cell = torch.as_tensor(cell)
    from_edge, to_edge, offset, bond_lengths = pymat_conf.get_neighbor_list(r=cutoff, exclude_self=True)
    offset = torch.as_tensor(offset)
    bond_lengths = torch.as_tensor(bond_lengths)
    offshift = offset @ cell

    bond_vecs = pos[from_edge] - pos[to_edge]
    shifted_bond_vecs = bond_vecs - offshift

    graph = dgl.graph(np.array([from_edge, to_edge]).T.tolist())
    cells = torch.ones((len(from_edge),3,3))
    cells *= cell

    g = {}

    graph.ndata['node_type'] =  torch.tensor(list(map(lambda x: x -1, pymat_conf.atomic_numbers)))
    graph.ndata['pos'] =  pos
    graph.edata['pbc_offset'] = offset
    graph.edata['pbc_offshift'] = offshift
    graph.edata['lattice'] = cells
    graph.edata['bond_vec'] = shifted_bond_vecs
    graph.edata['bond_dist'] = bond_lengths

    g["graph"] = graph
    g["atomic_numbers"] = pymat_conf.atomic_numbers
    g["positions"] = pos

    return g

#########################################################################
#### Server
#########################################################################

class MyServingServer(KLIFFServe):
    def __init__(self, model, cutoff, elem_map, cell):
        super().__init__(model)
        self.cutoff = cutoff
        self.elem_map = elem_map
        self.graph_in = None
        self.cell = cell
        self.n_atoms = -1
        self.config = None
    
    def prepare_model_inputs(self, atomic_numbers, positions, contributing_atoms):
        self.n_atoms = atomic_numbers.shape[0]
        config = raw_data_to_atoms(
            atomic_numbers, positions, contributing_atoms, self.cell, self.elem_map)
        self.graph_in = dgl_from_coords(config, cutoff=self.cutoff)
        self.config = config
        return {"batch":self.graph_in}
    
    def prepare_model_outputs(self,energies):
        energy = energies["energy_total"]
        energy.backward()
        forces_contributing = -1 * self.graph_in["positions"].grad
        forces = np.zeros((self.n_atoms, 3))
        forces[:forces_contributing.shape[0],:] = forces_contributing.double().detach().numpy()
        energy = energy.double().squeeze().detach().numpy()
        return {"energy": energy, "forces": forces}


if __name__ == "__main__":
    model = ScalarRegressionTask(
        encoder_class=M3GNet,
        encoder_kwargs={
            "element_types": element_types(),
        },
        output_kwargs={"lazy": False, "input_dim": 64, "hidden_dim": 64},
        task_keys=["energy_total"],
    )

    model.load_state_dict(torch.load("m3gnet.pt"))
    cutoff = 6.0
    cell = np.array([[10.826 * 2, 0.0, 0.0],
                     [0.0, 10.826 * 2, 0.0],
                     [0.0, 0.0, 10.826 * 2]])
    elem_map = ["Si"]
    server = MyServingServer(model=model,
                             cutoff=cutoff,
                             elem_map=elem_map,
                             cell=cell)
    server.serve()
