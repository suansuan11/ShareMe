add_library(
  shareme_screen_rtc
  STATIC
  src/macos_screen_capture_source.cpp
  src/screen_video_source.cpp
  src/screen_video_source_factory.cpp
)
add_library(ShareMe::ScreenRTC ALIAS shareme_screen_rtc)

target_compile_features(shareme_screen_rtc PUBLIC cxx_std_20)
target_include_directories(
  shareme_screen_rtc
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  shareme_screen_rtc
  PUBLIC
    ShareMe::Core
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_screen_rtc)

if(APPLE)
  enable_language(OBJCXX)
  target_sources(
    shareme_screen_rtc
    PRIVATE
      src/macos_screen_capture_source.mm
  )
  set_source_files_properties(
    src/macos_screen_capture_source.mm
    PROPERTIES
      COMPILE_OPTIONS "-fblocks;-fobjc-arc"
  )
  target_include_directories(
    shareme_screen_rtc
    PRIVATE
      "${WebRTC_INCLUDE_DIR}/sdk/objc/base"
  )
  target_link_libraries(
    shareme_screen_rtc
    PRIVATE
      "-framework CoreGraphics"
      "-framework CoreMedia"
      "-framework CoreVideo"
      "-framework Foundation"
      "-framework ScreenCaptureKit"
  )
endif()
