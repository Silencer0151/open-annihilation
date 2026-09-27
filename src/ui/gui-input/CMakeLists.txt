# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_ui_gui_input LANGUAGES CXX)

include(CTest)

add_library(oa-ui-gui-input
  src/gui_input.cpp
  src/gadget_state.cpp
  src/gadget_pointer.cpp
  src/gadget_update.cpp
  src/gadget_cursor.cpp)
add_library(oa::ui::gui_input ALIAS oa-ui-gui-input)
target_include_directories(oa-ui-gui-input PUBLIC include)
target_link_libraries(oa-ui-gui-input PUBLIC oa-ui-gui-layout)
target_compile_features(oa-ui-gui-input PUBLIC cxx_std_20)
if(TARGET oa-options)
  target_link_libraries(oa-ui-gui-input PUBLIC oa-options)
endif()
target_link_libraries(oa-ui-gui-input PUBLIC oa-sim-sprite-animation)

if(BUILD_TESTING)
  add_executable(oa-gui-input-test tests/gui_input_test.cpp)
  target_link_libraries(oa-gui-input-test PRIVATE oa-ui-gui-input oa-formats-hpi oa-test-game-data)
  add_test(NAME gui-input COMMAND oa-gui-input-test)
  # SPEEDS.GUI from the installed game.
  oa_add_game_data_test(gui-input-data oa-gui-input-test --data)

  add_executable(oa-gui-gadget-panel-test tests/gadget_panel_test.cpp)
  target_link_libraries(oa-gui-gadget-panel-test PRIVATE oa-ui-gui-input)
  add_test(NAME gui-gadget-panel COMMAND oa-gui-gadget-panel-test)
endif()
