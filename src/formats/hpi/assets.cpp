// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include <algorithm>
#include <limits>

namespace oa {

namespace {

using base::bytes::ByteReader;
using base::bytes::DecodeCode;
using base::bytes::DecodeError;

constexpr uint64_t kMaxImagePixels = 64ULL << 20;
// The 128-byte header's fields, by file offset.
constexpr std::size_t kHeaderBytes = 128;
constexpr std::size_t kManufacturerOffset = 0;
constexpr std::size_t kVersionOffset = 1;
constexpr std::size_t kEncodingOffset = 2;
constexpr std::size_t kBitsPerPixelOffset = 3;
constexpr std::size_t kXMinOffset = 4;
constexpr std::size_t kYMinOffset = 6;
constexpr std::size_t kXMaxOffset = 8;
constexpr std::size_t kYMaxOffset = 10;
constexpr std::size_t kPlanesOffset = 65;
constexpr std::size_t kBytesPerLineOffset = 66;
constexpr uint8_t kManufacturer = 0x0A;
constexpr uint8_t kRunLengthEncoding = 1;
constexpr uint8_t kRunMarker = 0xC0;
constexpr uint8_t kRunCountMask = 0x3F;
// An indexed image ends with a marker byte and 256 RGB entries.
constexpr uint8_t kPaletteMarker = 0x0C;
constexpr std::size_t kPaletteBytes = 768;

/// Returns whether a header version is one 3.1c's images use.
///
/// @param version the header's version byte
/// @return true for versions 0, 2, 3, 4 and 5
constexpr bool supported_version(uint8_t version) noexcept {
    return version == 0 || version == 2 || version == 3 || version == 4 || version == 5;
}

} // namespace

base::bytes::Decoded<Image> decode_pcx(std::span<const uint8_t> data) {
    if (data.size() < kHeaderBytes)
        return DecodeError{
            DecodeCode::truncated, data.size(), "PCX data is shorter than its 128-byte header"
        };
    ByteReader header(data.first(kHeaderBytes));
    if (header.u8_at(kManufacturerOffset) != kManufacturer)
        return DecodeError{
            DecodeCode::bad_signature, kManufacturerOffset, "invalid PCX manufacturer byte"
        };
    if (!supported_version(header.u8_at(kVersionOffset)))
        return DecodeError{
            DecodeCode::unsupported_version, kVersionOffset, "unsupported PCX version"
        };
    const uint8_t encoding = header.u8_at(kEncodingOffset);
    if (encoding > kRunLengthEncoding)
        return DecodeError{DecodeCode::malformed, kEncodingOffset, "unsupported PCX encoding"};
    const uint8_t bits_per_pixel = header.u8_at(kBitsPerPixelOffset);
    const uint16_t x_min = header.u16_at(kXMinOffset);
    const uint16_t y_min = header.u16_at(kYMinOffset);
    const uint16_t x_max = header.u16_at(kXMaxOffset);
    const uint16_t y_max = header.u16_at(kYMaxOffset);
    const uint8_t planes = header.u8_at(kPlanesOffset);
    const uint16_t bytes_per_line = header.u16_at(kBytesPerLineOffset);

    if (x_max < x_min || y_max < y_min)
        return DecodeError{DecodeCode::malformed, kXMinOffset, "invalid PCX coordinate bounds"};
    const uint32_t width = static_cast<uint32_t>(x_max) - x_min + 1U;
    const uint32_t height = static_cast<uint32_t>(y_max) - y_min + 1U;
    if (bits_per_pixel != 8 || (planes != 1 && planes != 3))
        return DecodeError{
            DecodeCode::malformed,
            kBitsPerPixelOffset,
            "unsupported PCX pixel layout (expected 8-bit indexed or 24-bit planar RGB)"
        };
    if (bytes_per_line < width || bytes_per_line == 0)
        return DecodeError{
            DecodeCode::malformed,
            kBytesPerLineOffset,
            "PCX bytes-per-line is smaller than the image width"
        };
    const uint64_t pixels = static_cast<uint64_t>(width) * height;
    if (pixels > kMaxImagePixels)
        return DecodeError{
            DecodeCode::limit_exceeded,
            kXMinOffset,
            "PCX image exceeds the 64-megapixel safety limit"
        };
    if (pixels > std::numeric_limits<std::size_t>::max() / 3U)
        return DecodeError{
            DecodeCode::limit_exceeded, kXMinOffset, "PCX RGB output is too large for this platform"
        };

    std::size_t encoded_end = data.size();
    std::span<const uint8_t> palette;
    if (planes == 1) {
        const std::size_t trailer = kPaletteBytes + 1;
        if (data.size() < kHeaderBytes + trailer || data[data.size() - trailer] != kPaletteMarker)
            return DecodeError{
                DecodeCode::malformed, data.size(), "8-bit PCX has no 256-color palette"
            };
        encoded_end = data.size() - trailer;
        palette = data.subspan(data.size() - kPaletteBytes, kPaletteBytes);
    }

    std::size_t cursor = kHeaderBytes;
    DecodeError failure{};
    const auto decode_row = [&](std::span<uint8_t> row) {
        std::size_t written = 0;
        while (written < row.size()) {
            if (cursor >= encoded_end) {
                failure = {DecodeCode::truncated, cursor, "truncated PCX pixel data"};
                return false;
            }
            const std::size_t control_at = cursor;
            const uint8_t control = data[cursor++];
            std::size_t count = 1;
            uint8_t value = control;
            if (encoding == kRunLengthEncoding && (control & kRunMarker) == kRunMarker) {
                count = control & kRunCountMask;
                if (count == 0 || cursor >= encoded_end) {
                    failure = {DecodeCode::malformed, control_at, "invalid PCX RLE run"};
                    return false;
                }
                value = data[cursor++];
            }
            if (count > row.size() - written) {
                failure = {
                    DecodeCode::malformed,
                    control_at,
                    "PCX RLE run crosses a plane scanline boundary"
                };
                return false;
            }
            std::fill_n(row.begin() + static_cast<std::ptrdiff_t>(written), count, value);
            written += count;
        }
        return true;
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
        if (!decode_row(row) || (planes == 3 && (!decode_row(green) || !decode_row(blue))))
            return failure;
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
    // Bytes between the last row and the palette are ignored, as the game
    // ignores them: it reads the rows from the header on and the palette
    // from the end of the file.
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
