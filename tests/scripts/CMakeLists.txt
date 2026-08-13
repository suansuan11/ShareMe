find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(
  shareme_signaled_call_cli_test_command
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/signaled_call_cli_test.py"
  --probe
  "$<TARGET_FILE:shareme_signaled_call_probe>"
  --source
  "${PROJECT_SOURCE_DIR}/client/tools/signaled_call/main.cpp"
)
if(TARGET ShareMe::MovieRTC)
  list(APPEND shareme_signaled_call_cli_test_command --has-movie-rtc)
endif()
add_test(
  NAME signaled_call_cli_contract
  COMMAND ${shareme_signaled_call_cli_test_command}
)
set_tests_properties(
  signaled_call_cli_contract
  PROPERTIES
    TIMEOUT 10
    ENVIRONMENT_MODIFICATION
      "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
)

add_test(
  NAME signaled_call_smoke_contract
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/signaled_call_smoke_test.py"
    --script
    "${PROJECT_SOURCE_DIR}/scripts/run_signaled_call_smoke.py"
)
set_tests_properties(signaled_call_smoke_contract PROPERTIES TIMEOUT 10)

if(TARGET shareme_rtc_demo)
  set(shareme_rtc_demo_test_arguments)
  if(TARGET ShareMe::MovieRTC)
    list(APPEND shareme_rtc_demo_test_arguments --movie-supported)
  endif()
  add_test(
    NAME rtc_demo_cli_contract
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/rtc_demo_cli_test.py"
      --demo
      "$<TARGET_FILE:shareme_rtc_demo>"
      --qml
      "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/qml"
      --controller-source
      "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/rtc_demo_controller.cpp"
      --controller-header
      "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/rtc_demo_controller.hpp"
      --qt-audio-source
      "${PROJECT_SOURCE_DIR}/client/tools/rtc_demo/qt_audio_output_device.cpp"
      --peer-source
      "${PROJECT_SOURCE_DIR}/client/rtc/webrtc/src/signaled_peer.cpp"
      ${shareme_rtc_demo_test_arguments}
  )
  add_test(
    NAME gui_qml_contract
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/gui_qml_contract_test.py"
      --demo
      "$<TARGET_FILE:shareme_rtc_demo>"
  )
  set_tests_properties(
    gui_qml_contract
    PROPERTIES
      TIMEOUT 15
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
  )
  add_test(
    NAME gui_call_smoke_contract
    COMMAND "${Python3_EXECUTABLE}"
            "${CMAKE_CURRENT_SOURCE_DIR}/gui_call_smoke_test.py"
  )
  if(TARGET shareme_screen_motion_fixture)
    add_test(
      NAME windows_gui_acceptance
      COMMAND "${Python3_EXECUTABLE}"
              "${CMAKE_CURRENT_SOURCE_DIR}/windows_gui_acceptance_test.py"
    )
    set_tests_properties(
      windows_gui_acceptance
      PROPERTIES
        TIMEOUT 15
        ENVIRONMENT_MODIFICATION
          "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen;SHAREME_MOTION_FIXTURE=set:$<TARGET_FILE:shareme_screen_motion_fixture>"
    )
  endif()
  set_tests_properties(
    rtc_demo_cli_contract
    PROPERTIES
      TIMEOUT 10
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
  )
endif()

add_test(
  NAME process_metrics_contract
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/process_metrics_test.py"
)

add_test(
  NAME screen_stream_smoke_contract
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/screen_stream_smoke_test.py"
)
set_tests_properties(screen_stream_smoke_contract PROPERTIES TIMEOUT 10)

add_test(
  NAME primary_voice_smoke_contract
  COMMAND "${Python3_EXECUTABLE}"
          "${CMAKE_CURRENT_SOURCE_DIR}/primary_voice_smoke_test.py"
)
set_tests_properties(primary_voice_smoke_contract PROPERTIES TIMEOUT 10)

add_test(
  NAME macos_session_lifecycle_smoke_contract
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/macos_session_lifecycle_smoke_test.py"
)
set_tests_properties(
  macos_session_lifecycle_smoke_contract PROPERTIES TIMEOUT 10
)

add_test(
  NAME windows_screen_acceptance
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/windows_screen_acceptance_test.py"
)
set_tests_properties(windows_screen_acceptance PROPERTIES TIMEOUT 10)

add_test(
  NAME movie_drift_study_contract
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/movie_drift_study_test.py"
)
set_tests_properties(movie_drift_study_contract PROPERTIES TIMEOUT 10)

add_test(
  NAME movie_performance_study_contract
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/movie_performance_study_test.py"
)
set_tests_properties(movie_performance_study_contract PROPERTIES TIMEOUT 10)

add_test(
  NAME video_quality_contract_portability
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/video_quality_contract_portability_test.py"
)
set_tests_properties(video_quality_contract_portability PROPERTIES TIMEOUT 10)

add_test(
  NAME windows_portability
  COMMAND
    "${Python3_EXECUTABLE}"
    "${CMAKE_CURRENT_SOURCE_DIR}/windows_portability_test.py"
)
set_tests_properties(windows_portability PROPERTIES TIMEOUT 10)
