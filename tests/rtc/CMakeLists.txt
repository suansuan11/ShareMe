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

add_executable(
  shareme_screen_video_source_test
  screen_video_source_test.cpp
)
target_compile_features(
  shareme_screen_video_source_test
  PRIVATE
    cxx_std_20
)
target_include_directories(
  shareme_screen_video_source_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/client/rtc/screen/include"
)
target_link_libraries(
  shareme_screen_video_source_test
  PRIVATE
    ShareMe::ScreenRTC
)
shareme_set_project_warnings(shareme_screen_video_source_test)
add_test(
  NAME screen_video_source
  COMMAND shareme_screen_video_source_test
)

add_executable(
  shareme_macos_screen_capture_source_test
  macos_screen_capture_source_test.cpp
)
target_compile_features(
  shareme_macos_screen_capture_source_test
  PRIVATE
    cxx_std_20
)
target_link_libraries(
  shareme_macos_screen_capture_source_test
  PRIVATE
    ShareMe::ScreenRTC
)
shareme_set_project_warnings(shareme_macos_screen_capture_source_test)
add_test(
  NAME macos_screen_capture_source
  COMMAND shareme_macos_screen_capture_source_test
)

add_executable(
  shareme_video_encoder_selection_test
  video_encoder_selection_test.cpp
)
target_compile_features(
  shareme_video_encoder_selection_test
  PRIVATE
    cxx_std_20
)
target_link_libraries(
  shareme_video_encoder_selection_test
  PRIVATE
    ShareMe::WebRTC
)
shareme_set_project_warnings(shareme_video_encoder_selection_test)
add_test(
  NAME video_encoder_selection
  COMMAND shareme_video_encoder_selection_test
)

add_executable(
  shareme_loopback_signaling_test
  loopback_signaling_test.cpp
)

target_compile_features(
  shareme_loopback_signaling_test
  PRIVATE
    cxx_std_20
)
target_include_directories(
  shareme_loopback_signaling_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
)
target_link_libraries(
  shareme_loopback_signaling_test
  PRIVATE
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_loopback_signaling_test)

add_test(
  NAME loopback_signaling
  COMMAND shareme_loopback_signaling_test
)

add_executable(
  shareme_audio_device_factory_test
  audio_device_factory_test.cpp
)

target_compile_features(
  shareme_audio_device_factory_test
  PRIVATE
    cxx_std_20
)
target_include_directories(
  shareme_audio_device_factory_test
  PRIVATE
    "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
)
target_link_libraries(
  shareme_audio_device_factory_test
  PRIVATE
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_audio_device_factory_test)

add_test(
  NAME audio_device_factory
  COMMAND shareme_audio_device_factory_test
)

add_executable(
  shareme_webrtc_loopback_test
  webrtc_loopback_test.cpp
)

target_compile_features(
  shareme_webrtc_loopback_test
  PRIVATE
    cxx_std_20
)
target_link_libraries(
  shareme_webrtc_loopback_test
  PRIVATE
    ShareMe::WebRTC
)

shareme_set_project_warnings(shareme_webrtc_loopback_test)

add_test(
  NAME webrtc_loopback
  COMMAND shareme_webrtc_loopback_test
)
set_tests_properties(
  webrtc_loopback
  PROPERTIES
    TIMEOUT 15
)

add_executable(shareme_signaled_peer_test signaled_peer_test.cpp)
target_link_libraries(shareme_signaled_peer_test PRIVATE ShareMe::WebRTC)
shareme_set_project_warnings(shareme_signaled_peer_test)
add_test(NAME signaled_peer COMMAND shareme_signaled_peer_test)

add_executable(shareme_movie_audio_peer_test movie_audio_peer_test.cpp)
target_link_libraries(shareme_movie_audio_peer_test PRIVATE ShareMe::WebRTC)
shareme_set_project_warnings(shareme_movie_audio_peer_test)
add_test(NAME movie_audio_peer COMMAND shareme_movie_audio_peer_test)
set_tests_properties(movie_audio_peer PROPERTIES TIMEOUT 15)

