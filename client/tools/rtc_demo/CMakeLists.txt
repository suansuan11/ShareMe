set(shareme_rtc_demo_sources main.cpp rtc_demo_controller.cpp
                             rtc_demo_controller.hpp)
if(WIN32)
  list(APPEND shareme_rtc_demo_sources app.manifest)
endif()

qt_add_executable(
  shareme_rtc_demo
  ${shareme_rtc_demo_sources}
)

set_target_properties(
  shareme_rtc_demo
  PROPERTIES
    WIN32_EXECUTABLE FALSE
)

qt_add_qml_module(
  shareme_rtc_demo
  URI ShareMe.RtcDemo
  VERSION 1.0
  QML_FILES
    qml/Main.qml
)

target_link_libraries(
  shareme_rtc_demo
  PRIVATE
    ShareMe::QtSignaling
    ShareMe::WebRTC
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

shareme_set_project_warnings(shareme_rtc_demo)
