# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Options, in-game menus and load/save dialogs.
add_library(oa-ui-frontend options.cpp ingame_menu.cpp savegame_dialogs.cpp main_menu.cpp
  end_mission.cpp resource_palette.cpp)
add_library(oa::ui::frontend ALIAS oa-ui-frontend)
target_link_libraries(oa-ui-frontend PRIVATE oa-base-text oa-base-game-math)
target_include_directories(oa-ui-frontend PUBLIC include)
target_link_libraries(oa-ui-frontend PUBLIC oa-ui-frontend-state oa-ui-gui-layout oa-present
  oa-present-world-renderer oa-ui-campaign oa-data-campaign oa-data-match-rules PRIVATE oa-data-persist oa-options
  oa-ui-gui-input)
target_compile_features(oa-ui-frontend PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-ui-frontend-options-test tests/options_test.cpp tests/ingame_menu_test.cpp
    tests/savegame_dialogs_test.cpp tests/savegame_campaign_test.cpp tests/main_menu_test.cpp
    tests/end_mission_test.cpp tests/resource_palette_test.cpp tests/test_main.cpp)
  target_link_libraries(oa-ui-frontend-options-test PRIVATE oa-base-text oa-ui-frontend oa-data-persist
    oa-data-campaign-assets oa-data-languages oa-formats-hpi oa-test-game-data oa-test-support
    oa-options)
  add_test(NAME ui-frontend-options COMMAND oa-ui-frontend-options-test)
  # The panels over the installed game's GUI and campaign files.
  oa_add_game_data_test(ui-frontend-options-data oa-ui-frontend-options-test --data)
endif()

