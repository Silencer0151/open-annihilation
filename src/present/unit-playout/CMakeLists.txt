# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The smooth playout of the units of players this machine does not simulate.
add_library(oa-present-unit-playout src/unit_playout.cpp)
add_library(oa::present::unit_playout ALIAS oa-present-unit-playout)
target_include_directories(oa-present-unit-playout PUBLIC include)
target_link_libraries(oa-present-unit-playout PUBLIC oa-core-types PRIVATE oa-options)
target_compile_features(oa-present-unit-playout PUBLIC cxx_std_20)

if(BUILD_TESTING)
  # Synthetic arrivals of an owner's records; the followed players are
  # checked against the simulation's locally simulated owners.
  add_executable(oa-present-unit-playout-test tests/unit_playout_test.cpp)
  target_link_libraries(oa-present-unit-playout-test PRIVATE oa-present-unit-playout oa-sim-simulation-state oa-options)
  add_test(NAME present-unit-playout COMMAND oa-present-unit-playout-test)
endif()
