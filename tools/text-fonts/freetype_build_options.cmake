# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Options for the pinned FreeType that tools/bootstrap_text_fonts.py,
# tools/bootstrap_macos_deps.py and tools/bootstrap_windows_deps.py build,
# read after FreeType's project() call.
#
# FreeType registers only the modules freetype_modules.h beside this file
# lists: the TrueType and CFF font drivers and what they need, the
# auto-hinter, and the monochrome and anti-aliasing rasterizers. The
# bundled fonts need nothing else, and a static link then leaves the other
# drivers out of the game.
#
# With Visual Studio's compiler, FreeType's objects name no C library (/Zl),
# as SDL's do (tools/sdl_build_options.cmake): the release build of FreeType
# then links into a Debug program too.
include_directories(BEFORE "${CMAKE_CURRENT_LIST_DIR}")
add_compile_definitions("FT_CONFIG_MODULES_H=<freetype_modules.h>")
if(MSVC)
  add_compile_options(/Zl)
endif()
