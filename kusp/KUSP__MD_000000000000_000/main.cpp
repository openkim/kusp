#include <filesystem>
#include <iostream>
#include <pybind11/embed.h>
#include "KUSPModel.hpp"

namespace py = pybind11;

int main(int argc, char** argv) {
    // Start Python interpreter
    // py::scoped_interpreter guard{};

    if (argc < 2) {
        std::cerr << "Usage: " << argv[0] << " <path>" << std::endl;
        return -1;
    }

    auto path = std::filesystem::path(argv[1]);
    const KUSPModel model(path);

    std::vector<int> species = {0, 0};
    std::vector<double> positions = {0.1,0.1,0.1, 2.0,-0.2,0};
    std::vector<int> contrib = {1, 1};

    double energy;
    std::vector<double> forces;

    model.Run(species, positions, contrib, energy, forces);

    std::cout << "Energy = " << energy << "\n";
    for (size_t i = 0; i < forces.size(); i += 3) {
        std::cout << "Force: " << forces[i] << ", "
                              << forces[i+1] << ", "
                              << forces[i+2] << "\n";
    }

    positions = {0.1,0.1,0.1, 1.0,-0.2,0};
    model.Run(species, positions, contrib, energy, forces);

    std::cout << "Energy = " << energy << "\n";
    for (size_t i = 0; i < forces.size(); i += 3) {
        std::cout << "Force: " << forces[i] << ", "
                              << forces[i+1] << ", "
                              << forces[i+2] << "\n";
    }


    return 0;
}