add_executable(
  shareme_drift_scenario_test
  drift_scenario_test.cpp
  "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/drift_scenario.cpp"
)
target_include_directories(
  shareme_drift_scenario_test
  PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
)
target_compile_features(shareme_drift_scenario_test PRIVATE cxx_std_20)
target_link_libraries(shareme_drift_scenario_test PRIVATE ShareMe::Core)
shareme_set_project_warnings(shareme_drift_scenario_test)
add_test(NAME drift_scenario COMMAND shareme_drift_scenario_test)

if(TARGET ShareMe::QtSignaling)
  add_executable(
    shareme_playback_state_test
    playback_state_test.cpp
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/playback_state.cpp"
  )
  target_include_directories(
    shareme_playback_state_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
  )
  target_compile_features(shareme_playback_state_test PRIVATE cxx_std_20)
  target_link_libraries(shareme_playback_state_test PRIVATE Qt6::Core)
  shareme_set_project_warnings(shareme_playback_state_test)
  add_test(NAME playback_state COMMAND shareme_playback_state_test)

  add_executable(
    shareme_movie_audio_clock_message_test
    movie_audio_clock_message_test.cpp
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/movie_audio_clock_message.cpp"
  )
  target_include_directories(
    shareme_movie_audio_clock_message_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
  )
  target_compile_features(
    shareme_movie_audio_clock_message_test
    PRIVATE cxx_std_20
  )
  target_link_libraries(
    shareme_movie_audio_clock_message_test
    PRIVATE ShareMe::Core Qt6::Core
  )
  shareme_set_project_warnings(shareme_movie_audio_clock_message_test)
  add_test(
    NAME movie_audio_clock_message
    COMMAND shareme_movie_audio_clock_message_test
  )

  add_executable(
    shareme_playout_report_test
    playout_report_test.cpp
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/playout_report.cpp"
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/playback_state.cpp"
  )
  target_include_directories(
    shareme_playout_report_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
  )
  target_compile_features(shareme_playout_report_test PRIVATE cxx_std_20)
  target_link_libraries(shareme_playout_report_test PRIVATE Qt6::Core)
  shareme_set_project_warnings(shareme_playout_report_test)
  add_test(NAME playout_report COMMAND shareme_playout_report_test)

  add_executable(
    shareme_drift_metrics_jsonl_test
    drift_metrics_jsonl_test.cpp
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/drift_metrics_jsonl.cpp"
  )
  target_include_directories(
    shareme_drift_metrics_jsonl_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
  )
  target_compile_features(shareme_drift_metrics_jsonl_test PRIVATE cxx_std_20)
  target_link_libraries(
    shareme_drift_metrics_jsonl_test
    PRIVATE
      ShareMe::Core
      Qt6::Core
  )
  shareme_set_project_warnings(shareme_drift_metrics_jsonl_test)
  add_test(NAME drift_metrics_jsonl COMMAND shareme_drift_metrics_jsonl_test)

  add_executable(
    shareme_drift_failure_test
    drift_failure_test.cpp
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/drift_failure.cpp"
  )
  target_include_directories(
    shareme_drift_failure_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
  )
  target_compile_features(shareme_drift_failure_test PRIVATE cxx_std_20)
  target_link_libraries(shareme_drift_failure_test PRIVATE Qt6::Core)
  shareme_set_project_warnings(shareme_drift_failure_test)
  add_test(NAME drift_failure COMMAND shareme_drift_failure_test)
endif()

if(WIN32)
  add_executable(
    shareme_desktop_frame_converter_test
    desktop_frame_converter_test.cpp
  )
  target_include_directories(
    shareme_desktop_frame_converter_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/rtc/desktop/src"
  )
  target_link_libraries(
    shareme_desktop_frame_converter_test
    PRIVATE ShareMe::DesktopRTC
  )
  shareme_set_project_warnings(shareme_desktop_frame_converter_test)
  add_test(
    NAME desktop_frame_converter
    COMMAND shareme_desktop_frame_converter_test
  )

  add_executable(
    shareme_desktop_capture_source_test
    desktop_capture_source_test.cpp
  )
  target_include_directories(
    shareme_desktop_capture_source_test
    PRIVATE
      "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
      "${PROJECT_SOURCE_DIR}/client/rtc/desktop/src"
  )
  target_link_libraries(
    shareme_desktop_capture_source_test
    PRIVATE ShareMe::DesktopRTC
  )
  shareme_set_project_warnings(shareme_desktop_capture_source_test)
  add_test(
    NAME desktop_capture_source
    COMMAND shareme_desktop_capture_source_test
  )
  set_tests_properties(
    desktop_capture_source
    PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 10
  )
