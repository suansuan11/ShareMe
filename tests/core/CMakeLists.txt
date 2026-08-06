add_executable(shareme_bounded_queue_test bounded_queue_test.cpp)
target_link_libraries(shareme_bounded_queue_test PRIVATE ShareMe::Core)

add_test(NAME bounded_queue COMMAND shareme_bounded_queue_test)

add_executable(shareme_sync_controller_test sync_controller_test.cpp)
target_link_libraries(shareme_sync_controller_test PRIVATE ShareMe::Core)

add_test(NAME sync_controller COMMAND shareme_sync_controller_test)

add_executable(shareme_drift_metrics_test drift_metrics_test.cpp)
target_link_libraries(shareme_drift_metrics_test PRIVATE ShareMe::Core)

add_test(NAME drift_metrics COMMAND shareme_drift_metrics_test)

add_executable(
  shareme_video_quality_contract_test
  video_quality_contract_test.cpp
)
target_link_libraries(shareme_video_quality_contract_test PRIVATE ShareMe::Core)

add_test(
  NAME video_quality_contract
  COMMAND shareme_video_quality_contract_test
)

add_executable(shareme_probe_contract_test probe_contract_test.cpp)
target_link_libraries(shareme_probe_contract_test PRIVATE ShareMe::Core)

add_test(NAME probe_contract COMMAND shareme_probe_contract_test)

add_executable(shareme_candidate_stager_test candidate_stager_test.cpp)
target_link_libraries(shareme_candidate_stager_test PRIVATE ShareMe::Core)

add_test(NAME candidate_stager COMMAND shareme_candidate_stager_test)

add_executable(shareme_probe_cli_test probe_cli_test.cpp)
target_link_libraries(shareme_probe_cli_test PRIVATE ShareMe::Core)

add_test(NAME probe_cli COMMAND shareme_probe_cli_test)

add_executable(shareme_signaling_session_test signaling_session_test.cpp)
target_link_libraries(shareme_signaling_session_test PRIVATE ShareMe::Core)
add_test(NAME signaling_session COMMAND shareme_signaling_session_test)

add_executable(shareme_playback_failure_test playback_failure_test.cpp)
target_link_libraries(shareme_playback_failure_test PRIVATE ShareMe::Core)

add_test(NAME playback_failure COMMAND shareme_playback_failure_test)

add_executable(shareme_audio_output_contract_test audio_output_contract_test.cpp)
target_link_libraries(shareme_audio_output_contract_test PRIVATE ShareMe::Core)

add_test(
  NAME audio_output_contract
  COMMAND shareme_audio_output_contract_test
)

add_executable(
  shareme_movie_audio_pts_mapper_test
  movie_audio_pts_mapper_test.cpp
)
target_link_libraries(shareme_movie_audio_pts_mapper_test PRIVATE ShareMe::Core)

add_test(
  NAME movie_audio_pts_mapper
  COMMAND shareme_movie_audio_pts_mapper_test
)

add_executable(
  shareme_movie_audio_renderer_test
  movie_audio_renderer_test.cpp
)
target_link_libraries(shareme_movie_audio_renderer_test PRIVATE ShareMe::Core)

add_test(
  NAME movie_audio_renderer
  COMMAND shareme_movie_audio_renderer_test
)

add_executable(
  shareme_movie_video_scheduler_test
  movie_video_scheduler_test.cpp
)
target_link_libraries(shareme_movie_video_scheduler_test PRIVATE ShareMe::Core)

add_test(
  NAME movie_video_scheduler
  COMMAND shareme_movie_video_scheduler_test
)

add_executable(
  shareme_qt_audio_output_device_test
  qt_audio_output_device_test.cpp
  "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/qt_audio_output_device.cpp"
)
target_include_directories(
  shareme_qt_audio_output_device_test
  PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
)
target_link_libraries(
  shareme_qt_audio_output_device_test
  PRIVATE ShareMe::Core Qt6::Core Qt6::Multimedia
)

add_test(
  NAME qt_audio_output_device_offscreen
  COMMAND shareme_qt_audio_output_device_test
)
set_tests_properties(
  qt_audio_output_device_offscreen
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)
