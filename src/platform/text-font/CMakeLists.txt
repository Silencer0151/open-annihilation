# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Text drawn with the fonts that travel with the game, through FreeType, on
# every platform. FreeType and the fonts come from cmake/OaTextFonts.cmake,
# which the top-level project includes.
add_library(oa-platform-text-font src/text_font.cpp src/font_directory.cpp)
add_library(oa::platform::text_font ALIAS oa-platform-text-font)
target_include_directories(oa-platform-text-font PUBLIC include)
target_compile_features(oa-platform-text-font PUBLIC cxx_std_20)
target_link_libraries(oa-platform-text-font PRIVATE Freetype::Freetype oa-options)
if(BUILD_TESTING)
  # The fonts are copied beside the test, where bundled_font_directory()
  # finds them, as it finds them beside the game.
  add_executable(oa-platform-text-font-test tests/text_font_test.cpp)
  target_link_libraries(oa-platform-text-font-test PRIVATE oa-platform-text-font oa-options oa-test-support)
  oa_copy_text_fonts(oa-platform-text-font-test "$<TARGET_FILE_DIR:oa-platform-text-font-test>")
  add_test(NAME platform-text-font COMMAND oa-platform-text-font-test)
  # A short mixed line pixel for pixel, as the pinned FreeType draws it; it
  # skips with another FreeType.
  add_test(NAME platform-text-font-pixels COMMAND oa-platform-text-font-test --pixels)
  set_tests_properties(platform-text-font platform-text-font-pixels PROPERTIES SKIP_RETURN_CODE 77 TIMEOUT 120)
endif()
