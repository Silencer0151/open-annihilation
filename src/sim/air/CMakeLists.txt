# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-air
  src/goal.cpp
  src/driver.cpp
  src/flight.cpp)
add_library(oa::sim::air ALIAS oa-sim-air)
target_include_directories(oa-sim-air PUBLIC include)
target_link_libraries(oa-sim-air PUBLIC oa-core-types oa-base-game-math oa-sim-unit-movement oa-sim-ground-orders oa-options)
target_compile_features(oa-sim-air PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-air-air-test tests/air_test.cpp)
  target_link_libraries(oa-air-air-test PRIVATE oa-sim-air)
  add_test(NAME air-air COMMAND oa-air-air-test)
endif()
