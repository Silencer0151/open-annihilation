# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-effect-particles src/particles.cpp src/explosions.cpp)
add_library(oa::sim::effect_particles ALIAS oa-sim-effect-particles)
target_include_directories(oa-sim-effect-particles PUBLIC include)
target_link_libraries(oa-sim-effect-particles PUBLIC oa-core-types oa-sim-sprite-animation oa-base-game-math oa-base-geometry oa-sim-unit-movement)
target_compile_features(oa-sim-effect-particles PUBLIC cxx_std_20)
if(TARGET oa-options)
  target_link_libraries(oa-sim-effect-particles PUBLIC oa-options)
endif()

if(BUILD_TESTING)
  add_executable(oa-effect-particles-test tests/particles_test.cpp)
  target_link_libraries(oa-effect-particles-test PRIVATE oa-sim-effect-particles)
  add_test(NAME effect-particles COMMAND oa-effect-particles-test)
  add_executable(oa-effect-explosions-test tests/explosions_test.cpp)
  target_link_libraries(oa-effect-explosions-test PRIVATE oa-sim-effect-particles)
  add_test(NAME effect-explosions COMMAND oa-effect-explosions-test)
endif()
