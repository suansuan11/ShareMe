add_library(
  shareme_webrtc
  STATIC
  src/audio_device_factory.cpp
  src/loopback_signaling.cpp
  src/movie_audio_peer.cpp
  src/signaled_peer.cpp
  src/test_pattern_source.cpp
  src/video_encoder_selection.cpp
  src/webrtc_probe.cpp
  src/webrtc_runtime.cpp
  src/windows/h264_bitstream.cpp
)
add_library(ShareMe::WebRTC ALIAS shareme_webrtc)

if(APPLE)
  enable_language(OBJCXX)
  target_sources(
    shareme_webrtc
    PRIVATE
      src/microphone_permission.mm
      src/macos_video_encoder_selection.mm
  )
  set_source_files_properties(
    src/microphone_permission.mm
    PROPERTIES
      COMPILE_OPTIONS "-fblocks"
  )
  set_source_files_properties(
    src/macos_video_encoder_selection.mm
    PROPERTIES
      COMPILE_OPTIONS "-fblocks;-fobjc-arc"
  )
  target_include_directories(
    shareme_webrtc
    PRIVATE
      "${WebRTC_INCLUDE_DIR}/sdk/objc"
      "${WebRTC_INCLUDE_DIR}/sdk/objc/base"
      "${WebRTC_INCLUDE_DIR}/sdk/objc/components/video_codec"
  )
  target_link_libraries(
    shareme_webrtc
    PRIVATE
      "-framework AVFoundation"
      "-framework Foundation"
  )
else()
  target_sources(
    shareme_webrtc
    PRIVATE
      src/microphone_permission.cpp
  )
endif()

if(WIN32)
  target_sources(
    shareme_webrtc
    PRIVATE
      src/windows_mf_h264_buffers.cpp
      src/windows_mf_h264_decoder.cpp
      src/windows_mf_h264_encoder.cpp
      src/windows_mf_video_codec.cpp
  )
  target_link_libraries(
    shareme_webrtc
    PRIVATE
      mf
      mfplat
      mfreadwrite
      mfuuid
      ole32
  )
endif()

target_compile_features(shareme_webrtc PUBLIC cxx_std_20)
target_include_directories(
  shareme_webrtc
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/src>"
)
target_link_libraries(
  shareme_webrtc
  PUBLIC
    ShareMe::Core
    WebRTC::webrtc
)

shareme_set_project_warnings(shareme_webrtc)
