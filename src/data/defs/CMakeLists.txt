# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_data_defs LANGUAGES C CXX)

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-data-defs
  src/categories.cpp
  src/files.cpp
  src/gamedata_tables.cpp
  src/locale.cpp
  src/move_classes.cpp
  src/palette.cpp
  src/sides.cpp
  src/sound_categories.cpp
  src/unit_catalog.cpp
  src/unit_def_loader.cpp
  src/unit_header.cpp
  src/unit_records.cpp
  src/version.cpp
  src/weapons.cpp)
add_library(oa::data::defs ALIAS oa-data-defs)
target_link_libraries(oa-data-defs PRIVATE oa-base-game-math)
target_include_directories(oa-data-defs PUBLIC include)
target_compile_features(oa-data-defs PUBLIC cxx_std_20)
target_link_libraries(oa-data-defs PUBLIC oa-formats-tdf oa-core-types PRIVATE oa-data-mission-types)
if(TARGET oa-options)
  target_link_libraries(oa-data-defs PUBLIC oa-options)
endif()

if(TARGET oa-formats-hpi)
  add_library(oa-data-defs-assets src/asset_files.cpp)
  add_library(oa::data::defs_assets ALIAS oa-data-defs-assets)
  target_link_libraries(oa-data-defs-assets PUBLIC oa-data-defs oa-formats-hpi)
endif()

if(BUILD_TESTING)
  add_executable(oa-defs-test tests/defs_test.cpp)
  target_link_libraries(oa-defs-test PRIVATE oa-data-defs oa-data-mission-types)
  add_test(NAME defs COMMAND oa-defs-test)
endif()

if(BUILD_TESTING)
  add_executable(oa-defs-file-loaders-test tests/file_loaders_test.cpp)
  target_link_libraries(oa-defs-file-loaders-test PRIVATE oa-data-defs)
  add_test(NAME defs-file-loaders COMMAND oa-defs-file-loaders-test)
endif()
