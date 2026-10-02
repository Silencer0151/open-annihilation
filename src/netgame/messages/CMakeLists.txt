# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The departure notice a network game posts when a player leaves.
add_library(oa-netgame-messages src/departures.cpp)
add_library(oa::netgame::messages ALIAS oa-netgame-messages)
target_include_directories(oa-netgame-messages PUBLIC include)
target_compile_features(oa-netgame-messages PUBLIC cxx_std_20)
target_link_libraries(oa-netgame-messages PUBLIC oa-sim-messages oa-core-types PRIVATE oa-netgame)

if(BUILD_TESTING)
  add_executable(oa-netgame-messages-test tests/departures_test.cpp)
  target_link_libraries(oa-netgame-messages-test PRIVATE oa-netgame-messages oa-options)
  add_test(NAME net-messages COMMAND oa-netgame-messages-test)
endif()
