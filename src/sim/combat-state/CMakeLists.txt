# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_combat_state LANGUAGES CXX)
include(CTest)
add_library(oa-sim-combat-state src/combat_state.cpp)
add_library(oa::sim::combat_state ALIAS oa-sim-combat-state)
target_include_directories(oa-sim-combat-state PUBLIC include)
target_link_libraries(oa-sim-combat-state PUBLIC oa-sim-ballistics)
target_compile_features(oa-sim-combat-state PUBLIC cxx_std_20)
target_link_libraries(oa-sim-combat-state PUBLIC oa-base-game-math oa-core-types oa-data-defs)
if(TARGET oa-options)
  target_link_libraries(oa-sim-combat-state PUBLIC oa-options)
endif()
if(BUILD_TESTING)
  add_executable(oa-combat-state-test tests/combat_state_test.cpp)
  target_link_libraries(oa-combat-state-test PRIVATE oa-sim-combat-state)
  add_test(NAME combat-state COMMAND oa-combat-state-test)
  # The installed game's weapons, loaded as a match loads them.
  add_executable(oa-combat-state-installed-weapons-test tests/installed_weapons_test.cpp)
  target_link_libraries(oa-combat-state-installed-weapons-test PRIVATE oa-sim-combat-state
    oa-data-defs-assets oa-formats-hpi oa-test-game-data)
  oa_add_game_data_test(combat-state-installed-weapons oa-combat-state-installed-weapons-test)
endif()
