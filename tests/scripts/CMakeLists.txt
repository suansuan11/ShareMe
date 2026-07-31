find_package(Python3 REQUIRED COMPONENTS Interpreter)

set(
  shareme_signaled_call_cli_test_command
  "${Python3_EXECUTABLE}"
  "${CMAKE_CURRENT_SOURCE_DIR}/signaled_call_cli_test.py"
  --probe
  "$<TARGET_FILE:shareme_signaled_call_probe>"
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
  add_test(
    NAME rtc_demo_cli_contract
    COMMAND
      "${Python3_EXECUTABLE}"
      "${CMAKE_CURRENT_SOURCE_DIR}/rtc_demo_cli_test.py"
      --demo
      "$<TARGET_FILE:shareme_rtc_demo>"
  )
  set_tests_properties(
    rtc_demo_cli_contract
    PROPERTIES
      TIMEOUT 10
      ENVIRONMENT_MODIFICATION
        "PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>;QT_QPA_PLATFORM=set:offscreen"
  )
endif()
