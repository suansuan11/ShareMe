add_library(
  shareme_movie_rtc
  STATIC
    src/movie_audio_source.cpp
    src/movie_timeline.cpp
    src/movie_video_source.cpp
)
add_library(ShareMe::MovieRTC ALIAS shareme_movie_rtc)

target_compile_features(shareme_movie_rtc PUBLIC cxx_std_20)
target_include_directories(
  shareme_movie_rtc
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)
target_link_libraries(
  shareme_movie_rtc
  PUBLIC
    ShareMe::Playback
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_movie_rtc)
