import numpy as np
from ase import Atoms, io
from ase.calculators.kim import KIM

# Initialize KIM Model
model = KIM("KUSP_JAXSiSW__MO_111111111111_000")

config = io.read("./Si.xyz")

config.calc = model

# Compute energy/forces
energy = config.get_potential_energy()
forces = config.get_forces()

print(f"Forces: {forces}")
print(f"Energy: {energy}")
