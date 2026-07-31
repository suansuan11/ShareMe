qt_add_executable(
  shareme_playback_demo
  src/main.cpp
  src/playback_controller.cpp
  src/playback_controller.hpp
)

if(WIN32)
  target_sources(shareme_playback_demo PRIVATE app.manifest)
endif()

target_include_directories(
  shareme_playback_demo
  PRIVATE
    "${CMAKE_CURRENT_SOURCE_DIR}/src"
)

if(APPLE)
  set_target_properties(
    shareme_playback_demo
    PROPERTIES
      MACOSX_BUNDLE TRUE
      MACOSX_BUNDLE_BUNDLE_NAME "ShareMe Playback Demo"
      MACOSX_BUNDLE_GUI_IDENTIFIER "dev.shareme.playback-demo"
  )
endif()

qt_add_qml_module(
  shareme_playback_demo
  URI ShareMe
  VERSION 1.0
  QML_FILES
    qml/Main.qml
    qml/PlaybackControls.qml
)

target_link_libraries(
  shareme_playback_demo
  PRIVATE
    ShareMe::Playback
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

shareme_set_project_warnings(shareme_playback_demo)
