# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Locks, condition variables, events and threads for every system the engine
# supports, Windows XP included; the platform layer and the archive readers
# share them.
add_library(oa-base-threads src/threads.cpp)
add_library(oa::base::threads ALIAS oa-base-threads)
target_include_directories(oa-base-threads PUBLIC include)
target_compile_features(oa-base-threads PUBLIC cxx_std_20)
target_link_libraries(oa-base-threads PUBLIC oa-options)
if(NOT WIN32)
  find_package(Threads REQUIRED)
  target_link_libraries(oa-base-threads PUBLIC Threads::Threads)
endif()
if(BUILD_TESTING)
  add_executable(oa-base-threads-test tests/threads_test.cpp)
  target_link_libraries(oa-base-threads-test PRIVATE oa-base-threads)
  add_test(NAME base-threads COMMAND oa-base-threads-test)
  set_tests_properties(base-threads PROPERTIES TIMEOUT 60)
endif()
