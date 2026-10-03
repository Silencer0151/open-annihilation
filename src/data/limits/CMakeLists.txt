# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-data-limits src/limits.cpp)
add_library(oa::data::limits ALIAS oa-data-limits)
target_include_directories(oa-data-limits PUBLIC include)
target_compile_features(oa-data-limits PUBLIC cxx_std_20)
target_link_libraries(oa-data-limits PUBLIC oa-options)
if(BUILD_TESTING)
  add_executable(oa-data-limits-test tests/limits_test.cpp)
  target_link_libraries(oa-data-limits-test PRIVATE oa-data-limits oa-test-support)
  add_test(NAME data-limits COMMAND oa-data-limits-test)
endif()
