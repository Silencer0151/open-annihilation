# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Weapon ballistics: the flags and aim solutions the combat state and the
# weapons read.
add_library(oa-sim-ballistics src/ballistic.cpp)
add_library(oa::sim::ballistics ALIAS oa-sim-ballistics)
target_include_directories(oa-sim-ballistics PUBLIC include)
target_compile_features(oa-sim-ballistics PUBLIC cxx_std_20)
target_link_libraries(oa-sim-ballistics PUBLIC oa-base-game-math oa-options)
if(BUILD_TESTING)
  add_executable(oa-combat-ballistic-test tests/ballistic_test.cpp)
  target_link_libraries(oa-combat-ballistic-test PRIVATE oa-sim-ballistics)
  add_test(NAME combat-state-ballistic COMMAND oa-combat-ballistic-test)
endif()
