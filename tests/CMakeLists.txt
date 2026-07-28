add_subdirectory(core)

if(TARGET ShareMe::Playback)
  add_subdirectory(media)
endif()
