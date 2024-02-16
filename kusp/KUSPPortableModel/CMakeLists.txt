cmake_minimum_required(VERSION 3.10..3.18)

set(CMAKE_CXX_STANDARD 17)
cmake_policy(SET CMP0074 NEW)
STRING(COMPARE EQUAL "${CMAKE_BUILD_TYPE}" "" flg)
if(flg)
    set(CMAKE_BUILD_TYPE Release)
endif()
message("Build Type: ${CMAKE_BUILD_TYPE}")

# KIM-API CMAKE FILES-------------------------
if (NOT DEFINED KIM_ROOT)
    if (DEFINED ENV{KIM_API_CMAKE_PREFIX_DIR})
        set(KIM_ROOT $ENV{KIM_API_CMAKE_PREFIX_DIR})
    else()
        set(KIM_ROOT "/usr/local")
    endif()
endif()

# print all files -----------------------------------------
message("KIM PREFIX DIR: ${KIM_ROOT}")

# YAML-CPP setup -----------------------------------------
include(FetchContent)

FetchContent_Declare(
  yaml-cpp
  GIT_REPOSITORY https://github.com/jbeder/yaml-cpp.git
  GIT_TAG "0.8.0" # Can be a tag (yaml-cpp-x.x.x), a commit hash, or a branch name (master)
)
FetchContent_GetProperties(yaml-cpp)

if(NOT yaml-cpp_POPULATED)
  message(STATUS "Fetching yaml-cpp...")
  FetchContent_Populate(yaml-cpp)
  add_subdirectory(${yaml-cpp_SOURCE_DIR} ${yaml-cpp_BINARY_DIR})
endif()

target_link_libraries(${PROJECT_NAME} PRIVATE yaml-cpp::yaml-cpp) # The library or executable that


find_package(KIM-API-ITEMS 2.2 REQUIRED CONFIG)
# KIM-API setup --------------------------------------------------
kim_api_items_setup_before_project(ITEM_TYPE "portableModel")
project(KUSP__MO_000000000000_000 LANGUAGES CXX)
kim_api_items_setup_after_project(ITEM_TYPE "portableModel")

add_kim_api_model_library(
  NAME                    ${PROJECT_NAME}
  CREATE_ROUTINE_NAME     "model_create"
  CREATE_ROUTINE_LANGUAGE "cpp"
)

target_sources(${PROJECT_NAME} PRIVATE
        KUSPPortableModel.cpp
)

set_property(TARGET ${PROJECT_NAME} PROPERTY CXX_STANDARD 17)
