# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_media LANGUAGES CXX)

option(OA_BUILD_INTRO_PLAYER "Build the FFmpeg/SDL3 intro player" ON)
if(OA_BUILD_INTRO_PLAYER)
  find_package(SDL3 3.2 CONFIG QUIET)
  find_path(OA_FFMPEG_INCLUDE_DIR libavformat/avformat.h
            HINTS /opt/homebrew/opt/ffmpeg/include)
  find_library(OA_AVFORMAT_LIBRARY NAMES avformat
               HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_AVCODEC_LIBRARY NAMES avcodec
               HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_AVUTIL_LIBRARY NAMES avutil
               HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_SWSCALE_LIBRARY NAMES swscale
               HINTS /opt/homebrew/opt/ffmpeg/lib)
  find_library(OA_SWRESAMPLE_LIBRARY NAMES swresample
               HINTS /opt/homebrew/opt/ffmpeg/lib)
  if(TARGET SDL3::SDL3 AND OA_FFMPEG_INCLUDE_DIR AND
     OA_AVFORMAT_LIBRARY AND OA_AVCODEC_LIBRARY AND OA_AVUTIL_LIBRARY AND
     OA_SWSCALE_LIBRARY AND OA_SWRESAMPLE_LIBRARY)
    add_library(oa-media-intro-player src/intro_player.cpp)
    add_library(oa::media::intro_player ALIAS oa-media-intro-player)
    target_include_directories(oa-media-intro-player PUBLIC include
                               ${OA_FFMPEG_INCLUDE_DIR})
    target_link_libraries(oa-media-intro-player PUBLIC oa-formats-smacker SDL3::SDL3
                          ${OA_AVFORMAT_LIBRARY} ${OA_AVCODEC_LIBRARY}
                          ${OA_AVUTIL_LIBRARY} ${OA_SWSCALE_LIBRARY}
                          ${OA_SWRESAMPLE_LIBRARY} PRIVATE oa-options)
    target_compile_features(oa-media-intro-player PUBLIC cxx_std_20)
    if(EXISTS "/opt/homebrew/opt/ffmpeg/lib")
      set_property(TARGET oa-media-intro-player APPEND PROPERTY
                   BUILD_RPATH "/opt/homebrew/opt/ffmpeg/lib")
    endif()
  else()
    set(oa_intro_player_note "oa-media-intro-player disabled: SDL3 and FFmpeg development libraries were not found")
    if(OA_BUILD_PLATFORM)
      string(APPEND oa_intro_player_note "; oa-game skips the intro movies")
    endif()
    message(STATUS "${oa_intro_player_note}")
  endif()
endif()

# The intro player without FFmpeg: it checks each movie's SMK2 header and
# tables, then skips it with one logged line. oa-game links it when
# oa-media-intro-player is not built, so the game always links.
add_library(oa-media-intro-player-null src/intro_player_null.cpp)
add_library(oa::media::intro_player_null ALIAS oa-media-intro-player-null)
target_include_directories(oa-media-intro-player-null PUBLIC include)
target_link_libraries(oa-media-intro-player-null PUBLIC oa-formats-smacker PRIVATE oa-options)

add_library(oa-media-movie-surface src/movie_surface.cpp)
add_library(oa::media::movie_surface ALIAS oa-media-movie-surface)
target_include_directories(oa-media-movie-surface PUBLIC include)
target_link_libraries(oa-media-movie-surface PUBLIC oa-present PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-movie-surface-test tests/movie_surface_test.cpp)
  target_link_libraries(oa-movie-surface-test PRIVATE oa-media-movie-surface oa-options)
  add_test(NAME movie-surface COMMAND oa-movie-surface-test)
  add_executable(oa-intro-player-null-test tests/intro_player_null_test.cpp)
  target_link_libraries(oa-intro-player-null-test PRIVATE oa-media-intro-player-null oa-options)
  add_test(NAME intro-player-null COMMAND oa-intro-player-null-test
    ${CMAKE_CURRENT_BINARY_DIR}/intro-player-null)
endif()
