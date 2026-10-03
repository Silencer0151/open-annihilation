# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# FreeType and the fonts the engine draws text with (src/platform/text-font),
# which tools/bootstrap_text_fonts.py fetches, pinned, and builds under
# local/deps. A configuration without them stops here and says how to get
# them.
#
# FreeType is the one an including project has already found
# (Freetype::Freetype), else the one CMAKE_PREFIX_PATH or a cross-build's
# prefixes hold (tools/bootstrap_macos_deps.py and
# tools/bootstrap_windows_deps.py build it beside SDL), else the bootstrap's
# local/deps/freetype-install-<release>, else the system's.
#
# OA_TEXT_FONTS_DIR names the folder of the fonts: the files OA_TEXT_FONT_FILES
# lists, which oa_copy_text_fonts() copies beside a program, into the folder
# oa::platform::text_font::bundled_font_directory() finds at run time. The list
# is the module's face_files (oa/platform/text_font.hpp) and the bootstrap's
# FONT_FILES; the three change together.
include_guard(GLOBAL)

set(OA_TEXT_FONT_FILES DejaVuSans-Bold.ttf DejaVuSans.ttf NotoSansCJKsc-Bold.otf NotoEmoji.ttf)
get_filename_component(oa_text_fonts_default "${CMAKE_CURRENT_LIST_DIR}/../local/deps/text-fonts" ABSOLUTE)
set(OA_TEXT_FONTS_DIR "${oa_text_fonts_default}" CACHE PATH
  "Folder holding the fonts tools/bootstrap_text_fonts.py makes, which the build copies beside the game")
foreach(font IN LISTS OA_TEXT_FONT_FILES)
  if(NOT EXISTS "${OA_TEXT_FONTS_DIR}/${font}")
    message(FATAL_ERROR "The text font ${font} is not in ${OA_TEXT_FONTS_DIR}. "
      "Run python3 tools/bootstrap_text_fonts.py, which fetches and builds FreeType and the fonts "
      "into local/deps, or name the folder that holds them with -DOA_TEXT_FONTS_DIR=.")
  endif()
endforeach()

if(NOT TARGET Freetype::Freetype)
  get_filename_component(oa_freetype_deps "${CMAKE_CURRENT_LIST_DIR}/../local/deps" ABSOLUTE)
  file(GLOB oa_freetype_bootstrap_prefixes LIST_DIRECTORIES true "${oa_freetype_deps}/freetype-install-*")
  list(APPEND CMAKE_PREFIX_PATH ${oa_freetype_bootstrap_prefixes})
  find_package(freetype 2.10 CONFIG QUIET)
  if(NOT TARGET Freetype::Freetype)
    find_package(Freetype 2.10 MODULE QUIET)
  endif()
  if(NOT TARGET Freetype::Freetype)
    message(FATAL_ERROR "FreeType 2.10 or later was not found. Run python3 tools/bootstrap_text_fonts.py, "
      "which builds the pinned FreeType into local/deps/freetype-install-<release>, where this "
      "configuration looks for it.")
  endif()
endif()
if(DEFINED freetype_VERSION)
  set(OA_FREETYPE_VERSION "${freetype_VERSION}")
elseif(DEFINED FREETYPE_VERSION_STRING)
  set(OA_FREETYPE_VERSION "${FREETYPE_VERSION_STRING}")
else()
  set(OA_FREETYPE_VERSION "unknown")
endif()
message(STATUS "Text fonts: FreeType ${OA_FREETYPE_VERSION}, fonts from ${OA_TEXT_FONTS_DIR}")

# Copies the text fonts into the fonts folder of a directory after a target
# is built, leaving files that are already current.
#   oa_copy_text_fonts(<target> <directory>)
# The directory may be a generator expression, such as the game's
# OA_GAME_FILES_DIR.
function(oa_copy_text_fonts target directory)
  set(sources "")
  foreach(font IN LISTS OA_TEXT_FONT_FILES)
    list(APPEND sources "${OA_TEXT_FONTS_DIR}/${font}")
  endforeach()
  add_custom_command(TARGET ${target} POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E make_directory "${directory}/fonts"
    COMMAND ${CMAKE_COMMAND} -E copy_if_different ${sources} "${directory}/fonts"
    VERBATIM)
endfunction()
