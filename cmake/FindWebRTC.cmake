include(FindPackageHandleStandardArgs)

function(_webrtc_read_json_array output_variable json member)
  string(JSON _WebRTC_array_count LENGTH "${json}" "${member}")
  set(_WebRTC_array_values)
  if(_WebRTC_array_count GREATER 0)
    math(EXPR _WebRTC_array_last "${_WebRTC_array_count} - 1")
    foreach(_WebRTC_array_index RANGE 0 ${_WebRTC_array_last})
      string(
        JSON _WebRTC_array_value
        GET "${json}" "${member}" ${_WebRTC_array_index}
      )
      list(APPEND _WebRTC_array_values "${_WebRTC_array_value}")
    endforeach()
  endif()
  set("${output_variable}" "${_WebRTC_array_values}" PARENT_SCOPE)
endfunction()

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
  if(DEFINED WEBRTC_MANIFEST_NAME AND NOT WEBRTC_MANIFEST_NAME STREQUAL "")
    set(_WebRTC_manifest_name "${WEBRTC_MANIFEST_NAME}")
  elseif(DEFINED WEBRTC_OUTPUT_NAME AND NOT WEBRTC_OUTPUT_NAME STREQUAL "")
    if(WEBRTC_OUTPUT_NAME STREQUAL "shareme")
      set(_WebRTC_manifest_name "shareme-webrtc-manifest.json")
    else()
      set(
        _WebRTC_manifest_name
        "shareme-webrtc-${WEBRTC_OUTPUT_NAME}-manifest.json"
      )
    endif()
  else()
    set(
      _WebRTC_manifest_name
      "shareme-webrtc-shareme-screen-feasibility-manifest.json"
    )
    if(NOT EXISTS "${WEBRTC_ROOT}/${_WebRTC_manifest_name}" AND
       EXISTS "${WEBRTC_ROOT}/shareme-webrtc-manifest.json")
      set(_WebRTC_manifest_name "shareme-webrtc-manifest.json")
    endif()
  endif()
  set(_WebRTC_manifest "${WEBRTC_ROOT}/${_WebRTC_manifest_name}")
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
    string(
      JSON WebRTC_MSVC_RUNTIME_LIBRARY
      GET "${_WebRTC_manifest_json}" msvcRuntimeLibrary
    )
    _webrtc_read_json_array(
      WebRTC_INCLUDE_DIRS "${_WebRTC_manifest_json}" includeDirs
    )
    _webrtc_read_json_array(
      WebRTC_COMPILE_DEFINITIONS
      "${_WebRTC_manifest_json}"
      compileDefinitions
    )
    _webrtc_read_json_array(
      _WebRTC_manifest_gn_args "${_WebRTC_manifest_json}" gnArgs
    )
    _webrtc_read_json_array(
      _WebRTC_locked_gn_args "${_WebRTC_lock_json}" gnArgs
    )
    if(WebRTC_INCLUDE_DIRS)
      list(GET WebRTC_INCLUDE_DIRS 0 WebRTC_INCLUDE_DIR)
    endif()

    if(CMAKE_SYSTEM_NAME STREQUAL "Windows")
      set(
        _WebRTC_expected_compile_definitions
        NDEBUG
        WEBRTC_WIN
        NOMINMAX
        WIN32_LEAN_AND_MEAN
      )
      set(_WebRTC_expected_runtime_library "MultiThreaded")
      set(
        _WebRTC_expected_library_names
        adapted_video_track_source.lib
        test_audio_device_module.lib
        webrtc.lib
      )
      set(
        _WebRTC_expected_library_roles
        adaptedVideoTrackSource
        testAudioDeviceModule
        webrtc
      )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Darwin")
      set(
        _WebRTC_expected_compile_definitions
        NDEBUG
        WEBRTC_POSIX
        WEBRTC_MAC
      )
      set(_WebRTC_expected_runtime_library "")
      set(
        _WebRTC_expected_library_names
        libadapted_video_track_source_shareme.a
        libtest_audio_device_module_shareme.a
        libwebrtc.a
        libnative_api_shareme.a
        libnative_video_shareme.a
        libbase_native_additions_objc_shareme.a
        libbase_objc_shareme.a
        libhelpers_objc_shareme.a
        libvideocodec_objc_shareme.a
        libvideoframebuffer_objc_shareme.a
        libvpx_codec_constants_shareme.a
        libwrapped_native_codec_objc_shareme.a
        libvideo_toolbox_cc_shareme.a
        libvideotoolbox_objc_shareme.a
      )
      set(
        _WebRTC_expected_library_roles
        adaptedVideoTrackSource
        testAudioDeviceModule
        webrtc
        nativeApi
        nativeVideo
        baseNativeAdditionsObjc
        baseObjc
        helpersObjc
        videoCodecObjc
        videoFrameBufferObjc
        vpxCodecConstants
        wrappedNativeCodecObjc
        videoToolboxCc
        videoToolbox
      )
    elseif(CMAKE_SYSTEM_NAME STREQUAL "Linux")
      set(
        _WebRTC_expected_compile_definitions
        NDEBUG
        WEBRTC_POSIX
        WEBRTC_LINUX
      )
      set(_WebRTC_expected_runtime_library "")
      set(
        _WebRTC_expected_library_names
        libadapted_video_track_source_shareme.a
        libtest_audio_device_module_shareme.a
        libwebrtc.a
      )
      set(
        _WebRTC_expected_library_roles
        adaptedVideoTrackSource
        testAudioDeviceModule
        webrtc
      )
    endif()

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
    elseif(NOT _WebRTC_manifest_gn_args STREQUAL _WebRTC_locked_gn_args)
      set(_WebRTC_failure_reason
          "manifest GN arguments do not match deps/webrtc.lock.json")
    elseif(NOT WebRTC_MANIFEST_SYSTEM STREQUAL CMAKE_SYSTEM_NAME)
      set(_WebRTC_failure_reason
          "manifest system ${WebRTC_MANIFEST_SYSTEM} does not match ${CMAKE_SYSTEM_NAME}")
    elseif(NOT _WebRTC_manifest_arch STREQUAL _WebRTC_cmake_arch)
      set(_WebRTC_failure_reason
          "manifest architecture ${WebRTC_MANIFEST_ARCHITECTURE} does not match ${CMAKE_SYSTEM_PROCESSOR}")
    elseif(NOT WebRTC_COMPILE_DEFINITIONS STREQUAL
               _WebRTC_expected_compile_definitions)
      set(_WebRTC_failure_reason
          "manifest compile definitions do not match the locked ABI")
    elseif(NOT WebRTC_MSVC_RUNTIME_LIBRARY STREQUAL
               _WebRTC_expected_runtime_library)
      set(_WebRTC_failure_reason
          "manifest MSVC runtime library does not match the locked ABI")
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
      list(LENGTH _WebRTC_expected_library_roles _WebRTC_expected_library_count)
      if(NOT _WebRTC_library_count EQUAL _WebRTC_expected_library_count)
        set(_WebRTC_failure_reason
            "manifest library count does not match the locked platform ABI")
      else()
        set(WebRTC_LIBRARIES)
        set(WebRTC_FORCE_LOAD_LIBRARIES)
        math(EXPR _WebRTC_library_last "${_WebRTC_library_count} - 1")
        foreach(_WebRTC_index RANGE 0 ${_WebRTC_library_last})
          string(
            JSON _WebRTC_library_role
            GET "${_WebRTC_manifest_json}" libraries ${_WebRTC_index} role
          )
          string(
            JSON _WebRTC_library
            GET "${_WebRTC_manifest_json}" libraries ${_WebRTC_index} path
          )
          list(
            GET _WebRTC_expected_library_roles
            ${_WebRTC_index}
            _WebRTC_expected_library_role
          )
          list(
            GET _WebRTC_expected_library_names
            ${_WebRTC_index}
            _WebRTC_expected_library_name
          )
          cmake_path(GET _WebRTC_library FILENAME _WebRTC_library_name)
          if(NOT _WebRTC_library_role STREQUAL
                 _WebRTC_expected_library_role
             OR NOT _WebRTC_library_name STREQUAL
                    _WebRTC_expected_library_name)
            set(_WebRTC_failure_reason
                "manifest WebRTC library roles or order do not match the locked ABI")
          endif()
          list(APPEND WebRTC_LIBRARIES "${_WebRTC_library}")
          if(_WebRTC_library_role STREQUAL "baseNativeAdditionsObjc" OR
             _WebRTC_library_role STREQUAL "helpersObjc")
            list(APPEND WebRTC_FORCE_LOAD_LIBRARIES "${_WebRTC_library}")
          endif()
          if(NOT EXISTS "${_WebRTC_library}")
            set(_WebRTC_failure_reason
                "manifest references a missing WebRTC archive")
          endif()
        endforeach()
      endif()
    endif()

    if(NOT _WebRTC_failure_reason)
      set(_WebRTC_manifest_valid TRUE)
    endif()
  endif()
