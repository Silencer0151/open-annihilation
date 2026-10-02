# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Unit definitions: [UNITINFO] FBI records read from TDF text into typed
# fields.
add_library(oa-data-unit-definitions src/unit_definitions.cpp)
add_library(oa::data::unit_definitions ALIAS oa-data-unit-definitions)
target_include_directories(oa-data-unit-definitions PUBLIC include)
target_compile_features(oa-data-unit-definitions PUBLIC cxx_std_20)
target_link_libraries(oa-data-unit-definitions PUBLIC oa-formats-tdf oa-data-defs oa-options)
if(BUILD_TESTING)
  add_executable(oa-unit-definitions-test tests/unit_definitions_test.cpp)
  target_link_libraries(oa-unit-definitions-test PRIVATE oa-base-text oa-data-unit-definitions oa-test-support)
  add_test(NAME unit-definitions COMMAND oa-unit-definitions-test)
  # The installed game's unit definitions and movement classes.
  add_executable(oa-installed-units-test tests/installed_units_test.cpp)
  target_link_libraries(oa-installed-units-test PRIVATE oa-data-unit-definitions oa-data-defs-assets oa-formats-hpi oa-test-game-data)
  oa_add_game_data_test(unit-definitions-data oa-installed-units-test)
endif()
