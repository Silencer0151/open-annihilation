# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-speed src/speed.cpp)
add_library(oa::sim::speed ALIAS oa-sim-speed)
target_include_directories(oa-sim-speed PUBLIC include)
target_compile_features(oa-sim-speed PUBLIC cxx_std_20)
target_link_libraries(oa-sim-speed PUBLIC oa-sim-messages oa-core-types oa-data-match-rules PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-speed-test tests/speed_test.cpp)
  target_link_libraries(oa-sim-speed-test PRIVATE oa-sim-speed oa-options)
  add_test(NAME sim-speed COMMAND oa-sim-speed-test)
endif()
