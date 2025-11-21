import numpy as np
from ase import Atoms, io
from ase.calculators.kim import KIM

# Initialize KIM Model
#model2 = KIM("KUSP__MO_000000000000_000")
# model2 = KIM("NEQUIP_L1_4A_GAP_Si__MO_000000000000_000")
model2 = KIM("KUSP_NequIPServer__MO_111111111111_000")

config = io.read("./Si.xyz")

# # Set it as calculator
# config.set_calculator(model)

# # Compute energy/forces
# energy = config.get_potential_energy()
# forces = config.get_forces()

# print(f"Forces: {forces}")
# print(f"Energy: {energy}")

config.set_calculator(model2)

# Compute energy/forces
energy = config.get_potential_energy()
forces = config.get_forces()

print(f"Forces: {forces}")
print(f"Energy: {energy}")
