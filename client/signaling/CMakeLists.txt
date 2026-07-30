add_library(
  shareme_qt_signaling
  STATIC
  qt_signaling_client.cpp
  qt_signaling_client.hpp
)
add_library(ShareMe::QtSignaling ALIAS shareme_qt_signaling)
target_link_libraries(shareme_qt_signaling PUBLIC ShareMe::Core Qt6::Core Qt6::WebSockets)
target_include_directories(shareme_qt_signaling PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
shareme_set_project_warnings(shareme_qt_signaling)

add_executable(shareme_signaling_probe signaling_probe_main.cpp)
target_link_libraries(shareme_signaling_probe PRIVATE ShareMe::QtSignaling)
shareme_set_project_warnings(shareme_signaling_probe)
