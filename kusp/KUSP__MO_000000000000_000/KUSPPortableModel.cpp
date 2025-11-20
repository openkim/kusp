#include "KUSPPortableModel.hpp"
#include "KIM_LogMacros.hpp"

#include <cstring>
#include <memory>
#include <vector>

#include <arpa/inet.h>
#include <cerrno>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>

#include <yaml-cpp/yaml.h>
// KLIFF_CONFIG are set by the python script
//==============================================================================
//
// This is the standard interface to KIM Model Drivers
//
//==============================================================================

//******************************************************************************
extern "C" {
int model_create(KIM::ModelCreate *const modelCreate, KIM::LengthUnit const requestedLengthUnit,
                 KIM::EnergyUnit const requestedEnergyUnit, KIM::ChargeUnit const requestedChargeUnit,
                 KIM::TemperatureUnit const requestedTemperatureUnit, KIM::TimeUnit const requestedTimeUnit) {
    int ier;
    // read input files, convert units if needed, compute
    // interpolation coefficients, set cutoff, and publish parameters
    auto modelObject = new KUSPPortableModel(modelCreate, requestedLengthUnit, requestedEnergyUnit, requestedChargeUnit,
                                             requestedTemperatureUnit, requestedTimeUnit, &ier);
    if (ier) {
        // constructor already reported the error
        delete modelObject;
        return ier;
    }

    // register pointer to TorchMLModelDriverImplementation object in KIM object
    modelCreate->SetModelBufferPointer(modelObject);

    // everything is good
    ier = false;
    return ier;
}
} // extern "C"

//==============================================================================
//
// Implementation of KUSPPortableModel public wrapper functions
//
//==============================================================================

// ****************************** ********* **********************************
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelCreate
KUSPPortableModel::KUSPPortableModel(KIM::ModelCreate *const modelCreate, KIM::LengthUnit const requestedLengthUnit,
                                     KIM::EnergyUnit const requestedEnergyUnit,
                                     [[maybe_unused]] KIM::ChargeUnit const requestedChargeUnit,
                                     [[maybe_unused]] KIM::TemperatureUnit const requestedTemperatureUnit,
                                     [[maybe_unused]] KIM::TimeUnit const requestedTimeUnit, int *const ier) {
    // check if env variable for config file is set
    char *config_path = std::getenv("KUSP_CONFIG");
    std::string config_path_str;

    if (config_path == nullptr) {
        // if not, use default config file
        config_path_str = "./kusp_config.yaml";
    } else {
        config_path_str = std::string(config_path);
    }

    LOG_INFORMATION("Using config file: " + config_path_str);

    YAML::Node config = YAML::LoadFile(config_path_str);

    if (const auto protocol = config["protocol"].as<std::string>(); protocol != "ip") {
        LOG_ERROR("Invalid protocol type; perhaps KUSP 1.0 YAML file?");
        *ier = static_cast<int>(true);
        return;
    }

    YAML::Node server_config = config["server"];
    server_ip = server_config["host"].as<std::string>();
    server_port = server_config["port"].as<int>();
    if (server_config["timeout_send"]) {
        timeout_send_ms = server_config["timeout_send"].as<int>();
    }
    if (server_config["timeout_recv"]) {
        timeout_recv_ms = server_config["timeout_recv"].as<int>();
    }
    LOG_INFORMATION("Connecting to server running at: " + server_ip + ":" + std::to_string(server_port));
    LOG_DEBUG("Timeouts: Send - " + std::to_string(timeout_send_ms) + "ms ; Recv - " + std::to_string(timeout_recv_ms));
    influence_distance = config["model"]["influence_distance"].as<double>();
    for (auto const &element: config["model"]["species"]) {
        elements_list.push_back(element.as<std::string>());
    }

    // register required pointers for model driver
    *ier = modelCreate->SetUnits(requestedLengthUnit, requestedEnergyUnit, KIM::CHARGE_UNIT::unused,
                                 KIM::TEMPERATURE_UNIT::unused, KIM::TIME_UNIT::unused);
    if (*ier) {
        LOG_ERROR("Unable to SetUnits");
        return;
    }
    modelCreate->SetInfluenceDistancePointer(&influence_distance);
    willNotRequestNeighborsOfNonContributing = static_cast<int>(true); // not supported yet.
    modelCreate->SetNeighborListPointers(1, &influence_distance, &willNotRequestNeighborsOfNonContributing);
    int code = 0;
    for (auto const &element: elements_list) {
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
    if (*ier)
        return;

    KIM::ModelDestroyFunction *destroy = KUSPPortableModel::Destroy;
    KIM::ModelRefreshFunction *refresh = KUSPPortableModel::Refresh;
    KIM::ModelComputeFunction *compute = KUSPPortableModel::Compute;
    KIM::ModelComputeArgumentsCreateFunction *CACreate = KUSPPortableModel::ComputeArgumentsCreate;
    KIM::ModelComputeArgumentsDestroyFunction *CADestroy = KUSPPortableModel::ComputeArgumentsDestroy;

    *ier = modelCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::Destroy, KIM::LANGUAGE_NAME::cpp, true,
                                          reinterpret_cast<KIM::Function *>(destroy)) ||
           modelCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::Refresh, KIM::LANGUAGE_NAME::cpp, true,
                                          reinterpret_cast<KIM::Function *>(refresh)) ||
           modelCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::Compute, KIM::LANGUAGE_NAME::cpp, true,
                                          reinterpret_cast<KIM::Function *>(compute)) ||
           modelCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::ComputeArgumentsCreate, KIM::LANGUAGE_NAME::cpp,
                                          true, reinterpret_cast<KIM::Function *>(CACreate)) ||
           modelCreate->SetRoutinePointer(KIM::MODEL_ROUTINE_NAME::ComputeArgumentsDestroy, KIM::LANGUAGE_NAME::cpp,
                                          true, reinterpret_cast<KIM::Function *>(CADestroy));
}

