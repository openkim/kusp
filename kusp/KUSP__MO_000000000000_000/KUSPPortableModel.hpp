#ifndef KUSP_MODEL_
#define KUSP_MODEL_

#include "KIM_ModelHeaders.hpp"
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
    int connection_socket{};
    int timeout_send_ms = 15000;
    int timeout_recv_ms = 15000;
    int init_socket(KIM::ModelCompute const *modelCompute);
    void close_socket() const;
    int data_to_socket(KIM::ModelCompute const *modelCompute, int n_atoms, const int * species, const double *coordinates,
                        const int *particleContributing) const;
    int data_from_socket(KIM::ModelCompute const *modelCompute, int n_atoms, double* energy, double *particleEnergy, double *forces) const;
};

#endif
