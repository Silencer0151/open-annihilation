# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# The languages the game shows its text in: the registry, the operating
# system's locales and the settings' choice matched against it, fallback
# chains, the interface catalogue of the engine's own words, and the names
# and descriptions players see for unit types.
add_library(oa-data-languages src/languages.cpp src/interface_text.cpp src/language_pack.cpp
  src/translation.cpp src/unit_texts.cpp)
add_library(oa::data::languages ALIAS oa-data-languages)
target_include_directories(oa-data-languages PUBLIC include)
target_compile_features(oa-data-languages PUBLIC cxx_std_20)
target_link_libraries(oa-data-languages PUBLIC oa-core-types oa-data-defs PRIVATE oa-formats-oamod oa-formats-tdf oa-options)

if(BUILD_TESTING)
  # The registry and its order, the fallback chains, locales matched alone
  # and in lists, the settings' choice, and the interface catalogue read
  # from small built files, malformed ones included.
  add_executable(oa-data-languages-test tests/languages_test.cpp)
  target_link_libraries(oa-data-languages-test PRIVATE oa-data-languages oa-test-support oa-options)
  add_test(NAME data-languages COMMAND oa-data-languages-test)
  # Language packs: the pseudo-language's pack of the tests' own data,
  # manifests refused by rule, and the layers each kind of text is looked
  # up through.
  add_executable(oa-data-languages-pack-test tests/language_pack_test.cpp)
  target_link_libraries(oa-data-languages-pack-test PRIVATE oa-data-languages oa-test-support
    oa-options)
  add_test(NAME data-languages-packs COMMAND oa-data-languages-pack-test
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/pseudo-pack)
endif()
