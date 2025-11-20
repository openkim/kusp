cmake_minimum_required(VERSION 3.10)

set(CMAKE_CXX_STANDARD 17)
cmake_policy(SET CMP0074 NEW)
STRING(COMPARE EQUAL "${CMAKE_BUILD_TYPE}" "" flg)
if(flg)
    set(CMAKE_BUILD_TYPE Debug)
endif()
message("Build Type: ${CMAKE_BUILD_TYPE}")
find_package(Python3 COMPONENTS Development REQUIRED)

message(STATUS "Python3_EXECUTABLE = ${Python3_EXECUTABLE}")
message(STATUS "Python3_LIBRARY    = ${Python3_LIBRARY}")
message(STATUS "Python3_INCLUDE_DIR= ${Python3_INCLUDE_DIR}")


# TODO: Log this error and behaviour
#-- Found KIM-API: /opt/kim_api/install/lib/libkim-api.so.2.4.1
#CMake Error at /opt/kim_api/install/share/cmake/kim-api-items/kim-api-items-macros.cmake:515 (message):
#  Unable to locate 'base64-encode' utility
#Call Stack (most recent call first):
#  /opt/kim_api/install/share/cmake/kim-api-items/kim-api-items-macros.cmake:597 (_add_custom_command_blob_to_cpp)
#  /opt/kim_api/install/share/cmake/kim-api-items/kim-api-items-macros.cmake:785 (_encode_process_files)
#  /opt/kim_api/install/share/cmake/kim-api-items/kim-api-items-macros.cmake:145 (_add_kim_api_library)
#  CMakeLists.txt:24 (add_kim_api_model_driver_library)
#
#
#-- Configuring incomplete, errors occurred!
#See also "/home/amit/Projects/FERMAT/KLIFF_Serve/kliff_serve/kusp/KUSP__MD_000000000000_000/tmp2/CMakeFiles/CMakeOutput.log".


# YAML-CPP setup -----------------------------------------
add_subdirectory(ext/yaml-cpp)
include_directories(ext/yaml-cpp/include)

add_subdirectory(ext/pybind11)
include_directories(ext/pybind11/include)
list(APPEND CMAKE_PREFIX_PATH $ENV{KIM_API_CMAKE_PREFIX_DIR})
find_package(KIM-API-ITEMS 2.2 REQUIRED CONFIG)
# KIM-API setup --------------------------------------------------
kim_api_items_setup_before_project(ITEM_TYPE "modelDriver")
project(KUSP__MD_000000000000_000 LANGUAGES CXX)
kim_api_items_setup_after_project(ITEM_TYPE "modelDriver")

add_kim_api_model_driver_library(
  NAME                    ${PROJECT_NAME}
  CREATE_ROUTINE_NAME     "model_driver_create"
  CREATE_ROUTINE_LANGUAGE "cpp"
)

target_sources(${PROJECT_NAME} PRIVATE
        KUSPModelDriverImplementation.cpp
        KUSPModelDriver.cpp
        KUSPModel.cpp
)

add_executable(test_kusp
    main.cpp
    KUSPModel.cpp
)

target_include_directories(test_kusp PUBLIC .)

target_link_libraries(test_kusp PRIVATE pybind11::embed)
target_link_libraries(${PROJECT_NAME} PRIVATE pybind11::embed)

target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp)
set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
set_property(TARGET test_kusp PROPERTY CXX_STANDARD 17)
