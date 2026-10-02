# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_media LANGUAGES CXX)

option(OA_BUILD_INTRO_PLAYER "Build the SDL3 intro player" ON)
if(OA_BUILD_INTRO_PLAYER)
  find_package(SDL3 3.2 CONFIG QUIET)
  if(TARGET SDL3::SDL3)
    # The movies are decoded by the engine's Smacker decoder
    # (oa-formats-smacker) and shown and heard through SDL3.
    add_library(oa-media-intro-player src/intro_player.cpp)
    add_library(oa::media::intro_player ALIAS oa-media-intro-player)
    target_include_directories(oa-media-intro-player PUBLIC include)
    target_link_libraries(oa-media-intro-player PUBLIC oa-formats-smacker SDL3::SDL3
                          PRIVATE oa-audio-output oa-options)
    target_compile_features(oa-media-intro-player PUBLIC cxx_std_20)
    if(BUILD_TESTING)
      add_executable(oa-intro-player-test tests/intro_player_test.cpp)
      target_link_libraries(oa-intro-player-test PRIVATE oa-media-intro-player oa-test-support
        oa-formats-smacker-test-movie oa-audio-output oa-options)
      add_test(NAME intro-player COMMAND oa-intro-player-test
        ${CMAKE_CURRENT_BINARY_DIR}/intro-player)
      # A few seconds: one case waits out the player's 5 s bound on a sound
      # device that stops taking samples. A hang fails at the time limit.
      set_tests_properties(intro-player PROPERTIES TIMEOUT 60
        ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
    endif()
  else()
    set(oa_intro_player_note "oa-media-intro-player disabled: SDL3 was not found")
    if(OA_BUILD_PLATFORM)
      string(APPEND oa_intro_player_note "; oa-game skips the intro movies")
    endif()
    message(STATUS "${oa_intro_player_note}")
  endif()
endif()

# The intro player without SDL3: it checks each movie's SMK2 header and
# tables, then skips it with one logged line. oa-game links it when
# oa-media-intro-player is not built, so the game always links.
add_library(oa-media-intro-player-null src/intro_player_null.cpp)
add_library(oa::media::intro_player_null ALIAS oa-media-intro-player-null)
target_include_directories(oa-media-intro-player-null PUBLIC include)
target_link_libraries(oa-media-intro-player-null PUBLIC oa-formats-smacker PRIVATE oa-options)

# The director's camera: views on a map, the engine camera that draws one,
# a script's shots compiled to frames, transitions and the frame clock.
add_library(oa-media-director src/director.cpp src/director_clock.cpp
  src/director_transition.cpp)
add_library(oa::media::director ALIAS oa-media-director)
target_include_directories(oa-media-director PUBLIC include)
target_link_libraries(oa-media-director PUBLIC oa-formats-oascript PRIVATE oa-options)
target_compile_features(oa-media-director PUBLIC cxx_std_20)
if(BUILD_TESTING)
  add_executable(oa-media-director-test tests/director_test.cpp)
  target_link_libraries(oa-media-director-test PRIVATE oa-media-director oa-base-sha256
    oa-options)
  add_test(NAME media-director COMMAND oa-media-director-test)
  add_executable(oa-media-director-clock-test tests/director_clock_test.cpp)
  target_link_libraries(oa-media-director-clock-test PRIVATE oa-media-director oa-options)
  add_test(NAME media-director-clock COMMAND oa-media-director-clock-test)
  add_executable(oa-media-director-transition-test tests/director_transition_test.cpp)
  target_link_libraries(oa-media-director-transition-test PRIVATE oa-media-director
    oa-options)
  add_test(NAME media-director-transition COMMAND oa-media-director-transition-test)
endif()

# The director's planner: a replayed game's timeline, its recorder and the
# shots planned from it.
add_library(oa-media-director-planner src/timeline_recorder.cpp src/planner.cpp
  src/planner_analysis.cpp src/planner_framing.cpp src/planner_subjects.cpp)
add_library(oa::media::director_planner ALIAS oa-media-director-planner)
target_include_directories(oa-media-director-planner PUBLIC include)
target_link_libraries(oa-media-director-planner PUBLIC oa-media-director oa-formats-oascript
  oa-sim-match-runtime oa-core-types PRIVATE oa-sim-simulation-state oa-options)
target_compile_features(oa-media-director-planner PUBLIC cxx_std_20)
if(BUILD_TESTING)
  add_executable(oa-media-director-planner-recorder-test tests/timeline_recorder_test.cpp)
  target_link_libraries(oa-media-director-planner-recorder-test PRIVATE
    oa-media-director-planner oa-options)
  add_test(NAME media-director-planner-recorder COMMAND oa-media-director-planner-recorder-test)
  add_executable(oa-media-director-planner-test tests/planner_test.cpp)
  target_link_libraries(oa-media-director-planner-test PRIVATE oa-media-director-planner
    oa-base-sha256 oa-options)
  add_test(NAME media-director-planner COMMAND oa-media-director-planner-test)
endif()

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
