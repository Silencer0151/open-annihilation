# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-selection src/selection.cpp)
add_library(oa::sim::selection ALIAS oa-sim-selection)
target_include_directories(oa-sim-selection PUBLIC include)
target_compile_features(oa-sim-selection PUBLIC cxx_std_20)
target_link_libraries(oa-sim-selection PUBLIC oa-core-types oa-data-defs PRIVATE oa-sim-simulation-state oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-selection-test tests/selection_test.cpp)
  target_link_libraries(oa-sim-selection-test PRIVATE oa-sim-selection oa-options)
  add_test(NAME sim-selection COMMAND oa-sim-selection-test)
endif()
