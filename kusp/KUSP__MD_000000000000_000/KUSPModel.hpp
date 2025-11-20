#pragma once

#include <fstream>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <vector>

enum class KUSPEnvType {
    AST,
    Pip,
    Conda,
    // Uv ?
    None
};

class KUSPModel {
public:
    explicit KUSPModel(const std::string &python_script_path);

    ~KUSPModel();

    // I dont fully understand the implications of carrying around python machinery
    // deleting copy consturctor and making python Model unique_ptr to be safe.
    // TODO: investigate implications for multithreading.
    KUSPModel() = delete;
    KUSPModel(const KUSPModel &) = delete;
    KUSPModel &operator=(const KUSPModel &) = delete;
    KUSPModel(KUSPModel &&) = delete; // remove for now. might be useful to make KUSPModel movable. TODO
    KUSPModel &operator=(KUSPModel &&) = delete;

    void Run(const std::vector<int> &species,
             const std::vector<double> &positions_flat, // N*3
             const std::vector<int> &contributing, double &energy_out, std::vector<double> &forces_out) const;

    void Run(int n_atoms, const int *species_, const double *positions, const int *contributing, double *energy_out,
             double *forces_out) const;

    double influence_distance;
    std::vector<std::string> species;

private:
    struct Model; // for hidden variable warning, not strictly needed
    std::unique_ptr<Model> model_;
};


std::optional<std::pair<KUSPEnvType, std::string>> detect_kusp_env_type(const std::string &model_dir);

inline void print_kusp_env_help(const std::string &model_dir, const std::string &error_msg,
                                std::ostream &os = std::cerr) {
    os << "[KUSP] Model instantiation failed: " << error_msg << "\n";

    auto info = detect_kusp_env_type(model_dir);
    if (!info) {
        os << "[KUSP] No KUSP environment file (kusp_env.*.*) found in: " << model_dir << "\n";
        return;
    }

    auto [env_type, env_path] = info.value();

    os << "[KUSP] Environment description detected at: " << env_path << "\n";

    switch (env_type) {
        case KUSPEnvType::Conda:
            os << "[KUSP] Detected CONDA environment (kusp_env.conda.yml).\n"
                  "       Try:   conda env create -f \""
               << env_path << "\"\n";
            break;
        case KUSPEnvType::Pip:
            os << "[KUSP] Detected PIP requirements (kusp_env.pip.txt).\n"
                  "       Try:   pip install -r \""
               << env_path << "\"\n";
            break;
        case KUSPEnvType::AST:
            os << "[KUSP] Detected minimal AST-based environment "
                  "(kusp_env.ast.env).\n"
                  "       Inspect it and install the listed packages.\n";
            break;
        default:
            break;
    }

    // ------------------ Print env file ---------------------
    std::ifstream ifs(env_path);
    if (!ifs.is_open()) {
        os << "[KUSP] Failed to open environment file for printing: " << env_path << "\n";
    } else {
        os << " ------------------------ Env --------------------------------- \n";
        os << ifs.rdbuf() << "\n";
        os << " -------------------------------------------------------------- \n";
        os.flush();
    }
}
