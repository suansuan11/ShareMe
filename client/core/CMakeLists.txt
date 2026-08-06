add_library(
  shareme_core
  STATIC
  src/movie_audio_clock.cpp
  src/movie_audio_pts_mapper.cpp
  src/movie_audio_renderer.cpp
  src/movie_video_scheduler.cpp
  src/playback_failure.cpp
  src/drift_metrics.cpp
  src/probe_cli.cpp
  src/probe_contract.cpp
  src/sync_controller.cpp
  src/signaling_session.cpp
  src/video_quality_contract.cpp
)
add_library(ShareMe::Core ALIAS shareme_core)

target_compile_features(shareme_core PUBLIC cxx_std_20)
target_include_directories(
  shareme_core
  PUBLIC
    "$<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>"
)

shareme_set_project_warnings(shareme_core)
