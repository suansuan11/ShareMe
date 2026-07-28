function(shareme_set_project_warnings target)
  get_target_property(target_type "${target}" TYPE)
  if(target_type STREQUAL "INTERFACE_LIBRARY")
    set(warning_scope INTERFACE)
  else()
    set(warning_scope PRIVATE)
  endif()

  if(MSVC)
    target_compile_options("${target}" ${warning_scope} /W4 /permissive-)
  else()
    target_compile_options(
      "${target}"
      ${warning_scope}
      -Wall
      -Wextra
      -Wpedantic
      -Wconversion
      -Wsign-conversion
    )
  endif()
endfunction()
