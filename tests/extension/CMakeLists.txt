# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The extension boundary's own tests: a unit that sees Runtime with other
# members than oa-game's fails to link, and, in a build configured with
# -DOA_RECORD_EXTENSION_HOOKS=ON, two test extensions record the calls of
# every hook of the extension table and show that oa-game calls them,
# combined with network play's extension as extension.hpp says: the
# recorder, which fills every hook but those one extension at most may fill
# (network play fills them), and the follower, whose library links the
# recorder's and which fills the same hooks. The game lists network play
# first, then the recorder; the follower is registered before the recorder
# and listed after it all the same. The recorder also adds one Runtime
# member through OA_RUNTIME_EXTENSION_MEMBERS, so that the mechanism is
# built while it exists. Added by the top-level project after network play
# and before it applies the members header, which this directory names for
# the recorder.
if(NOT TARGET oa-game)
  return()
endif()

option(OA_RECORD_EXTENSION_HOOKS
  "Build oa-game with the recorder test extensions, which record every hook's calls" OFF)
if(OA_RECORD_EXTENSION_HOOKS)
  set(OA_RUNTIME_EXTENSION_MEMBERS "${CMAKE_CURRENT_SOURCE_DIR}/recorder_runtime_members.hpp" PARENT_SCOPE)
  add_library(oa-extension-recorder STATIC recorder.cpp)
  target_link_libraries(oa-extension-recorder PRIVATE oa::extension-sdk oa-options)
  add_library(oa-extension-recorder-follower STATIC follower.cpp)
  target_link_libraries(oa-extension-recorder-follower PRIVATE oa-extension-recorder oa::app::headers
    oa-options)
  oa_add_extension(oa-extension-recorder-follower INIT oa_extension_init_recorder_follower)
  oa_add_extension(oa-extension-recorder INIT oa_extension_init_recorder)
  if(BUILD_TESTING)
    find_package(Python3 COMPONENTS Interpreter QUIET)
    if(Python3_Interpreter_FOUND)
      set(oa_hooks_scratch "${CMAKE_CURRENT_BINARY_DIR}/hooks")
      # The option hooks, through --help, the -r switch, an unknown option,
      # a missing game directory and a second frontend_game, which stops
      # the start; no game data needed.
      add_test(NAME extension-hooks-options COMMAND ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/check_hooks.py" --runs options
        --game $<TARGET_FILE:oa-game> --scratch-root "${oa_hooks_scratch}")
      # The rest, in headless, interactive and check runs over the
      # installation OA_GAME_DIR names.
      oa_add_game_data_test(extension-hooks-game ${Python3_EXECUTABLE}
        "${CMAKE_CURRENT_SOURCE_DIR}/check_hooks.py" --runs game
        --game $<TARGET_FILE:oa-game> --scratch-root "${oa_hooks_scratch}"
        --skip-code ${OA_GAME_DATA_SKIP_CODE})
      set_tests_properties(extension-hooks-options PROPERTIES TIMEOUT 120)
      set_tests_properties(extension-hooks-game PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
    endif()
  endif()
endif()

if(BUILD_TESTING)
  # A unit compiled with a members header, linked with the layout marker
  # compiled without one, as oa-game's would be in another build: the link
  # must fail on the marker of the layout the unit sees. Built only by the
  # test, from the libraries oa-game's sources use, without the SDK and the
  # members definition it may carry.
  add_executable(oa-extension-layout-mismatch EXCLUDE_FROM_ALL
    layout_mismatch.cpp "${open_annihilation_SOURCE_DIR}/src/app/extension_members.cpp")
  target_link_libraries(oa-extension-layout-mismatch PRIVATE oa-app-headers)
  target_link_libraries(oa-extension-layout-mismatch PRIVATE oa-ui-display-layout)
  target_link_libraries(oa-extension-layout-mismatch PRIVATE SDL3::SDL3 ${oa_game_libraries} oa-ui-frontend-dialogs)
  set_source_files_properties(layout_mismatch.cpp PROPERTIES COMPILE_DEFINITIONS
    "OA_RUNTIME_EXTENSION_MEMBERS=\"${CMAKE_CURRENT_SOURCE_DIR}/layout_mismatch_members.hpp\"")
  # The build runs in this build tree, so no other test runs beside it.
  add_test(NAME extension-layout-mismatch COMMAND ${CMAKE_COMMAND}
    "-DBUILD_DIR=${CMAKE_BINARY_DIR}" -DTARGET=oa-extension-layout-mismatch
    "-DSYMBOL=runtime_layout_with_extension_members|oa_runtime_layout" "-DCONFIG=$<CONFIG>"
    -P "${CMAKE_CURRENT_SOURCE_DIR}/expect_link_failure.cmake")
  set_tests_properties(extension-layout-mismatch PROPERTIES TIMEOUT 600 RUN_SERIAL TRUE)
endif()
