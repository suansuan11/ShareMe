cmake_minimum_required(VERSION 3.25)

if(POLICY CMP0144)
  cmake_policy(SET CMP0144 NEW)
endif()

project(
  ShareMe
  VERSION 0.1.0
  DESCRIPTION "One-to-one real-time movie sharing and voice calling"
  LANGUAGES CXX
)

option(SHAREME_BUILD_TESTS "Build ShareMe tests" ON)
option(SHAREME_ENABLE_QT "Enable Qt application targets" OFF)
option(SHAREME_ENABLE_FFMPEG "Enable FFmpeg media targets" OFF)
option(SHAREME_ENABLE_WEBRTC "Enable libwebrtc targets" OFF)

if(MSVC AND SHAREME_ENABLE_WEBRTC)
  set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")
endif()

set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_SOURCE_DIR}/cmake")
include(ShareMeWarnings)

add_subdirectory(client/core)

if(SHAREME_ENABLE_QT AND NOT SHAREME_ENABLE_FFMPEG)
  message(FATAL_ERROR "SHAREME_ENABLE_QT requires SHAREME_ENABLE_FFMPEG")
endif()

if(SHAREME_ENABLE_FFMPEG)
  find_package(FFmpeg REQUIRED)
  add_subdirectory(client/media)
endif()

if(SHAREME_ENABLE_QT)
  find_package(
    Qt6 6.8
    REQUIRED
    COMPONENTS Core Gui Qml Quick QuickControls2 Multimedia
  )
  qt_standard_project_setup(REQUIRES 6.8)
  add_subdirectory(client/app)
endif()

if(SHAREME_ENABLE_WEBRTC)
  find_package(WebRTC REQUIRED)
  add_subdirectory(client/rtc)
endif()

if(SHAREME_BUILD_TESTS)
  include(CTest)
  enable_testing()
  add_subdirectory(tests)
endif()
