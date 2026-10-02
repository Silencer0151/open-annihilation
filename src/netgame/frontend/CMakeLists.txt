# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The multiplayer half of the frontend state machine, which oa-game's
# dispatcher runs through its StateHandler.
add_library(oa-netgame-frontend src/multiplayer_states.cpp)
add_library(oa::netgame::frontend ALIAS oa-netgame-frontend)
target_include_directories(oa-netgame-frontend PUBLIC include)
target_compile_features(oa-netgame-frontend PUBLIC cxx_std_20)
target_link_libraries(oa-netgame-frontend PUBLIC oa-ui-frontend-state)

if(BUILD_TESTING)
  add_executable(oa-netgame-frontend-test tests/multiplayer_states_test.cpp)
  target_link_libraries(oa-netgame-frontend-test PRIVATE oa-netgame-frontend)
  add_test(NAME net-frontend-states COMMAND oa-netgame-frontend-test)
  set_tests_properties(net-frontend-states PROPERTIES TIMEOUT 30)
endif()
