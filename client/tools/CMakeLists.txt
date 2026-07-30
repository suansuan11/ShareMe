add_subdirectory(webrtc_probe)
if(TARGET ShareMe::QtSignaling)
  add_subdirectory(signaled_call)
endif()
