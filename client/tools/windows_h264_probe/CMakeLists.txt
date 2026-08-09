add_executable(shareme_windows_h264_probe main.cpp)
target_compile_features(shareme_windows_h264_probe PRIVATE cxx_std_20)
target_include_directories(
  shareme_windows_h264_probe
  PRIVATE "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
)
target_link_libraries(shareme_windows_h264_probe PRIVATE ShareMe::WebRTC)
shareme_set_project_warnings(shareme_windows_h264_probe)
