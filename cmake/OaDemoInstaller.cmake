# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The installer of the Total Annihilation demo (1997) that the demo installer
# tests read.
#
# OA_DEMO_INSTALLER names the installer file itself, the one the engine
# recognises by its size and SHA-256. It defaults to the OA_DEMO_INSTALLER
# environment variable at configure time. Tests receive it at run time through
# their environment, never as a compile definition. A test given none prints
# one line saying what it skipped and exits with OA_GAME_DATA_SKIP_CODE, which
# ctest reports as skipped; the demo is optional, so it stays a skip under
# OA_REQUIRE_GAME_DATA too. A test given a path that names no installer, or
# the wrong file, fails.
#
# OA_REQUIRE_DEMO_INSTALLER is for a build that must prove the demo's tests
# ran, such as CI's: configure fails when OA_DEMO_INSTALLER names no file,
# and a demo test that skips fails instead. Every demo test carries the ctest
# label "demo", so `ctest -L demo` runs them all.
include_guard(GLOBAL)
include("${CMAKE_CURRENT_LIST_DIR}/OaGameData.cmake")
set(OA_DEMO_INSTALLER "$ENV{OA_DEMO_INSTALLER}" CACHE FILEPATH
  "Installer of the Total Annihilation demo (1997) that the demo installer tests read")
# A build tree configured before OA_DEMO_INSTALLER was exported holds an empty
# entry: take the environment's value.
if(OA_DEMO_INSTALLER STREQUAL "" AND NOT "$ENV{OA_DEMO_INSTALLER}" STREQUAL "")
  set_property(CACHE OA_DEMO_INSTALLER PROPERTY VALUE "$ENV{OA_DEMO_INSTALLER}")
endif()
# Tests run in their own build directories, where a relative path would name
# another file. The entry is cleared before stopping, so the next configure
# takes a corrected value.
if(NOT OA_DEMO_INSTALLER STREQUAL "" AND NOT IS_ABSOLUTE "${OA_DEMO_INSTALLER}")
  set(oa_relative_demo_installer "${OA_DEMO_INSTALLER}")
  set_property(CACHE OA_DEMO_INSTALLER PROPERTY VALUE "")
  message(FATAL_ERROR "OA_DEMO_INSTALLER (\"${oa_relative_demo_installer}\") is a relative path; pass "
    "the absolute path of the Total Annihilation demo (1997) installer, as "
    "-DOA_DEMO_INSTALLER=/absolute/path or in the OA_DEMO_INSTALLER environment variable")
endif()

option(OA_REQUIRE_DEMO_INSTALLER
  "Fail the demo's tests instead of skipping them, and configuration when OA_DEMO_INSTALLER names no file" OFF)
if(OA_REQUIRE_DEMO_INSTALLER AND NOT EXISTS "${OA_DEMO_INSTALLER}")
  message(FATAL_ERROR "OA_REQUIRE_DEMO_INSTALLER is ON but OA_DEMO_INSTALLER (\"${OA_DEMO_INSTALLER}\") names no "
    "file; set it to the Total Annihilation demo (1997) installer")
endif()

# Marks registered tests as reading the demo's installer: each sees
# OA_DEMO_INSTALLER in its environment, carries the label "demo", and
# reports OA_GAME_DATA_SKIP_CODE as skipped, or as failed under
# OA_REQUIRE_DEMO_INSTALLER. With OA_DEMO_INSTALLER empty the environment
# ctest runs in is passed through unchanged.
function(oa_demo_installer_tests)
  foreach(test IN LISTS ARGN)
    if(OA_DEMO_INSTALLER)
      set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "OA_DEMO_INSTALLER=${OA_DEMO_INSTALLER}")
    endif()
    set_property(TEST ${test} APPEND PROPERTY LABELS demo)
    if(NOT OA_REQUIRE_DEMO_INSTALLER)
      set_property(TEST ${test} PROPERTY SKIP_RETURN_CODE ${OA_GAME_DATA_SKIP_CODE})
    endif()
  endforeach()
endfunction()
