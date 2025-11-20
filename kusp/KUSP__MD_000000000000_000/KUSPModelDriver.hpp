#pragma once

#include <memory>


#include "KIM_ModelDriverHeaders.hpp"

extern "C" {
int model_driver_create(KIM::ModelDriverCreate *modelDriverCreate,
                        KIM::LengthUnit requestedLengthUnit,
                        KIM::EnergyUnit requestedEnergyUnit,
                        KIM::ChargeUnit requestedChargeUnit,
                        KIM::TemperatureUnit requestedTemperatureUnit,
                        KIM::TimeUnit requestedTimeUnit);
}

class KUSPModelDriverImplementation;

/*
 * Core model driver class.
 *
 * As per other KIM model driver examples, KUSPModel driver follows a PIMPL model, which abstracts away implementation
 * to a separate implementation class. So other than the core skeleton functions, required by the KIM-API, this class
 * does not contain any details.
 *
 */
class KUSPModelDriver {
public:
    KUSPModelDriver(KIM::ModelDriverCreate *modelDriverCreate,
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

    ~KUSPModelDriver();

private:
    //! Pointer to ML model driver implementation
    std::unique_ptr<KUSPModelDriverImplementation> implementation_;

};
