# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The installed game that the game-data tests read.
#
# OA_GAME_DIR names an ordinary Total Annihilation 3.1c installation: the
# folder that holds totala1.hpi. It defaults to the OA_GAME_DIR environment
# variable at configure time. Tests receive it at run time through their
# environment, never as a compile definition, so one build serves any
# install. A test given no install prints one line saying what it skipped and
# exits with OA_GAME_DATA_SKIP_CODE, which ctest reports as skipped.
#
# OA_REQUIRE_GAME_DATA makes a missing install a failure, for a build that
# must prove the game data was exercised: configure fails when OA_GAME_DIR
# names no directory, and the tests see OA_REQUIRE_GAME_DATA=1 and fail
# instead of skipping when they find none. A test that skips because the
# install lacks optional content (another edition's archive, a music folder)
# or the build lacks a decoder still reports skipped.
include_guard(GLOBAL)
set(OA_GAME_DIR "$ENV{OA_GAME_DIR}" CACHE PATH
  "Total Annihilation 3.1c installation (the folder holding totala1.hpi) that game-data tests and native checks read")
# set() never replaces an existing entry, and a build tree configured before
# OA_GAME_DIR was exported holds an empty one: take the environment's value.
if(OA_GAME_DIR STREQUAL "" AND NOT "$ENV{OA_GAME_DIR}" STREQUAL "")
  set_property(CACHE OA_GAME_DIR PROPERTY VALUE "$ENV{OA_GAME_DIR}")
endif()
# Tests run in their own build directories, so a relative path would name a
# different folder for each; CMake turns only an untyped -D value absolute.
# The entry is cleared before stopping, so the next configure takes a
# corrected -D value or the environment's.
if(NOT OA_GAME_DIR STREQUAL "" AND NOT IS_ABSOLUTE "${OA_GAME_DIR}")
  set(oa_relative_game_dir "${OA_GAME_DIR}")
  set_property(CACHE OA_GAME_DIR PROPERTY VALUE "")
  message(FATAL_ERROR "OA_GAME_DIR (\"${oa_relative_game_dir}\") is a relative path; pass the absolute "
    "path of the Total Annihilation folder that holds totala1.hpi, as -DOA_GAME_DIR=/absolute/path "
    "or in the OA_GAME_DIR environment variable")
endif()
option(OA_REQUIRE_GAME_DATA "Fail game-data tests instead of skipping them when OA_GAME_DIR names no installation" OFF)
# Exit code of a skipped game-data test; ctest's SKIP_RETURN_CODE. A cache
# entry, so projects that add the engine see it too, and the one definition:
# oa-test-game-data hands it to the locator as a compile definition.
set(OA_GAME_DATA_SKIP_CODE 77 CACHE INTERNAL "Exit code of a skipped game-data test")

if(OA_REQUIRE_GAME_DATA AND NOT IS_DIRECTORY "${OA_GAME_DIR}")
  message(FATAL_ERROR "OA_REQUIRE_GAME_DATA is ON but OA_GAME_DIR (\"${OA_GAME_DIR}\") names no directory; "
    "set it to the Total Annihilation folder that holds totala1.hpi")
endif()

# The locator the game-data tests include: oa/test/game_data.hpp finds the
# installation, reading the environment through oa-platform-shims, and
# oa/test/game_assets.hpp opens its archives (link oa-formats-hpi too for that
# one).
add_library(oa-test-game-data INTERFACE)
add_library(oa::test::game_data ALIAS oa-test-game-data)
target_include_directories(oa-test-game-data INTERFACE "${CMAKE_CURRENT_LIST_DIR}/../tests/support/include")
target_compile_features(oa-test-game-data INTERFACE cxx_std_20)
target_compile_definitions(oa-test-game-data INTERFACE OA_GAME_DATA_SKIP_CODE=${OA_GAME_DATA_SKIP_CODE})
target_link_libraries(oa-test-game-data INTERFACE oa-platform-shims)

# Marks registered tests as reading the installed game: each sees
# OA_GAME_DIR in its environment, and OA_REQUIRE_GAME_DATA=1 when that option
# is on, and reports OA_GAME_DATA_SKIP_CODE as skipped. With OA_GAME_DIR empty
# the environment ctest runs in is passed through unchanged.
function(oa_game_data_tests)
  foreach(test IN LISTS ARGN)
    if(OA_GAME_DIR)
      set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "OA_GAME_DIR=${OA_GAME_DIR}")
    endif()
    if(OA_REQUIRE_GAME_DATA)
      set_property(TEST ${test} APPEND PROPERTY ENVIRONMENT "OA_REQUIRE_GAME_DATA=1")
    endif()
    set_property(TEST ${test} PROPERTY SKIP_RETURN_CODE ${OA_GAME_DATA_SKIP_CODE})
  endforeach()
endfunction()

# Registers a test that reads the installed game: add_test(NAME <name>
# COMMAND <command...>) marked by oa_game_data_tests().
function(oa_add_game_data_test name)
  add_test(NAME ${name} COMMAND ${ARGN})
  oa_game_data_tests(${name})
endfunction()