endif()

if(TARGET ShareMe::Playback)
  find_program(SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE ffmpeg REQUIRED)
  set(
    SHAREME_MOVIE_CALL_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-movie-call.mp4"
  )
  set(
    SHAREME_AUDIO_ONLY_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-audio-only.m4a"
  )
  set(
    SHAREME_NONZERO_PTS_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-nonzero-pts.mp4"
  )
  set(
    SHAREME_PTS_GAP_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-pts-gap.mp4"
  )
  set(
    SHAREME_STAGGERED_AV_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-staggered-av.mp4"
  )
  set(
    SHAREME_VIDEO_ONLY_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-video-only.mp4"
  )
  set(
    SHAREME_AUDIO_GAP_FIXTURE
    "${CMAKE_CURRENT_BINARY_DIR}/generated-audio-gap.m4a"
  )
  add_test(
    NAME generate_movie_call_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "testsrc2=size=320x180:rate=30"
      -f lavfi
      -i "sine=frequency=660:sample_rate=48000"
      -t 2
      -c:v mpeg4
      -pix_fmt yuv420p
      -c:a aac
      "${SHAREME_MOVIE_CALL_FIXTURE}"
  )
  set_tests_properties(
    generate_movie_call_fixture
    PROPERTIES FIXTURES_SETUP movie_call_fixture
  )
  add_test(
    NAME generate_audio_only_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "sine=frequency=880:sample_rate=48000"
      -t 1
      -c:a aac
      "${SHAREME_AUDIO_ONLY_FIXTURE}"
  )
  set_tests_properties(
    generate_audio_only_fixture
    PROPERTIES FIXTURES_SETUP audio_only_fixture
  )
  add_test(
    NAME generate_nonzero_pts_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "testsrc2=size=320x180:rate=30"
      -vf "setpts=PTS+5/TB"
      -frames:v 60
      -c:v mpeg4
      -pix_fmt yuv420p
      -copyts
      "${SHAREME_NONZERO_PTS_FIXTURE}"
  )
  set_tests_properties(
    generate_nonzero_pts_fixture
    PROPERTIES FIXTURES_SETUP nonzero_pts_fixture
  )
  add_test(
    NAME generate_pts_gap_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "testsrc2=size=320x180:rate=1"
      -vf "setpts=N*5/TB"
      -frames:v 2
      -fps_mode vfr
      -c:v mpeg4
      -pix_fmt yuv420p
      "${SHAREME_PTS_GAP_FIXTURE}"
  )
  set_tests_properties(
    generate_pts_gap_fixture
    PROPERTIES FIXTURES_SETUP pts_gap_fixture
  )
  add_test(
    NAME generate_staggered_av_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "testsrc2=size=320x180:rate=30:duration=2"
      -f lavfi
      -i "sine=frequency=660:sample_rate=48000:duration=2"
      -filter_complex
      "[0:v]setpts=PTS+5/TB[v];[1:a]asetpts=PTS+4/TB[a]"
      -map "[v]"
      -map "[a]"
      -c:v mpeg4
      -pix_fmt yuv420p
      -c:a aac
      "${SHAREME_STAGGERED_AV_FIXTURE}"
  )
  set_tests_properties(
    generate_staggered_av_fixture
    PROPERTIES FIXTURES_SETUP staggered_av_fixture
  )
  add_test(
    NAME generate_video_only_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "testsrc2=size=320x180:rate=30"
      -t 1
      -c:v mpeg4
      -pix_fmt yuv420p
      "${SHAREME_VIDEO_ONLY_FIXTURE}"
  )
  set_tests_properties(
    generate_video_only_fixture
    PROPERTIES FIXTURES_SETUP video_only_fixture
  )
  add_test(
    NAME generate_audio_gap_fixture
    COMMAND
      "${SHAREME_MOVIE_CALL_FFMPEG_EXECUTABLE}"
      -hide_banner
      -loglevel error
      -y
      -f lavfi
      -i "sine=frequency=440:sample_rate=48000:duration=6"
      -af "aselect=lt(t\\,0.05)+gte(t\\,5)"
      -c:a aac
      -copyts
      "${SHAREME_AUDIO_GAP_FIXTURE}"
  )
  set_tests_properties(
    generate_audio_gap_fixture
    PROPERTIES FIXTURES_SETUP audio_gap_fixture
  )

  add_executable(
    shareme_video_preview_adapter_test
    video_preview_adapter_test.cpp
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/video_preview_adapter.cpp"
    "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/movie_video_playout_adapter.cpp"
  )
  target_include_directories(
    shareme_video_preview_adapter_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo"
  )
  target_link_libraries(
    shareme_video_preview_adapter_test
    PRIVATE ShareMe::Core ShareMe::WebRTC Qt6::Core Qt6::Gui Qt6::Multimedia
  )
  shareme_set_project_warnings(shareme_video_preview_adapter_test)
  add_test(NAME video_preview_adapter COMMAND shareme_video_preview_adapter_test)
  set_tests_properties(
    video_preview_adapter
    PROPERTIES
      TIMEOUT 10
      ENVIRONMENT_MODIFICATION "QT_QPA_PLATFORM=set:offscreen"
  )

  add_executable(shareme_movie_timeline_test movie_timeline_test.cpp)
  target_link_libraries(
    shareme_movie_timeline_test
    PRIVATE ShareMe::MovieRTC
  )
  shareme_set_project_warnings(shareme_movie_timeline_test)
  add_test(NAME movie_timeline COMMAND shareme_movie_timeline_test)

  add_executable(shareme_movie_video_source_test movie_video_source_test.cpp)
  target_include_directories(
    shareme_movie_video_source_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
  )
  target_link_libraries(
    shareme_movie_video_source_test
    PRIVATE ShareMe::MovieRTC
  )
  shareme_set_project_warnings(shareme_movie_video_source_test)
  add_test(
    NAME movie_video_source
    COMMAND
      shareme_movie_video_source_test
      "${SHAREME_MOVIE_CALL_FIXTURE}"
      "${SHAREME_AUDIO_ONLY_FIXTURE}"
      "${SHAREME_NONZERO_PTS_FIXTURE}"
      "${SHAREME_PTS_GAP_FIXTURE}"
      "${SHAREME_STAGGERED_AV_FIXTURE}"
  )
  set_tests_properties(
    movie_video_source
    PROPERTIES
      FIXTURES_REQUIRED
        "movie_call_fixture;audio_only_fixture;nonzero_pts_fixture;pts_gap_fixture;staggered_av_fixture"
      TIMEOUT 15
  )

  add_executable(shareme_movie_audio_source_test movie_audio_source_test.cpp)
  target_include_directories(
    shareme_movie_audio_source_test
    PRIVATE "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src"
  )
  target_link_libraries(
    shareme_movie_audio_source_test
    PRIVATE ShareMe::MovieRTC
  )
  shareme_set_project_warnings(shareme_movie_audio_source_test)
  add_test(
    NAME movie_audio_source
    COMMAND
      shareme_movie_audio_source_test
      "${SHAREME_MOVIE_CALL_FIXTURE}"
      "${SHAREME_AUDIO_ONLY_FIXTURE}"
      "${SHAREME_VIDEO_ONLY_FIXTURE}"
      "${SHAREME_AUDIO_GAP_FIXTURE}"
      "${SHAREME_STAGGERED_AV_FIXTURE}"
      "${SHAREME_GENERATED_NEGATIVE_AV_MEDIA}"
  )
  set_tests_properties(
    movie_audio_source
    PROPERTIES
      FIXTURES_REQUIRED
        "movie_call_fixture;audio_only_fixture;video_only_fixture;audio_gap_fixture;staggered_av_fixture;negative_av_media_fixture"
      TIMEOUT 15
  )
endif()
