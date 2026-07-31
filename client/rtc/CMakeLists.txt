add_subdirectory(webrtc)

if(TARGET ShareMe::Playback)
  add_subdirectory(movie)
endif()
