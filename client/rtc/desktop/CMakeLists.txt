add_library(
  shareme_desktop_rtc
  STATIC
    src/desktop_capture_source.cpp
    src/desktop_frame_converter.cpp
)
add_library(ShareMe::DesktopRTC ALIAS shareme_desktop_rtc)

target_compile_features(shareme_desktop_rtc PUBLIC cxx_std_20)
target_include_directories(
  shareme_desktop_rtc
  PUBLIC "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
  PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}/src"
)
target_link_libraries(
  shareme_desktop_rtc
  PUBLIC ShareMe::WebRTC
  PRIVATE d3d11 dxgi
)

shareme_set_project_warnings(shareme_desktop_rtc)
