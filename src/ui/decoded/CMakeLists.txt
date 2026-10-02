# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Turns a decoder's error into an exception for the screens and the
# application.
add_library(oa-ui-decoded src/decoded.cpp)
add_library(oa::ui::decoded ALIAS oa-ui-decoded)
target_include_directories(oa-ui-decoded PUBLIC include)
target_compile_features(oa-ui-decoded PUBLIC cxx_std_20)
target_link_libraries(oa-ui-decoded PUBLIC oa-base-bytes PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-ui-decoded-test tests/decoded_test.cpp)
  target_link_libraries(oa-ui-decoded-test PRIVATE oa-ui-decoded oa-options)
  add_test(NAME ui-decoded COMMAND oa-ui-decoded-test)
endif()
