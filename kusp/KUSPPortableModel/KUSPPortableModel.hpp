#ifndef SOCKS_MODEL_DRIVER_HPP
#define SOCKS_MODEL_DRIVER_HPP

#include "KIM_ModelHeaders.hpp"
#include "KIM_LogMacros.hpp"
#include <string>
#include <vector>

extern "C" {
int model_create(KIM::ModelCreate *modelCreate,
                        KIM::LengthUnit requestedLengthUnit,
                        KIM::EnergyUnit requestedEnergyUnit,
                        KIM::ChargeUnit requestedChargeUnit,
                        KIM::TemperatureUnit requestedTemperatureUnit,
                        KIM::TimeUnit requestedTimeUnit);
}

/*
 * Core model driver class.
 *
 * As per other KIM model driver examples, TorchMLModel driver follows a PIMPL model, which abstracts away implementation
 * to a separate implementation class. So other than the core skeleton functions, required by the KIM-API, this class
 * does not contain any details.
 *
 */
class KUSPPortableModel {
public:
    KUSPPortableModel(KIM::ModelCreate *modelCreate,
                       KIM::LengthUnit requestedLengthUnit,
                       KIM::EnergyUnit requestedEnergyUnit,
                       KIM::ChargeUnit requestedChargeUnit,
                       KIM::TemperatureUnit requestedTemperatureUnit,
                       KIM::TimeUnit requestedTimeUnit,
                       int *ier);

    static int Destroy(KIM::ModelDestroy *modelDestroy);

    static int Refresh(KIM::ModelRefresh *modelRefresh);

    static int Compute(KIM::ModelCompute const *modelCompute,
                       KIM::ModelComputeArguments const *modelComputeArguments);

    static int ComputeArgumentsCreate(
            KIM::ModelCompute const *modelCompute,
            KIM::ModelComputeArgumentsCreate *modelComputeArgumentsCreate);

    static int ComputeArgumentsDestroy(
            KIM::ModelCompute const *modelCompute,
            KIM::ModelComputeArgumentsDestroy *modelComputeArgumentsDestroy);

    ~KUSPPortableModel();

private:
    double influence_distance;
    std::vector<std::string> elements_list;
    int willNotRequestNeighborsOfNonContributing;

    int server_port = 12345;
    std::string server_ip = "127.0.0.1";
    int connection_socket;
    void init_socket();
    void close_socket();
    void data_to_socket(int n_atoms, int* species, double *coordinates, int *particleContributing);
    void data_from_socket(int n_atoms, double* energy, double *particleEnergy, double *forces);
};

#endif
