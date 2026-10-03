# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The world the graphics card draws: card-ready data built from the game's
# assets, one library whose parts each have their own header, source and
# test, so that a part lands and is tested alone. Nothing draws from them
# yet.
add_library(oa-present-gpu-world src/terrain_atlas.cpp src/sprite_pages.cpp src/model_meshes.cpp)
add_library(oa::present::gpu_world ALIAS oa-present-gpu-world)
target_include_directories(oa-present-gpu-world PUBLIC include)
# The terrain atlas reads a parsed TNT map and the palette bytes; the sprite
# pages read rendered GAF frames and the present layer's surfaces; the model
# meshes read loaded 3DO models with the primitives the model library
# prepares for them, and place corners as the model runtime transforms a
# piece's vertices.
target_link_libraries(oa-present-gpu-world
  PUBLIC oa-present oa-present-model oa-formats-gaf oa-formats-hpi oa-formats-objects3d oa-formats-tnt
    oa-core-types
  PRIVATE oa-sim-model-runtime oa-base-geometry oa-base-game-math oa-options)
target_compile_features(oa-present-gpu-world PUBLIC cxx_std_20)

if(BUILD_TESTING)
  # The terrain atlas: page planning within every page edge, packing and
  # gutters, zoom 1 against today's terrain fill, the mip levels against the
  # area pass and today's box filter, the whole chain's exact reduction,
  # pinned digests, malformed maps and the memory figures; with --data, the
  # same over every map of the installed game.
  add_executable(oa-gpu-world-terrain-atlas-test tests/terrain_atlas_test.cpp)
  target_link_libraries(oa-gpu-world-terrain-atlas-test PRIVATE oa-present-gpu-world oa-present-world-renderer
    oa-present oa-formats-hpi oa-test-support oa-test-game-data oa-options)
  add_test(NAME gpu-world-terrain-atlas COMMAND oa-gpu-world-terrain-atlas-test)
  set_tests_properties(gpu-world-terrain-atlas PROPERTIES TIMEOUT 300)
  oa_add_game_data_test(gpu-world-terrain-atlas-data oa-gpu-world-terrain-atlas-test --data)
  set_tests_properties(gpu-world-terrain-atlas-data PROPERTIES TIMEOUT 900)

  # The sprite pages: frames through the palette and gamma in each draw
  # mode, RLE edge cases through the GAF reader, malformed frames refused,
  # packing, eviction under the memory limit and the memory figures; with
  # --data, every frame of the installed game's GAF files against today's
  # sprite drawers.
  add_executable(oa-present-gpu-world-sprite-pages-test tests/sprite_pages_test.cpp)
  target_link_libraries(oa-present-gpu-world-sprite-pages-test PRIVATE oa-present-gpu-world
    oa-formats-hpi oa-test-game-data oa-test-support oa-options)
  add_test(NAME present-gpu-world-sprite-pages COMMAND oa-present-gpu-world-sprite-pages-test)
  set_tests_properties(present-gpu-world-sprite-pages PROPERTIES TIMEOUT 120)
  oa_add_game_data_test(present-gpu-world-sprite-pages-data oa-present-gpu-world-sprite-pages-test --data)
  set_tests_properties(present-gpu-world-sprite-pages-data PROPERTIES TIMEOUT 300)

  # The model meshes: the projection against today's placement, synthetic
  # models rasterised from their meshes against today's images at rest and
  # turned, the quad split, malformed models refused and the memory figure;
  # with --data, every unit model of the installed game against today's
  # images, at rest and turned, with its texture archives and palette.
  add_executable(oa-present-gpu-world-model-meshes-test tests/model_meshes_test.cpp)
  target_link_libraries(oa-present-gpu-world-model-meshes-test PRIVATE oa-present-gpu-world
    oa-sim-model-runtime oa-formats-hpi oa-formats-tdf oa-test-game-data oa-test-support oa-options)
  add_test(NAME present-gpu-world-model-meshes COMMAND oa-present-gpu-world-model-meshes-test)
  set_tests_properties(present-gpu-world-model-meshes PROPERTIES TIMEOUT 120)
  oa_add_game_data_test(present-gpu-world-model-meshes-data oa-present-gpu-world-model-meshes-test --data)
  set_tests_properties(present-gpu-world-model-meshes-data PROPERTIES TIMEOUT 300)
endif()
