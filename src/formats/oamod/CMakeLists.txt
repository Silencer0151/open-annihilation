# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Mod profiles (oamod.yaml): the strict YAML subset they are written in, read
# into a node tree, and the exact numbers it holds.
add_library(oa-formats-oamod src/number.cpp src/reader.cpp)
add_library(oa::formats::oamod ALIAS oa-formats-oamod)
target_include_directories(oa-formats-oamod PUBLIC include)
target_link_libraries(oa-formats-oamod PRIVATE oa-options)
target_compile_features(oa-formats-oamod PUBLIC cxx_std_20)

if(BUILD_TESTING)
  add_executable(oa-formats-oamod-test tests/oamod_test.cpp)
  target_link_libraries(oa-formats-oamod-test PRIVATE oa-formats-oamod oa-test-support oa-options)
  add_test(NAME formats-oamod COMMAND oa-formats-oamod-test)
endif()
