# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The C++ library's steady clock on the performance counter, for a MinGW
# build whose C++ run-time library reads it from the time of day (README.md).
# cmake/OaOptions.cmake links it, whole, into every executable of a MinGW
# build. Other builds keep their C++ library's clock and build nothing here.
if(MINGW)
  add_library(oa-platform-steady-clock src/steady_clock.cpp)
  add_library(oa::platform::steady_clock ALIAS oa-platform-steady-clock)
  target_compile_features(oa-platform-steady-clock PUBLIC cxx_std_20)
  target_link_libraries(oa-platform-steady-clock PRIVATE oa-options)

  if(BUILD_TESTING)
    add_executable(oa-platform-steady-clock-test tests/steady_clock_test.cpp)
    target_link_libraries(oa-platform-steady-clock-test PRIVATE oa-platform-steady-clock
      oa-test-support oa-options)
    add_test(NAME platform-steady-clock COMMAND oa-platform-steady-clock-test)
    set_tests_properties(platform-steady-clock PROPERTIES TIMEOUT 60)
  endif()
endif()
