# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The precision double operations round to, and the floating-point settings
# the program saves as it starts and puts back when they change. Every 32-bit
# x86 executable that links oa-options takes this library whole
# (cmake/OaOptions.cmake), so its start-up hook runs before main() in each
# engine program there.
add_library(oa-base-float-precision src/float_precision.cpp)
add_library(oa::base::float_precision ALIAS oa-base-float-precision)
target_include_directories(oa-base-float-precision PUBLIC include)
target_compile_features(oa-base-float-precision PUBLIC cxx_std_20)
target_link_libraries(oa-base-float-precision PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-base-float-precision-test tests/float_precision_test.cpp)
  target_link_libraries(oa-base-float-precision-test PRIVATE oa-base-float-precision
    oa-base-threads oa-test-support oa-options)
  add_test(NAME base-float-precision COMMAND oa-base-float-precision-test)
  set_tests_properties(base-float-precision PROPERTIES TIMEOUT 60)
endif()
