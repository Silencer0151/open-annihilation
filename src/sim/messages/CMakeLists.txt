# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

add_library(oa-sim-messages src/messages.cpp)
add_library(oa::sim::messages ALIAS oa-sim-messages)
target_include_directories(oa-sim-messages PUBLIC include)
target_compile_features(oa-sim-messages PUBLIC cxx_std_20)
target_link_libraries(oa-sim-messages PUBLIC oa-core-types PRIVATE oa-base-text oa-options)
if(BUILD_TESTING)
  add_executable(oa-sim-messages-test tests/messages_test.cpp)
  target_link_libraries(oa-sim-messages-test PRIVATE oa-sim-messages oa-options)
  add_test(NAME sim-messages COMMAND oa-sim-messages-test)
endif()
