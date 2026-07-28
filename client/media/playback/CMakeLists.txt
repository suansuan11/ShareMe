add_library(shareme_playback INTERFACE)
add_library(ShareMe::Playback ALIAS shareme_playback)

target_compile_features(shareme_playback INTERFACE cxx_std_20)
target_link_libraries(
  shareme_playback
  INTERFACE
    ShareMe::Core
    FFmpeg::avformat
    FFmpeg::avcodec
    FFmpeg::avutil
    FFmpeg::swscale
    FFmpeg::swresample
)

shareme_set_project_warnings(shareme_playback)
