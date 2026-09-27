# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_formats_cob LANGUAGES CXX)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-formats-cob src/cob.cpp)
add_library(oa::formats::cob ALIAS oa-formats-cob)
target_include_directories(oa-formats-cob PUBLIC include)
target_compile_features(oa-formats-cob PUBLIC cxx_std_20)
target_link_libraries(oa-formats-cob PUBLIC oa-options)

if(BUILD_TESTING)
  add_executable(oa-script-format-test tests/cob_test.cpp)
  target_link_libraries(oa-script-format-test PRIVATE oa-formats-cob)
  add_test(NAME script-format-cob COMMAND oa-script-format-test)
endif()
