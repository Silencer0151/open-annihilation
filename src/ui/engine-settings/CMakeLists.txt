# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The Open Annihilation settings: their values, defaults, keys and locks
# (engine_settings.hpp), and the dialog that shows them (engine_settings/dialog.hpp),
# Developer Mode's list of the standard hacks among its sections (src/developer.cpp),
# the one-off notices of Open Annihilation's own in the dialog's look
# (engine_settings/notice.hpp), and the prompts that ask with buttons of
# their own in the same look (engine_settings/prompt.hpp).
add_library(oa-ui-engine-settings src/engine_settings.cpp src/dialog.cpp src/dialog_draw.cpp
  src/developer.cpp src/mods.cpp src/notice.cpp src/prompt.cpp)
add_library(oa::ui::engine_settings ALIAS oa-ui-engine-settings)
target_include_directories(oa-ui-engine-settings PUBLIC include)
target_compile_features(oa-ui-engine-settings PUBLIC cxx_std_20)
target_link_libraries(oa-ui-engine-settings PUBLIC oa-platform-preferences oa-data-languages oa-data-limits
  oa-data-mod-profile oa-ui-pad-controls oa-present oa-ui-frontend-renderer oa-formats-fnt oa-formats-hpi PRIVATE oa-sim-session
  oa-ui-decoded oa-options)

if(BUILD_TESTING)
  add_executable(oa-ui-engine-settings-test tests/engine_settings_test.cpp)
  target_link_libraries(oa-ui-engine-settings-test PRIVATE oa-ui-engine-settings oa-options)
  add_test(NAME ui-engine-settings COMMAND oa-ui-engine-settings-test)

  # Prompts of one to three buttons: their layout, keys, pointer and finger.
  add_executable(oa-ui-engine-settings-prompt-test tests/prompt_test.cpp)
  target_link_libraries(oa-ui-engine-settings-prompt-test PRIVATE oa-ui-engine-settings
    oa-test-support oa-options)
  add_test(NAME ui-engine-settings-prompt COMMAND oa-ui-engine-settings-prompt-test)

  add_executable(oa-ui-engine-settings-dialog-test tests/dialog_test.cpp)
  target_link_libraries(oa-ui-engine-settings-dialog-test PRIVATE oa-ui-engine-settings
    oa-test-game-data oa-options)
  # The test places parts through the dialog's own geometry (src/geometry.hpp).
  target_include_directories(oa-ui-engine-settings-dialog-test PRIVATE src)
  add_test(NAME ui-engine-settings-dialog COMMAND oa-ui-engine-settings-dialog-test)
  # The dialog's fonts from the installed game, and its texts in them.
  oa_add_game_data_test(ui-engine-settings-dialog-data oa-ui-engine-settings-dialog-test --data)
endif()
