#include "KUSPModelDriver.hpp"

#include <memory>

#include "KUSPModelDriverImplementation.hpp"

//==============================================================================
//
// This is the standard interface to KIM Model Drivers
//
//==============================================================================

//******************************************************************************
extern "C" {
int model_driver_create(KIM::ModelDriverCreate *const modelDriverCreate, KIM::LengthUnit const requestedLengthUnit,
                        KIM::EnergyUnit const requestedEnergyUnit, KIM::ChargeUnit const requestedChargeUnit,
                        KIM::TemperatureUnit const requestedTemperatureUnit, KIM::TimeUnit const requestedTimeUnit) {
    int ier;
    // read input files, convert units if needed, compute
    // interpolation coefficients, set cutoff, and publish parameters
    auto modelObject =
            std::make_unique<KUSPModelDriver>(modelDriverCreate, requestedLengthUnit, requestedEnergyUnit,
                                              requestedChargeUnit, requestedTemperatureUnit, requestedTimeUnit, &ier);

    if (ier) {
        // constructor already reported the error
        return ier;
    }

    // register pointer to KUSPModelDriverImplementation object in KIM object
    modelDriverCreate->SetModelBufferPointer(static_cast<void *>(modelObject.get()));
    modelObject.release();
    // everything is good
    ier = false;
    return ier;
}
} // extern "C"

//==============================================================================
//
// Implementation of KUSPModelDriver public wrapper functions
//
//==============================================================================

// ****************************** ********* **********************************
KUSPModelDriver::KUSPModelDriver(KIM::ModelDriverCreate *const modelDriverCreate,
                                 KIM::LengthUnit const requestedLengthUnit, KIM::EnergyUnit const requestedEnergyUnit,
                                 KIM::ChargeUnit const requestedChargeUnit,
                                 KIM::TemperatureUnit const requestedTemperatureUnit,
                                 KIM::TimeUnit const requestedTimeUnit, int *const ier) {
    implementation_ = std::make_unique<KUSPModelDriverImplementation>(modelDriverCreate, requestedLengthUnit,
                                                                      requestedEnergyUnit, requestedChargeUnit,
                                                                      requestedTemperatureUnit, requestedTimeUnit, ier);
}

// **************************************************************************
KUSPModelDriver::~KUSPModelDriver() = default; // Impl is smart ptr now

//******************************************************************************
// static member function
int KUSPModelDriver::Destroy(KIM::ModelDestroy *const modelDestroy) {
    KUSPModelDriver *modelObject;
    modelDestroy->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    delete modelObject;
    return false;
}

//******************************************************************************
// static member function
int KUSPModelDriver::Refresh(KIM::ModelRefresh *const modelRefresh) {
    KUSPModelDriver *modelObject;
    modelRefresh->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));

    return modelObject->implementation_->Refresh(modelRefresh);
}

//******************************************************************************
// static member function
int KUSPModelDriver::Compute(KIM::ModelCompute const *const modelCompute,
                             KIM::ModelComputeArguments const *const modelComputeArguments) {

    KUSPModelDriver *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    return modelObject->implementation_->Compute(modelComputeArguments);
}

//******************************************************************************
// static member function
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelComputeArgumentsCreate

int KUSPModelDriver::ComputeArgumentsCreate(KIM::ModelCompute const *const modelCompute,
                                            KIM::ModelComputeArgumentsCreate *const modelComputeArgumentsCreate) {
    KUSPModelDriver *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    return modelObject->implementation_->ComputeArgumentsCreate(modelComputeArgumentsCreate);
}

//******************************************************************************
// static member function
int KUSPModelDriver::ComputeArgumentsDestroy(KIM::ModelCompute const *modelCompute,
                                             KIM::ModelComputeArgumentsDestroy *const modelComputeArgumentsDestroy) {
    KUSPModelDriver *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    return modelObject->implementation_->ComputeArgumentsDestroy(modelComputeArgumentsDestroy);
}
