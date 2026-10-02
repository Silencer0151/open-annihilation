# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Engine services: clock and interval timers, typed per-user preferences,
# token lines and named commands, input queues, the software cursor,
# outlined labels and the SQSH chunk writer, over base/geometry.
add_library(oa-ui-services src/commands.cpp src/cursor.cpp src/input.cpp src/label.cpp
  src/prefs.cpp src/sqsh_writer.cpp src/timers.cpp)
add_library(oa::ui::services ALIAS oa-ui-services)
target_include_directories(oa-ui-services PUBLIC include)
target_compile_features(oa-ui-services PUBLIC cxx_std_20)
target_link_libraries(oa-ui-services PUBLIC oa-base-geometry oa-platform-preferences oa-platform-shims oa-present
  oa-options PRIVATE oa-base-bytes oa-base-game-loop oa-formats-sqsh oa-formats-hpi ZLIB::ZLIB)

if(BUILD_TESTING)
  add_executable(oa-engine-services-test tests/services_test.cpp)
  target_link_libraries(oa-engine-services-test PRIVATE oa-ui-services oa-formats-sqsh ZLIB::ZLIB)
  add_test(NAME engine-services COMMAND oa-engine-services-test)
  set_tests_properties(engine-services PROPERTIES TIMEOUT 60)
endif()

if(BUILD_TESTING)
  # The SQSH chunk writer's bytes, pinned for every platform.
  add_executable(oa-sqsh-writer-golden-test tests/sqsh_writer_golden_test.cpp)
  target_link_libraries(oa-sqsh-writer-golden-test PRIVATE oa-ui-services oa-formats-hpi oa-options)
  add_test(NAME sqsh-writer-golden COMMAND oa-sqsh-writer-golden-test)
endif()
