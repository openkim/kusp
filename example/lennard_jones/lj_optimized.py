from typing import Tuple

import numpy as np
from loguru import logger
from kusp import kusp_model

from numba import njit



@njit(fastmath=True)
def _lj_energy_forces_numba(positions, epsilon, sigma):
    n_atoms = positions.shape[0]
    forces = np.zeros_like(positions)
    energy_per_atom = np.zeros(n_atoms, dtype=np.float64)

    for i in range(n_atoms):
        xi0 = positions[i, 0]
        xi1 = positions[i, 1]
        xi2 = positions[i, 2]

        for j in range(i + 1, n_atoms):
            dx0 = xi0 - positions[j, 0]
            dx1 = xi1 - positions[j, 1]
            dx2 = xi2 - positions[j, 2]

            r2 = dx0 * dx0 + dx1 * dx1 + dx2 * dx2

            if r2 == 0.0:
                continue

            inv_r2 = (sigma * sigma) / r2
            inv_r6 = inv_r2 * inv_r2 * inv_r2
            inv_r12 = inv_r6 * inv_r6        

            e_ij = 4.0 * epsilon * (inv_r12 - inv_r6)
            energy_per_atom[i] += 0.5 * e_ij
            energy_per_atom[j] += 0.5 * e_ij

            f_over_r = 24.0 * epsilon * (2.0 * inv_r12 - inv_r6) / r2

            fx = f_over_r * dx0
            fy = f_over_r * dx1
            fz = f_over_r * dx2

            forces[i, 0] += fx
            forces[i, 1] += fy
            forces[i, 2] += fz

            forces[j, 0] -= fx
            forces[j, 1] -= fy
            forces[j, 2] -= fz

    
    return energy_per_atom, forces


@kusp_model(
    influence_distance=8.5,
    species=("H",),
    strict_arg_check=True,
)
class LJ:
    """Simple Lennard-Jones potential for H-H interaction."""

    def __init__(self, epsilon: float = -0.00103, sigma: float = 3.4):
        """
        epsilon: depth of the potential well (in eV)
        sigma: distance at which potential = 0 (in Å)
        """
        self.epsilon = float(epsilon)
        self.sigma = float(sigma)
        logger.info(f"Initialized LJ with epsilon={self.epsilon}, sigma={self.sigma}")

    def __call__(
        self,
        species: np.ndarray,
        positions: np.ndarray,
        contributing: np.ndarray,
    ) -> Tuple[np.ndarray, np.ndarray]:
        """
        species: (N,) atomic numbers
        positions: (N,3) Cartesian coordinates in
        contributing: (N,) mask of contributing atoms 
        Returns: (energy [1,], forces [N,3])
        """
        n_atoms = len(species)
        if n_atoms < 2:
            energy = np.zeros(1, dtype=np.float64)
            forces = np.zeros((n_atoms, 3), dtype=np.float64)
            return energy, forces

        # Ensure contiguous float64 for numba
        pos = np.ascontiguousarray(positions, dtype=np.float64)

        energy_scalar, forces = _lj_energy_forces_numba(
            pos, self.epsilon, self.sigma
        )

        energy = np.ascontiguousarray(np.sum(energy_scalar * contributing).reshape(1), dtype=np.float64)
        forces = np.ascontiguousarray(forces * contributing[:, np.newaxis], dtype=np.float64)

        return energy, forces

