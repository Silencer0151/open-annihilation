# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-trace src/trace.cpp)
add_library(oa::sim::trace ALIAS oa-sim-trace)
target_include_directories(oa-sim-trace PUBLIC include)
target_compile_features(oa-sim-trace PUBLIC cxx_std_20)
target_link_libraries(oa-sim-trace PUBLIC oa-core-types PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-trace-test tests/trace_test.cpp)
  target_link_libraries(oa-sim-trace-test PRIVATE oa-sim-trace oa-options)
  add_test(NAME sim-trace COMMAND oa-sim-trace-test)
endif()
