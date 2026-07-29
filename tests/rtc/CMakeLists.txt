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
