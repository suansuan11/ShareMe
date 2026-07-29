add_library(
  shareme_webrtc
  STATIC
  src/audio_device_factory.cpp
  src/loopback_signaling.cpp
  src/test_pattern_source.cpp
  src/webrtc_runtime.cpp
)
add_library(ShareMe::WebRTC ALIAS shareme_webrtc)

target_compile_features(shareme_webrtc PUBLIC cxx_std_20)
target_include_directories(
  shareme_webrtc
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_link_libraries(
  shareme_webrtc
  PUBLIC
    ShareMe::Core
    WebRTC::webrtc
)

shareme_set_project_warnings(shareme_webrtc)
