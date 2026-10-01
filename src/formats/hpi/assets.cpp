// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include <algorithm>
#include <limits>
#include <stdexcept>
#include <string>

namespace oa {

namespace {

constexpr uint64_t kMaxImagePixels = 64ULL << 20;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

uint16_t le16(std::span<const uint8_t> bytes, std::size_t offset) {
    if (offset > bytes.size() || bytes.size() - offset < 2) {
        fail("truncated little-endian uint16");
    }
    return static_cast<uint16_t>(bytes[offset]) | (static_cast<uint16_t>(bytes[offset + 1]) << 8U);
}

} // namespace

Image decode_pcx(std::span<const uint8_t> data) {
    if (data.size() < 128) {
        fail("PCX data is shorter than its 128-byte header");
    }
    if (data[0] != 0x0A) {
        fail("invalid PCX manufacturer byte");
    }
    const uint8_t version = data[1];
    if (version != 0 && version != 2 && version != 3 && version != 4 && version != 5) {
        fail("unsupported PCX version " + std::to_string(version));
    }
    const uint8_t encoding = data[2];
    if (encoding > 1) {
        fail("unsupported PCX encoding");
    }
    const uint8_t bits_per_pixel = data[3];
    const uint16_t x_min = le16(data, 4);
    const uint16_t y_min = le16(data, 6);
    const uint16_t x_max = le16(data, 8);
    const uint16_t y_max = le16(data, 10);
    const uint8_t planes = data[65];
    const uint16_t bytes_per_line = le16(data, 66);

    if (x_max < x_min || y_max < y_min) {
        fail("invalid PCX coordinate bounds");
    }
    const uint32_t width = static_cast<uint32_t>(x_max) - x_min + 1U;
    const uint32_t height = static_cast<uint32_t>(y_max) - y_min + 1U;
    if (bits_per_pixel != 8 || (planes != 1 && planes != 3)) {
        fail("unsupported PCX pixel layout (expected 8-bit indexed or 24-bit planar RGB)");
    }
    if (bytes_per_line < width || bytes_per_line == 0) {
        fail("PCX bytes-per-line is smaller than the image width");
    }
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > kMaxImagePixels) {
        fail("PCX image exceeds the 64-megapixel safety limit");
    }
    if (pixels > std::numeric_limits<std::size_t>::max() / 3U) {
        fail("PCX RGB output is too large for this platform");
    }

    std::size_t encoded_end = data.size();
    std::span<const uint8_t> palette;
    if (planes == 1) {
        if (data.size() < 128 + 769 || data[data.size() - 769] != 0x0C) {
            fail("8-bit PCX has no 256-color palette");
        }
        encoded_end = data.size() - 769;
        palette = data.subspan(data.size() - 768, 768);
    }

    std::size_t cursor = 128;
    const auto decode_row = [&](std::span<uint8_t> row, std::size_t row_number) {
        std::size_t written = 0;
        while (written < row.size()) {
            if (cursor >= encoded_end) {
                fail("truncated PCX pixel data at row " + std::to_string(row_number));
            }
            const uint8_t control = data[cursor++];
            std::size_t count = 1;
            uint8_t value = control;
            if (encoding == 1 && (control & 0xC0U) == 0xC0U) {
                count = control & 0x3FU;
                if (count == 0 || cursor >= encoded_end) {
                    fail("invalid PCX RLE run");
                }
                value = data[cursor++];
            }
            if (count > row.size() - written) {
                fail("PCX RLE run crosses a plane scanline boundary");
            }
            std::fill_n(row.begin() + static_cast<std::ptrdiff_t>(written), count, value);
            written += count;
        }
    };

    Image image{width, height, std::vector<uint8_t>(static_cast<std::size_t>(pixels * 3U))};
    if (planes == 1) {
        image.indices.assign(static_cast<std::size_t>(pixels), 0);
        image.palette.emplace();
        for (std::size_t color = 0; color < palette_color_count; ++color) {
            for (std::size_t channel = 0; channel < 3; ++channel)
                (*image.palette)[color * palette_entry_bytes + channel] =
                    palette[color * 3 + channel];
        }
    }
    std::vector<uint8_t> row(bytes_per_line);
    std::vector<uint8_t> green;
    std::vector<uint8_t> blue;
    if (planes == 3) {
        green.resize(bytes_per_line);
        blue.resize(bytes_per_line);
    }

    for (uint32_t y = 0; y < height; ++y) {
        decode_row(row, y);
        if (planes == 3) {
            decode_row(green, y);
            decode_row(blue, y);
        }
        for (uint32_t x = 0; x < width; ++x) {
            const std::size_t output = (static_cast<std::size_t>(y) * width + x) * 3U;
            if (planes == 1) {
                image.indices[static_cast<std::size_t>(y) * width + x] = row[x];
                const std::size_t color = static_cast<std::size_t>(row[x]) * 3U;
                image.rgb[output] = palette[color];
                image.rgb[output + 1] = palette[color + 1];
                image.rgb[output + 2] = palette[color + 2];
            } else {
                image.rgb[output] = row[x];
                image.rgb[output + 1] = green[x];
                image.rgb[output + 2] = blue[x];
            }
        }
    }
    if (cursor != encoded_end) {
        fail("PCX contains trailing bytes before its palette");
    }
    return image;
}

PaletteMap remap_palette(const PaletteBytes& source, const PaletteBytes& destination) {
    constexpr unsigned rgb_channels = 3;
    constexpr unsigned maximum_channel_difference = 255;

    // The last pair of palettes this thread mapped, and their map: screens
    // map the same pair on every frame they draw.
    struct LastMap {
        bool kept = false;
        PaletteBytes source{};
        PaletteBytes destination{};
        PaletteMap map{};
    };

    thread_local LastMap last;
    if (last.kept && last.source == source && last.destination == destination)
        return last.map;
    PaletteMap result{};
    for (std::size_t color = 0; color < palette_color_count; ++color) {
        unsigned best_distance = rgb_channels * maximum_channel_difference + 1;
        for (std::size_t candidate = 0; candidate < palette_color_count; ++candidate) {
            unsigned distance = 0;
            for (unsigned channel = 0; channel < rgb_channels; ++channel) {
                const unsigned a = source[color * palette_entry_bytes + channel];
                const unsigned b = destination[candidate * palette_entry_bytes + channel];
                distance += a > b ? a - b : b - a;
            }
            if (distance < best_distance) {
                best_distance = distance;
                result[color] = static_cast<uint8_t>(candidate);
            }
        }
    }
    last = {true, source, destination, result};
    return result;
}

} // namespace oa
