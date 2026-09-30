# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-present-world-renderer src/world_renderer.cpp src/unit_renderer.cpp src/camera.cpp src/radar.cpp src/display_modes.cpp
  src/overlays.cpp src/fog.cpp src/terrain_view.cpp src/draw_order.cpp)
add_library(oa::present::world_renderer ALIAS oa-present-world-renderer)
target_include_directories(oa-present-world-renderer PUBLIC include)
target_link_libraries(oa-present-world-renderer PUBLIC oa-formats-hpi oa-formats-tnt oa-sim-model-runtime oa-options oa-formats-gaf
  oa-core-types oa-present oa-sim-visibility-state)

add_executable(oa-render-terrain EXCLUDE_FROM_ALL tools/render_crop.cpp)
target_link_libraries(oa-render-terrain PRIVATE oa-present-world-renderer)

add_executable(oa-render-unit EXCLUDE_FROM_ALL tools/render_unit.cpp)
target_link_libraries(oa-render-unit PRIVATE oa-present-world-renderer)

if(BUILD_TESTING)
  add_executable(oa-world-renderer-test tests/world_renderer_test.cpp)
  target_link_libraries(oa-world-renderer-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-renderer COMMAND oa-world-renderer-test)
  add_executable(oa-world-screen-span-test tests/world_screen_span_test.cpp)
  target_link_libraries(oa-world-screen-span-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-screen-span COMMAND oa-world-screen-span-test)
  add_executable(oa-world-camera-test tests/world_camera_test.cpp)
  target_link_libraries(oa-world-camera-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-camera COMMAND oa-world-camera-test)
  add_executable(oa-world-overlays-test tests/world_overlays_test.cpp)
  target_link_libraries(oa-world-overlays-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-overlays COMMAND oa-world-overlays-test)
  add_executable(oa-world-fog-test tests/world_fog_test.cpp)
  target_link_libraries(oa-world-fog-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-fog COMMAND oa-world-fog-test)
  add_executable(oa-world-draw-order-test tests/world_draw_order_test.cpp)
  target_link_libraries(oa-world-draw-order-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-draw-order COMMAND oa-world-draw-order-test)
  add_executable(oa-world-radar-test tests/world_radar_test.cpp)
  target_link_libraries(oa-world-radar-test PRIVATE oa-present-world-renderer oa-test-game-data)
  add_test(NAME world-radar COMMAND oa-world-radar-test)
  # The radar picture of an installed map.
  oa_add_game_data_test(world-radar-data oa-world-radar-test --data)
  add_executable(oa-world-terrain-view-test tests/world_terrain_view_test.cpp)
  target_link_libraries(oa-world-terrain-view-test PRIVATE oa-present-world-renderer oa-test-game-data)
  add_test(NAME world-terrain-view COMMAND oa-world-terrain-view-test)
  # The terrain view over the installed game's maps.
  oa_add_game_data_test(world-terrain-view-data oa-world-terrain-view-test --data)
  set_tests_properties(world-terrain-view-data PROPERTIES TIMEOUT 300)
endif()

if(BUILD_TESTING)
  add_executable(oa-world-camera-clamp-test tests/world_camera_clamp_test.cpp)
  target_link_libraries(oa-world-camera-clamp-test PRIVATE oa-present-world-renderer)
  add_test(NAME world-camera-clamp COMMAND oa-world-camera-clamp-test)
endif()
