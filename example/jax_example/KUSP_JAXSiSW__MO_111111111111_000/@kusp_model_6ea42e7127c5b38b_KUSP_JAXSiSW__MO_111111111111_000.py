import jax
jax.config.update("jax_enable_x64", True)

import jax.numpy as jnp
from jax import grad, vmap
from functools import partial
import jax_md
import jax_md.space as space
import jax_md.energy as energy
import jax_md.util as util
import numpy as np

from kusp import kusp_model

from typing import Callable, Any

Array = Any


def stillinger_weber_per_atom(displacement: Callable,
                              sigma: float = 2.0951,
                              A: float = 7.049556277,
                              B: float = 0.6022245584,
                              lam: float = 21.0,
                              gamma: float = 1.2,
                              epsilon: float = 2.16826,
                              three_body_strength: float = 1.0,
                              cutoff: float = 3.77118) -> Callable[[Array], Array]:
    """
    Compute the per-atom energy of a Stillinger-Weber potential.
    This is the same function as jax_md.energy.stillinger_weber, but it returns the per-atom energy by not calling the jax_md.utils.high_precision_sum function.
    """
    two_body_fn = partial(energy._sw_radial_interaction, sigma, B, cutoff)
    three_body_fn = partial(energy._sw_angle_interaction, gamma, sigma, cutoff)
    three_body_fn = vmap(vmap(vmap(three_body_fn, (0, None)), (None, 0)))

    def compute_fn(R, **kwargs):
        d = partial(displacement, **kwargs)
        dR = space.map_product(d)(R, R)
        dr = space.distance(dR)
        two_body_energy = jnp.sum(two_body_fn(dr), axis=1) * A / 2.0
        three_body_energy = jnp.sum(jnp.sum(three_body_fn(dR, dR), axis=2), axis=1) * lam / 2.0
        per_atom_energy = epsilon * (two_body_energy + three_body_strength * three_body_energy)
        return per_atom_energy
    return compute_fn


def sum_per_atom_energy_and_force(energy_fn, positions, contributions):
    """Sum the per-atom energy and force."""
    per_atom_energy = energy_fn(positions)
    per_atom_energy *= contributions
    total_energy = jnp.sum(per_atom_energy)
    forces = -grad(lambda R: jnp.sum(energy_fn(R) * contributions))(positions)
    return total_energy, forces

@kusp_model(influence_distance=3.77118, species=['Si']) # thats all thats needed
class JAXMDPotential:
    def __init__(self):    
        displacement, shift = space.free()
        self.sw = stillinger_weber_per_atom(displacement)

    def __call__(self, atomic_numbers: np.ndarray, positions: np.ndarray, contributing_atoms:np.ndarray) -> tuple[np.ndarray, np.ndarray]:
        pos = jnp.array(positions)
        contributing_atoms = jnp.array(contributing_atoms)
        e, f = sum_per_atom_energy_and_force(self.sw, pos, contributing_atoms)
        return np.array(e), np.array(f)

