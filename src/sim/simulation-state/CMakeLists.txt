# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_simulation_state LANGUAGES CXX)
include(CTest)
add_library(oa-sim-simulation-state src/simulation.cpp)
add_library(oa::sim::simulation_state ALIAS oa-sim-simulation-state)
target_include_directories(oa-sim-simulation-state PUBLIC include)
target_link_libraries(oa-sim-simulation-state PUBLIC oa-core-types)
target_compile_features(oa-sim-simulation-state PUBLIC cxx_std_20)
if(TARGET oa-options)
  target_link_libraries(oa-sim-simulation-state PUBLIC oa-options)
endif()
if(BUILD_TESTING)
  add_executable(oa-simulation-state-test tests/simulation_test.cpp)
  target_link_libraries(oa-simulation-state-test PRIVATE oa-sim-simulation-state)
  add_test(NAME simulation-state COMMAND oa-simulation-state-test)
endif()
