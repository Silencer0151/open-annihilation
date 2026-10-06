# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The screen sizes a display offers: its full-screen modes, each size once
# and sorted, from 640x480 up, or for a window those that fit the desktop,
# from a report of the display that a test or a check can make up.
add_library(oa-platform-display-modes src/display_modes.cpp)
add_library(oa::platform::display_modes ALIAS oa-platform-display-modes)
target_include_directories(oa-platform-display-modes PUBLIC include)
target_compile_features(oa-platform-display-modes PUBLIC cxx_std_20)
target_link_libraries(oa-platform-display-modes PRIVATE oa-options)

if(BUILD_TESTING)
  # The sizes offered for made-up monitors: a 4K monitor, a Retina Mac's
  # built-in display, a 1280x1024 monitor of Windows XP's time, an
  # ultrawide, a portrait monitor, one that reports more than the list holds
  # and one that reports nothing; for full screen and for a window; the mode
  # each size switches to; and the reports a check names in text.
  add_executable(oa-platform-display-modes-test tests/display_modes_test.cpp)
  target_link_libraries(oa-platform-display-modes-test PRIVATE oa-platform-display-modes
    oa-test-support oa-options)
  add_test(NAME platform-display-modes COMMAND oa-platform-display-modes-test)
endif()

# The report read through SDL, and the full-screen mode a window takes.
if(NOT TARGET SDL3::SDL3)
  return()
endif()
add_library(oa-platform-display-modes-sdl src/sdl_display.cpp)
add_library(oa::platform::display_modes_sdl ALIAS oa-platform-display-modes-sdl)
target_compile_features(oa-platform-display-modes-sdl PUBLIC cxx_std_20)
target_link_libraries(oa-platform-display-modes-sdl PUBLIC oa-platform-display-modes
  PRIVATE SDL3::SDL3 oa-options)

if(BUILD_TESTING)
  # The same rules over what SDL's dummy video driver reports: no
  # full-screen mode and a 1024x768 desktop.
  add_executable(oa-platform-display-modes-sdl-test tests/sdl_display_test.cpp)
  target_link_libraries(oa-platform-display-modes-sdl-test PRIVATE oa-platform-display-modes-sdl
    SDL3::SDL3 oa-test-support oa-options)
  add_test(NAME platform-display-modes-sdl COMMAND oa-platform-display-modes-sdl-test)
  set_tests_properties(platform-display-modes-sdl PROPERTIES
    ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
endif()
