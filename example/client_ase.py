from ase.calculators.kim import KIM
from ase import Atoms, io
import numpy as np


# Initialize KIM Model
model = KIM("KIM_SOCKS__MO_000000000000_000")

config = io.read("./Si_example/Si_alat5.909_scale0.0015_perturb1.xyz")

# Set it as calculator
config.set_calculator(model)

# Compute energy/forces
energy = config.get_potential_energy()
forces = config.get_forces()

print(f"Forces: {forces}")
print(f"Energy: {energy}")

