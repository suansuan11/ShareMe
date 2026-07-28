find_package(PkgConfig REQUIRED)

pkg_check_modules(AVFORMAT IMPORTED_TARGET libavformat)
pkg_check_modules(AVCODEC IMPORTED_TARGET libavcodec)
pkg_check_modules(AVUTIL IMPORTED_TARGET libavutil)
pkg_check_modules(SWSCALE IMPORTED_TARGET libswscale)
pkg_check_modules(SWRESAMPLE IMPORTED_TARGET libswresample)

include(FindPackageHandleStandardArgs)
find_package_handle_standard_args(
  FFmpeg
  REQUIRED_VARS
    AVFORMAT_FOUND
    AVCODEC_FOUND
    AVUTIL_FOUND
    SWSCALE_FOUND
    SWRESAMPLE_FOUND
)

function(shareme_add_ffmpeg_target component pkgconfig_target)
  if(NOT TARGET "FFmpeg::${component}")
    add_library("FFmpeg::${component}" INTERFACE IMPORTED)
    set_property(
      TARGET "FFmpeg::${component}"
      PROPERTY INTERFACE_LINK_LIBRARIES "${pkgconfig_target}"
    )
  endif()
endfunction()

if(FFmpeg_FOUND)
  shareme_add_ffmpeg_target(avformat PkgConfig::AVFORMAT)
  shareme_add_ffmpeg_target(avcodec PkgConfig::AVCODEC)
  shareme_add_ffmpeg_target(avutil PkgConfig::AVUTIL)
  shareme_add_ffmpeg_target(swscale PkgConfig::SWSCALE)
  shareme_add_ffmpeg_target(swresample PkgConfig::SWRESAMPLE)
endif()
