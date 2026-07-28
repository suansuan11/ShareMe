add_library(
  shareme_core
  STATIC
  src/probe_contract.cpp
  src/sync_controller.cpp
)
add_library(ShareMe::Core ALIAS shareme_core)

target_compile_features(shareme_core PUBLIC cxx_std_20)
target_include_directories(
  shareme_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

shareme_set_project_warnings(shareme_core)
