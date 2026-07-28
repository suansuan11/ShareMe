add_test(
  NAME playback_app_smoke
  COMMAND $<TARGET_FILE:shareme_playback_demo> --smoke-test
)
set_tests_properties(
  playback_app_smoke
  PROPERTIES ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
)

add_test(
  NAME playback_media_smoke
  COMMAND
    $<TARGET_FILE:shareme_playback_demo>
    --media-smoke
    "${CMAKE_BINARY_DIR}/tests/media/generated-playback.mp4"
)
set_tests_properties(
  playback_media_smoke
  PROPERTIES
    ENVIRONMENT "QT_QPA_PLATFORM=offscreen"
    FIXTURES_REQUIRED media_fixture
    TIMEOUT 5
)
