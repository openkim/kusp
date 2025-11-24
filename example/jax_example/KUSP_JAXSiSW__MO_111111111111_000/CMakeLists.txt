cmake_minimum_required(VERSION 3.10)

list(APPEND CMAKE_PREFIX_PATH $ENV{KIM_API_CMAKE_PREFIX_DIR})
find_package(KIM-API-ITEMS 2.2 REQUIRED CONFIG)

kim_api_items_setup_before_project(ITEM_TYPE "portableModel")
project(KUSP_JAXSiSW__MO_111111111111_000 LANGUAGES CXX)
kim_api_items_setup_after_project(ITEM_TYPE "portableModel")

add_kim_api_model_library(
  NAME                    "${PROJECT_NAME}"
  DRIVER_NAME             "KUSP__MD_000000000000_000"
  PARAMETER_FILES         "@kusp_model_6ea42e7127c5b38b_KUSP_JAXSiSW__MO_111111111111_000.py" "kusp_env.ast.env"
)
