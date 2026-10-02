# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# oa-doc-check: Clang reads the /// blocks of the engine's public headers.
#
# Each header under an include/ directory of src/ (network play's Runtime
# members, a part of class Runtime, through runtime.hpp), and each header of
# src/app that oa-extension-sdk publishes to an extension, is compiled on its own in
# a generated source that includes it, with the usage requirements of every
# library in the tree. Clang's documentation warnings are errors there: a
# @param that names no parameter, @return on a function returning void, a
# command it does not know (@quirk is declared as one) or a malformed block.
# The target exists only when the C++ compiler is Clang or AppleClang, and
# only a build that names it (cmake --build <dir> --target oa-doc-check)
# compiles it; CI builds it on macOS.
include_guard(GLOBAL)

# Collects every library target defined in directory and the directories below it.
function(oa_collect_library_targets directory out_var)
  set(targets "")
  get_property(directory_targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
  foreach(target IN LISTS directory_targets)
    get_target_property(type ${target} TYPE)
    if(type MATCHES "^(STATIC|SHARED|OBJECT|INTERFACE)_LIBRARY$")
      list(APPEND targets ${target})
    endif()
  endforeach()
  get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
  foreach(subdirectory IN LISTS subdirectories)
    oa_collect_library_targets("${subdirectory}" subdirectory_targets)
    list(APPEND targets ${subdirectory_targets})
  endforeach()
  set(${out_var} ${targets} PARENT_SCOPE)
endfunction()

# Defines oa-doc-check from the headers and library targets of the whole tree; call it after every target exists.
function(oa_add_documentation_check)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    return()
  endif()
  oa_collect_library_targets("${PROJECT_SOURCE_DIR}" libraries)
  file(GLOB_RECURSE headers CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}"
    "${PROJECT_SOURCE_DIR}/src/*.h" "${PROJECT_SOURCE_DIR}/src/*.hpp")
  list(FILTER headers INCLUDE REGEX "/include/")
  list(FILTER headers EXCLUDE REGEX "(^|/)tests/")
  if(TARGET oa-extension-sdk)
    file(GLOB app_headers CONFIGURE_DEPENDS RELATIVE "${PROJECT_SOURCE_DIR}" "${PROJECT_SOURCE_DIR}/src/app/*.hpp")
    list(APPEND headers ${app_headers})
  endif()
  list(SORT headers)
  set(sources "")
  foreach(relative IN LISTS headers)
    set(header "${PROJECT_SOURCE_DIR}/${relative}")
    set(source "${PROJECT_BINARY_DIR}/doc-check/${relative}.cpp")
    file(CONFIGURE OUTPUT "${source}" CONTENT "#include \"${header}\"\n" @ONLY)
    list(APPEND sources "${source}")
  endforeach()
  add_library(oa-doc-check OBJECT EXCLUDE_FROM_ALL ${sources})
  add_library(oa::doc::check ALIAS oa-doc-check)
  target_link_libraries(oa-doc-check PRIVATE ${libraries})
  # SDL's package marks its headers as the project's own, not the system's;
  # its documentation is not the engine's to check.
  if(TARGET SDL3::Headers)
    get_target_property(sdl_include_directories SDL3::Headers INTERFACE_INCLUDE_DIRECTORIES)
    target_include_directories(oa-doc-check SYSTEM PRIVATE ${sdl_include_directories})
  endif()
  target_compile_options(oa-doc-check PRIVATE
    -Wdocumentation -Wdocumentation-unknown-command -fcomment-block-commands=quirk
    -Werror=documentation -Werror=documentation-unknown-command)
endfunction()
