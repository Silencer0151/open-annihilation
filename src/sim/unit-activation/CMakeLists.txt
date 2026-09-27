# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-unit-activation src/activation.cpp)
add_library(oa::sim::unit_activation ALIAS oa-sim-unit-activation)
target_include_directories(oa-sim-unit-activation PUBLIC include)
target_compile_features(oa-sim-unit-activation PUBLIC cxx_std_20)
target_link_libraries(oa-sim-unit-activation PUBLIC oa-options)
if(BUILD_TESTING)
  add_executable(oa-unit-activation-test tests/activation_test.cpp)
  target_link_libraries(oa-unit-activation-test PRIVATE oa-sim-unit-activation)
  add_test(NAME unit-activation COMMAND oa-unit-activation-test)
endif()
