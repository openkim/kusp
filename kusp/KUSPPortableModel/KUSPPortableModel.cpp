#include "KUSPPortableModel.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <cstdlib>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

// KLIFF_SERVE_CONFIG_PATH are set by the python script
//==============================================================================
//
// This is the standard interface to KIM Model Drivers
//
//==============================================================================

//******************************************************************************
extern "C" {
int model_create(KIM::ModelCreate *const modelCreate,
                        KIM::LengthUnit const requestedLengthUnit,
                        KIM::EnergyUnit const requestedEnergyUnit,
                        KIM::ChargeUnit const requestedChargeUnit,
                        KIM::TemperatureUnit const requestedTemperatureUnit,
                        KIM::TimeUnit const requestedTimeUnit) {
    int ier;
    // read input files, convert units if needed, compute
    // interpolation coefficients, set cutoff, and publish parameters
    std::cout << "Creating model" << std::endl;
    auto modelObject = new KUSPPortableModel(modelCreate,
                                              requestedLengthUnit,
                                              requestedEnergyUnit,
                                              requestedChargeUnit,
                                              requestedTemperatureUnit,
                                              requestedTimeUnit,
                                              &ier);
    std::cout << "Model created" << std::endl;
    if (ier) {
        // constructor already reported the error
        delete modelObject;
        return ier;
    }

    // register pointer to TorchMLModelDriverImplementation object in KIM object
    modelCreate->SetModelBufferPointer(static_cast<void *>(modelObject));

    // everything is good
    ier = false;
    return ier;
}
}  // extern "C"

//==============================================================================
//
// Implementation of KUSPPortableModel public wrapper functions
//
//==============================================================================

// ****************************** ********* **********************************
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelCreate
KUSPPortableModel::KUSPPortableModel(
        KIM::ModelCreate *const modelCreate,
        KIM::LengthUnit const requestedLengthUnit,
        KIM::EnergyUnit const requestedEnergyUnit,
        KIM::ChargeUnit const requestedChargeUnit,
        KIM::TemperatureUnit const requestedTemperatureUnit,
        KIM::TimeUnit const requestedTimeUnit,
        int *const ier) {
    // check if env variable for config file is set
    char *config_path = std::getenv("KLIFF_SERVE_CONFIG_PATH");
    std::string config_path_str;

    if (config_path == nullptr) {
        // if not, use default config file
        config_path_str = "./kliff_serve_config.dat";
    } else {
        config_path_str = std::string(config_path);
    }

    std::cout << "Using config file: " << config_path_str << std::endl;

    // read config file
    // Requirements: 1st line: server ip, 2nd line: server port
    // 3rd line: influence distance
    // 4th line: list of elements symbols
    std::ifstream config_file(config_path_str.c_str());
    if (!config_file.is_open()) {
        throw std::runtime_error("Error: config file not found.\n");
    } else {
        config_file >> server_ip;
        config_file >> server_port;
        config_file >> influence_distance;
        std::string element;
        while (config_file >> element) {
            elements_list.push_back(element);
        }
    }
    config_file.close();

    // init socket
    init_socket();

    // register required pointers for model driver
    *ier = modelCreate->SetUnits(requestedLengthUnit,
                                       requestedEnergyUnit,
                                       KIM::CHARGE_UNIT::unused,
                                       KIM::TEMPERATURE_UNIT::unused,
                                       KIM::TIME_UNIT::unused);
    if (*ier) {
        LOG_ERROR("Unable to SetUnits");
        return;
    }
    modelCreate->SetInfluenceDistancePointer(&influence_distance);
    willNotRequestNeighborsOfNonContributing = static_cast<int>(true);
    modelCreate->SetNeighborListPointers(1, &influence_distance, &willNotRequestNeighborsOfNonContributing);
    int code = 0;
    for(auto const &element: elements_list) {
        KIM::SpeciesName const specName1(element);
        *ier = modelCreate->SetSpeciesCode(specName1, code);
        code++;
        if (*ier) {
            LOG_ERROR("Unable to SetSpeciesCode");
            return;
        }
    }
    modelCreate->SetModelNumbering(KIM::NUMBERING::zeroBased);
    *ier = modelCreate->SetParameterPointer(1, &influence_distance, "influence_distance", "influence distance");
    LOG_DEBUG("Registered Parameter");
    if (*ier) return;

    KIM::ModelDestroyFunction *destroy = KUSPPortableModel::Destroy;
    KIM::ModelRefreshFunction *refresh = KUSPPortableModel::Refresh;
    KIM::ModelComputeFunction *compute = KUSPPortableModel::Compute;
    KIM::ModelComputeArgumentsCreateFunction *CACreate = KUSPPortableModel::ComputeArgumentsCreate;
    KIM::ModelComputeArgumentsDestroyFunction *CADestroy = KUSPPortableModel::ComputeArgumentsDestroy;

    *ier = modelCreate->SetRoutinePointer(
            KIM::MODEL_ROUTINE_NAME::Destroy,
            KIM::LANGUAGE_NAME::cpp,
            true,
            reinterpret_cast<KIM::Function *>(destroy))
           || modelCreate->SetRoutinePointer(
            KIM::MODEL_ROUTINE_NAME::Refresh,
            KIM::LANGUAGE_NAME::cpp,
            true,
            reinterpret_cast<KIM::Function *>(refresh))
           || modelCreate->SetRoutinePointer(
            KIM::MODEL_ROUTINE_NAME::Compute,
            KIM::LANGUAGE_NAME::cpp,
            true,
            reinterpret_cast<KIM::Function *>(compute))
           || modelCreate->SetRoutinePointer(
            KIM::MODEL_ROUTINE_NAME::ComputeArgumentsCreate,
            KIM::LANGUAGE_NAME::cpp,
            true,
            reinterpret_cast<KIM::Function *>(CACreate))
           || modelCreate->SetRoutinePointer(
            KIM::MODEL_ROUTINE_NAME::ComputeArgumentsDestroy,
            KIM::LANGUAGE_NAME::cpp,
            true,
            reinterpret_cast<KIM::Function *>(CADestroy));

}

