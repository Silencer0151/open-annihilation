# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Live map features over the canonical World: placement and removal,
# burning, death and reclaim, falling wrecks and tree reproduction.
add_library(oa-sim-feature-runtime src/feature_runtime.cpp)
add_library(oa::sim::feature_runtime ALIAS oa-sim-feature-runtime)
target_include_directories(oa-sim-feature-runtime PUBLIC include)
target_compile_features(oa-sim-feature-runtime PUBLIC cxx_std_20)
target_link_libraries(oa-sim-feature-runtime PUBLIC oa-core-types oa-options PRIVATE oa-base-bytes)
if(MSVC)
  target_compile_options(oa-sim-feature-runtime PRIVATE /W4)
else()
  target_compile_options(oa-sim-feature-runtime PRIVATE -Wall -Wextra -Wpedantic -Wconversion -Wsign-conversion)
endif()
if(BUILD_TESTING)
  add_executable(oa-feature-runtime-test tests/feature_runtime_test.cpp)
  target_link_libraries(oa-feature-runtime-test PRIVATE oa-sim-feature-runtime)
  add_test(NAME feature-runtime COMMAND oa-feature-runtime-test)
endif()
