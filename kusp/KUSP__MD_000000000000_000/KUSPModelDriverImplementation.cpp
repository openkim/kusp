#include "KUSPModelDriverImplementation.hpp"

#include <filesystem>

#include "KIM_LogMacros.hpp"
#include "KUSPModelDriver.hpp"

//******************************************************************************
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelDriverCreate

KUSPModelDriverImplementation::KUSPModelDriverImplementation(KIM::ModelDriverCreate *const modelDriverCreate,
                                                             KIM::LengthUnit const requestedLengthUnit,
                                                             KIM::EnergyUnit const requestedEnergyUnit,
                                                             KIM::ChargeUnit const requestedChargeUnit,
                                                             KIM::TemperatureUnit const requestedTemperatureUnit,
                                                             KIM::TimeUnit const requestedTimeUnit, int *const ier) {
    *ier = false;
    // initialize members to remove warning----
    //  Read parameter files from model driver ---------------------------------------
    //  also initialize the ml_model
    //  init python model name file, get cutoffs / influence distance
    //  Most of the python file io stuff is wrapped in KUSPModel now ---------------------------------------
    int num_param_files;
    const std::string *param_dir_name_ptr = nullptr;
    std::string model_file;
    std::vector<std::string> param_files;
    modelDriverCreate->GetNumberOfParameterFiles(&num_param_files);
    modelDriverCreate->GetParameterFileDirectoryName(&param_dir_name_ptr);

    for (int i = 0; i < num_param_files; i++) {
        const std::string *file_name = nullptr;
        modelDriverCreate->GetParameterFileBasename(i, &file_name);
        if (file_name->compare(0, 11, "@kusp_model") == 0) {
            model_file = *file_name; // it is the only file that needs reading
        } else {
            param_files.push_back(*file_name);
        }
    }

    const std::filesystem::path fully_qualified_model_file = std::filesystem::path(*param_dir_name_ptr) / model_file;

    LOG_DEBUG("Reading Python files: " + fully_qualified_model_file.string());
    model_ = std::make_unique<KUSPModel>(fully_qualified_model_file.string());
    influence_distance = model_->influence_distance;
    cutoff_distance = model_->influence_distance; // as of now no support for different cutoffs, may be in future
    modelWillNotRequestNeighborsOfNoncontributingParticles_ = true; // no support for that either

    // Unit conversions -----------------------------------------------------------------
    unitConversion(modelDriverCreate, requestedLengthUnit, requestedEnergyUnit, requestedChargeUnit,
                   requestedTemperatureUnit, requestedTimeUnit, ier);
    LOG_DEBUG("Registered Unit Conversion");
    if (*ier)
        return;

    modelDriverCreate->SetInfluenceDistancePointer(&influence_distance);
    modelDriverCreate->SetNeighborListPointers(1, &cutoff_distance,
                                               &modelWillNotRequestNeighborsOfNoncontributingParticles_);

    // Species code --------------------------------------------------------------------
    elements_list = model_->species;
    setSpecies(modelDriverCreate, ier);
    LOG_DEBUG("Registered Species");
    if (*ier)
        return;

    // Register Index settings-----------------------------------------------------------
    modelDriverCreate->SetModelNumbering(KIM::NUMBERING::zeroBased); // python is zero based

    // Register Parameters --------------------------------------------------------------
    // no params as such
    *ier = modelDriverCreate->SetParameterPointer(1, &cutoff_distance, "cutoff", "Model cutoff provided");
    LOG_DEBUG("Registered Parameter");
    if (*ier)
        return;

    // Register function pointers -----------------------------------------------------------
    registerFunctionPointers(modelDriverCreate, ier);
    if (*ier)
        return;
}

