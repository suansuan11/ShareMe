add_subdirectory(webrtc)
add_subdirectory(screen)

if(WIN32)
  add_subdirectory(desktop)
endif()

if(TARGET ShareMe::Playback)
  add_subdirectory(movie)
endif()
