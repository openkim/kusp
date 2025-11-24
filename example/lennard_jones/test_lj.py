from ase.calculators.kim.kim import KIM
from ase.build import bulk

#model = KIM("KUSP__MO_000000000000_000")
model = KIM("KUSP_lj__MO_111111111111_000")

h = bulk("He", "fcc", a=4.0)
h.calc = model
e = h.get_potential_energy()
print("Energy per atom:", e / len(h))
f = h.get_forces()
print("Forces:", f)
print("Test passed")
