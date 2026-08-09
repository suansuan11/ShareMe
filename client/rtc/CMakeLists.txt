add_subdirectory(webrtc)

if(WIN32)
  add_subdirectory(desktop)
endif()

add_subdirectory(screen)

if(TARGET ShareMe::Playback)
  add_subdirectory(movie)
endif()
