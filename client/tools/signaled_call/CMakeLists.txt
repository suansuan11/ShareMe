add_executable(shareme_signaled_call_probe main.cpp)
target_compile_features(shareme_signaled_call_probe PRIVATE cxx_std_20)
target_link_libraries(shareme_signaled_call_probe PRIVATE ShareMe::QtSignaling)

if(TARGET ShareMe::MovieRTC)
  target_link_libraries(shareme_signaled_call_probe PRIVATE ShareMe::MovieRTC)
  target_compile_definitions(
    shareme_signaled_call_probe
    PRIVATE SHAREME_HAS_MOVIE_RTC=1
  )
else()
  target_link_libraries(shareme_signaled_call_probe PRIVATE ShareMe::WebRTC)
endif()

if(TARGET ShareMe::DesktopRTC)
  target_link_libraries(
    shareme_signaled_call_probe
    PRIVATE ShareMe::DesktopRTC
  )
  target_compile_definitions(
    shareme_signaled_call_probe
    PRIVATE SHAREME_HAS_DESKTOP_RTC=1
  )
endif()

if(APPLE)
  target_link_options(
    shareme_signaled_call_probe
    PRIVATE
      "-Wl,-sectcreate,__TEXT,__info_plist,${CMAKE_CURRENT_SOURCE_DIR}/Info.plist"
  )
endif()

shareme_set_project_warnings(shareme_signaled_call_probe)
