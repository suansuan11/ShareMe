add_executable(
  shareme_test_pattern_source_test
  test_pattern_source_test.cpp
)

target_compile_features(
  shareme_test_pattern_source_test
  PRIVATE
    cxx_std_20
)
target_include_directories(
  shareme_test_pattern_source_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
)
target_link_libraries(
  shareme_test_pattern_source_test
  PRIVATE
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_test_pattern_source_test)

add_test(
  NAME test_pattern_source
  COMMAND shareme_test_pattern_source_test
)

add_executable(
  shareme_loopback_signaling_test
  loopback_signaling_test.cpp
)

target_compile_features(
  shareme_loopback_signaling_test
  PRIVATE
    cxx_std_20
)
target_include_directories(
  shareme_loopback_signaling_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
)
target_link_libraries(
  shareme_loopback_signaling_test
  PRIVATE
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_loopback_signaling_test)

add_test(
  NAME loopback_signaling
  COMMAND shareme_loopback_signaling_test
)

add_executable(
  shareme_audio_device_factory_test
  audio_device_factory_test.cpp
)

target_compile_features(
  shareme_audio_device_factory_test
  PRIVATE
    cxx_std_20
)
target_include_directories(
  shareme_audio_device_factory_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
)
target_link_libraries(
  shareme_audio_device_factory_test
  PRIVATE
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_audio_device_factory_test)

add_test(
  NAME audio_device_factory
  COMMAND shareme_audio_device_factory_test
)
