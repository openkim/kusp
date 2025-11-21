#include "KUSPModel.hpp"
#include "PythonUtils.hpp"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <utility>

#include <pybind11/embed.h>
#include <pybind11/numpy.h>

namespace py = pybind11;

struct __attribute__((visibility("hidden"))) KUSPModel::Model {
    py::function model_;

    explicit Model(py::function model) : model_(std::move(model)) {};
};

KUSPModel::KUSPModel(const std::string &python_script_path) {
    namespace fs = std::filesystem;
    const fs::path script_path(python_script_path);
    const std::string model_dir = script_path.parent_path().string();

    try {
        auto gil = python_utils::acquire_gil();

        const py::module_ kusp_utils = py::module_::import("kusp.utils");
        const py::object loader = kusp_utils.attr("load_kusp_callable");

        py::object callable = loader(python_script_path);

        if (!PyCallable_Check(callable.ptr())) {
            // This is a "user model" error, not an env error.
            throw std::runtime_error("Loaded KUSP model is not callable. "
                                     "Make sure your model exposes a callable object "
                                     "(function or class with __call__).");
        }

        auto throw_err_if_no_attr = [&](const char *attr, const std::string &message) -> py::object {
            try {
                return py::object(callable.attr(attr));
            } catch (const std::exception &e) {
                throw std::runtime_error(std::string(e.what()) + "\n" + message);
            }
        };

        bool is_kusp_model =
                throw_err_if_no_attr("__kusp_model__",
                                     "Not a KUSP model, please decorate the model properly using @kusp_model")
                        .cast<py::bool_>()
                        .cast<bool>(); // obj -> py bool -> bool as otherwise I get error
        if (!is_kusp_model) {
            throw std::runtime_error("KUSP model attribute is false, please check if exported correctly\n");
        }

        influence_distance =
                throw_err_if_no_attr("__kusp_influence_distance__",
                                     "KUSP model missing attributes, please check if influence distance is provided.")
                        .cast<double>();
        const auto py_species =
                throw_err_if_no_attr("__kusp_species__",
                                     "KUSP model missing attributes, please check if species list was provided.")
                        .cast<py::tuple>();

        for (auto &elem: py_species) {
            species.push_back(py::cast<std::string>(elem));
        }

        model_ = std::make_unique<Model>(static_cast<py::function>(std::move(callable)));
    } catch (...) {
        const std::string msg = "Error while instantiating the KUSP Python model";
        std::cerr << "[KUSP] " << msg << "\n";
        print_kusp_env_help(model_dir, msg, std::cerr);
        throw;
    }
}

void KUSPModel::Run(const std::vector<int> &species_, const std::vector<double> &positions_flat,
                    const std::vector<int> &contributing, double &energy_out, std::vector<double> &forces_out) const {
    // py::gil_scoped_acquire gil;
    auto gil = python_utils::acquire_gil();

    const std::size_t n_atoms = species_.size();
    if (positions_flat.size() != n_atoms * 3) {
        throw std::runtime_error("positions_flat must have n_atoms*3 elements");
    }
    if (!contributing.empty() && contributing.size() != n_atoms) {
        throw std::runtime_error("contributing mask must be size n_atoms");
    }

    // Prepare numpy inputs
    py::array_t<int> species_np(n_atoms, species_.data());

    py::array_t<double> positions_np({static_cast<py::ssize_t>(n_atoms), static_cast<py::ssize_t>(3)},
                                     {sizeof(double) * 3, sizeof(double)}, positions_flat.data());

    py::array_t<int> contrib_np;
    if (!contributing.empty())
        contrib_np = py::array_t<int>(n_atoms, contributing.data());
    else {
        contrib_np = py::array_t<int>(n_atoms);
        auto buf = contrib_np.mutable_unchecked<1>();
        for (py::size_t i = 0; i < n_atoms; ++i)
            buf(i) = 1;
    }

    const py::object out = model_->model_(species_np, positions_np, contrib_np);
    auto [energy_np, forces_np] = out.cast<std::pair<py::array, py::array>>();

    // Copy energy
    {
        const auto buf = energy_np.request();
        const auto ptr = static_cast<double *>(buf.ptr);
        energy_out = ptr[0];
    }

    // Copy forces
    {
        const auto buf = forces_np.request();
        if (buf.ndim != 2 || buf.shape[0] != static_cast<py::ssize_t>(n_atoms) || buf.shape[1] != 3)
            throw std::runtime_error("forces must have shape (n_atoms,3)");

        auto ptr = static_cast<double *>(buf.ptr);
        forces_out.assign(ptr, ptr + n_atoms * 3);
    }
}

void KUSPModel::Run(const int n_atoms, const int *const species_, const double *const positions,
                    const int *const contributing, double *const energy_out, double *const forces_out) const {
    // py::gil_scoped_acquire gil;
    auto gil = python_utils::acquire_gil();

    // Prepare numpy inputs
    py::array_t<int> species_np(n_atoms, species_);

    py::array_t<double> positions_np({static_cast<py::ssize_t>(n_atoms), static_cast<py::ssize_t>(3)},
                                     {sizeof(double) * 3, sizeof(double)}, positions);

    py::array_t<int> contrib_np;
    if (contributing) // if not nullptr
        contrib_np = py::array_t<int>(n_atoms, contributing);
    else { // everyone is contributing
        contrib_np = py::array_t<int>(n_atoms);
        auto buf = contrib_np.mutable_unchecked<1>();
        for (py::size_t i = 0; i < static_cast<py::size_t>(n_atoms); ++i)
            buf(i) = 1;
    }

    const py::object out = model_->model_(species_np, positions_np, contrib_np);
    auto [energy_np, forces_np] = out.cast<std::pair<py::array, py::array>>();

    // Copy energy
    {
        const auto buf = energy_np.request();
        const auto ptr = static_cast<double *>(buf.ptr);
        if (energy_out) {
            *energy_out = *ptr;
        }
    }

    // Copy forces
    {
        const auto buf = forces_np.request();
        if (buf.ndim != 2 || buf.shape[0] != static_cast<py::ssize_t>(n_atoms) || buf.shape[1] != 3)
            throw std::runtime_error("forces must have shape (n_atoms,3), including forces for padding atoms.");

        const auto ptr = static_cast<double *>(buf.ptr);
        if (forces_out) {
            std::memcpy(forces_out, ptr, sizeof(double) * n_atoms * 3);
        }
    }
}


std::optional<std::pair<KUSPEnvType, std::string>> detect_kusp_env_type(const std::string &model_dir) {
    namespace fs = std::filesystem;

    const fs::path base(model_dir);

    const fs::path ast_file = base / "kusp_env.ast.env";
    const fs::path pip_file = base / "kusp_env.pip.txt";
    const fs::path conda_file = base / "kusp_env.conda.yml";

    if (fs::exists(conda_file)) {
        return std::make_pair(KUSPEnvType::Conda, conda_file.string());
    }
    if (fs::exists(pip_file)) {
        return std::make_pair(KUSPEnvType::Pip, pip_file.string());
    }
    if (fs::exists(ast_file)) {
        return std::make_pair(KUSPEnvType::AST, ast_file.string());
    }
    return std::nullopt;
}

KUSPModel::~KUSPModel() = default;
