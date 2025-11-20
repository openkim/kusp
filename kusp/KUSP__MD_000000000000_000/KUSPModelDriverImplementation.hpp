#pragma once

#include "KIM_ModelDriverHeaders.hpp"
#include "KUSPModel.hpp"

class KUSPModelDriverImplementation {
public:
    // All file params are public
    KUSPModelDriverImplementation(KIM::ModelDriverCreate *modelDriverCreate,
                                     KIM::LengthUnit requestedLengthUnit,
                                     KIM::EnergyUnit requestedEnergyUnit,
                                     KIM::ChargeUnit requestedChargeUnit,
                                     KIM::TemperatureUnit requestedTemperatureUnit,
                                     KIM::TimeUnit requestedTimeUnit,
                                     int *ier);

    ~KUSPModelDriverImplementation() = default;

    int Refresh(KIM::ModelRefresh *modelRefresh);
    int Refresh(KIM::ModelDriverCreate *modelRefresh);

    int Compute(
            KIM::ModelComputeArguments const *modelComputeArguments);

    int ComputeArgumentsCreate(
            KIM::ModelComputeArgumentsCreate *modelComputeArgumentsCreate);

    static int ComputeArgumentsDestroy(const KIM::ModelComputeArgumentsDestroy *modelComputeArgumentsDestroy);

private:
    // Derived or assigned variables are private
    int modelWillNotRequestNeighborsOfNoncontributingParticles_;

    std::unique_ptr<KUSPModel> model_;

    std::vector<int> num_neighbors_;
    std::vector<int> neighbor_list;
    std::vector<int> z_map;
    std::vector<std::string> elements_list;
    double influence_distance, cutoff_distance;

    static void unitConversion(KIM::ModelDriverCreate *modelDriverCreate,
                               KIM::LengthUnit requestedLengthUnit,
                               KIM::EnergyUnit requestedEnergyUnit,
                               KIM::ChargeUnit requestedChargeUnit,
                               KIM::TemperatureUnit requestedTemperatureUnit,
                               KIM::TimeUnit requestedTimeUnit,
                               int *ier);
    void setSpecies(KIM::ModelDriverCreate *modelDriverCreate, int *ier) const;

    static void registerFunctionPointers(KIM::ModelDriverCreate *modelDriverCreate, int *ier);

    void Run(KIM::ModelComputeArguments const *modelComputeArguments);
};

