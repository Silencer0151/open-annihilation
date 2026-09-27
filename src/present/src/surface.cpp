// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/surface.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::present {

void init_surface(
    Surface& surface, int32_t width, int32_t height, int32_t pitch, uint8_t* pixels
) noexcept {
    surface.reserved_before_origin = OA_SURFACE_RESERVED_BEFORE_ORIGIN_INIT;
    surface.pitch = pitch;
    surface.pixels = pixels;
    surface.width = width;
    surface.height = height;
    surface.origin_x = 0;
    surface.origin_y = 0;
    surface.flags = (surface.flags & ~OA_SURFACE_FLAG_CLEARED_ON_INIT) | OA_SURFACE_FLAG_MEMORY;
    surface.reserved_after_pixels = OA_SURFACE_RESERVED_AFTER_PIXELS_INIT;
    reset_clip(surface);
}

void reset_clip(Surface& surface) noexcept {
    surface.clip.x1 = 0;
    surface.clip.y1 = 0;
    surface.clip.x2 = surface.width - 1;
    surface.clip.y2 = surface.height - 1;
}

Rect32 surface_clip(const Surface& surface) noexcept {
    return surface.clip;
}

void set_surface_clip(Surface& surface, const Rect32& clip) noexcept {
    surface.clip = clip;
}

void surface_from_sprite(Surface& surface, const Sprite& sprite) noexcept {
    surface.width = sprite.width;
    surface.height = sprite.height;
    surface.pitch = sprite.width;
    surface.reserved_before_origin = OA_SURFACE_RESERVED_BEFORE_ORIGIN_INIT;
    surface.pixels = static_cast<uint8_t*>(sprite.data);
    surface.reserved_after_pixels = OA_SURFACE_RESERVED_AFTER_PIXELS_INIT;
    surface.origin_x = sprite.origin_x;
    surface.origin_y = sprite.origin_y;
    surface.flags = (surface.flags & ~OA_SURFACE_FLAG_CLEARED_ON_INIT) | OA_SURFACE_FLAG_MEMORY;
    reset_clip(surface);
}

SurfaceBuffer create_surface(int32_t width, int32_t height) {
    SurfaceBuffer buffer;
    const auto w = std::max(width, 0);
    const auto h = std::max(height, 0);
    buffer.pixels.assign(static_cast<std::size_t>(w) * static_cast<std::size_t>(h), 0);
    buffer.surface.flags = 0;
    init_surface(buffer.surface, width, height, width, buffer.pixels.data());
    return buffer;
}

SpriteBuffer create_sprite(uint16_t width, uint16_t height) {
    SpriteBuffer buffer;
    buffer.pixels.assign(static_cast<std::size_t>(width) * height, 0);
    buffer.sprite.width = width;
    buffer.sprite.height = height;
    buffer.sprite.data = buffer.pixels.data();
    return buffer;
}

Palette palette_from_bytes(std::span<const uint8_t> bytes) noexcept {
    Palette palette{};
    const std::size_t count = std::min<std::size_t>(bytes.size() / 4, OA_PALETTE_COLORS);
    for (std::size_t i = 0; i < count; ++i) {
        palette.entries[i] =
            PaletteEntry{bytes[i * 4], bytes[i * 4 + 1], bytes[i * 4 + 2], bytes[i * 4 + 3]};
    }
    return palette;
}

void expand_to_rgb(
    const Surface& surface, const Palette& palette, uint8_t* rgb, std::size_t rgb_stride
) noexcept {
    if (surface.pixels == nullptr || rgb == nullptr) {
        return;
    }
    for (int32_t y = 0; y < surface.height; ++y) {
        const uint8_t* row = surface.pixels + static_cast<std::ptrdiff_t>(y) * surface.pitch;
        uint8_t* out = rgb + static_cast<std::size_t>(y) * rgb_stride;
        for (int32_t x = 0; x < surface.width; ++x) {
            const PaletteEntry& entry = palette.entries[row[x]];
            out[x * 3] = entry.r;
            out[x * 3 + 1] = entry.g;
            out[x * 3 + 2] = entry.b;
        }
    }
}

std::vector<uint8_t> to_rgb(const Surface& surface, const Palette& palette) {
    const auto w = static_cast<std::size_t>(std::max(surface.width, 0));
    const auto h = static_cast<std::size_t>(std::max(surface.height, 0));
    std::vector<uint8_t> rgb(w * h * 3);
    expand_to_rgb(surface, palette, rgb.data(), w * 3);
    return rgb;
}

} // namespace oa::present
