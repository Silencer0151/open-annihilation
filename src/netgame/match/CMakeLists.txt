# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Multiplayer integration: the lobby's session binding, the launch path and
# the in-match packet pump, plus their binding to the offline match runtime.
add_library(oa-netgame-match
  src/packet_layer.cpp
  src/session_lobby.cpp
  src/launch.cpp
  src/net_match.cpp
  src/vote_reject.cpp
  src/integrity_check.cpp)
add_library(oa::netgame::match ALIAS oa-netgame-match)
target_include_directories(oa-netgame-match PUBLIC include)
target_link_libraries(oa-netgame-match
  PUBLIC oa-netgame oa-netgame-replication oa-ui-frontend-multiplayer oa-base-game-loop
  PRIVATE oa-sim-visibility-state oa-sim-speed oa-data-languages oa-base-sha256 oa-base-text)

# Records built from the simulation's shared events, and ground route deltas.
add_library(oa-netgame-match-sim-records src/sim_records.cpp src/route_delta.cpp)
add_library(oa::netgame::match_sim_records ALIAS oa-netgame-match-sim-records)
target_include_directories(oa-netgame-match-sim-records PUBLIC include)
target_link_libraries(oa-netgame-match-sim-records PUBLIC oa-netgame oa-sim-ground-orders oa-sim-feature-runtime oa-core-types)

add_library(oa-netgame-match-runtime src/match_binding.cpp)
add_library(oa::netgame::match_runtime ALIAS oa-netgame-match-runtime)
target_link_libraries(oa-netgame-match-runtime PUBLIC oa-netgame-match oa-sim-match-runtime oa-netgame-match-sim-records oa-netgame-messages)

if(BUILD_TESTING)
  add_executable(oa-netgame-match-test tests/net_match_test.cpp)
  target_link_libraries(oa-netgame-match-test PRIVATE oa-base-text oa-data-languages oa-netgame-match)
  add_test(NAME net-match COMMAND oa-netgame-match-test)
  set_tests_properties(net-match PROPERTIES TIMEOUT 120)
  add_executable(oa-netgame-match-rules-test tests/net_match_rules_test.cpp)
  target_link_libraries(oa-netgame-match-rules-test PRIVATE oa-netgame-match)
  add_test(NAME net-match-rules COMMAND oa-netgame-match-rules-test)

  # The battle room over the session binding, machines on 127.0.0.1.
  add_executable(oa-netgame-session-lobby-test tests/session_lobby_test.cpp)
  target_link_libraries(oa-netgame-session-lobby-test PRIVATE oa-netgame-match)
  add_test(NAME net-session-lobby COMMAND oa-netgame-session-lobby-test)
  set_tests_properties(net-session-lobby PROPERTIES TIMEOUT 120)
endif()

if(BUILD_TESTING)
  add_executable(oa-netgame-sim-records-test tests/sim_records_test.cpp)
  target_link_libraries(oa-netgame-sim-records-test PRIVATE oa-netgame-match-sim-records oa-sim-unit-activation)
  add_test(NAME net-sim-records COMMAND oa-netgame-sim-records-test)
endif()

if(BUILD_TESTING)
  add_executable(oa-netgame-match-runtime-test tests/net_match_runtime_test.cpp)
  target_link_libraries(oa-netgame-match-runtime-test PRIVATE oa-netgame-match-runtime oa-sim-world-environment
    oa-test-match)
  add_test(NAME net-match-runtime COMMAND oa-netgame-match-runtime-test)
  set_tests_properties(net-match-runtime PROPERTIES TIMEOUT 120)
endif()

if(BUILD_TESTING)
  # A D-gun kill shared between two machines, over the install OA_GAME_DIR
  # names: no wreck on either machine, and a received 0x0c applied as sent.
  add_executable(oa-netgame-match-dgun-test tests/dgun_kill_test.cpp)
  target_link_libraries(oa-netgame-match-dgun-test PRIVATE oa-netgame-match-runtime oa-netgame-test-installs
    oa-data-unit-definitions oa-data-defs-assets oa-data-mission-types oa-sim-map-runtime oa-test-match)
  add_test(NAME net-match-dgun-data COMMAND oa-netgame-match-dgun-test)
  set_tests_properties(net-match-dgun-data PROPERTIES TIMEOUT 300)
  oa_game_data_tests(net-match-dgun-data)
endif()
