execute_process(
  COMMAND git rev-parse --short=12 HEAD
  WORKING_DIRECTORY "${PROJECT_SOURCE_DIR}"
  OUTPUT_VARIABLE SHAREME_PROBE_REVISION
  OUTPUT_STRIP_TRAILING_WHITESPACE
  ERROR_QUIET
)
if(NOT SHAREME_PROBE_REVISION)
  set(SHAREME_PROBE_REVISION "unknown")
endif()

add_executable(
  shareme_webrtc_probe
  main.cpp
)

target_compile_features(shareme_webrtc_probe PRIVATE cxx_std_20)
target_compile_definitions(
  shareme_webrtc_probe
  PRIVATE
    SHAREME_PROBE_REVISION="${SHAREME_PROBE_REVISION}"
)
target_link_libraries(
  shareme_webrtc_probe
  PRIVATE
    ShareMe::WebRTC
)

if(APPLE)
  target_link_options(
    shareme_webrtc_probe
    PRIVATE
      "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  )
endif()

shareme_set_project_warnings(shareme_webrtc_probe)
