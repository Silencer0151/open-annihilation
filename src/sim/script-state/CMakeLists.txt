# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_script_state LANGUAGES CXX)

include(CTest)

add_library(oa-sim-script-state src/script_state.cpp)
add_library(oa::sim::script_state ALIAS oa-sim-script-state)
target_include_directories(oa-sim-script-state PUBLIC include)
target_compile_features(oa-sim-script-state PUBLIC cxx_std_20)
if(TARGET oa-options)
  target_link_libraries(oa-sim-script-state PUBLIC oa-options)
endif()

if(BUILD_TESTING)
  add_executable(oa-script-state-test tests/script_state_test.cpp)
  target_link_libraries(oa-script-state-test PRIVATE oa-sim-script-state)
  add_test(NAME script-state COMMAND oa-script-state-test)
endif()
