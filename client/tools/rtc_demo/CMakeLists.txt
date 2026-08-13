set(shareme_rtc_demo_native_route_sources)
set(shareme_rtc_demo_has_native_route_supplement OFF)
set(shareme_rtc_demo_linux_audio_backend)
if(WIN32)
  list(APPEND shareme_rtc_demo_native_route_sources
       windows_audio_route_monitor.cpp)
  set(shareme_rtc_demo_has_native_route_supplement ON)
elseif(APPLE)
  enable_language(OBJCXX)
  list(APPEND shareme_rtc_demo_native_route_sources
       macos_audio_route_monitor.mm
       session_lifecycle_monitor_mac.mm)
  set_source_files_properties(
    macos_audio_route_monitor.mm
    PROPERTIES COMPILE_OPTIONS "-fblocks"
  )
  set_source_files_properties(
    session_lifecycle_monitor_mac.mm
    PROPERTIES COMPILE_OPTIONS "-fblocks;-fobjc-arc"
  )
  set(shareme_rtc_demo_has_native_route_supplement ON)
elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
  find_package(PkgConfig QUIET)
  if(PkgConfig_FOUND)
    pkg_check_modules(SHAREME_PIPEWIRE QUIET IMPORTED_TARGET libpipewire-0.3)
    pkg_check_modules(SHAREME_PULSEAUDIO QUIET IMPORTED_TARGET libpulse)
  endif()
  if(TARGET PkgConfig::SHAREME_PIPEWIRE)
    list(APPEND shareme_rtc_demo_native_route_sources
         linux_audio_route_monitor.cpp)
    set(shareme_rtc_demo_has_native_route_supplement ON)
    set(shareme_rtc_demo_linux_audio_backend pipewire)
  elseif(TARGET PkgConfig::SHAREME_PULSEAUDIO)
    list(APPEND shareme_rtc_demo_native_route_sources
         linux_audio_route_monitor.cpp)
    set(shareme_rtc_demo_has_native_route_supplement ON)
    set(shareme_rtc_demo_linux_audio_backend pulseaudio)
  else()
    message(STATUS
      "ShareMe RTC demo: Linux native audio route supplement disabled; "
      "libpipewire-0.3 and libpulse development files are unavailable. "
      "Qt QMediaDevices remains enabled.")
  endif()
endif()

set(shareme_rtc_demo_sources main.cpp playback_state.cpp playback_state.hpp
                              app_session_config.cpp app_session_config.hpp
                              app_preferences.cpp app_preferences.hpp
                              call_session.cpp call_session.hpp
                              launch_options.cpp launch_options.hpp
                              shareme_app_controller.cpp shareme_app_controller.hpp
                              rtc_control_state.cpp rtc_control_state.hpp
                              voice_quality_policy.cpp voice_quality_policy.hpp
                              session_lifecycle_policy.cpp session_lifecycle_policy.hpp
                              session_lifecycle_monitor.cpp session_lifecycle_monitor.hpp
                              screen_capture_recovery_policy.cpp screen_capture_recovery_policy.hpp
                              playout_report.cpp playout_report.hpp
                              movie_audio_clock_message.cpp movie_audio_clock_message.hpp
                              drift_metrics_jsonl.cpp drift_metrics_jsonl.hpp
                              drift_failure.cpp drift_failure.hpp
                              drift_scenario.cpp drift_scenario.hpp
                              video_preview_adapter.cpp video_preview_adapter.hpp
                              movie_video_playout_adapter.cpp movie_video_playout_adapter.hpp
                              qt_audio_output_device.cpp qt_audio_output_device.hpp
                              qt_audio_route_monitor.cpp qt_audio_route_monitor.hpp
                              rtc_demo_controller.cpp rtc_demo_controller.hpp
                              ${shareme_rtc_demo_native_route_sources})
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
    qml/ShareMeTheme.qml
    qml/PrimaryButton.qml
    qml/IconControl.qml
    qml/InfoRow.qml
    qml/StatusBanner.qml
    qml/HomePage.qml
    qml/PreflightPage.qml
    qml/SettingsDialog.qml
    qml/HelpDialog.qml
    qml/CallPage.qml
    qml/CallTopBar.qml
    qml/CallControlDock.qml
    qml/CallDetailsDrawer.qml
    qml/VideoStage.qml
    qml/RecoveryDialog.qml
)

target_link_libraries(
  shareme_rtc_demo
  PRIVATE
    ShareMe::QtSignaling
    ShareMe::Core
    ShareMe::WebRTC
    ShareMe::ScreenRTC
    Qt6::Core
    Qt6::Gui
    Qt6::Qml
    Qt6::Quick
    Qt6::QuickControls2
    Qt6::Multimedia
)

if(TARGET ShareMe::MovieRTC)
  target_link_libraries(shareme_rtc_demo PRIVATE ShareMe::MovieRTC)
  target_compile_definitions(shareme_rtc_demo PRIVATE SHAREME_HAS_MOVIE_RTC=1)
endif()

if(TARGET ShareMe::DesktopRTC)
  target_link_libraries(shareme_rtc_demo PRIVATE ShareMe::DesktopRTC)
  target_compile_definitions(
    shareme_rtc_demo
    PRIVATE SHAREME_HAS_DESKTOP_CAPTURE=1
  )
endif()

if(shareme_rtc_demo_has_native_route_supplement)
  target_compile_definitions(
    shareme_rtc_demo
    PRIVATE SHAREME_HAS_NATIVE_AUDIO_ROUTE_SUPPLEMENT=1
  )
  if(WIN32)
    target_link_libraries(shareme_rtc_demo PRIVATE Mmdevapi ole32 uuid)
  elseif(APPLE)
    target_link_libraries(shareme_rtc_demo PRIVATE "-framework CoreAudio")
  elseif(shareme_rtc_demo_linux_audio_backend STREQUAL "pipewire")
    target_compile_definitions(
      shareme_rtc_demo PRIVATE SHAREME_HAS_PIPEWIRE=1
    )
    target_link_libraries(
      shareme_rtc_demo PRIVATE PkgConfig::SHAREME_PIPEWIRE
    )
  elseif(shareme_rtc_demo_linux_audio_backend STREQUAL "pulseaudio")
    target_link_libraries(
      shareme_rtc_demo PRIVATE PkgConfig::SHAREME_PULSEAUDIO
    )
  endif()
endif()

if(APPLE)
  target_compile_definitions(
    shareme_rtc_demo PRIVATE SHAREME_HAS_NATIVE_SESSION_LIFECYCLE=1
  )
  target_link_libraries(
    shareme_rtc_demo PRIVATE "-framework AppKit" "-framework Foundation"
  )
endif()

shareme_set_project_warnings(shareme_rtc_demo)
