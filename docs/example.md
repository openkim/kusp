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