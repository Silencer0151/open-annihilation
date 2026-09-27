# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The registry of frontend screens that the ui modules and the app fill.
add_library(oa-ui-screen-registry src/screen_registry.cpp)
target_include_directories(oa-ui-screen-registry PUBLIC include)
target_link_libraries(oa-ui-screen-registry PUBLIC oa-options)
if(BUILD_TESTING)
  add_executable(oa-screen-registry-test tests/screen_registry_test.cpp)
  target_link_libraries(oa-screen-registry-test PRIVATE oa-ui-screen-registry oa-ui-frontend-state oa-options)
  add_test(NAME screen-registry COMMAND oa-screen-registry-test)
endif()
