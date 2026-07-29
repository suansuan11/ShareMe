add_subdirectory(core)

if(TARGET ShareMe::Playback)
  add_subdirectory(media)
endif()

if(SHAREME_ENABLE_QT)
  add_subdirectory(app)
endif()

if(SHAREME_ENABLE_WEBRTC)
  add_subdirectory(rtc)
endif()
