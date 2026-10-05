# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Stacked frontend dialogs: MSGBOX.GUI, CDCHECK.GUI, HELP.GUI, YESORNO.GUI and
# DEMOMSG.GUI.
add_library(oa-ui-frontend-dialogs
  src/dialog_art.cpp
  src/dialog_panel.cpp
  src/message_box.cpp
  src/cd_check.cpp
  src/help_panel.cpp
  src/notice.cpp
  src/dialog_screens.cpp)
add_library(oa::ui::frontend_dialogs ALIAS oa-ui-frontend-dialogs)
target_include_directories(oa-ui-frontend-dialogs PUBLIC include)
target_compile_features(oa-ui-frontend-dialogs PUBLIC cxx_std_20)
target_link_libraries(oa-ui-frontend-dialogs
  PUBLIC oa-ui-frontend-renderer oa-ui-screen-registry oa-ui-gui-layout oa-options
  PRIVATE oa-ui-gui-input oa-ui-gadget-render oa-present oa-formats-hpi oa-formats-tdf oa-formats-fnt oa-formats-gaf
    oa-ui-decoded oa-data-defs oa-base-text)
if(BUILD_TESTING)
  # The dialogs over the installed game's store, and drawn from its archives.
  add_executable(oa-frontend-dialogs-test tests/dialogs_test.cpp)
  target_link_libraries(oa-frontend-dialogs-test PRIVATE oa-ui-frontend-dialogs oa-formats-hpi oa-test-game-data
    oa-app-game-directory oa-app-headers)
  oa_add_game_data_test(frontend-dialogs oa-frontend-dialogs-test --screens)
  oa_add_game_data_test(frontend-dialogs-packed oa-frontend-dialogs-test --packed)
  # DEMOMSG.GUI over the data of the Total Annihilation demo (1997), unpacked
  # from the installer OA_DEMO_INSTALLER names; skipped without one.
  add_test(NAME frontend-dialogs-notice COMMAND oa-frontend-dialogs-test --notice)
  oa_demo_installer_tests(frontend-dialogs-notice)
  set_tests_properties(frontend-dialogs frontend-dialogs-packed frontend-dialogs-notice PROPERTIES
    TIMEOUT 120)
  add_executable(oa-frontend-dialogs-art-test tests/dialog_art_test.cpp)
  target_include_directories(oa-frontend-dialogs-art-test PRIVATE src)
  target_link_libraries(oa-frontend-dialogs-art-test PRIVATE
    oa-ui-frontend-dialogs oa-ui-gui-input oa-ui-gadget-render oa-present oa-formats-hpi oa-formats-gaf oa-test-game-data)
  add_test(NAME frontend-dialogs-art COMMAND oa-frontend-dialogs-art-test)
  oa_add_game_data_test(frontend-dialogs-art-data oa-frontend-dialogs-art-test --data)
  # HELP.GUI over a live match and the in-game BRIEFING.GUI over a paused
  # campaign mission through the SDL presenter, on dummy devices.
  if(TARGET oa-game AND IS_DIRECTORY "${OA_GAME_DIR}")
    set(OA_DIALOG_CHECK_DIR "${open_annihilation_BINARY_DIR}/native-checks/match-dialogs")
    file(MAKE_DIRECTORY "${OA_DIALOG_CHECK_DIR}")
    add_test(NAME native-match-dialogs COMMAND oa-game --game-dir "${OA_GAME_DIR}"
      --skip-intro --mute --check-match-dialogs
      --preferences-file "${OA_DIALOG_CHECK_DIR}/match-dialogs.conf")
    set_tests_properties(native-match-dialogs PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT}
      WORKING_DIRECTORY "${OA_DIALOG_CHECK_DIR}"
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy;SDL_RENDER_DRIVER=software")
  endif()
endif()
