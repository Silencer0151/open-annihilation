# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# In-game command console, cheats, debug hotkeys and the mission tag table.
add_library(oa-ui-console src/commands.cpp src/debug_hotkeys.cpp src/unit_tags.cpp)
add_library(oa::ui::console ALIAS oa-ui-console)
target_include_directories(oa-ui-console PUBLIC include)
target_link_libraries(oa-ui-console PUBLIC oa-core-types oa-data-campaign oa-ui-services
  PRIVATE oa-sim-simulation-state oa-options oa-sim-ai oa-data-mission-types)
target_compile_features(oa-ui-console PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-ui-console-test tests/console_test.cpp)
  target_link_libraries(oa-ui-console-test PRIVATE oa-ui-console oa-options)
  add_test(NAME ui-console COMMAND oa-ui-console-test)

  add_executable(oa-ui-console-unit-tags-test tests/unit_tags_test.cpp)
  target_link_libraries(oa-ui-console-unit-tags-test PRIVATE oa-ui-console oa-data-mission-types oa-options)
  add_test(NAME ui-console-unit-tags COMMAND oa-ui-console-unit-tags-test)
endif()
