// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Named 256-colour palettes from PALETTES\<name>.PAL.
#pragma once

#include "oa/data/defs/files.hpp"

#include <cstdint>

namespace oa::data::defs {

inline constexpr uint32_t palette_file_bytes = 0x400; // 256 four-byte entries

// Reads the palette of an image file into palette_file_bytes bytes; false
// when the image is missing or has no palette.
struct PaletteImageReader {
    void* context;
    bool (*read)(void* context, const char* path, uint8_t* palette);
};

/// Loads a named palette from PALETTES\<name>.PAL, or from PALETTES\<name>.PCX
/// when the .PAL is missing or empty.
///
/// 3.1c then saved the image palette as the .PAL and deleted the .ALP, .LHT
/// and .SHD tables built from the previous one; here the game directory is
/// left untouched.
///
/// @param files file boundary
/// @param name palette name without directory or extension
/// @param images reader for the PCX fallback; a null callback disables it
/// @param[out] palette palette_file_bytes bytes of four-byte entries
/// @return false when neither file yields a whole palette, including a
///     non-empty .PAL shorter than palette_file_bytes
bool load_palette_file(
    const Files* files, const char* name, const PaletteImageReader& images, uint8_t* palette
) noexcept;

} // namespace oa::data::defs
