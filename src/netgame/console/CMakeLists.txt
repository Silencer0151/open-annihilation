# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The in-game console's network and service commands, added to the engine
# console through its ConsoleHost::extend hook.
add_library(oa-netgame-console src/console_commands.cpp)
add_library(oa::netgame::console ALIAS oa-netgame-console)
target_include_directories(oa-netgame-console PUBLIC include)
target_compile_features(oa-netgame-console PUBLIC cxx_std_20)
target_link_libraries(oa-netgame-console PUBLIC oa-ui-console oa-ui-services PRIVATE oa-options oa-ui-frontend-multiplayer)

if(BUILD_TESTING)
  add_executable(oa-netgame-console-test tests/console_commands_test.cpp)
  target_link_libraries(oa-netgame-console-test PRIVATE oa-netgame-console oa-options)
  add_test(NAME net-console-commands COMMAND oa-netgame-console-test)
endif()
