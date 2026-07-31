add_executable(shareme_media_time_test media_time_test.cpp)
target_link_libraries(shareme_media_time_test PRIVATE ShareMe::Playback)

add_test(NAME media_time COMMAND shareme_media_time_test)

add_executable(shareme_pcm_chunker_test pcm_chunker_test.cpp)
target_link_libraries(shareme_pcm_chunker_test PRIVATE ShareMe::Playback)

add_test(NAME pcm_chunker COMMAND shareme_pcm_chunker_test)

find_program(SHAREME_FFMPEG_EXECUTABLE ffmpeg REQUIRED)
set(SHAREME_GENERATED_MEDIA "${CMAKE_CURRENT_BINARY_DIR}/generated-playback.mp4")
set(
  SHAREME_GENERATED_VIDEO_ONLY_MEDIA
  "${CMAKE_CURRENT_BINARY_DIR}/generated-video-only.mp4"
)
set(
  SHAREME_GENERATED_AUDIO_ONLY_MEDIA
  "${CMAKE_CURRENT_BINARY_DIR}/generated-audio-only.m4a"
)
set(
  SHAREME_GENERATED_NONZERO_AUDIO_MEDIA
  "${CMAKE_CURRENT_BINARY_DIR}/generated-nonzero-audio.m4a"
)
set(
  SHAREME_GENERATED_RAW_AAC_MEDIA
  "${CMAKE_CURRENT_BINARY_DIR}/generated-raw-audio.aac"
)

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

add_test(
  NAME generate_video_only_media_fixture
  COMMAND
    "${SHAREME_FFMPEG_EXECUTABLE}"
    -hide_banner
    -loglevel error
    -y
    -f lavfi
    -i "testsrc2=size=160x90:rate=30"
    -t 1
    -c:v mpeg4
    -pix_fmt yuv420p
    -an
    "${SHAREME_GENERATED_VIDEO_ONLY_MEDIA}"
)
set_tests_properties(
  generate_video_only_media_fixture
  PROPERTIES FIXTURES_SETUP video_only_media_fixture
)

add_test(
  NAME generate_audio_only_media_fixture
  COMMAND
    "${SHAREME_FFMPEG_EXECUTABLE}"
    -hide_banner
    -loglevel error
    -y
    -f lavfi
    -i "sine=frequency=880:sample_rate=48000"
    -t 1
    -c:a aac
    "${SHAREME_GENERATED_AUDIO_ONLY_MEDIA}"
)
set_tests_properties(
  generate_audio_only_media_fixture
  PROPERTIES FIXTURES_SETUP audio_only_media_fixture
)

add_test(
  NAME generate_nonzero_audio_media_fixture
  COMMAND
    "${SHAREME_FFMPEG_EXECUTABLE}"
    -hide_banner
    -loglevel error
    -y
    -f lavfi
    -i "sine=frequency=990:sample_rate=48000:duration=1"
    -af "asetpts=PTS+3/TB"
    -c:a aac
    -copyts
    "${SHAREME_GENERATED_NONZERO_AUDIO_MEDIA}"
)
set_tests_properties(
  generate_nonzero_audio_media_fixture
  PROPERTIES FIXTURES_SETUP nonzero_audio_media_fixture
)

add_test(
  NAME generate_raw_aac_media_fixture
  COMMAND
    "${SHAREME_FFMPEG_EXECUTABLE}"
    -hide_banner
    -loglevel error
    -y
    -f lavfi
    -i "sine=frequency=770:sample_rate=48000"
    -t 1
    -c:a aac
    -f adts
    "${SHAREME_GENERATED_RAW_AAC_MEDIA}"
)
set_tests_properties(
  generate_raw_aac_media_fixture
  PROPERTIES FIXTURES_SETUP raw_aac_media_fixture
)

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
  COMMAND
    shareme_ffmpeg_media_source_test
    "${SHAREME_GENERATED_MEDIA}"
    "${SHAREME_GENERATED_VIDEO_ONLY_MEDIA}"
    "${SHAREME_GENERATED_AUDIO_ONLY_MEDIA}"
    "${SHAREME_GENERATED_NONZERO_AUDIO_MEDIA}"
    "${SHAREME_GENERATED_RAW_AAC_MEDIA}"
)
set_tests_properties(
  ffmpeg_media_source
  PROPERTIES
    FIXTURES_REQUIRED
      "media_fixture;video_only_media_fixture;audio_only_media_fixture;nonzero_audio_media_fixture;raw_aac_media_fixture"
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
