// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/palette.hpp"

#include <cstdint>
#include <cstring>

namespace oa::data::defs {

bool load_palette_file(
    const Files* files, const char* name, const PaletteImageReader& images, uint8_t* palette
) noexcept {
    char path[path_capacity];
    build_variant_path(files, path, sizeof path, "palettes", name, "PAL", nullptr);
    uint8_t* data = nullptr;
    uint32_t size = 0;
    bool from_archive = false;
    if (files->read(files->context, path, &data, &size, &from_archive)) {
        const bool whole = size >= palette_file_bytes;
        if (whole)
            std::memcpy(palette, data, palette_file_bytes);
        files->release(files->context, data);
        if (size != 0)
            return whole;
    }
    build_variant_path(files, path, sizeof path, "palettes", name, "PCX", nullptr);
    return images.read != nullptr && images.read(images.context, path, palette);
}

} // namespace oa::data::defs