endif()

find_package_handle_standard_args(
  WebRTC
  REQUIRED_VARS
    WEBRTC_ROOT
    _WebRTC_manifest_valid
    WebRTC_INCLUDE_DIRS
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

  set(_WebRTC_link_options)
  if(APPLE)
    # ObjC categories in static archives are otherwise dropped by the linker.
    foreach(_WebRTC_library IN LISTS WebRTC_FORCE_LOAD_LIBRARIES)
      list(APPEND _WebRTC_link_options "-Wl,-force_load,${_WebRTC_library}")
    endforeach()
  endif()

  add_library(WebRTC::webrtc INTERFACE IMPORTED)
  set_target_properties(
    WebRTC::webrtc
    PROPERTIES
      INTERFACE_COMPILE_DEFINITIONS "${WebRTC_COMPILE_DEFINITIONS}"
      INTERFACE_INCLUDE_DIRECTORIES "${WebRTC_INCLUDE_DIRS}"
      INTERFACE_LINK_OPTIONS "${_WebRTC_link_options}"
      INTERFACE_LINK_LIBRARIES "${WebRTC_LIBRARIES};${_WebRTC_platform_libraries}"
  )
endif()

mark_as_advanced(
  WebRTC_INCLUDE_DIR
  WebRTC_INCLUDE_DIRS
  WebRTC_LIBRARIES
  WebRTC_REVISION
)
