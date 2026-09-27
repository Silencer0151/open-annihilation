# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-app-command-line src/command_line.cpp)
add_library(oa::app::command_line ALIAS oa-app-command-line)
target_include_directories(oa-app-command-line PUBLIC include)
target_compile_features(oa-app-command-line PUBLIC cxx_std_20)
target_link_libraries(oa-app-command-line PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-command-line-test tests/command_line_test.cpp)
  target_link_libraries(oa-command-line-test PRIVATE oa-app-command-line oa-options)
  add_test(NAME command-line COMMAND oa-command-line-test)
endif()