// **************************************************************************
KUSPPortableModel::~KUSPPortableModel() = default;

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
    modelRefresh->SetNeighborListPointers(1, &modelObject->influence_distance,
                                          &modelObject->willNotRequestNeighborsOfNonContributing);
    return false;
}

//******************************************************************************
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelCompute
int KUSPPortableModel::Compute(KIM::ModelCompute const *const modelCompute,
                               KIM::ModelComputeArguments const *const modelComputeArguments) {

    // auto start_time = std::chrono::high_resolution_clock::now();

    KUSPPortableModel *modelObject;
    modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));

    // get number of particles
    int *numberOfParticlesPointer;
    int *particleContributing;
    int *speciesCode;

    double *coordinates;
    auto ier =
            modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::numberOfParticles,
                                                      &numberOfParticlesPointer) ||
            modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::particleContributing,
                                                      &particleContributing) ||
            modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::particleSpeciesCodes, &speciesCode) ||
            modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::coordinates, &coordinates);
    if (ier) {
        LOG_ERROR("Could not get number of particles @ Compute");
        return ier; // TODO: fix LOG_ERROR
    }

    // connect to socket
    ier = modelObject->init_socket(modelCompute);
    if (ier) {
        LOG_ERROR("Could not initialize socket");
        return ier;
    }

    // send data to socket
    ier = modelObject->data_to_socket(modelCompute, *numberOfParticlesPointer, speciesCode, coordinates,
                                      particleContributing);
    if (ier)
        return ier;

    // get data from socket
    double *energy;
    double *forces;
    double *particleEnergy;
    ier = modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::partialForces,
                                                    const_cast<double const **>(&forces)) ||
          modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::partialEnergy, &energy);// ||
          // modelComputeArguments->GetArgumentPointer(KIM::COMPUTE_ARGUMENT_NAME::partialParticleEnergy, &particleEnergy);

    if (ier) {
        return ier;
    }

    // receive data from socket
    ier = modelObject->data_from_socket(modelCompute, *numberOfParticlesPointer, energy, particleEnergy, forces);
    if (ier) {
        return ier;
    }
    // close socket
    modelObject->close_socket();
    return false;
}

