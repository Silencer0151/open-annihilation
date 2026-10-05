// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Layout shared by the PNG reader and writer.
#pragma once

#include <cstddef>
#include <cstdint>

#include "oa/formats/png.hpp"

namespace oa::formats::png::detail {

inline constexpr std::size_t k_chunk_length_bytes = 4;
inline constexpr std::size_t k_chunk_type_bytes = 4;
inline constexpr std::size_t k_chunk_crc_bytes = 4;
inline constexpr std::size_t k_chunk_overhead =
    k_chunk_length_bytes + k_chunk_type_bytes + k_chunk_crc_bytes;
inline constexpr uint32_t k_ihdr_bytes = 13;
// Bit 5 of the first type byte marks an ancillary chunk.
inline constexpr uint8_t k_ancillary_bit = 0x20;

// IHDR body offsets.
inline constexpr std::size_t k_ihdr_width = 0;
inline constexpr std::size_t k_ihdr_height = 4;
inline constexpr std::size_t k_ihdr_bit_depth = 8;
inline constexpr std::size_t k_ihdr_color_type = 9;
inline constexpr std::size_t k_ihdr_compression = 10;
inline constexpr std::size_t k_ihdr_filter = 11;
inline constexpr std::size_t k_ihdr_interlace = 12;

inline constexpr uint8_t k_compression_deflate = 0;
inline constexpr uint8_t k_filter_method_adaptive = 0;

enum class Filter : uint8_t {
    none = 0,
    sub = 1,
    up = 2,
    average = 3,
    paeth = 4,
};
inline constexpr uint8_t k_filter_count = 5;

// One Adam7 pass: the pixels at (x0 + i*dx, y0 + j*dy).
struct Pass {
    uint32_t x0{};
    uint32_t y0{};
    uint32_t dx{};
    uint32_t dy{};
};

inline constexpr uint32_t k_adam7_pass_count = 7;
inline constexpr Pass k_adam7_passes[k_adam7_pass_count] = {
    {0, 0, 8, 8},
    {4, 0, 8, 8},
    {0, 4, 4, 8},
    {2, 0, 4, 4},
    {0, 2, 2, 4},
    {1, 0, 2, 2},
    {0, 1, 1, 2},
};
inline constexpr Pass k_whole_image = {0, 0, 1, 1};

/// Reads a 32-bit big-endian value.
///
/// @param p the first of four bytes
/// @return the value
inline uint32_t read_be32(const uint8_t* p) {
    return (static_cast<uint32_t>(p[0]) << 24) | (static_cast<uint32_t>(p[1]) << 16) |
           (static_cast<uint32_t>(p[2]) << 8) | static_cast<uint32_t>(p[3]);
}

/// Writes a 32-bit value big-endian.
///
/// @param[out] p the first of four bytes
/// @param value the value
inline void write_be32(uint8_t* p, uint32_t value) {
    p[0] = static_cast<uint8_t>(value >> 24);
    p[1] = static_cast<uint8_t>(value >> 16);
    p[2] = static_cast<uint8_t>(value >> 8);
    p[3] = static_cast<uint8_t>(value);
}

/// Returns the bits one pixel takes in an image's rows.
///
/// @param header the image header
/// @return bit depth times the colour type's channel count
inline uint32_t bits_per_pixel(const Header& header) {
    return header.bit_depth * channel_count(header.color_type);
}

/// Returns the bytes that hold a run of pixels, rounded up to whole bytes.
///
/// @param pixels pixels in the run
/// @param bits bits per pixel
/// @return the bytes the run takes
inline std::size_t packed_bytes(uint64_t pixels, uint32_t bits) {
    return static_cast<std::size_t>((pixels * bits + 7) / 8);
}

/// Tells whether rows of an image can be held: a row of `width` pixels of
/// `bits` each, counted at 64 bits, is not empty, is below k_max_dimension
/// bytes and fits in a size_t, so packed_bytes gives every such row and pass
/// exactly.
///
/// @param width pixels in a row
/// @param bits bits per pixel
/// @return true when a row's bytes are in range
inline bool row_size_decodable(uint32_t width, uint32_t bits) {
    const uint64_t row = (uint64_t{width} * bits + 7) / 8;
    return row != 0 && row < k_max_dimension && static_cast<std::size_t>(row) == row;
}

/// Returns the pixels an interlace pass covers along one axis.
///
/// @param size pixels along the axis
/// @param start the pass's first pixel along it
/// @param step pixels between the pass's pixels along it
/// @return the pixels covered, zero when the pass starts past the end
inline uint32_t pass_extent(uint32_t size, uint32_t start, uint32_t step) {
    return size > start ? (size - start + step - 1) / step : 0;
}

/// Predicts a byte from its left, upper and upper-left neighbours, as the
/// Paeth filter does: the neighbour closest to left + up - up_left, ties going
/// to left, then up.
///
/// @param left the byte to the left
/// @param up the byte above
/// @param up_left the byte above and to the left
/// @return the predicted byte
inline uint8_t paeth_predictor(int32_t left, int32_t up, int32_t up_left) {
    const int32_t estimate = left + up - up_left;
    const int32_t to_left = estimate > left ? estimate - left : left - estimate;
    const int32_t to_up = estimate > up ? estimate - up : up - estimate;
    const int32_t to_up_left = estimate > up_left ? estimate - up_left : up_left - estimate;
    if (to_left <= to_up && to_left <= to_up_left)
        return static_cast<uint8_t>(left);
    return static_cast<uint8_t>(to_up <= to_up_left ? up : up_left);
}

/// Returns the bytes a filter looks back to find the left neighbour.
///
/// @param header the image header
/// @return the bytes of one pixel, at least one
inline std::size_t filter_stride(const Header& header) {
    const uint32_t bytes = bits_per_pixel(header) / 8;
    return bytes > 0 ? bytes : 1;
}

/// CRC-32 of a chunk's type and data.
uint32_t chunk_crc(const uint8_t* type_and_data, std::size_t size);

/// Extends a running chunk CRC over `size` more bytes.
uint32_t crc_update(uint32_t crc, const uint8_t* data, std::size_t size);

// Characters of the text a chunk message keeps.
inline constexpr std::size_t k_chunk_message_text = 63;
// Four type bytes written as [XX], ": ", the text and its terminator.
inline constexpr std::size_t k_chunk_message_bytes =
    4 * k_chunk_type_bytes + 2 + k_chunk_message_text + 1;

/// Reverses row filter `filter` in place against the previous row; `stride`
/// is the bytes back to the left neighbour. False for an unknown filter.
bool unfilter_row(
    uint8_t filter, uint8_t* row, const uint8_t* previous, std::size_t size, std::size_t stride
);

/// Writes "TYPE: text": type bytes outside ')'..'Z' and 'a'..'z' become
/// [XX], and the text is cut to 63 characters.
void format_chunk_message(const uint8_t* type, const char* text, char* out);

} // namespace oa::formats::png::detail
