# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_formats_objects3d LANGUAGES CXX)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-formats-objects3d src/three_do.cpp)
add_library(oa::formats::objects3d ALIAS oa-formats-objects3d)
target_include_directories(oa-formats-objects3d PUBLIC include)
target_compile_features(oa-formats-objects3d PUBLIC cxx_std_20)
target_link_libraries(oa-formats-objects3d PUBLIC oa-options)

if(BUILD_TESTING)
  add_executable(oa-model-format-test tests/three_do_test.cpp)
  target_link_libraries(oa-model-format-test PRIVATE oa-formats-objects3d)
  add_test(NAME model-format-3do COMMAND oa-model-format-test)
endif()