//******************************************************************************
// static member function
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelComputeArgumentsCreate

int KUSPPortableModel::ComputeArgumentsCreate([[maybe_unused]] KIM::ModelCompute const *const modelCompute,
                                              KIM::ModelComputeArgumentsCreate *const modelComputeArgumentsCreate) {
    // KUSPPortableModel *modelObject;
    // modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    const int error = modelComputeArgumentsCreate->SetArgumentSupportStatus(KIM::COMPUTE_ARGUMENT_NAME::partialEnergy,
                                                                            KIM::SUPPORT_STATUS::required)
                      // Support state can be optional for energy
                      // But then we need to explicitly handle nullptr case
                      // As energy is anyway always needs to be computed
                      // as a prerequisite for force computation, easier to
                      // make it required.
                      // TODO: Handle it properly in future for GD models
                      || modelComputeArgumentsCreate->SetArgumentSupportStatus(
                                 KIM::COMPUTE_ARGUMENT_NAME::partialForces, KIM::SUPPORT_STATUS::optional) ||
                      modelComputeArgumentsCreate->SetArgumentSupportStatus(
                              KIM::COMPUTE_ARGUMENT_NAME::partialParticleEnergy, KIM::SUPPORT_STATUS::notSupported);
    return error;
}

//******************************************************************************
// static member function
int KUSPPortableModel::ComputeArgumentsDestroy(
        [[maybe_unused]] KIM::ModelCompute const *modelCompute,
        [[maybe_unused]] KIM::ModelComputeArgumentsDestroy *const modelComputeArgumentsDestroy) {
    // KUSPPortableModel *modelObject;
    // modelCompute->GetModelBufferPointer(reinterpret_cast<void **>(&modelObject));
    // nothing to do here?
    return false;
}

//==============================================================================
#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelCompute
int KUSPPortableModel::init_socket(KIM::ModelCompute const *modelCompute) {
    connection_socket = socket(AF_INET, SOCK_STREAM, 0);
    if (connection_socket == -1) {
        LOG_ERROR("Socket creation failed.\n");
        return true;
    }
    // set timeouts
    // recv
    timeval timeout{};
    timeout.tv_sec = timeout_recv_ms / 1000; // sec
    timeout.tv_usec = (timeout_recv_ms % 1000) * 1000; // remaining usec

    auto error_init = setsockopt(connection_socket, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof(timeout));
    if (error_init < 0) {
        close_socket();
        LOG_ERROR("Could not set socket option: RCV timeout");
        return true;
    }

    timeout.tv_sec = timeout_send_ms / 1000; // sec
    timeout.tv_usec = (timeout_send_ms % 1000) * 1000; // remaining usec

    error_init = setsockopt(connection_socket, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof(timeout));
    if (error_init < 0) {
        close_socket();
        LOG_ERROR("Could not set socket option: SND timeout");
        return true;
    }

    // set buffer size to 8MB
    // int buffer_size = 8 * 1024 * 1024;
    // setsockopt(connection_socket, SOL_SOCKET, SO_SNDBUF, &buffer_size, sizeof(buffer_size));
    // setsockopt(connection_socket, SOL_SOCKET, SO_RCVBUF, &buffer_size, sizeof(buffer_size));
    sockaddr_in server_address{};
    memset(&server_address, 0, sizeof(server_address));
    server_address.sin_family = AF_INET;
    server_address.sin_port = htons(server_port);
    inet_pton(AF_INET, server_ip.c_str(), &server_address.sin_addr);
    int connection_status =
            connect(connection_socket, reinterpret_cast<struct sockaddr *>(&server_address), sizeof(server_address));
    if (connection_status == -1) {
        LOG_ERROR("Error: connection failed. Please check if the server is running.");
        return true;
    }
    return false;
}

void KUSPPortableModel::close_socket() const { close(connection_socket); }

