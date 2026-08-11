qt_add_executable(
  shareme_screen_motion_fixture
  main.cpp
)
if(WIN32)
  target_sources(shareme_screen_motion_fixture PRIVATE app.manifest)
endif()

set_target_properties(
  shareme_screen_motion_fixture
  PROPERTIES WIN32_EXECUTABLE FALSE
)

qt_add_qml_module(
  shareme_screen_motion_fixture
  URI ShareMe.ScreenMotionFixture
  VERSION 1.0
  QML_FILES qml/Main.qml
)

target_link_libraries(
  shareme_screen_motion_fixture
  PRIVATE Qt6::Core Qt6::Gui Qt6::Qml Qt6::Quick
)

shareme_set_project_warnings(shareme_screen_motion_fixture)
