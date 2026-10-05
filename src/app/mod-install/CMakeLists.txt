# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Mod packages (.oamod): a package read and checked before anything is
# written, where it installs in the player's Mods folder and what it asks,
# its files unpacked into a staging folder a piece at a time, the renames
# that put them in place and keep one earlier version in the mod's .backup,
# the recovery of a change a stop left half done and the deletion of the
# folders a change drops; the packages waiting for the main menu and the
# change waiting for its run to end; the prompts' texts; and the hand-off
# of a second start's packages to the copy already running. No SDL.
add_library(oa-app-mod-install src/package.cpp src/names.cpp src/plan.cpp src/change.cpp
  src/unpack.cpp src/files.cpp src/inbox.cpp src/prompts.cpp src/handoff.cpp)
add_library(oa::app::mod_install ALIAS oa-app-mod-install)
target_include_directories(oa-app-mod-install PUBLIC include)
target_compile_features(oa-app-mod-install PUBLIC cxx_std_20)
target_link_libraries(oa-app-mod-install PUBLIC oa-formats-zip oa-formats-hpi oa-data-mod-profile
  oa-platform-preferences oa-ui-engine-settings
  PRIVATE oa-formats-oamod oa-data-languages oa-app-mod-profile oa-app-user-folder
  oa-app-game-directory oa-ui-game-files oa-platform-shims oa-base-threads oa-options)

if(BUILD_TESTING)
  # Packages read and checked: a Finder-made package with its top folder
  # stripped, a package with its profile at its top, its own .backup left
  # out, and each refusal; the portable name rule and the folder-safe
  # version.
  add_executable(oa-app-mod-install-package-test tests/package_test.cpp)
  target_link_libraries(oa-app-mod-install-package-test PRIVATE oa-app-mod-install oa-formats-zip
    ZLIB::ZLIB oa-test-support oa-options)
  add_test(NAME app-mod-install-package COMMAND oa-app-mod-install-package-test)
  # The planner's decision table, row by row, and each prompt's title,
  # buttons, first mark and sentences, each fitting its height.
  add_executable(oa-app-mod-install-plan-test tests/plan_test.cpp)
  target_link_libraries(oa-app-mod-install-plan-test PRIVATE oa-app-mod-install oa-test-support
    oa-options)
  add_test(NAME app-mod-install-plan COMMAND oa-app-mod-install-plan-test)
  # The renames in a scratch Mods folder: an install, three replaces that
  # keep one .backup, a reinstall, two roll backs, a rename refused at
  # each step, every crash point settled, the lock, the retries, the
  # unpacking and the deletion of discard folders.
  add_executable(oa-app-mod-install-change-test tests/change_test.cpp)
  target_link_libraries(oa-app-mod-install-change-test PRIVATE oa-app-mod-install oa-formats-zip
    oa-platform-shims ZLIB::ZLIB oa-test-support oa-options)
  add_test(NAME app-mod-install-change COMMAND oa-app-mod-install-change-test)
  # The instance lock and the hand-off folder.
  add_executable(oa-app-mod-install-handoff-test tests/handoff_test.cpp)
  target_link_libraries(oa-app-mod-install-handoff-test PRIVATE oa-app-mod-install oa-test-support
    oa-options)
  add_test(NAME app-mod-install-handoff COMMAND oa-app-mod-install-handoff-test)
endif()