#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelCompute
int KUSPPortableModel::data_to_socket(KIM::ModelCompute const *modelCompute, const int n_atoms, const int *species,
                                      const double *coordinates, const int *particleContributing) const {
    constexpr int32_t size_of_int = sizeof(int);
    auto err_send = send(connection_socket, &size_of_int, sizeof(int32_t), 0);
    if (err_send == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LOG_ERROR("Error: int size send failed, TIMEOUT error, if this is unintentional, please increase "
                      "`timeout_send_ms` (in ms) in server block of the $KUSP_CONFIG file. Current value: " +
                      std::to_string(timeout_send_ms));
            return true;
        }
        LOG_ERROR("Error: int send failed, errno: " + std::to_string(errno));
        return true;
    }
    err_send = send(connection_socket, &n_atoms, sizeof(int), 0);
    if (err_send == -1) {
        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            LOG_ERROR("Error: n_atoms send failed, TIMEOUT error, if this is unintentional, please increase "
                      "`timeout_send_ms` (in ms) in server block of the $KUSP_CONFIG file. Current value: " +
                      std::to_string(timeout_send_ms));
            return true;
        }
        LOG_ERROR("Error: n_atoms send failed, errno: " + std::to_string(errno));
        return true;
    }

    const int outgoing_size = n_atoms * static_cast<int>(sizeof(int) * 2 + 3 * sizeof(double));
    const std::unique_ptr<char[]> buffer(new char[outgoing_size]);
    std::memcpy(buffer.get(), species, n_atoms * sizeof(int));
    std::memcpy(buffer.get() + n_atoms * sizeof(int), coordinates, n_atoms * 3 * sizeof(double));
    std::memcpy(buffer.get() + n_atoms * sizeof(int) + n_atoms * 3 * sizeof(double), particleContributing,
                n_atoms * sizeof(int));

    ssize_t bytes_sent = 0;
    while (bytes_sent < outgoing_size) {
        const auto bytes = send(connection_socket, buffer.get() + bytes_sent, outgoing_size - bytes_sent, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_ERROR(
                        "Error: config data send failed, TIMEOUT error, if this is unintentional, please increase "
                        "`timeout_send_ms` (in ms) in server block of the $KUSP_CONFIG file. Current value: " +
                        std::to_string(timeout_send_ms));
                return true;
            }
            LOG_ERROR("Error: config data send failed, errno: " + std::to_string(errno));
            return true;
        }
        bytes_sent += bytes;
    }
    return false;
}

#undef KIM_LOGGER_OBJECT_NAME
#define KIM_LOGGER_OBJECT_NAME modelCompute
int KUSPPortableModel::data_from_socket(KIM::ModelCompute const *modelCompute, int n_atoms, double *energy,
                                        [[maybe_unused]] double *particleEnergy, double *forces) const {
    // recv(connection_socket, energy, sizeof(double), 0);
    // recv(connection_socket, forces, n_atoms * 3 * sizeof(double), 0);
    const auto incoming_size = n_atoms * 3 * sizeof(double) + sizeof(double);
    const std::unique_ptr<char[]> buffer(new char[incoming_size]);

    unsigned long bytes_received = 0;
    while (bytes_received < incoming_size) {
        const auto bytes = recv(connection_socket, buffer.get() + bytes_received, incoming_size - bytes_received, 0);
        if (bytes == -1) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) {
                LOG_ERROR(
                        "Error: config data recv failed, TIMEOUT error, if this is unintentional, please increase "
                        "`timeout_recv_ms` (in ms) in server block of the $KUSP_CONFIG file. Current value: " +
                        std::to_string(timeout_recv_ms));
                return true;
            }
            LOG_ERROR("Error: config data recv failed, errno: " + std::to_string(errno));
            return true;
        }
        if (bytes == 0) {
            // connection closed
            break;
        }
        bytes_received += bytes;
    }
    if (bytes_received < incoming_size) {
        //incomplete transfer
        LOG_ERROR("Incomplete data received. received bytes < energy + forces");
        return true;
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
    return false;
}
