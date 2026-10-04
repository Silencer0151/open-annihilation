# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The Game files screen's model: its steps, sheets, banners and problems, its
# texts through the interface catalogue, its layouts in points for the
# tablet and phone forms, the hit test, the focus order and what presses and
# keys ask of the app, with no SDL and no app header (game_files.hpp).
add_library(oa-ui-game-files src/game_files_model.cpp src/game_files_texts.cpp
  src/game_files_content.cpp src/game_files_layout.cpp)
add_library(oa::ui::game_files ALIAS oa-ui-game-files)
target_include_directories(oa-ui-game-files PUBLIC include)
target_compile_features(oa-ui-game-files PUBLIC cxx_std_20)
target_link_libraries(oa-ui-game-files PUBLIC oa-ui-touch-hud oa-ui-display-layout
  PRIVATE oa-data-languages oa-options)

if(BUILD_TESTING)
  # Every step and sheet at 1194x834, 852x393 with its safe area and
  # 640x480: button heights, the safe area, text inside its box as the
  # bundled fonts measure it, hit tests, focus, presses, switches, scrolling
  # and the texts with neutral and platform words.
  add_executable(oa-ui-game-files-test tests/game_files_test.cpp)
  target_link_libraries(oa-ui-game-files-test PRIVATE oa-ui-game-files oa-platform-text-font
    oa-test-support oa-options)
  # The text checks read the bundled fonts beside the test, as the game does.
  oa_copy_text_fonts(oa-ui-game-files-test "$<TARGET_FILE_DIR:oa-ui-game-files-test>")
  add_test(NAME ui-game-files COMMAND oa-ui-game-files-test)
endif()
