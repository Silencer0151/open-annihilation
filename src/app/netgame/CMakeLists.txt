# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Network play's part of oa-game: the Runtime members it defines (the
# runtime_*.cpp sources here, declared in
# src/app/include/oa/app/netgame_runtime_members.hpp, which runtime.hpp
# includes in every build), the hooks of its extension table, the launch
# options and 3.1c game switches it takes, the checks that run the game
# networked, and the API an extension built on network play links
# (oa-app-netgame-sdk). The top-level CMakeLists.txt registers oa-app-netgame
# with oa_add_extension.

# The 3.1c command line's network switches, which write the launch block.
add_library(oa-app-netgame-launch STATIC launch_switches.cpp)
add_library(oa::app::netgame_launch ALIAS oa-app-netgame-launch)
target_include_directories(oa-app-netgame-launch PUBLIC include)
target_compile_features(oa-app-netgame-launch PUBLIC cxx_std_20)
target_link_libraries(oa-app-netgame-launch PUBLIC oa-app-command-line oa-ui-frontend-multiplayer
  PRIVATE oa-options)

# The long options and 3.1c network switches, which the option test parses
# without a running app.
add_library(oa-app-netgame-options STATIC net_options.cpp)
add_library(oa::app::netgame_options ALIAS oa-app-netgame-options)
target_include_directories(oa-app-netgame-options PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(oa-app-netgame-options PUBLIC oa::app::headers oa-app-netgame-launch oa-netgame-frontend
  oa-app-command-line oa-ui-frontend-renderer oa-ui-frontend-state oa-sim-scenario oa-ui-screen-registry
  oa-core-types
  PRIVATE oa-options)

# The traffic readouts drawn over the battlefield through the engine's match
# overlay, which their test draws without a running app.
add_library(oa-app-netgame-traffic-overlay STATIC traffic_overlay.cpp)
add_library(oa::app::netgame_traffic_overlay ALIAS oa-app-netgame-traffic-overlay)
target_include_directories(oa-app-netgame-traffic-overlay PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(oa-app-netgame-traffic-overlay PUBLIC oa::app::headers oa-netgame oa-core-types
  PRIVATE oa-netgame-match oa-present-world-renderer oa-ui-console)

# The handler a request to end the program runs, as the game keeps it
# installed through its application modes and matches, which its test runs
# without a running app.
add_library(oa-app-netgame-close-handlers STATIC close_handlers.cpp)
add_library(oa::app::netgame_close_handlers ALIAS oa-app-netgame-close-handlers)
target_include_directories(oa-app-netgame-close-handlers PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
target_link_libraries(oa-app-netgame-close-handlers PUBLIC oa-core-types
  PRIVATE oa-ui-frontend-state oa-ui-frontend-multiplayer)

# The launch block as the running game binds it (where a launch lands,
# whether one is active, the multiplayer screens' link, the extension API's
# hooks) and the lines posted to a launched battle, which their test runs
# without a running app. It implements network play's API for the
# extensions built on it (oa/app/netgame/extension_api.hpp).
add_library(oa-app-netgame-launch-binding STATIC launch_binding.cpp battle_lines.cpp)
add_library(oa::app::netgame_launch_binding ALIAS oa-app-netgame-launch-binding)
target_include_directories(oa-app-netgame-launch-binding PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}" include)
target_link_libraries(oa-app-netgame-launch-binding
  PUBLIC oa-app-netgame-close-handlers oa-app-netgame-launch oa-ui-frontend-multiplayer oa-core-types)

# The sources that define the Runtime members and the Runtime friend.
set(oa_app_netgame_sources
  runtime_console_network.cpp
  runtime_net.cpp
  runtime_demo.cpp
  extension.cpp
  runtime_netgame.cpp
  runtime_report.cpp
  runtime_session_cheats.cpp)

if(TARGET oa-game)
  # multiplayer_menu_check.cpp drives the game through the engine's check
  # host and reaches no Runtime member.
  add_library(oa-app-netgame STATIC ${oa_app_netgame_sources} multiplayer_menu_check.cpp)
  add_library(oa::app::netgame ALIAS oa-app-netgame)
  target_include_directories(oa-app-netgame PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
  # The engine's extension SDK: its hooks, and the Runtime its members
  # compile into, seen as oa-game's own sources see it (the engine applies
  # an extension's OA_RUNTIME_EXTENSION_MEMBERS to both).
  target_link_libraries(oa-app-netgame
    PUBLIC oa-app-netgame-options oa-app-netgame-traffic-overlay oa-app-netgame-close-handlers
      oa-app-netgame-launch-binding oa-ui-frontend-multiplayer oa-netgame-match-runtime oa-session-demo
      oa-netgame-console oa-netgame-messages
    PRIVATE oa::extension-sdk)
  # Network play's API for the extensions built on it: an extension library
  # that links it links network play's extension too, so the engine lists
  # that extension after network play's.
  add_library(oa-app-netgame-sdk INTERFACE)
  add_library(oa::app::netgame_sdk ALIAS oa-app-netgame-sdk)
  target_link_libraries(oa-app-netgame-sdk INTERFACE oa-app-netgame-launch-binding oa-app-netgame)
endif()

if(BUILD_TESTING)
  add_executable(oa-netgame-launch-switches-test tests/launch_switches_test.cpp)
  target_link_libraries(oa-netgame-launch-switches-test PRIVATE oa-app-netgame-launch oa-options)
  add_test(NAME net-launch-switches COMMAND oa-netgame-launch-switches-test)
  add_executable(oa-netgame-options-test tests/options_test.cpp)
  target_link_libraries(oa-netgame-options-test PRIVATE oa-app-netgame-options oa-app-game-options oa-options)
  add_test(NAME netgame-game-options COMMAND oa-netgame-options-test)
  add_executable(oa-netgame-close-handlers-test tests/close_handlers_test.cpp)
  target_link_libraries(oa-netgame-close-handlers-test PRIVATE oa-app-netgame-launch-binding oa-ui-frontend-state)
  add_test(NAME netgame-close-handlers COMMAND oa-netgame-close-handlers-test)
  add_executable(oa-netgame-launch-binding-test tests/launch_binding_test.cpp)
  target_link_libraries(oa-netgame-launch-binding-test PRIVATE oa-app-netgame-launch-binding
    oa-netgame-test-launch)
  add_test(NAME netgame-launch-binding COMMAND oa-netgame-launch-binding-test)
  set_tests_properties(netgame-close-handlers netgame-launch-binding PROPERTIES TIMEOUT 60)
  add_executable(oa-netgame-traffic-overlay-test tests/traffic_overlay_test.cpp)
  target_link_libraries(oa-netgame-traffic-overlay-test PRIVATE oa-app-netgame-traffic-overlay oa-netgame-match
    oa-ui-console)
  add_test(NAME netgame-traffic-overlay COMMAND oa-netgame-traffic-overlay-test)
  # The members header and the private Runtime names these sources use stay
  # within tools/runtime-surface-baseline.json, which only shrinks as they
  # move behind hooks.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME netgame-runtime-surface COMMAND ${Python3_EXECUTABLE}
      "${PROJECT_SOURCE_DIR}/tools/check_runtime_surface.py"
      --members "${PROJECT_SOURCE_DIR}/src/app/include/oa/app/netgame_runtime_members.hpp"
      ${oa_app_netgame_sources})
    set_tests_properties(netgame-runtime-surface PROPERTIES TIMEOUT 60
      WORKING_DIRECTORY "${CMAKE_CURRENT_SOURCE_DIR}")
  endif()
endif()

# The checks that run the game networked, over the installation OA_GAME_DIR
# names.
set(oa_netgame_checks "${CMAKE_CURRENT_BINARY_DIR}/native-checks")
if(BUILD_TESTING AND TARGET oa-game AND IS_DIRECTORY "${OA_GAME_DIR}")
  file(MAKE_DIRECTORY "${oa_netgame_checks}")
  add_test(NAME native-net-loopback COMMAND oa-game --game-dir "${OA_GAME_DIR}"
    --skip-intro --mute --headless-check --net-loopback-check 300
    --preferences-file "${oa_netgame_checks}/net-loopback.conf")
  set_tests_properties(native-net-loopback PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
  add_test(NAME native-net-loopback-watcher COMMAND oa-game --game-dir "${OA_GAME_DIR}"
    --skip-intro --mute --headless-check --net-loopback-check 300 --net-loopback-watcher
    --preferences-file "${oa_netgame_checks}/net-loopback-watcher.conf")
  set_tests_properties(native-net-loopback-watcher PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
  # The host seats a computer player, which builds for 6000 ticks, is given
  # a squad and strikes the joiner's commander; with the watcher, the
  # joiner watches it strike the host's.
  add_test(NAME native-net-loopback-computer COMMAND oa-game --game-dir "${OA_GAME_DIR}"
    --skip-intro --mute --headless-check --net-loopback-check 6000 --net-loopback-computer
    --preferences-file "${oa_netgame_checks}/net-loopback-computer.conf")
  set_tests_properties(native-net-loopback-computer PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
  add_test(NAME native-net-loopback-computer-watcher COMMAND oa-game --game-dir "${OA_GAME_DIR}"
    --skip-intro --mute --headless-check --net-loopback-check 6000 --net-loopback-computer
    --net-loopback-watcher
    --preferences-file "${oa_netgame_checks}/net-loopback-computer-watcher.conf")
  set_tests_properties(native-net-loopback-computer-watcher PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
  # The engine's recording hook (open_recording) declines bytes that are no
  # recording and refuses a recording's truncated header; with --play-demo
  # the check would also replay that recording through the replay hooks.
  add_test(NAME native-recording-hook COMMAND oa-game --game-dir "${OA_GAME_DIR}"
    --skip-intro --mute --check-recording-hook
    --preferences-file "${oa_netgame_checks}/recording-hook.conf")
  set_tests_properties(native-recording-hook PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
  # A joiner whose host is not found, through the main loop: "Host not
  # found.  Exiting..." shows, and 4 s later the game leaves and the run ends
  # with no reason and exit status 0.
  find_package(Python3 COMPONENTS Interpreter QUIET)
  if(Python3_Interpreter_FOUND)
    add_test(NAME native-host-not-found COMMAND ${Python3_EXECUTABLE}
      "${PROJECT_SOURCE_DIR}/tools/check_native_host_not_found.py"
      --native $<TARGET_FILE:oa-game> --game-dir "${OA_GAME_DIR}"
      --scratch-root "${oa_netgame_checks}")
    set_tests_properties(native-host-not-found PROPERTIES TIMEOUT ${OA_NATIVE_CHECK_TIMEOUT})
  endif()
endif()
