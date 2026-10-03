# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-ui-campaign briefing_text.cpp single_player.cpp campaign.cpp endgame.cpp screens.cpp)
add_library(oa::ui::campaign ALIAS oa-ui-campaign)
target_link_libraries(oa-ui-campaign PRIVATE oa-base-text oa-base-game-math oa-data-defs)
target_include_directories(oa-ui-campaign PUBLIC include)
target_compile_features(oa-ui-campaign PUBLIC cxx_std_20)
target_link_libraries(oa-ui-campaign PUBLIC oa-data-campaign oa-data-match-rules oa-core-types
  PRIVATE oa-ui-screen-registry oa-ui-frontend-renderer oa-ui-frontend-state oa-ui-gui-layout
  oa-present-model oa-sim-simulation-state oa-options)
if(BUILD_TESTING)
  add_executable(oa-ui-campaign-test tests/campaign_ui_test.cpp)
  target_link_libraries(oa-ui-campaign-test PRIVATE oa-base-text oa-ui-campaign oa-data-campaign-assets oa-formats-hpi
    oa-test-game-data oa-options)
  add_test(NAME ui-campaign COMMAND oa-ui-campaign-test)
  # The campaign screens over the installed game's campaigns and briefings.
  oa_add_game_data_test(ui-campaign-data oa-ui-campaign-test --data)
  add_executable(oa-ui-campaign-endgame-test tests/endgame_test.cpp)
  target_link_libraries(oa-ui-campaign-endgame-test PRIVATE oa-base-text oa-ui-campaign oa-ui-screen-registry oa-ui-frontend-renderer oa-options)
  add_test(NAME ui-campaign-endgame COMMAND oa-ui-campaign-endgame-test)
endif()
