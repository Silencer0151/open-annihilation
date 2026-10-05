# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The in-engine game folder chooser's model: the list of found folders, the
# folder browser, their layouts in the Game files screen's look, the hit
# test, the focus order and what presses and keys ask of the app, with no SDL
# and no app header (folder_chooser.hpp).
add_library(oa-ui-folder-chooser src/folder_chooser.cpp src/folder_chooser_layout.cpp)
add_library(oa::ui::folder_chooser ALIAS oa-ui-folder-chooser)
target_include_directories(oa-ui-folder-chooser PUBLIC include)
target_compile_features(oa-ui-folder-chooser PUBLIC cxx_std_20)
target_link_libraries(oa-ui-folder-chooser PUBLIC oa-ui-game-files PRIVATE oa-data-languages oa-options)

if(BUILD_TESTING)
  # The list and the browser laid out at 1280x800 and 852x393 with its safe
  # area, the focus order, the keys, presses and scrolling.
  add_executable(oa-ui-folder-chooser-test tests/folder_chooser_test.cpp)
  target_link_libraries(oa-ui-folder-chooser-test PRIVATE oa-ui-folder-chooser oa-test-support
    oa-options)
  add_test(NAME ui-folder-chooser COMMAND oa-ui-folder-chooser-test)
endif()
