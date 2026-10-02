# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_formats_tdf LANGUAGES CXX)

include(CTest)

add_library(oa-formats-tdf src/tdf.cpp)
add_library(oa::formats::tdf ALIAS oa-formats-tdf)
target_link_libraries(oa-formats-tdf PRIVATE oa-base-text oa-base-game-math)
target_include_directories(oa-formats-tdf PUBLIC include)
target_compile_features(oa-formats-tdf PUBLIC cxx_std_20)
if(TARGET oa-options)
  target_link_libraries(oa-formats-tdf PUBLIC oa-options)
endif()

if(BUILD_TESTING)
  add_executable(oa-tdf-test tests/tdf_test.cpp)
  target_link_libraries(oa-tdf-test PRIVATE oa-formats-tdf)
  add_test(NAME tdf-parser COMMAND oa-tdf-test)
endif()
