# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The layout check (tools/check_layout.py) reads the engine's targets as
# this configuration defines them: each target's directory, type, sources,
# include directories and links, and the targets its links name by alias.
include_guard(GLOBAL)

# Writes every target of the directory tree below the calling directory to
# a file: one line per property, "<target>\t<property>\t<value>", and one
# "ALIAS\t<alias>\t<target>" line per alias a link names.
function(oa_write_layout_targets output)
  set(directories "${CMAKE_CURRENT_SOURCE_DIR}")
  set(lines "")
  set(aliases "")
  while(directories)
    list(POP_FRONT directories directory)
    get_property(subdirectories DIRECTORY "${directory}" PROPERTY SUBDIRECTORIES)
    list(APPEND directories ${subdirectories})
    get_property(targets DIRECTORY "${directory}" PROPERTY BUILDSYSTEM_TARGETS)
    foreach(target IN LISTS targets)
      get_target_property(type ${target} TYPE)
      string(APPEND lines "${target}\tTYPE\t${type}\n${target}\tSOURCE_DIR\t${directory}\n")
      if(type STREQUAL "UTILITY")
        continue()
      endif()
      foreach(property IN ITEMS SOURCES INCLUDE_DIRECTORIES INTERFACE_INCLUDE_DIRECTORIES
          LINK_LIBRARIES INTERFACE_LINK_LIBRARIES)
        get_target_property(value ${target} ${property})
        if(NOT value)
          continue()
        endif()
        string(APPEND lines "${target}\t${property}\t${value}\n")
        if(property MATCHES "LINK_LIBRARIES$")
          foreach(item IN LISTS value)
            string(REGEX REPLACE "^\\$<LINK_ONLY:(.*)>$" "\\1" item "${item}")
            if(item MATCHES "::" AND NOT item MATCHES "^::@" AND TARGET "${item}")
              get_target_property(aliased "${item}" ALIASED_TARGET)
              if(aliased)
                list(APPEND aliases "ALIAS\t${item}\t${aliased}\n")
              endif()
            endif()
          endforeach()
        endif()
      endforeach()
    endforeach()
  endwhile()
  list(REMOVE_DUPLICATES aliases)
  string(REPLACE ";" "" aliases "${aliases}")
  file(WRITE "${output}" "${lines}${aliases}")
endfunction()

# Registers engine-layout, which checks the engine's layout, its self-test
# and the layout pass's self-test, when tests are built and Python is found.
function(oa_register_layout_check)
  if(NOT BUILD_TESTING)
    return()
  endif()
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(NOT Python3_Interpreter_FOUND)
    return()
  endif()
  set(targets "${CMAKE_CURRENT_BINARY_DIR}/layout-targets.tsv")
  oa_write_layout_targets("${targets}")
  add_test(NAME engine-layout COMMAND ${Python3_EXECUTABLE} "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layout.py"
    --root "${CMAKE_CURRENT_SOURCE_DIR}" --targets "${targets}")
  add_test(NAME engine-layout-selftest COMMAND ${Python3_EXECUTABLE}
    "${CMAKE_CURRENT_SOURCE_DIR}/tools/check_layout.py" --self-test)
  set_tests_properties(engine-layout engine-layout-selftest PROPERTIES TIMEOUT 120)
  # The layout pass's own self-test, for as long as the pass is kept.
  if(EXISTS "${CMAKE_CURRENT_SOURCE_DIR}/tools/layout/layout.py")
    add_test(NAME layout-pass-selftest COMMAND ${Python3_EXECUTABLE}
      "${CMAKE_CURRENT_SOURCE_DIR}/tools/layout/layout.py" --self-test)
    set_tests_properties(layout-pass-selftest PROPERTIES TIMEOUT 120)
  endif()
endfunction()
