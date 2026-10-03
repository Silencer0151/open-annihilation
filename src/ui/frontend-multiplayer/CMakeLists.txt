# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Multiplayer connect, lobby and battleroom screens.
add_library(oa-ui-frontend-multiplayer
  src/panel.cpp
  src/lobby_net.cpp
  src/battleroom.cpp
  src/team_rules.cpp
  src/unit_sync.cpp
  src/restrict.cpp
  src/connect.cpp
  src/dialogs.cpp
  src/screens.cpp)
add_library(oa::ui::frontend_multiplayer ALIAS oa-ui-frontend-multiplayer)
target_include_directories(oa-ui-frontend-multiplayer PUBLIC include)
target_link_libraries(oa-ui-frontend-multiplayer
  PUBLIC oa-core-types oa-ui-gui-layout oa-ui-gui-input oa-netgame oa-ui-screen-registry oa-options
    oa-data-campaign oa-data-match-rules
  PRIVATE oa-platform-shims oa-base-text oa-base-bytes oa-ui-frontend-renderer oa-ui-decoded oa-formats-hpi oa-data-defs-assets oa-formats-tnt oa-data-unit-definitions
    oa-data-campaign-assets oa-data-defs oa-data-languages oa-netgame-sync oa-present oa-present-model
    oa-base-game-loop oa-sim-mission-units)

if(BUILD_TESTING)
  add_executable(oa-ui-frontend-multiplayer-lobby-test tests/lobby_test.cpp)
  target_link_libraries(oa-ui-frontend-multiplayer-lobby-test PRIVATE oa-ui-frontend-multiplayer oa-formats-hpi
    oa-formats-gaf oa-data-languages oa-test-game-data)
  add_test(NAME ui-multiplayer-lobby COMMAND oa-ui-frontend-multiplayer-lobby-test)
  # The lounge and game list over the GUI files of the install OA_GAME_DIR
  # names, and the screens over its store.
  add_test(NAME ui-multiplayer-lobby-data COMMAND oa-ui-frontend-multiplayer-lobby-test --data)
  set_tests_properties(ui-multiplayer-lobby ui-multiplayer-lobby-data PROPERTIES TIMEOUT 60)
  add_executable(oa-ui-frontend-multiplayer-screens-test tests/screens_test.cpp)
  target_link_libraries(oa-ui-frontend-multiplayer-screens-test PRIVATE oa-ui-frontend-multiplayer oa-formats-hpi
    oa-formats-tnt oa-ui-frontend-renderer oa-present oa-data-unit-definitions oa-data-defs-assets
    oa-test-game-data)
  add_test(NAME ui-multiplayer-screens COMMAND oa-ui-frontend-multiplayer-screens-test)
  set_tests_properties(ui-multiplayer-screens PROPERTIES TIMEOUT 120)
  add_executable(oa-ui-frontend-multiplayer-team-rules-test tests/team_rules_test.cpp)
  target_link_libraries(oa-ui-frontend-multiplayer-team-rules-test PRIVATE oa-ui-frontend-multiplayer)
  add_test(NAME ui-multiplayer-team-rules COMMAND oa-ui-frontend-multiplayer-team-rules-test)
  add_executable(oa-ui-frontend-multiplayer-lobby-rules-test tests/lobby_rules_test.cpp)
  target_link_libraries(oa-ui-frontend-multiplayer-lobby-rules-test PRIVATE oa-ui-frontend-multiplayer
    oa-formats-hpi oa-test-game-data)
  add_test(NAME ui-multiplayer-lobby-rules COMMAND oa-ui-frontend-multiplayer-lobby-rules-test)
  # The panel cases over the GUI files of the install OA_GAME_DIR names.
  add_test(NAME ui-multiplayer-lobby-rules-data COMMAND oa-ui-frontend-multiplayer-lobby-rules-test --data)
  oa_game_data_tests(ui-multiplayer-lobby-rules-data)
  oa_game_data_tests(ui-multiplayer-lobby-data ui-multiplayer-screens)
endif()
