# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The gamepad controls' model: the pad in use and what it has, the layers and
# the maps from buttons to actions, the timing pieces, the right trackpad as a
# pointer, the gyro, the stick cursor, haptics and the button glyphs, with no
# SDL, no clock of its own and no app header (pad_controls.hpp,
# docs/controllers.md).
add_library(oa-ui-pad-controls src/pad_map.cpp src/pad_pointer.cpp src/stick_cursor.cpp
  src/pad_timing.cpp src/pad_feel.cpp)
add_library(oa::ui::pad_controls ALIAS oa-ui-pad-controls)
target_include_directories(oa-ui-pad-controls PUBLIC include)
target_compile_features(oa-ui-pad-controls PUBLIC cxx_std_20)
target_link_libraries(oa-ui-pad-controls PRIVATE oa-options)

if(BUILD_TESTING)
  # Timelines of pad input and what each piece gives, and the maps; the wedge points are
  # checked against the touch radial's own hit test.
  add_executable(oa-ui-pad-controls-test tests/pad_controls_test.cpp)
  target_link_libraries(oa-ui-pad-controls-test PRIVATE oa-ui-pad-controls oa-ui-touch-hud
    oa-test-support oa-options)
  add_test(NAME ui-pad-controls COMMAND oa-ui-pad-controls-test)
endif()
