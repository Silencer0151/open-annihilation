# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The touch controls' model: the device class, the tablet and phone layouts
# in points, the QUEUE, ADD and x5 latches, the radial order menu, the
# sheets, the banner, the status line and the hit tests, with no SDL and no
# runtime (touch_hud.hpp).
add_library(oa-ui-touch-hud src/touch_hud.cpp src/touch_hud_layout.cpp src/touch_hud_radial.cpp)
add_library(oa::ui::touch_hud ALIAS oa-ui-touch-hud)
target_include_directories(oa-ui-touch-hud PUBLIC include)
target_compile_features(oa-ui-touch-hud PUBLIC cxx_std_20)
target_link_libraries(oa-ui-touch-hud PUBLIC oa-ui-display-layout PRIVATE oa-options)

if(BUILD_TESTING)
  # The layouts at 1180x820 and 852x393, the latches, the radial, the hit
  # tests and the left-handed mirror.
  add_executable(oa-ui-touch-hud-test tests/touch_hud_test.cpp)
  target_link_libraries(oa-ui-touch-hud-test PRIVATE oa-ui-touch-hud oa-test-support oa-options)
  add_test(NAME ui-touch-hud COMMAND oa-ui-touch-hud-test)
endif()
