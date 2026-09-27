# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Where a resource's data lies in a Windows program file (a PE image). It
# reads bytes only; nothing in the image runs.
add_library(oa-formats-pe src/resources.cpp)
add_library(oa::formats::pe ALIAS oa-formats-pe)
target_include_directories(oa-formats-pe PUBLIC include)
target_compile_features(oa-formats-pe PUBLIC cxx_std_20)
target_link_libraries(oa-formats-pe PRIVATE oa-options)
if(BUILD_TESTING)
  # Builds synthetic PE images for the tests of code that reads them
  # (oa/test/pe_image.hpp).
  add_library(oa-formats-pe-test-image INTERFACE)
  add_library(oa::formats::pe_test_image ALIAS oa-formats-pe-test-image)
  target_include_directories(oa-formats-pe-test-image INTERFACE tests)
  target_compile_features(oa-formats-pe-test-image INTERFACE cxx_std_20)
  add_executable(oa-formats-pe-test tests/pe_test.cpp)
  target_link_libraries(oa-formats-pe-test PRIVATE oa-formats-pe oa-formats-pe-test-image oa-options)
  add_test(NAME formats-pe COMMAND oa-formats-pe-test)
endif()
