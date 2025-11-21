# NequIP deployment walkthrough

This guide mirrors the Lennard-Jones tutorial but swaps in the heavier
`example/nequip_example` directory to show how graph neural network potentials can be
prototyped and deployed with the same KUSP workflow.

## 1. Wrap the NequIP checkpoint

`example/nequip_example/NequIPServer.py` is the entry point. It loads a TorchScript checkpoint
(`deployed_nequip.pt`), builds neighbor lists via ASE, applies the NequIP scaling/shift, and exposes
the model through `@kusp_model`. Update the script if you have a different checkpoint or input
format—the only contract is that calling the decorated object returns `(energy, forces)` NumPy arrays.

## 2. Install the bridge artifacts

Before simulators can talk to the TCP server, register the bundled portable model (`KUSP__MO...`)
and driver (`KUSP__MD...`) with your preferred KIM collection:

```bash
kusp install model
kusp install driver
```

Pass `--installer` and `--collection` if you want to target `system` collections or use `kimitems`.

## 3. Serve with hot reload

Launch the TCP server and point it to the NequIP entry script. Keep this terminal open so you can hit
`Ctrl+C` once to reload after editing the Python code:

```bash
kusp serve example/nequip_example/NequIPServer.py \
    --kusp-config example/nequip_example/kusp_config.yaml
```

The server prints the config path; export it as `KUSP_CONFIG` for any simulator shells. Pressing
`Ctrl+C` twice within ~2 seconds shuts it down completely.

## 4. Exercise from Python or simulators

The folder contains helper scripts:

- `native_nequip.py` – evaluates the checkpoint directly with PyTorch.
- `eval_torch_nequip_kusp.py` – talks to the running KUSP server and compares predictions.

You can also drive the server from ASE (`from ase.calculators.kim import KIM`) or LAMMPS by simply
selecting the portable model name (`KUSP__MO_000000000000_000` or whatever you deploy later) once
`KUSP_CONFIG` points to the generated YAML.

## 5. Package the model for redistribution

Once the NequIP wrapper behaves as expected, snapshot it into a portable KIM model directory:

```bash
kusp deploy example/nequip_example/NequIPServer.py \
    -n KUSP_nequip__MO_111111111111_000 \
    --resource example/nequip_example/deployed_nequip.pt \
    --resource example/nequip_example/config.yml \
    --env pip
```

The command copies a hashed Python module, the Torch checkpoint, any extra resources, an environment
description, and a `CMakeLists.txt` that references the bundled driver. Zip it or install it directly
with `kim-api-collections-management install`.

## 6. Optional: run KIM verification tests

Just like the Lennard-Jones example, copy the generated portable model directory into a KDP
environment, edit `kimspec.edn` to include the species supported by the NequIP checkpoint, install
tests with `kimitems`, and run `pipeline-run-pair <TEST_ID> KUSP_nequip__MO_111111111111_000 -v` to
validate the server-backed model against OpenKIM’s pipelines.
