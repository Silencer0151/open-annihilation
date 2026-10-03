# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The rules a mod profile sets for the simulation: plain, fixed-size records
# whose defaults are 3.1c's, generated from the OAMOD registry
# (tools/gen_mod_registry.py). Header-only.
add_library(oa-data-match-rules INTERFACE)
add_library(oa::data::match_rules ALIAS oa-data-match-rules)
target_include_directories(oa-data-match-rules INTERFACE include)
target_compile_features(oa-data-match-rules INTERFACE cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-data-match-rules-test tests/match_rules_test.cpp)
  target_link_libraries(oa-data-match-rules-test PRIVATE oa-data-match-rules oa-test-support oa-options)
  add_test(NAME data-match-rules COMMAND oa-data-match-rules-test)
endif()
