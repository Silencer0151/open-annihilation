// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "xrgb_conversion.hpp"

#include <algorithm>
#include <bit>
#include <cstring>

namespace oa::app {

void pack_rgb24_row(uint32_t* out, const uint8_t* rgb, int width) noexcept {
    static_assert(std::endian::native == std::endian::little);
    constexpr uint32_t opaque = 0xff000000u;
    int x = 0;
    for (; x + 4 <= width; x += 4, rgb += 12, out += 4) {
        uint32_t first = 0, second = 0, third = 0;
        std::memcpy(&first, rgb, 4);
        std::memcpy(&second, rgb + 4, 4);
        std::memcpy(&third, rgb + 8, 4);
        out[0] = opaque | ((first & 0xffu) << 16) | (first & 0xff00u) | ((first >> 16) & 0xffu);
        out[1] =
            opaque | ((first >> 8) & 0xff0000u) | ((second & 0xffu) << 8) | ((second >> 8) & 0xffu);
        out[2] = opaque | (second & 0xff0000u) | ((second >> 16) & 0xff00u) | (third & 0xffu);
        out[3] = opaque | ((third << 8) & 0xff0000u) | ((third >> 8) & 0xff00u) | (third >> 24);
    }
    for (; x < width; ++x, rgb += 3)
        *out++ = opaque | (static_cast<uint32_t>(rgb[0]) << 16) |
                 (static_cast<uint32_t>(rgb[1]) << 8) | static_cast<uint32_t>(rgb[2]);
}

void gamma_xrgb_row(uint32_t* row, int width, const std::array<uint8_t, 256>& table) noexcept {
    for (int x = 0; x < width; ++x) {
        const uint32_t pixel = row[x];
        row[x] = (pixel & 0xff000000u) |
                 (static_cast<uint32_t>(table[(pixel >> 16) & 0xffu]) << 16) |
                 (static_cast<uint32_t>(table[(pixel >> 8) & 0xffu]) << 8) |
                 static_cast<uint32_t>(table[pixel & 0xffu]);
    }
}

void convert_rgb24_xrgb(
    const uint8_t* rgb,
    uint32_t width,
    uint32_t height,
    uint8_t* pixels,
    std::size_t pitch,
    const std::array<uint8_t, 256>* gamma,
    platform::job_pool::Pool* pool
) noexcept {
    convert_rgb24_xrgb_rect(
        rgb, static_cast<std::size_t>(width) * 3U, width, height, pixels, pitch, gamma, pool
    );
}

void convert_rgb24_xrgb_rect(
    const uint8_t* rgb,
    std::size_t rgb_pitch,
    uint32_t width,
    uint32_t height,
    uint8_t* pixels,
    std::size_t pitch,
    const std::array<uint8_t, 256>* gamma,
    platform::job_pool::Pool* pool
) noexcept {
    const auto row_pixels = static_cast<int>(width);
    platform::job_pool::run_bands(
        pool, platform::job_pool::bands_of_rows(height, xrgb_band_rows), [&](uint32_t band) {
            const uint32_t first_row = band * xrgb_band_rows;
            const uint32_t end_row = std::min(height, first_row + xrgb_band_rows);
            for (uint32_t y = first_row; y < end_row; ++y) {
                auto* row =
                    reinterpret_cast<uint32_t*>(pixels + static_cast<std::size_t>(y) * pitch);
                pack_rgb24_row(row, rgb + static_cast<std::size_t>(y) * rgb_pitch, row_pixels);
                if (gamma != nullptr)
                    gamma_xrgb_row(row, row_pixels, *gamma);
            }
        }
    );
}

void pack_rgb24_rgb565_row(
    uint16_t* out, const uint8_t* rgb, int width, const std::array<uint8_t, 256>* gamma
) noexcept {
    // Each channel's top bits and where they go in the word.
    constexpr unsigned red_drop = 3, green_drop = 2, blue_drop = 3;
    constexpr unsigned red_shift = 11, green_shift = 5;
    const auto pack = [](unsigned red, unsigned green, unsigned blue) {
        return static_cast<uint16_t>(
            ((red >> red_drop) << red_shift) | ((green >> green_drop) << green_shift) |
            (blue >> blue_drop)
        );
    };
    if (gamma == nullptr) {
        for (int x = 0; x < width; ++x, rgb += 3)
            out[x] = pack(rgb[0], rgb[1], rgb[2]);
        return;
    }
    const auto& table = *gamma;
    for (int x = 0; x < width; ++x, rgb += 3)
        out[x] = pack(table[rgb[0]], table[rgb[1]], table[rgb[2]]);
}

void convert_rgb24_rgb565(
    const uint8_t* rgb,
    uint32_t width,
    uint32_t height,
    uint8_t* pixels,
    std::size_t pitch,
    const std::array<uint8_t, 256>* gamma,
    platform::job_pool::Pool* pool
) noexcept {
    convert_rgb24_rgb565_rect(
        rgb, static_cast<std::size_t>(width) * 3U, width, height, pixels, pitch, gamma, pool
    );
}

void convert_rgb24_rgb565_rect(
    const uint8_t* rgb,
    std::size_t rgb_pitch,
    uint32_t width,
    uint32_t height,
    uint8_t* pixels,
    std::size_t pitch,
    const std::array<uint8_t, 256>* gamma,
    platform::job_pool::Pool* pool
) noexcept {
    const auto row_pixels = static_cast<int>(width);
    platform::job_pool::run_bands(
        pool, platform::job_pool::bands_of_rows(height, xrgb_band_rows), [&](uint32_t band) {
            const uint32_t first_row = band * xrgb_band_rows;
            const uint32_t end_row = std::min(height, first_row + xrgb_band_rows);
            for (uint32_t y = first_row; y < end_row; ++y)
                pack_rgb24_rgb565_row(
                    reinterpret_cast<uint16_t*>(pixels + static_cast<std::size_t>(y) * pitch),
                    rgb + static_cast<std::size_t>(y) * rgb_pitch,
                    row_pixels,
                    gamma
                );
        }
    );
}

} // namespace oa::app
