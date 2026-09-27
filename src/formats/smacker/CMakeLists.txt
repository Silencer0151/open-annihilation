# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_formats_smacker LANGUAGES CXX)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-formats-smacker src/smacker.cpp)
add_library(oa::formats::smacker ALIAS oa-formats-smacker)
target_include_directories(oa-formats-smacker PUBLIC include)
target_compile_features(oa-formats-smacker PUBLIC cxx_std_20)
target_link_libraries(oa-formats-smacker PUBLIC oa-options)

if(BUILD_TESTING)
  add_executable(oa-intro-media-test tests/smacker_test.cpp)
  target_link_libraries(oa-intro-media-test PRIVATE oa-media-intro-player-null)
  target_link_libraries(oa-intro-media-test PRIVATE oa-formats-smacker)
  add_test(NAME intro-media-smacker COMMAND oa-intro-media-test)
  # Inside the engine, which provides the game-data helper: the installed
  # game's five movies.
  if(COMMAND oa_add_game_data_test)
    add_executable(oa-installed-movies-test tests/installed_movies_test.cpp)
    target_link_libraries(oa-installed-movies-test PRIVATE oa-formats-smacker oa-test-game-data)
    oa_add_game_data_test(intro-media-smacker-data oa-installed-movies-test)
  endif()
endif()
