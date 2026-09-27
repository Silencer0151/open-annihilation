# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

file(GLOB oa_ui_hud_sources CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/src/*.cpp")
add_library(oa-ui-hud ${oa_ui_hud_sources})
add_library(oa::ui::hud ALIAS oa-ui-hud)
target_include_directories(oa-ui-hud PUBLIC include)
target_link_libraries(oa-ui-hud PUBLIC oa-core-types oa-data-unit-definitions oa-data-persist
  PRIVATE oa-data-defs oa-sim-feature-runtime oa-sim-messages oa-sim-unit-movement oa-options)
target_compile_features(oa-ui-hud PUBLIC cxx_std_20)

if(BUILD_TESTING)
  file(GLOB oa_ui_hud_tests CONFIGURE_DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/tests/*_test.cpp")
  foreach(test_source IN LISTS oa_ui_hud_tests)
    get_filename_component(test_name "${test_source}" NAME_WE)
    string(REPLACE "_" "-" test_id "${test_name}")
    add_executable(oa-ui-hud-${test_id} "${test_source}")
    target_link_libraries(oa-ui-hud-${test_id} PRIVATE oa-ui-hud oa-data-defs oa-sim-feature-runtime
      oa-sim-messages oa-options)
    add_test(NAME ui-hud-${test_id} COMMAND oa-ui-hud-${test_id})
  endforeach()
  # The installed game's side layouts and build pages.
  foreach(test_id IN ITEMS hud-labels-test order-panel-test)
    target_link_libraries(oa-ui-hud-${test_id} PRIVATE oa-formats-hpi oa-test-game-data)
    oa_add_game_data_test(ui-hud-${test_id}-data oa-ui-hud-${test_id} --data)
  endforeach()
endif()
