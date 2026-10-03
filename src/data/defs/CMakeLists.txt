# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

cmake_minimum_required(VERSION 3.24)
project(oa_data_defs LANGUAGES C CXX)

include(CTest)
include("${CMAKE_CURRENT_LIST_DIR}/../../../cmake/OaOptions.cmake")

add_library(oa-data-defs
  src/categories.cpp
  src/files.cpp
  src/gamedata_tables.cpp
  src/layout.cpp
  src/locale.cpp
  src/move_classes.cpp
  src/palette.cpp
  src/rule_keys.cpp
  src/sides.cpp
  src/sound_categories.cpp
  src/unit_catalog.cpp
  src/unit_def_loader.cpp
  src/unit_header.cpp
  src/unit_records.cpp
  src/unit_texts.cpp
  src/version.cpp
  src/weapons.cpp)
add_library(oa::data::defs ALIAS oa-data-defs)
target_link_libraries(oa-data-defs PRIVATE oa-base-text oa-base-game-math)
target_include_directories(oa-data-defs PUBLIC include)
target_compile_features(oa-data-defs PUBLIC cxx_std_20)
target_link_libraries(oa-data-defs PUBLIC oa-formats-tdf oa-core-types oa-data-limits oa-data-match-rules PRIVATE oa-data-mission-types)
if(TARGET oa-options)
  target_link_libraries(oa-data-defs PUBLIC oa-options)
endif()

if(TARGET oa-formats-hpi)
  add_library(oa-data-defs-assets src/asset_files.cpp)
  add_library(oa::data::defs_assets ALIAS oa-data-defs-assets)
  target_link_libraries(oa-data-defs-assets PUBLIC oa-data-defs oa-formats-hpi)
endif()

if(BUILD_TESTING)
  add_executable(oa-defs-test tests/defs_test.cpp)
  target_link_libraries(oa-defs-test PRIVATE oa-base-text oa-data-defs oa-data-mission-types)
  add_test(NAME defs COMMAND oa-defs-test)
endif()

if(BUILD_TESTING AND TARGET oa-data-defs-assets)
  # Every loader reads the directories the data layout names, and with a
  # renamed layout never a base directory.
  add_executable(oa-defs-layout-test tests/layout_test.cpp)
  target_link_libraries(oa-defs-layout-test PRIVATE oa-data-defs oa-data-defs-assets)
  add_test(NAME defs-layout COMMAND oa-defs-layout-test)
endif()

if(BUILD_TESTING)
  add_executable(oa-defs-file-loaders-test tests/file_loaders_test.cpp)
  target_link_libraries(oa-defs-file-loaders-test PRIVATE oa-data-defs)
  add_test(NAME defs-file-loaders COMMAND oa-defs-file-loaders-test)
endif()

if(BUILD_TESTING)
  add_executable(oa-defs-rule-keys-test tests/rule_keys_test.cpp)
  target_link_libraries(oa-defs-rule-keys-test PRIVATE oa-data-defs oa-test-support)
  add_test(NAME defs-rule-keys COMMAND oa-defs-rule-keys-test)
endif()

# The rule keys over an installed mod's own files: the game folder
# OA_MOD_GAME_DIR names, with a mod installed over it, and the reference
# profiles in OA_MOD_PROFILES_DIR. Skipped without them.
if(BUILD_TESTING AND TARGET oa-data-defs-assets AND TARGET oa-data-mod-profile)
  add_executable(oa-defs-rule-keys-mod-install-test tests/rule_keys_mod_test.cpp)
  target_link_libraries(oa-defs-rule-keys-mod-install-test PRIVATE
    oa-data-defs-assets oa-data-mod-profile oa-formats-hpi oa-platform-shims oa-test-support)
  add_test(NAME defs-rule-keys-mod-install COMMAND oa-defs-rule-keys-mod-install-test)
  set_tests_properties(defs-rule-keys-mod-install PROPERTIES SKIP_RETURN_CODE 77)
  set(oa_mod_install_environment)
  if(DEFINED OA_MOD_GAME_DIR)
    list(APPEND oa_mod_install_environment "OA_MOD_GAME_DIR=${OA_MOD_GAME_DIR}")
  endif()
  if(DEFINED OA_MOD_PROFILES_DIR)
    list(APPEND oa_mod_install_environment "OA_MOD_PROFILES_DIR=${OA_MOD_PROFILES_DIR}")
  endif()
  if(oa_mod_install_environment)
    set_tests_properties(defs-rule-keys-mod-install PROPERTIES
      ENVIRONMENT "${oa_mod_install_environment}")
  endif()
endif()
