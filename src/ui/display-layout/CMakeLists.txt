# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Where the game's 640x480 frame sits in a window of another size: a header
# the app and its checks share.
add_library(oa-ui-display-layout INTERFACE)
add_library(oa::ui::display_layout ALIAS oa-ui-display-layout)
target_include_directories(oa-ui-display-layout INTERFACE include)
target_compile_features(oa-ui-display-layout INTERFACE cxx_std_20)

if(BUILD_TESTING)
  # The side column fitted to its tallest page, and points mapped through it.
  add_executable(oa-ui-display-layout-test tests/display_layout_test.cpp)
  target_link_libraries(oa-ui-display-layout-test PRIVATE oa-ui-display-layout oa-test-support)
  add_test(NAME ui-display-layout COMMAND oa-ui-display-layout-test)
endif()
