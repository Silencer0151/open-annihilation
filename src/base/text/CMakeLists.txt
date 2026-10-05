# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Copying and appending zero-terminated text in fixed-size character fields,
# and where a line of text may break into rows.
add_library(oa-base-text INTERFACE)
add_library(oa::base::text ALIAS oa-base-text)
target_include_directories(oa-base-text INTERFACE include)
target_compile_features(oa-base-text INTERFACE cxx_std_20)
target_link_libraries(oa-base-text INTERFACE oa-options)
if(BUILD_TESTING)
  add_executable(oa-base-text-test tests/text_test.cpp)
  target_link_libraries(oa-base-text-test PRIVATE oa-base-text oa-test-support)
  add_test(NAME base-text COMMAND oa-base-text-test)
  add_executable(oa-base-text-line-break-test tests/line_break_test.cpp)
  target_link_libraries(oa-base-text-line-break-test PRIVATE oa-base-text oa-test-support)
  add_test(NAME base-text-line-break COMMAND oa-base-text-line-break-test)
endif()
