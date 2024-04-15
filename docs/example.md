# Deploying IntelMatSciML models
## Example folder
Please see the example folder of the github repository for the complete example.
Basic steps includes,
1. Installing the accompanying KUSP portable model,
```python
import kusp
kusp.install_kim_model()
```
By default it installs the model driver under `user` collection, 
which keeps the models at `~/.kim-api/2.3.0.../model-drivers-dir` directory.
But you can select other kim-api collections by passing appropriate option
to the `install_kim_model` function. (Please check KIM-API documentation for more details
on collections and model installation).
```python
import kusp
kusp.install_kim_model(collection='CWD')
#or
kusp.install_kim_model(collection='system')
```

2. Running the kusp server
```bash
python serve_matsciml_models.py
```
This would start the server at `localhost:12345` by default, and serve a
M3GNET model from IntelMatSciML package. Please note that this model is untrained,
and is only for demonstration purposes. None of the results it produces are valid.

3. Running the KIM API client
```bash
python client_ase.py
```
or 
```bash
cd lammps
lmp -in lmp_m3gnet.in
```
Now inferring the model using KUSP is as simple as running the KIM Model.

> Use `KUSP__MO_000000000000_000` in KIM models to run the model.

## Running KIM Tests in KDP
To run the kim tests, you currently need few extra steps after starting the server,
1. Copy the `KUSP__MO_000000000000_000` folder in the `/home/openkim/models` directory.
2. Edit the `/home/openkim/models/KUSP__MO_000000000000_000/kimspec.edn` file to include the species you want to test the model against,
```clojure
 ...
 "maintainer-id" "729049db-685a-43b1-97a8-617daa2586ba"
 "publication-year" "2024"
 "species" ["Si" "Cu"]
 ...
```
3. Install the desired tests,
```bash
kimitems -D install LatticeConstantCubicEnergy_fcc_Si__TE_828776015817_007
```
4. Run the tests using the pipeline tools,
```bash
pipeline-run-pair LatticeConstantCubicEnergy_fcc_Si__TE_828776015817_007 KUSP__MO_000000000000_000 -v
```

You should now see the tests running against the model.
