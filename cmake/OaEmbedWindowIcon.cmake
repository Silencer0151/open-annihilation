# SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
# SPDX-License-Identifier: GPL-3.0-only

# Writes the C++ source that holds the window icon's PNG file, run as a
# script: cmake -DINPUT=<png> -DOUTPUT=<source> -P OaEmbedWindowIcon.cmake.
# The source defines oa::app::window_icon_png(), which window_icon.hpp
# declares, over the file's bytes. OUTPUT is rewritten only when its text
# changes.
if(NOT INPUT OR NOT OUTPUT)
  message(FATAL_ERROR "OaEmbedWindowIcon.cmake needs -DINPUT=<png> -DOUTPUT=<source>")
endif()
file(READ "${INPUT}" hex HEX)
string(LENGTH "${hex}" hex_digits)
if(hex_digits EQUAL 0)
  message(FATAL_ERROR "${INPUT} is empty")
endif()
# Sixteen bytes to a line; CMake's expressions have no counted repeat.
string(REGEX REPLACE "([0-9a-f][0-9a-f])" "0x\\1, " bytes "${hex}")
string(REPEAT "0x[0-9a-f][0-9a-f], " 16 line)
string(REGEX REPLACE "(${line})" "\\1\n    " bytes "${bytes}")
string(REGEX REPLACE "[ \n]+$" "" bytes "${bytes}")
string(REGEX REPLACE ", \n" ",\n" bytes "${bytes}")
get_filename_component(name "${INPUT}" NAME)
file(CONFIGURE OUTPUT "${OUTPUT}" @ONLY CONTENT [[
// Written by cmake/OaEmbedWindowIcon.cmake from @name@; edit the icon, not this file.
#include "oa/app/window_icon.hpp"

namespace oa::app {
namespace {

// The bytes of @name@.
constexpr uint8_t window_icon_file[] = {
    @bytes@
};

} // namespace

std::span<const uint8_t> window_icon_png() {
    return window_icon_file;
}

} // namespace oa::app
]])
