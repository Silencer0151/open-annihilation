# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Base-content compatibility guard: every archive of the installed game at
# OA_GAME_DIR is read and every entry decoded with the engine's decoders, and
# the definition loaders run over the archives the game mounts. Without an
# installed game the sweep reports what it skipped and exits with
# OA_GAME_DATA_SKIP_CODE, or fails when OA_REQUIRE_GAME_DATA is on. The sweep
# runs on one thread per logical core, or on as many as OA_TEST_THREADS names.
if(BUILD_TESTING)
  find_package(Threads REQUIRED)
  add_executable(oa-installed-content-test installed_content_test.cpp)
  target_link_libraries(oa-installed-content-test PRIVATE oa-formats-hpi oa-formats-gaf oa-formats-fnt
    oa-formats-objects3d oa-formats-cob oa-formats-tnt oa-formats-tdf oa-data-unit-definitions oa-ui-gui-layout oa-present
    oa-data-defs oa-data-defs-assets oa-audio oa-sim-ai oa-test-game-data oa-options Threads::Threads)
  add_test(NAME installed-content COMMAND oa-installed-content-test)
  # In a parallel ctest run the sweep's threads share the cores with the
  # other tests, and its cost starts it among the first, beside the longer
  # native checks.
  set_tests_properties(installed-content PROPERTIES TIMEOUT 600 COST 100 LABELS "content;data")
  oa_game_data_tests(installed-content)
endif()
