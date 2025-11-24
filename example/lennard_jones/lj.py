"""Reference Lennard-Jones model used throughout the KUSP documentation."""

from typing import Tuple

import numpy as np
from loguru import logger

from kusp import kusp_model


@kusp_model(
    influence_distance=2.6,
    species=("He",),
    strict_arg_check=True,
)
class LJ:
    """Simple Lennard-Jones potential for He-He interaction.
       https://www.accessengineeringlibrary.com/content/book/9780070116825/back-matter/appendix2
    """

    def __init__(self, epsilon: float = 0.00088, sigma: float = 2.551):
        """
        epsilon: depth of the potential well (in eV)
        sigma: distance at which potential = 0 (in Å)
        """
        self.epsilon = float(epsilon)
        self.sigma = float(sigma)
        logger.info(f"Initialized LJ with epsilon={self.epsilon}, sigma={self.sigma}")

    def __call__(self, species: np.ndarray, positions: np.ndarray, contributing: np.ndarray) -> Tuple[np.ndarray, np.ndarray]:
        """
        species: (N,) atomic numbers
        positions: (N,3) Cartesian coordinates in Å
        contributing: (N,) mask of contributing atoms (0/1)
        Returns: (energy [1,], forces [N,3])
        """
        n_atoms = len(species)
        if n_atoms < 2:
            energy = np.zeros(1, dtype=np.float64)
            forces = np.zeros((n_atoms, 3), dtype=np.float64)
            return energy, forces

        eps = self.epsilon
        sig = self.sigma

        # pairwise differences
        rij = positions[np.newaxis, :, :] - positions[:, np.newaxis, :]
        r = np.linalg.norm(rij, axis=-1)
        np.fill_diagonal(r, np.inf)

        inv_r6 = (sig / r) ** 6
        inv_r12 = inv_r6 ** 2
        pair_energy = 4 * eps * (inv_r12 - inv_r6)
        energy = np.array([0.5 * np.sum(pair_energy)], dtype=np.float64)  # avoid double counting

        # Forces = -dU/dr * r_hat
        dUdr = 24 * eps * (2 * inv_r12 - inv_r6) / r
        F = np.sum(-dUdr[..., np.newaxis] * (rij / r[..., np.newaxis]), axis=1)

        forces = np.ascontiguousarray(F, dtype=np.float64)
        return energy, forces
