# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Host services: token locks, clock, threads, heap, performance counters,
# file/log streams, the log folder and the machine the game runs on.
add_library(oa-platform-shims src/allocator.cpp src/crash.cpp src/files.cpp src/lock.cpp src/log_files.cpp
  src/machine.cpp src/memory_status.cpp src/perf.cpp src/system.cpp)
add_library(oa::platform::shims ALIAS oa-platform-shims)
target_include_directories(oa-platform-shims PUBLIC include)
target_compile_features(oa-platform-shims PUBLIC cxx_std_20)
target_link_libraries(oa-platform-shims PUBLIC oa-base-threads oa-options)
if(WIN32)
  target_link_libraries(oa-platform-shims PRIVATE psapi)
endif()

if(BUILD_TESTING)
  add_executable(oa-platform-shims-test tests/platform_test.cpp)
  target_link_libraries(oa-platform-shims-test PRIVATE oa-platform-shims oa-test-support)
  add_test(NAME platform-shims COMMAND oa-platform-shims-test)
  add_executable(oa-platform-log-files-test tests/log_files_test.cpp)
  target_link_libraries(oa-platform-log-files-test PRIVATE oa-platform-shims oa-test-support)
  add_test(NAME platform-log-files COMMAND oa-platform-log-files-test)
  add_executable(oa-platform-machine-test tests/machine_test.cpp)
  target_link_libraries(oa-platform-machine-test PRIVATE oa-platform-shims oa-test-support)
  add_test(NAME platform-machine COMMAND oa-platform-machine-test)
  set_tests_properties(platform-shims PROPERTIES TIMEOUT 60)
  if(OA_SANITIZERS)
    # The crash tests end a child with SIGFPE on purpose; the sanitizer must
    # leave that signal to the program. Leaks are not checked yet. The options
    # are appended to any the caller set, and win over them.
    set_tests_properties(platform-shims PROPERTIES
      ENVIRONMENT_MODIFICATION "ASAN_OPTIONS=string_append::detect_leaks=0:handle_sigfpe=0")
  endif()
endif()

