add_subdirectory(webrtc)

if(WIN32)
  add_subdirectory(desktop)
endif()

if(TARGET ShareMe::Playback)
  add_subdirectory(movie)
endif()
