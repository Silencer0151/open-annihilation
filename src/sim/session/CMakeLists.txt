# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-session game_session.cpp)
add_library(oa::sim::session ALIAS oa-sim-session)
target_include_directories(oa-sim-session PUBLIC include)
target_compile_features(oa-sim-session PUBLIC cxx_std_20)
target_link_libraries(oa-sim-session PUBLIC oa-core-types oa-data-campaign oa-data-limits PRIVATE oa-options)
if(BUILD_TESTING)
  add_executable(oa-session-test tests/game_session_test.cpp)
  target_link_libraries(oa-session-test PRIVATE oa-sim-session oa-options)
  add_test(NAME game-session COMMAND oa-session-test)
endif()
