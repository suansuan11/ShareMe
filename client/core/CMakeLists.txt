add_library(shareme_core INTERFACE)
add_library(ShareMe::Core ALIAS shareme_core)

target_compile_features(shareme_core INTERFACE cxx_std_20)
target_include_directories(
  shareme_core
  INTERFACE
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

shareme_set_project_warnings(shareme_core)
