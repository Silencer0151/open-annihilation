# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# SHA-256 (FIPS 180-4), with which the engine checks that a file is the one
# it expects.
add_library(oa-base-sha256 src/sha256.cpp)
add_library(oa::base::sha256 ALIAS oa-base-sha256)
target_include_directories(oa-base-sha256 PUBLIC include)
target_compile_features(oa-base-sha256 PUBLIC cxx_std_20)
target_link_libraries(oa-base-sha256 PUBLIC oa-options)
# A start that plays an unpacked release hashes tens of megabytes; without
# optimisation that takes seconds, so Debug builds optimise this library too.
# The Microsoft compiler's Debug run-time checks do not allow it.
if(NOT MSVC)
  target_compile_options(oa-base-sha256 PRIVATE $<$<CONFIG:Debug>:-O2>)
endif()
if(BUILD_TESTING)
  add_executable(oa-base-sha256-test tests/sha256_test.cpp)
  target_link_libraries(oa-base-sha256-test PRIVATE oa-base-sha256)
  add_test(NAME base-sha256 COMMAND oa-base-sha256-test)
endif()
