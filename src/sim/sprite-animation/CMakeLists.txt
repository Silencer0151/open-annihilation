# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_sprite_animation LANGUAGES CXX)

include(CTest)

add_library(oa-sim-sprite-animation src/animation.cpp)
add_library(oa::sim::sprite_animation ALIAS oa-sim-sprite-animation)
target_include_directories(oa-sim-sprite-animation PUBLIC include)
target_compile_features(oa-sim-sprite-animation PUBLIC cxx_std_20)
target_link_libraries(oa-sim-sprite-animation PUBLIC oa-formats-gaf)
if(TARGET oa-options)
  target_link_libraries(oa-sim-sprite-animation PUBLIC oa-options)
endif()

if(BUILD_TESTING)
  add_executable(oa-sprite-animation-test tests/animation_test.cpp)
  target_link_libraries(oa-sprite-animation-test PRIVATE oa-sim-sprite-animation)
  add_test(NAME sprite-animation COMMAND oa-sprite-animation-test)
endif()
