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
    const auto row_pixels = static_cast<int>(width);
    platform::job_pool::run_bands(
        pool, platform::job_pool::bands_of_rows(height, xrgb_band_rows), [&](uint32_t band) {
            const uint32_t first_row = band * xrgb_band_rows;
            const uint32_t end_row = std::min(height, first_row + xrgb_band_rows);
            for (uint32_t y = first_row; y < end_row; ++y) {
                auto* row =
                    reinterpret_cast<uint32_t*>(pixels + static_cast<std::size_t>(y) * pitch);
                pack_rgb24_row(row, rgb + static_cast<std::size_t>(y) * width * 3U, row_pixels);
                if (gamma != nullptr)
                    gamma_xrgb_row(row, row_pixels, *gamma);
            }
        }
    );
}

} // namespace oa::app
