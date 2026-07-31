add_subdirectory(core)

if(TARGET ShareMe::Playback)
  add_subdirectory(media)
endif()

if(TARGET shareme_playback_demo)
  add_subdirectory(app)
endif()

if(SHAREME_ENABLE_WEBRTC)
  add_subdirectory(rtc)
endif()

if(TARGET shareme_signaled_call_probe)
  add_subdirectory(scripts)
endif()