//******************************************************************************
// TODO: Can be done with templating. Deal with it later
int KUSPModelDriverImplementation::Refresh(KIM::ModelRefresh *const modelRefresh) {
    modelRefresh->SetInfluenceDistancePointer(&influence_distance);
    modelRefresh->SetNeighborListPointers(1, &cutoff_distance,
                                          &modelWillNotRequestNeighborsOfNoncontributingParticles_);
    return false;
}
int KUSPModelDriverImplementation::Refresh(KIM::ModelDriverCreate *const modelRefresh) {
    modelRefresh->SetInfluenceDistancePointer(&influence_distance);
    modelRefresh->SetNeighborListPointers(1, &cutoff_distance,
                                          &modelWillNotRequestNeighborsOfNoncontributingParticles_);
    // TODO Distance matrix for computational efficiency, which will be refreshed to -1
    return false;
}
// TODO: do we need these two? what does model driver create refresh do?
//******************************************************************************
int KUSPModelDriverImplementation::Compute(KIM::ModelComputeArguments const *const modelComputeArguments) {

    Run(modelComputeArguments);
    // TODO see proper way to return error codes
    return false;
}

//******************************************************************************
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelComputeArgumentsCreate

int KUSPModelDriverImplementation::ComputeArgumentsCreate(
        KIM::ModelComputeArgumentsCreate *const modelComputeArgumentsCreate) {
    LOG_INFORMATION("Compute argument create");
    int error = modelComputeArgumentsCreate->SetArgumentSupportStatus(KIM::COMPUTE_ARGUMENT_NAME::partialEnergy,
                                                                      KIM::SUPPORT_STATUS::required) ||
                modelComputeArgumentsCreate->SetArgumentSupportStatus(KIM::COMPUTE_ARGUMENT_NAME::partialForces,
                                                                      KIM::SUPPORT_STATUS::optional) ||
                modelComputeArgumentsCreate->SetArgumentSupportStatus(KIM::COMPUTE_ARGUMENT_NAME::partialParticleEnergy,
                                                                      KIM::SUPPORT_STATUS::notSupported);
    // || modelComputeArgumentsCreate->SetArgumentSupportStatus(
    //   KIM::COMPUTE_ARGUMENT_NAME::partialVirial,
    //   KIM::SUPPORT_STATUS::optional);
    // register callbacks
    LOG_INFORMATION("Register callback supportStatus");
    error = error ||
            modelComputeArgumentsCreate->SetCallbackSupportStatus(KIM::COMPUTE_CALLBACK_NAME::ProcessDEDrTerm,
                                                                  KIM::SUPPORT_STATUS::notSupported) ||
            modelComputeArgumentsCreate->SetCallbackSupportStatus(KIM::COMPUTE_CALLBACK_NAME::ProcessD2EDr2Term,
                                                                  KIM::SUPPORT_STATUS::notSupported);
    return error;
}

// *****************************************************************************
// Auxiliary methods------------------------------------------------------------
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelComputeArguments
void KUSPModelDriverImplementation::Run(const KIM::ModelComputeArguments *const modelComputeArguments) {
    const int *numberOfParticlesPointer = nullptr;
    const int *particleSpeciesCodes = nullptr;
    const int *particlesContributing = nullptr;
    const double *coordinates = nullptr;
    double *forces = nullptr;
    double *energy = nullptr;
    auto ier = modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::numberOfParticles,
                                                         &numberOfParticlesPointer) ||
               modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::particleSpeciesCodes,
                                                         &particleSpeciesCodes) ||
               modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::particleContributing,
                                                         &particlesContributing) ||
               modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::coordinates, &coordinates) ||
               modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::partialForces, &forces) ||
               modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::partialEnergy, &energy);
    if (ier) {
        LOG_ERROR("@Run: Model Arguments failure");
        return;
    }
    model_->Run(*numberOfParticlesPointer, particleSpeciesCodes, coordinates, particlesContributing, energy, forces);
}


// --------------------------------------------------------------------------------
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelDriverCreate

void KUSPModelDriverImplementation::unitConversion(KIM::ModelDriverCreate *const modelDriverCreate,
                                                   KIM::LengthUnit const requestedLengthUnit,
                                                   KIM::EnergyUnit const requestedEnergyUnit,
                                                   KIM::ChargeUnit const requestedChargeUnit,
                                                   KIM::TemperatureUnit const requestedTemperatureUnit,
                                                   KIM::TimeUnit const requestedTimeUnit, int *const ier) {
    // TODO: read units from model decorator field. Only Length and Energy
    // I guess it will just raise error if the two dont match
    // Or perhaps have an adopter mode to convert units externally? like DL_POLY
    const KIM::LengthUnit fromLength = KIM::LENGTH_UNIT::A;
    const KIM::EnergyUnit fromEnergy = KIM::ENERGY_UNIT::eV;
    const KIM::ChargeUnit fromCharge = KIM::CHARGE_UNIT::e;
    const KIM::TemperatureUnit fromTemperature = KIM::TEMPERATURE_UNIT::K;
    const KIM::TimeUnit fromTime = KIM::TIME_UNIT::ps;
    double convertLength = 1.0;
    *ier = KIM::ModelDriverCreate::ConvertUnit(
            fromLength, fromEnergy, fromCharge, fromTemperature, fromTime, requestedLengthUnit, requestedEnergyUnit,
            requestedChargeUnit, requestedTemperatureUnit, requestedTimeUnit, 1.0, 0.0, 0.0, 0.0, 0.0, &convertLength);

    if (*ier) {
        LOG_ERROR("Unable to convert length unit");
        return;
    }

    *ier = modelDriverCreate->SetUnits(requestedLengthUnit, requestedEnergyUnit, KIM::CHARGE_UNIT::unused,
                                       KIM::TEMPERATURE_UNIT::unused, KIM::TIME_UNIT::unused);
}

// --------------------------------------------------------------------------------
void KUSPModelDriverImplementation::setSpecies(KIM::ModelDriverCreate *const modelDriverCreate, int *const ier) const {
    int code = 0;
    for (auto const &species: elements_list) {
        KIM::SpeciesName const specName1(species);

        //    std::map<KIM::SpeciesName const, int, KIM::SPECIES_NAME::Comparator> modelSpeciesMap;
        //    std::vector<KIM::SpeciesName> speciesNameVector;
        //
        //    speciesNameVector.push_back(species);
        //    // check for new species
        //    std::map<KIM::SpeciesName const, int, KIM::SPECIES_NAME::Comparator>::const_iterator iIter =
        //    modelSpeciesMap.find(specName1);
        // all the above is to remove species duplicates
        *ier = modelDriverCreate->SetSpeciesCode(specName1, code);
        code += 1;
        if (*ier)
            return;
    }
}

// --------------------------------------------------------------------------------
void KUSPModelDriverImplementation::registerFunctionPointers(KIM::ModelDriverCreate *const modelDriverCreate,
                                                             int *const ier) {
    // Use function pointer definitions to verify correct prototypes
    // TODO This doesn't look nice, implementation calling parent class
    // See if there is a workaround
    KIM::ModelDestroyFunction *destroy = KUSPModelDriver::Destroy;
    KIM::ModelRefreshFunction *refresh = KUSPModelDriver::Refresh;
    KIM::ModelComputeFunction *compute = KUSPModelDriver::Compute;
    KIM::ModelComputeArgumentsCreateFunction *CACreate = KUSPModelDriver::ComputeArgumentsCreate;
    KIM::ModelComputeArgumentsDestroyFunction *CADestroy = KUSPModelDriver::ComputeArgumentsDestroy;

    *ier = modelDriverCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::Destroy, KIM::LANGUAGE_NAME::cpp, true,
                                                reinterpret_cast<KIM::Function *>(destroy)) ||
           modelDriverCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::Refresh, KIM::LANGUAGE_NAME::cpp, true,
                                                reinterpret_cast<KIM::Function *>(refresh)) ||
           modelDriverCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::Compute, KIM::LANGUAGE_NAME::cpp, true,
                                                reinterpret_cast<KIM::Function *>(compute)) ||
           modelDriverCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::ComputeArgumentsCreate,
                                                KIM::LANGUAGE_NAME::cpp, true,
                                                reinterpret_cast<KIM::Function *>(CACreate)) ||
           modelDriverCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::ComputeArgumentsDestroy,
                                                KIM::LANGUAGE_NAME::cpp, true,
                                                reinterpret_cast<KIM::Function *>(CADestroy));
}

//******************************************************************************
int KUSPModelDriverImplementation::ComputeArgumentsDestroy(
        const KIM::ModelComputeArgumentsDestroy *const modelComputeArgumentsDestroy) {
    // Nothing to do here?
    KUSPModelDriver *modelObject; // To silence the compiler
    modelComputeArgumentsDestroy->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    return false;
}
