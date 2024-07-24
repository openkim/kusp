#include "KUSPPortableModel.hpp"

#include <iostream>
#include <fstream>
#include <cstring>
#include <vector>
#include <stdexcept>
#include <cstdlib>
#include <cstdio>
#include <memory>
#include <chrono>
#include <fstream>

#include <sys/socket.h>
#include <netinet/in.h>
#include <unistd.h>
#include <arpa/inet.h>

#include <yaml-cpp/yaml.h>
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
    auto modelObject = new KUSPPortableModel(modelCreate,
                                              requestedLengthUnit,
                                              requestedEnergyUnit,
                                              requestedChargeUnit,
                                              requestedTemperatureUnit,
                                              requestedTimeUnit,
                                              &ier);
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
    char *config_path = std::getenv("KUSP_SERVER_CONFIG");
    std::string config_path_str;

    if (config_path == nullptr) {
        // if not, use default config file
        config_path_str = "./kusp_config.yaml";
    } else {
        config_path_str = std::string(config_path);
    }

    LOG_DEBUG("Using config file: " + config_path_str);

    YAML::Node config = YAML::LoadFile(config_path_str);
    server_ip = config["server"]["host"].as<std::string>();
    server_port = config["server"]["port"].as<int>();
    LOG_DEBUG("Server IP: " + server_ip);
    LOG_DEBUG("Server Port: " + std::to_string(server_port));
    influence_distance = config["global"]["influence_distance"].as<double>();
    for (auto const &element: config["global"]["elements"]) {
        elements_list.push_back(element.as<std::string>());
    }

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
KUSPPortableModel::~KUSPPortableModel() {}

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

    // auto start_time = std::chrono::high_resolution_clock::now();

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

    // connect to socket
    modelObject->init_socket();

    // send data to socket
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
    // close socket
    modelObject->close_socket();

    // auto end_time = std::chrono::high_resolution_clock::now();

    // auto duration = std::chrono::duration_cast<std::chrono::microseconds>(end_time - start_time);

    // auto f = std::ofstream("kusp_compute_time.txt", std::ios::app);
    // time in milliseconds as float
    // f << static_cast<float>(duration.count())/1000.0 << std::endl;
    // f.close();

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
    // set buffer size to 8MB
    // int buffer_size = 8 * 1024 * 1024;
    // setsockopt(connection_socket, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    // setsockopt(connection_socket, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    struct sockaddr_in server_address;
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_address.sin_addr);
    int connection_status = connect(connection_socket, (struct sockaddr *) &server_address, sizeof(server_address));
    if (connection_status == -1) {
        throw std::runtime_error("Error: connection failed. Please check if the server is running.\n");
    }
}

void KUSPPortableModel::close_socket() {
    close(connection_socket);
}


void KUSPPortableModel::data_to_socket(int n_atoms, int* species, double *coordinates, int *particleContributing) {
    int32_t size_of_int = sizeof(int);
    send(connection_socket, &size_of_int, sizeof(int32_t), 0);
    send(connection_socket, &n_atoms, sizeof(int), 0);

    int outgoing_size = n_atoms * (sizeof(int) * 2 + 3 * sizeof(double));
    std::unique_ptr<char[]> buffer(new char[outgoing_size]);
    std::memcpy(buffer.get(), species, n_atoms * sizeof(int));
    std::memcpy(buffer.get() + n_atoms * sizeof(int), coordinates, n_atoms * 3 * sizeof(double));
    std::memcpy(buffer.get() + n_atoms * sizeof(int) + n_atoms * 3 * sizeof(double), particleContributing, n_atoms * sizeof(int));

    int bytes_sent = 0;
    while (bytes_sent < outgoing_size) {
        int bytes = send(connection_socket, buffer.get() + bytes_sent, outgoing_size - bytes_sent, 0);
        if (bytes == -1) {
            throw std::runtime_error("Error: send failed.\n");
        }
        bytes_sent += bytes;
    }
}

void  KUSPPortableModel::data_from_socket(int n_atoms, double* energy, double *particleEnergy, double *forces) {
    // recv(connection_socket, energy, sizeof(double), 0);
    // recv(connection_socket, forces, n_atoms * 3 * sizeof(double), 0);
    int incoming_size = n_atoms * 3 * sizeof(double) + sizeof(double);
    std::unique_ptr<char[]> buffer(new char[incoming_size]);

    int bytes_received = 0;
    while (bytes_received < incoming_size) {
        int bytes = recv(connection_socket, buffer.get() + bytes_received, incoming_size - bytes_received, 0);
        if (bytes == -1) {
            throw std::runtime_error("Error: recv failed.\n");
        }
        bytes_received += bytes;
    }

    // copy data from buffer to respective arrays
    memcpy(energy, buffer.get(), sizeof(double));
    memcpy(forces, buffer.get() + sizeof(double), n_atoms * 3 * sizeof(double));

    // print data
    // std::cout << "Energy: " << *energy << std::endl;
    // std::cout << "Forces: \n";
    // for (int i = 0 ; i < n_atoms; i++) {
    //     // print upto 15 decimal places
    //     std::printf("%1.15f %1.15f %1.15f\n", forces[i * 3], forces[i * 3 + 1], forces[i * 3 + 2]);
    // }
    // optional particleEnergy
    // if (particleEnergy) {
    //     recv(connection_socket, particleEnergy, n_atoms * sizeof(int), 0);
    // }
}
