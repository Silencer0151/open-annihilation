# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The card command list (card.hpp): what one frame asks the graphics card
# to draw, as plain data with no SDL, with its check and the quad builder.
add_library(oa-app-card src/frame.cpp)
add_library(oa::app::card ALIAS oa-app-card)
target_include_directories(oa-app-card PUBLIC include)
target_compile_features(oa-app-card PUBLIC cxx_std_20)
target_link_libraries(oa-app-card PRIVATE oa-options)
if(OA_BUILD_PLATFORM)
  # The executor (card/executor.hpp): texture pages and render targets made,
  # updated and destroyed on SDL's renderer, and a frame run on it with
  # SDL_RenderGeometryRaw and the renderer's blend modes, one call for each
  # run of batches that share their state. Only the Full tier reaches it.
  add_library(oa-app-card-executor src/executor.cpp)
  add_library(oa::app::card_executor ALIAS oa-app-card-executor)
  target_link_libraries(oa-app-card-executor PUBLIC oa-app-card SDL3::SDL3 PRIVATE oa-options)
  if(BUILD_TESTING)
    # Frames run on SDL's software renderer over a surface and read back
    # against a reference rasteriser on the processor: textured and
    # untextured quads and triangles under every blend mode, premultiplied
    # alpha among them, a scissor, vertex colours, a draw's own sampling
    # mode, a page's levels and a part of one updated, render targets at
    # supersampling factors 2 and 4 resolved with a downscale, a transparent
    # target resolved by premultiplied alpha, a target written again
    # between two resolves, edges between a target's pixels reduced to
    # their coverage, and the two-level reduction of a target that keeps
    # its half against the renderer's own stretch and blend; malformed
    # frames and frames naming a destroyed page or a target without its
    # half refused with nothing drawn; pages and targets beyond the
    # renderer's texture limit refused with an error naming both; batches
    # that share their state merged into one call; the renderer's state put
    # back; and the processor cost of 5,000 and 20,000 quads a frame,
    # printed.
    add_executable(oa-app-card-frame-test tests/card_frame_test.cpp)
    target_link_libraries(oa-app-card-frame-test PRIVATE oa-app-card-executor SDL3::SDL3
      oa-test-support oa-options)
    add_test(NAME app-card-frame COMMAND oa-app-card-frame-test)
    set_tests_properties(app-card-frame PROPERTIES TIMEOUT 180
      ENVIRONMENT "SDL_VIDEO_DRIVER=dummy;SDL_AUDIO_DRIVER=dummy")
  endif()
endif()
