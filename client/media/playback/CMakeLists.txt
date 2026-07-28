add_library(shareme_playback STATIC src/media_time.cpp)
add_library(ShareMe::Playback ALIAS shareme_playback)

target_compile_features(shareme_playback PUBLIC cxx_std_20)
target_include_directories(
  shareme_playback
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  shareme_playback
  PUBLIC
    ShareMe::Core
  PRIVATE
    FFmpeg::avformat
    FFmpeg::avcodec
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
)

shareme_set_project_warnings(shareme_playback)
