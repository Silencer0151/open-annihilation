# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_model_runtime LANGUAGES CXX)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-sim-model-runtime src/instance.cpp src/model_host.cpp)
add_library(oa::sim::model_runtime ALIAS oa-sim-model-runtime)
target_include_directories(oa-sim-model-runtime PUBLIC include)
target_compile_features(oa-sim-model-runtime PUBLIC cxx_std_20)
target_link_libraries(oa-sim-model-runtime PUBLIC oa-formats-objects3d oa-sim-script-vm oa-base-game-math oa-options)

if(BUILD_TESTING)
  add_executable(oa-model-runtime-test tests/instance_test.cpp)
  target_link_libraries(oa-model-runtime-test PRIVATE oa-sim-model-runtime)
  add_test(NAME model-runtime-instance COMMAND oa-model-runtime-test)
endif()
