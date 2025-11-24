# Common Issues & Troubleshooting

This page captures the most frequent problems encountered when exporting or running KUSP models,
along with suggested fixes or workarounds. Contributions are welcome, open an issue if you spot
another recurring pattern.

## CMake cannot find `base64-encode`

```
-- Found KIM-API: /opt/kim_api/install/lib/libkim-api.so.2.4.1
CMake Error at .../kim-api-items-macros.cmake:515 (message):
  Unable to locate 'base64-encode' utility
```

Symptoms:
- KIM-API > 2.4
- The bundled driver (`KUSP__MD_...`) fails to build during `kusp export ...`.
- The error often appears when mixing KIM-API CMake configurations (e.g., Debug while installations of KIM-API, but Release when installing the driver).

Fix:
- Best is to re-install KIM-API with -DCMAKE_BUILD_TYPE=Release

## Embedded Python import failures (`ModuleNotFoundError: No module named 'kusp'`)

Symptoms:
- Console output shows:
  ```
  [KUSP] Error while instantiating the KUSP Python model
  [KUSP] Model instantiation failed: Error while instantiating the KUSP Python model
  ...
  pybind11::error_already_set: ModuleNotFoundError: No module named 'kusp'
  ```
- Happens when the compiled driver links against a different Python runtime than the one providing the `kusp`
  package.
- Sometimes this also show up as a random segfault in C++ exported model driver.

Fix:
- Align the compile-time and runtime Python interpreters. Reinstall `kusp` into the same environment used to
  install the driver, or rebuild the driver while activating the target environment.
- For tricky setups set `KUSP_PYTHON_EXEC` to the absolute path of the interpreter you want the driver to use:
  ```bash
  export KUSP_PYTHON_EXEC=/path/to/env/bin/python
  ```

## Incomplete environement

Symptoms:
- `module not found error` The driver will print the minimum requirements of the model.

```shell
[KUSP] Error while instantiating the KUSP Python model
[KUSP] Model instantiation failed: Error while instantiating the KUSP Python model
[KUSP] Environment description detected at: kusp_env.ast.env
[KUSP] Detected minimal AST-based environment (kusp_env.ast.env).
       Inspect it and install the listed packages.
 ------------------------ Env --------------------------------- 
name: KUSP_JAXSiSW__MO_111111111111_000
channels:
- conda-forge
dependencies:
- python=3.12
- pip:
  - jax==0.5.3
  - kusp==2.0.0
  - numpy==2.3.5

 -------------------------------------------------------------- 

```

Fix: Install the required dependencies.
