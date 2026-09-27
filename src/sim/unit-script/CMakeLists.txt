# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Engine-side unit-script calls over the canonical World, on top of the
# script VM and the COB parser.
add_library(oa-sim-unit-script src/unit_script.cpp src/unit_value.cpp)
add_library(oa::sim::unit_script ALIAS oa-sim-unit-script)
target_include_directories(oa-sim-unit-script PUBLIC include)
target_link_libraries(oa-sim-unit-script PUBLIC oa-sim-script-vm oa-core-types oa-formats-cob PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-unit-script-test tests/unit_script_test.cpp)
  target_link_libraries(oa-unit-script-test PRIVATE oa-sim-unit-script oa-options)
  add_test(NAME unit-script COMMAND oa-unit-script-test)
endif()
