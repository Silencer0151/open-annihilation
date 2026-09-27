# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_sim_script_vm LANGUAGES CXX)

include(CTest)

add_library(oa-sim-script-vm src/script_vm.cpp)
add_library(oa::sim::script_vm ALIAS oa-sim-script-vm)
target_include_directories(oa-sim-script-vm PUBLIC include)
target_compile_features(oa-sim-script-vm PUBLIC cxx_std_20)
target_link_libraries(oa-sim-script-vm PUBLIC oa-sim-script-state)
if(TARGET oa-options)
  target_link_libraries(oa-sim-script-vm PUBLIC oa-options)
endif()

if(BUILD_TESTING)
  add_executable(oa-script-vm-test tests/script_vm_test.cpp)
  target_link_libraries(oa-script-vm-test PRIVATE oa-sim-script-vm)
  add_test(NAME script-vm COMMAND oa-script-vm-test)
endif()

if(BUILD_TESTING)
  add_executable(oa-script-vm-tick-context-test tests/tick_context_test.cpp)
  target_link_libraries(oa-script-vm-tick-context-test PRIVATE oa-sim-script-vm)
  add_test(NAME script-vm-tick-context COMMAND oa-script-vm-tick-context-test)
endif()
