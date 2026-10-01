# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_formats_smacker LANGUAGES CXX)
include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-formats-smacker src/smacker.cpp src/decoder.cpp)
add_library(oa::formats::smacker ALIAS oa-formats-smacker)
target_include_directories(oa-formats-smacker PUBLIC include)
target_compile_features(oa-formats-smacker PUBLIC cxx_std_20)
target_link_libraries(oa-formats-smacker PUBLIC oa-options)

# The bit-level stream builder and small movie the decoder's and the intro
# player's tests share.
add_library(oa-formats-smacker-test-movie INTERFACE)
add_library(oa::formats::smacker_test_movie ALIAS oa-formats-smacker-test-movie)
target_include_directories(oa-formats-smacker-test-movie INTERFACE tests/support)
target_link_libraries(oa-formats-smacker-test-movie INTERFACE oa-formats-smacker)

if(BUILD_TESTING)
  add_executable(oa-smacker-decoder-test tests/decoder_test.cpp)
  target_link_libraries(oa-smacker-decoder-test PRIVATE oa-formats-smacker-test-movie oa-options)
  add_test(NAME formats-smacker-decoder COMMAND oa-smacker-decoder-test
    ${CMAKE_CURRENT_BINARY_DIR}/smacker-decoder)
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
    # Every frame and sample of the five movies against the 3.1c release's.
    add_executable(oa-installed-movies-decode-test tests/installed_movies_decode_test.cpp)
    target_link_libraries(oa-installed-movies-decode-test PRIVATE oa-formats-smacker
      oa-base-sha256 oa-test-game-data)
    oa_add_game_data_test(intro-media-smacker-decode-data oa-installed-movies-decode-test)
  endif()
endif()
