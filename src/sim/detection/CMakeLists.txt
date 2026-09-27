# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-detection src/detection.cpp)
add_library(oa::sim::detection ALIAS oa-sim-detection)
target_include_directories(oa-sim-detection PUBLIC include)
target_compile_features(oa-sim-detection PUBLIC cxx_std_20)
target_link_libraries(oa-sim-detection PUBLIC oa-core-types PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-detection-test tests/detection_test.cpp)
  target_link_libraries(oa-sim-detection-test PRIVATE oa-sim-detection oa-options)
  add_test(NAME sim-detection COMMAND oa-sim-detection-test)
endif()
