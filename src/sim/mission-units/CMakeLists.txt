# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-mission-units src/mission_units.cpp)
add_library(oa::sim::mission_units ALIAS oa-sim-mission-units)
target_include_directories(oa-sim-mission-units PUBLIC include)
target_compile_features(oa-sim-mission-units PUBLIC cxx_std_20)
target_link_libraries(oa-sim-mission-units PUBLIC oa-core-types oa-data-campaign PRIVATE oa-base-text oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-mission-units-test tests/mission_units_test.cpp)
  target_link_libraries(oa-sim-mission-units-test PRIVATE oa-base-text oa-sim-mission-units oa-options)
  add_test(NAME sim-mission-units COMMAND oa-sim-mission-units-test)
  add_executable(oa-sim-mission-scripts-test tests/mission_scripts_test.cpp)
  target_link_libraries(oa-sim-mission-scripts-test PRIVATE oa-base-text oa-sim-mission-units oa-data-mission-types oa-options)
  add_test(NAME sim-mission-scripts COMMAND oa-sim-mission-scripts-test)
endif()