// **************************************************************************
KUSPPortableModel::~KUSPPortableModel() {
    close_socket();
}

//******************************************************************************
// static member function
int KUSPPortableModel::Destroy(KIM::ModelDestroy *const modelDestroy) {
    KUSPPortableModel *modelObject;
    modelDestroy->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    delete modelObject;
    return false;
}

//******************************************************************************
// static member function
int KUSPPortableModel::Refresh(KIM::ModelRefresh *const modelRefresh) {
    KUSPPortableModel *modelObject;
    modelRefresh->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    modelRefresh->SetInfluenceDistancePointer(&modelObject->influence_distance);
    modelRefresh->SetNeighborListPointers(1, &modelObject->influence_distance, &modelObject->willNotRequestNeighborsOfNonContributing);
    return false;
}

//******************************************************************************
int KUSPPortableModel::Compute(
        KIM::ModelCompute const *const modelCompute,
        KIM::ModelComputeArguments const *const modelComputeArguments) {

    KUSPPortableModel *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));

    // get number of particles
    int *numberOfParticlesPointer;
    int *particleContributing;
    int *speciesCode;

    double * coordinates;
    auto ier = modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::numberOfParticles,
            &numberOfParticlesPointer)
            || modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::particleContributing,
            &particleContributing)
            || modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::particleSpeciesCodes,
            &speciesCode)
            || modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::coordinates,
            &coordinates);
    if (ier) {
        std::cerr << "Could not get number of particles @ Compute" << std::endl;
        return ier; //TODO: fix LOG_ERROR
    }

    // send data to socket
    std::cout << "Sending data to socket" << std::endl;
    std::cout << "Number of particles: " << *numberOfParticlesPointer << std::endl;
    modelObject->data_to_socket(*numberOfParticlesPointer, speciesCode, coordinates, particleContributing);

    // get data from socket
    double *energy;
    double *forces;
    double *particleEnergy;
    ier = modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::partialForces,
            (double const **) &forces)
               || modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::partialEnergy,
            &energy)
                || modelComputeArguments->GetArgumentPointer(
            KIM::COMPUTE_ARGUMENT_NAME::partialParticleEnergy,
            &particleEnergy);

    // receive data from socket
    modelObject->data_from_socket(*numberOfParticlesPointer, energy, particleEnergy, forces);
    return false;
}

//******************************************************************************
// static member function
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelComputeArgumentsCreate

int KUSPPortableModel::ComputeArgumentsCreate(
        KIM::ModelCompute const *const modelCompute,
        KIM::ModelComputeArgumentsCreate *const modelComputeArgumentsCreate) {
    KUSPPortableModel *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    int error = modelComputeArgumentsCreate->SetArgumentSupportStatus(
            KIM::COMPUTE_ARGUMENT_NAME::partialEnergy,
            KIM::SUPPORT_STATUS::required)
                    // Support state can be optional for energy
                    // But then we need to explicitly handle nullptr case
                    // As energy is anyway always needs to be computed
                    // as a prerequisite for force computation, easier to
                    // make it required.
                    // TODO: Handle it properly in future for GD models
                || modelComputeArgumentsCreate->SetArgumentSupportStatus(
            KIM::COMPUTE_ARGUMENT_NAME::partialForces,
            KIM::SUPPORT_STATUS::optional)
                || modelComputeArgumentsCreate->SetArgumentSupportStatus(
                  KIM::COMPUTE_ARGUMENT_NAME::partialParticleEnergy,
                  KIM::SUPPORT_STATUS::optional);
    return error;
}

//******************************************************************************
// static member function
int KUSPPortableModel::ComputeArgumentsDestroy(
        KIM::ModelCompute const *modelCompute,
        KIM::ModelComputeArgumentsDestroy *const modelComputeArgumentsDestroy) {
    KUSPPortableModel *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    return false;
}

//==============================================================================

void KUSPPortableModel::init_socket() {
    connection_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connection_socket == -1) {
        throw std::runtime_error("Error: socket creation failed.\n");
    }
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_address.sin_addr);
    std::cout << "Connecting to " << server_ip << ":" << server_port << std::endl;
    int connection_status = connect(connection_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    if (connection_status == -1) {
        throw std::runtime_error("Error: connection failed.\n");
    }
}

void KUSPPortableModel::close_socket() {
    close(connection_socket);
}

void KUSPPortableModel::data_to_socket(int n_atoms, int* species, double *coordinates, int *particleContributing) {
    int32_t size_of_int = sizeof(int);
    send(connection_socket, &size_of_int, sizeof(int32_t), 0);
    send(connection_socket, &n_atoms, sizeof(int), 0);
    send(connection_socket, species, n_atoms * sizeof(int), 0);
    send(connection_socket, coordinates, n_atoms * 3 * sizeof(double), 0);
    send(connection_socket, particleContributing, n_atoms * sizeof(int), 0);
}

void  KUSPPortableModel::data_from_socket(int n_atoms, double* energy, double *particleEnergy, double *forces) {
    recv(connection_socket, energy, sizeof(double), 0);
    recv(connection_socket, forces, n_atoms * 3 * sizeof(double), 0);
    // optional particleEnergy
    // if (particleEnergy) {
    //     recv(connection_socket, particleEnergy, n_atoms * sizeof(int), 0);
    // }
}