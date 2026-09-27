# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_visibility_state LANGUAGES CXX)
include(CTest)
add_library(oa-sim-visibility-state src/visibility.cpp)
add_library(oa::sim::visibility_state ALIAS oa-sim-visibility-state)
target_include_directories(oa-sim-visibility-state PUBLIC include)
target_compile_features(oa-sim-visibility-state PUBLIC cxx_std_20)
target_link_libraries(oa-sim-visibility-state PUBLIC oa-core-types)
if(TARGET oa-options)
 target_link_libraries(oa-sim-visibility-state PUBLIC oa-options)
endif()
if(BUILD_TESTING)
 add_executable(oa-visibility-state-test tests/visibility_test.cpp)
 target_link_libraries(oa-visibility-state-test PRIVATE oa-sim-visibility-state)
 add_test(NAME visibility-state COMMAND oa-visibility-state-test)
endif()
