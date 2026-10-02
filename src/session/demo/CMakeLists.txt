# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# TA Demo (.tad) playback through the match's network receive path.
add_library(oa-session-demo src/demo_playback.cpp)
add_library(oa::session::demo ALIAS oa-session-demo)
target_include_directories(oa-session-demo PUBLIC include)
target_compile_features(oa-session-demo PUBLIC cxx_std_20)
target_link_libraries(oa-session-demo
  PUBLIC oa-formats-tad oa-netgame-match-runtime oa-netgame oa-ui-frontend-multiplayer
  PRIVATE oa-base-text oa-options)

if(BUILD_TESTING)
  add_executable(oa-session-demo-test tests/demo_playback_test.cpp)
  target_link_libraries(oa-session-demo-test PRIVATE oa-session-demo oa-options oa-test-match)
  add_test(NAME demo-playback COMMAND oa-session-demo-test)
  set_tests_properties(demo-playback PROPERTIES TIMEOUT 120)
endif()
