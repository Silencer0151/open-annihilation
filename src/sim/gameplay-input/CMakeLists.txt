# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-gameplay-input src/input.cpp src/order_cursor.cpp)
add_library(oa::sim::gameplay_input ALIAS oa-sim-gameplay-input)
target_include_directories(oa-sim-gameplay-input PUBLIC include)
target_link_libraries(oa-sim-gameplay-input PUBLIC oa-core-types oa-sim-model-runtime oa-sim-unit-movement oa-options
  oa-sim-weapon-execution)
target_compile_features(oa-sim-gameplay-input PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-gameplay-input-test tests/input_test.cpp)
  target_link_libraries(oa-gameplay-input-test PRIVATE oa-sim-gameplay-input oa-test-support)
  add_test(NAME gameplay-input COMMAND oa-gameplay-input-test)
  add_executable(oa-order-cursor-test tests/order_cursor_test.cpp)
  target_link_libraries(oa-order-cursor-test PRIVATE oa-sim-gameplay-input)
  add_test(NAME gameplay-input-order-cursor COMMAND oa-order-cursor-test)
endif()
