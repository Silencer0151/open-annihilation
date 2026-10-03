# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The world the graphics card draws: card-ready data built from the game's
# assets, each part its own library so that it can be built and tested
# alone. Nothing draws from them yet.

# GAF sprite frames decoded once into pages of texels.
add_library(oa-present-gpu-world-sprite-pages src/sprite_pages.cpp)
add_library(oa::present::gpu_world_sprite_pages ALIAS oa-present-gpu-world-sprite-pages)
target_include_directories(oa-present-gpu-world-sprite-pages PUBLIC include)
target_link_libraries(oa-present-gpu-world-sprite-pages PUBLIC oa-present oa-formats-gaf oa-core-types PRIVATE oa-options)
target_compile_features(oa-present-gpu-world-sprite-pages PUBLIC cxx_std_20)

if(BUILD_TESTING)
  # Frames through the palette and gamma in each draw mode, RLE edge cases
  # through the GAF reader, malformed frames refused, packing, eviction
  # under the memory limit and the memory figures; with --data, every frame
  # of the installed game's GAF files against today's sprite drawers.
  add_executable(oa-present-gpu-world-sprite-pages-test tests/sprite_pages_test.cpp)
  target_link_libraries(oa-present-gpu-world-sprite-pages-test PRIVATE oa-present-gpu-world-sprite-pages
    oa-formats-hpi oa-test-game-data oa-test-support oa-options)
  add_test(NAME present-gpu-world-sprite-pages COMMAND oa-present-gpu-world-sprite-pages-test)
  set_tests_properties(present-gpu-world-sprite-pages PROPERTIES TIMEOUT 120)
  oa_add_game_data_test(present-gpu-world-sprite-pages-data oa-present-gpu-world-sprite-pages-test --data)
  set_tests_properties(present-gpu-world-sprite-pages-data PROPERTIES TIMEOUT 300)
endif()
