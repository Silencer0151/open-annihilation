# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-unit-effects src/effects.cpp src/offline.cpp)
add_library(oa::sim::unit_effects ALIAS oa-sim-unit-effects)
target_include_directories(oa-sim-unit-effects PUBLIC include)
target_link_libraries(oa-sim-unit-effects PUBLIC oa-sim-match-runtime)
target_compile_features(oa-sim-unit-effects PUBLIC cxx_std_20)
if(TARGET oa-options)
 target_link_libraries(oa-sim-unit-effects PUBLIC oa-options)
endif()
if(BUILD_TESTING)
 add_executable(oa-unit-effects-test tests/effects_test.cpp)
 target_link_libraries(oa-unit-effects-test PRIVATE oa-sim-unit-effects)
 add_test(NAME unit-effects COMMAND oa-unit-effects-test)
 add_executable(oa-unit-effects-offline-test tests/offline_test.cpp)
 target_link_libraries(oa-unit-effects-offline-test PRIVATE oa-sim-unit-effects oa-test-match)
 add_test(NAME unit-effects-offline COMMAND oa-unit-effects-offline-test)
 add_executable(oa-unit-effects-shatter-test tests/shatter_test.cpp)
 target_link_libraries(oa-unit-effects-shatter-test PRIVATE oa-sim-unit-effects oa-present-model oa-formats-hpi
   oa-test-game-data oa-test-match)
 # Shatters of the installed game's ARMPW and CORUWMEX models.
 oa_add_game_data_test(unit-effects-shatter oa-unit-effects-shatter-test)
 set_tests_properties(unit-effects-shatter PROPERTIES TIMEOUT 120)
endif()
