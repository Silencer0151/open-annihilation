# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The languages the user prefers, as the operating system gives them:
# SDL's preferred locales, else the system's own query.
if(NOT TARGET SDL3::SDL3)
  return()
endif()
add_library(oa-platform-locale src/locale.cpp)
add_library(oa::platform::locale ALIAS oa-platform-locale)
target_include_directories(oa-platform-locale PUBLIC include)
target_compile_features(oa-platform-locale PUBLIC cxx_std_20)
target_link_libraries(oa-platform-locale PRIVATE SDL3::SDL3 oa-options)

if(BUILD_TESTING)
  # The locale environment variables read in their order, each locale once,
  # and SDL's list, which a hint the test sets stands in for the system's.
  add_executable(oa-platform-locale-test tests/locale_test.cpp)
  target_link_libraries(oa-platform-locale-test PRIVATE oa-platform-locale SDL3::SDL3 oa-test-support
    oa-options)
  add_test(NAME platform-locale COMMAND oa-platform-locale-test)
endif()
