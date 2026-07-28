include(FindPackageHandleStandardArgs)

set(_WebRTC_manifest_valid FALSE)
set(_WebRTC_failure_reason "")

if(NOT WEBRTC_ROOT AND DEFINED ENV{WEBRTC_ROOT})
  set(WEBRTC_ROOT "$ENV{WEBRTC_ROOT}")
endif()

if(NOT WEBRTC_ROOT)
  set(_WebRTC_failure_reason
      "set WEBRTC_ROOT to the external locked WebRTC build directory")
else()
  cmake_path(ABSOLUTE_PATH WEBRTC_ROOT NORMALIZE)
  set(_WebRTC_manifest "${WEBRTC_ROOT}/shareme-webrtc-manifest.json")
  if(NOT EXISTS "${_WebRTC_manifest}")
    set(_WebRTC_failure_reason
        "missing ${_WebRTC_manifest}. Run scripts/bootstrap_webrtc.py first")
  else()
    file(READ "${_WebRTC_manifest}" _WebRTC_manifest_json)
    file(READ
         "${CMAKE_CURRENT_LIST_DIR}/../deps/webrtc.lock.json"
         _WebRTC_lock_json)

    string(JSON WebRTC_REVISION GET "${_WebRTC_manifest_json}" revision)
    string(JSON _WebRTC_locked_revision GET "${_WebRTC_lock_json}" revision)
    string(JSON WebRTC_MANIFEST_SYSTEM GET "${_WebRTC_manifest_json}" system)
    string(JSON
           WebRTC_MANIFEST_ARCHITECTURE
           GET
           "${_WebRTC_manifest_json}"
           architecture)
    string(JSON
           WebRTC_INCLUDE_DIR
           GET
           "${_WebRTC_manifest_json}"
           includeDir)

    string(TOLOWER "${WebRTC_MANIFEST_ARCHITECTURE}" _WebRTC_manifest_arch)
    string(TOLOWER "${CMAKE_SYSTEM_PROCESSOR}" _WebRTC_cmake_arch)
    if(_WebRTC_manifest_arch STREQUAL "amd64")
      set(_WebRTC_manifest_arch "x86_64")
    elseif(_WebRTC_manifest_arch STREQUAL "aarch64")
      set(_WebRTC_manifest_arch "arm64")
    endif()
    if(_WebRTC_cmake_arch STREQUAL "amd64")
      set(_WebRTC_cmake_arch "x86_64")
    elseif(_WebRTC_cmake_arch STREQUAL "aarch64")
      set(_WebRTC_cmake_arch "arm64")
    endif()

    if(NOT WebRTC_REVISION STREQUAL _WebRTC_locked_revision)
      set(_WebRTC_failure_reason
          "manifest revision does not match deps/webrtc.lock.json")
    elseif(NOT WebRTC_MANIFEST_SYSTEM STREQUAL CMAKE_SYSTEM_NAME)
      set(_WebRTC_failure_reason
          "manifest system ${WebRTC_MANIFEST_SYSTEM} does not match ${CMAKE_SYSTEM_NAME}")
    elseif(NOT _WebRTC_manifest_arch STREQUAL _WebRTC_cmake_arch)
      set(_WebRTC_failure_reason
          "manifest architecture ${WebRTC_MANIFEST_ARCHITECTURE} does not match ${CMAKE_SYSTEM_PROCESSOR}")
    elseif(NOT EXISTS "${WebRTC_INCLUDE_DIR}/api/peer_connection_interface.h"
           OR NOT
              EXISTS
              "${WebRTC_INCLUDE_DIR}/api/create_modular_peer_connection_factory.h"
           OR NOT
              EXISTS
              "${WebRTC_INCLUDE_DIR}/modules/audio_device/include/test_audio_device.h")
      set(_WebRTC_failure_reason "manifest include root is missing required headers")
    else()
      string(JSON
             _WebRTC_library_count
             LENGTH
             "${_WebRTC_manifest_json}"
             libraries)
      if(_WebRTC_library_count LESS 2)
        set(_WebRTC_failure_reason
            "manifest must contain the WebRTC and test audio device archives")
      else()
        math(EXPR _WebRTC_library_last "${_WebRTC_library_count} - 1")
        foreach(_WebRTC_index RANGE 0 ${_WebRTC_library_last})
          string(JSON
                 _WebRTC_library
                 GET
                 "${_WebRTC_manifest_json}"
                 libraries
                 ${_WebRTC_index})
          list(APPEND WebRTC_LIBRARIES "${_WebRTC_library}")
          if(NOT EXISTS "${_WebRTC_library}")
            set(_WebRTC_failure_reason
                "manifest references a missing WebRTC archive")
          endif()
        endforeach()
      endif()
    endif()

    if(NOT _WebRTC_failure_reason)
      string(JSON
             _WebRTC_definition_count
             LENGTH
             "${_WebRTC_manifest_json}"
             compileDefinitions)
      if(_WebRTC_definition_count GREATER 0)
        math(EXPR _WebRTC_definition_last "${_WebRTC_definition_count} - 1")
        foreach(_WebRTC_index RANGE 0 ${_WebRTC_definition_last})
          string(JSON
                 _WebRTC_definition
                 GET
                 "${_WebRTC_manifest_json}"
                 compileDefinitions
                 ${_WebRTC_index})
          list(APPEND WebRTC_COMPILE_DEFINITIONS "${_WebRTC_definition}")
        endforeach()
      endif()
      set(_WebRTC_manifest_valid TRUE)
    endif()
  endif()
endif()

find_package_handle_standard_args(
  WebRTC
  REQUIRED_VARS
    WEBRTC_ROOT
    _WebRTC_manifest_valid
    WebRTC_INCLUDE_DIR
    WebRTC_LIBRARIES
  REASON_FAILURE_MESSAGE "${_WebRTC_failure_reason}"
)

if(WebRTC_FOUND AND NOT TARGET WebRTC::webrtc)
  find_package(Threads REQUIRED)

  set(_WebRTC_platform_libraries Threads::Threads)
  if(APPLE)
    foreach(
      _WebRTC_framework
      AppKit
      AudioToolbox
      AVFoundation
      CoreAudio
      CoreGraphics
      CoreMedia
      CoreVideo
      Foundation
      Security
      SystemConfiguration
      VideoToolbox
    )
      find_library(
        WebRTC_${_WebRTC_framework}_FRAMEWORK
        NAMES "${_WebRTC_framework}"
        REQUIRED
      )
      list(APPEND
           _WebRTC_platform_libraries
           "${WebRTC_${_WebRTC_framework}_FRAMEWORK}")
    endforeach()
  elseif(WIN32)
    list(
      APPEND
      _WebRTC_platform_libraries
      amstrmid
      crypt32
      dmoguids
      iphlpapi
      msdmo
      secur32
      strmiids
      winmm
      wmcodecdspuuid
      ws2_32
    )
  endif()

  add_library(WebRTC::webrtc INTERFACE IMPORTED)
  set_target_properties(
    WebRTC::webrtc
    PROPERTIES
      INTERFACE_COMPILE_DEFINITIONS "${WebRTC_COMPILE_DEFINITIONS}"
      INTERFACE_INCLUDE_DIRECTORIES "${WebRTC_INCLUDE_DIR}"
      INTERFACE_LINK_LIBRARIES "${WebRTC_LIBRARIES};${_WebRTC_platform_libraries}"
  )
endif()

mark_as_advanced(WebRTC_INCLUDE_DIR WebRTC_LIBRARIES WebRTC_REVISION)
