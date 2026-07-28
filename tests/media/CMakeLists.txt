add_executable(shareme_media_time_test media_time_test.cpp)
target_link_libraries(shareme_media_time_test PRIVATE ShareMe::Playback)

add_test(NAME media_time COMMAND shareme_media_time_test)

find_program(SHAREME_FFMPEG_EXECUTABLE ffmpeg REQUIRED)
set(SHAREME_GENERATED_MEDIA "${CMAKE_CURRENT_BINARY_DIR}/generated-playback.mp4")

add_test(
  NAME generate_media_fixture
  COMMAND
    "${SHAREME_FFMPEG_EXECUTABLE}"
    -hide_banner
    -loglevel error
    -y
    -f lavfi
    -i "testsrc2=size=160x90:rate=30"
    -f lavfi
    -i "sine=frequency=440:sample_rate=48000"
    -t 1
    -c:v mpeg4
    -pix_fmt yuv420p
    -c:a aac
    "${SHAREME_GENERATED_MEDIA}"
)
set_tests_properties(generate_media_fixture PROPERTIES FIXTURES_SETUP media_fixture)

add_executable(
  shareme_ffmpeg_media_source_test
  ffmpeg_media_source_test.cpp
)
target_link_libraries(
  shareme_ffmpeg_media_source_test
  PRIVATE ShareMe::Playback
)

add_test(
  NAME ffmpeg_media_source
  COMMAND shareme_ffmpeg_media_source_test "${SHAREME_GENERATED_MEDIA}"
)
set_tests_properties(
  ffmpeg_media_source
  PROPERTIES FIXTURES_REQUIRED media_fixture
)

add_executable(
  shareme_playback_session_test
  playback_session_test.cpp
)
target_link_libraries(
  shareme_playback_session_test
  PRIVATE ShareMe::Playback
)

add_test(
  NAME playback_session
  COMMAND shareme_playback_session_test
)
