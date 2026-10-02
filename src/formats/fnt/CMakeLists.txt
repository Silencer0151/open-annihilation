# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-formats-fnt src/font.cpp)
add_library(oa::formats::fnt ALIAS oa-formats-fnt)
target_include_directories(oa-formats-fnt PUBLIC include)
target_link_libraries(oa-formats-fnt PUBLIC oa-formats-hpi oa-formats-gaf oa-options)
if(BUILD_TESTING)
  add_executable(oa-font-format-test tests/font_test.cpp)
  target_link_libraries(oa-font-format-test PRIVATE oa-formats-fnt oa-test-support)
  add_test(NAME font-format COMMAND oa-font-format-test)
endif()
