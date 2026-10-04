# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The touch controls' gesture recogniser: finger reports in, taps, holds,
# drags, pans, pinches and two-finger taps out, with no SDL and no clock of
# its own (touch_gestures.hpp).
add_library(oa-ui-touch-gestures src/touch_gestures.cpp)
add_library(oa::ui::touch_gestures ALIAS oa-ui-touch-gestures)
target_include_directories(oa-ui-touch-gestures PUBLIC include)
target_compile_features(oa-ui-touch-gestures PUBLIC cxx_std_20)
target_link_libraries(oa-ui-touch-gestures PRIVATE oa-options)

if(BUILD_TESTING)
  # Timelines of finger reports and the gestures each gives.
  add_executable(oa-ui-touch-gestures-test tests/touch_gestures_test.cpp)
  target_link_libraries(oa-ui-touch-gestures-test PRIVATE oa-ui-touch-gestures oa-test-support
    oa-options)
  add_test(NAME ui-touch-gestures COMMAND oa-ui-touch-gestures-test)
endif()
