// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// PNG writer: per-row filter choice, Adam7 pass extraction and chunking of
// the deflated stream.

#include <cstring>
#include <vector>

#include <zlib.h>

#include "png_internal.hpp"

namespace oa::formats::png {

using namespace detail;

namespace {

// Deflated image data per IDAT chunk.
constexpr std::size_t k_idat_chunk_bytes = 0x2000;
// Largest filtered image the writer deflates in one call.
constexpr std::size_t k_max_filtered_bytes = 0x3fffffff;
// Bytes handed to one zlib crc32 call, which takes a 32-bit length.
constexpr std::size_t k_crc_piece_bytes = 0x40000000;

bool valid_depth(ColorType color_type, uint8_t depth) {
    switch (color_type) {
    case ColorType::grey:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8 || depth == 16;
    case ColorType::palette:
        return depth == 1 || depth == 2 || depth == 4 || depth == 8;
    case ColorType::rgb:
    case ColorType::grey_alpha:
    case ColorType::rgb_alpha:
        return depth == 8 || depth == 16;
    }
    return false;
}

bool valid_header(const Header& header) {
    return header.width >= 1 && header.width <= k_max_dimension && header.height >= 1 &&
           header.height <= k_max_dimension && valid_depth(header.color_type, header.bit_depth) &&
           (header.interlace == Interlace::none || header.interlace == Interlace::adam7);
}

void append_chunk(
    std::vector<uint8_t>& out, const char* type, const uint8_t* data, std::size_t size
) {
    const std::size_t start = out.size();
    out.resize(start + k_chunk_overhead + size);
    uint8_t* p = out.data() + start;
    write_be32(p, static_cast<uint32_t>(size));
    std::memcpy(p + k_chunk_length_bytes, type, k_chunk_type_bytes);
    if (size != 0)
        std::memcpy(p + k_chunk_length_bytes + k_chunk_type_bytes, data, size);
    write_be32(
        p + k_chunk_length_bytes + k_chunk_type_bytes + size,
        chunk_crc(p + k_chunk_length_bytes, k_chunk_type_bytes + size)
    );
}

// Filters `row` against `previous` with one filter type into `out`.
void apply_filter(
    Filter filter,
    const uint8_t* row,
    const uint8_t* previous,
    std::size_t size,
    std::size_t stride,
    uint8_t* out
) {
    if (filter == Filter::none) {
        std::memcpy(out, row, size);
        return;
    }
    for (std::size_t i = 0; i < size; ++i) {
        const int32_t left = i >= stride ? row[i - stride] : 0;
        const int32_t up = previous[i];
        const int32_t up_left = i >= stride ? previous[i - stride] : 0;
        int32_t predictor = 0;
        switch (filter) {
        case Filter::none:
            break;
        case Filter::sub:
            predictor = left;
            break;
        case Filter::up:
            predictor = up;
            break;
        case Filter::average:
            predictor = (left + up) / 2;
            break;
        case Filter::paeth:
            predictor = paeth_predictor(left, up, up_left);
            break;
        }
        out[i] = static_cast<uint8_t>(row[i] - predictor);
    }
}

// Sum of the filtered bytes read as signed values; smaller usually deflates better.
uint64_t filter_cost(const uint8_t* filtered, std::size_t size) {
    uint64_t cost = 0;
    for (std::size_t i = 0; i < size; ++i)
        cost += filtered[i] < 0x80 ? filtered[i] : 0x100u - filtered[i];
    return cost;
}

// Appends one filtered scanline: palette and sub-byte images, and every row
// of a stored file, are left unfiltered, others take the cheapest filter.
void append_scanline(
    const Header& header,
    bool stored,
    const uint8_t* row,
    const uint8_t* previous,
    std::size_t size,
    std::vector<uint8_t>& candidate,
    std::vector<uint8_t>& out
) {
    const std::size_t stride = filter_stride(header);
    Filter best = Filter::none;
    if (!stored && header.color_type != ColorType::palette && header.bit_depth >= 8) {
        uint64_t best_cost = UINT64_MAX;
        for (uint8_t f = 0; f < k_filter_count; ++f) {
            apply_filter(static_cast<Filter>(f), row, previous, size, stride, candidate.data());
            const uint64_t cost = filter_cost(candidate.data(), size);
            if (cost < best_cost) {
                best_cost = cost;
                best = static_cast<Filter>(f);
            }
        }
    }
    out.push_back(static_cast<uint8_t>(best));
    const std::size_t start = out.size();
    out.resize(start + size);
    apply_filter(best, row, previous, size, stride, out.data() + start);
}

// Copies the pixels of one pass row out of the full image row.
void gather_row(
    const uint8_t* image_row, const Pass& pass, uint32_t count, uint32_t bits, uint8_t* out
) {
    if (pass.dx == 1) {
        std::memcpy(out, image_row, packed_bytes(count, bits));
        return;
    }
    if (bits >= 8) {
        const uint32_t bytes = bits / 8;
        for (uint32_t i = 0; i < count; ++i)
            std::memcpy(
                out + static_cast<std::size_t>(i) * bytes,
                image_row +
                    (static_cast<std::size_t>(pass.x0) + static_cast<std::size_t>(i) * pass.dx) *
                        bytes,
                bytes
            );
        return;
    }
    std::memset(out, 0, packed_bytes(count, bits));
    const uint32_t mask = (1u << bits) - 1;
    for (uint32_t i = 0; i < count; ++i) {
        const uint64_t from = (pass.x0 + static_cast<uint64_t>(i) * pass.dx) * bits;
        const uint32_t value = (image_row[from / 8] >> (8 - bits - from % 8)) & mask;
        const uint64_t to = static_cast<uint64_t>(i) * bits;
        out[to / 8] = static_cast<uint8_t>(out[to / 8] | (value << (8 - bits - to % 8)));
    }
}

// Encodes an image as a PNG file: filtered and compressed as tightly as zlib
// can, or, stored, unfiltered and in deflate's uncompressed blocks.
bool write_file(const Image& image, bool stored, std::vector<uint8_t>* out);

} // namespace

uint32_t detail::crc_update(uint32_t crc, const uint8_t* data, std::size_t size) {
    uLong value = crc;
    while (size > 0) {
        const auto piece = static_cast<uInt>(size > k_crc_piece_bytes ? k_crc_piece_bytes : size);
        value = crc32(value, data, piece);
        data += piece;
        size -= piece;
    }
    return static_cast<uint32_t>(value);
}

uint32_t detail::chunk_crc(const uint8_t* type_and_data, std::size_t size) {
    return crc_update(static_cast<uint32_t>(crc32(0L, Z_NULL, 0)), type_and_data, size);
}

bool write(const Image& image, std::vector<uint8_t>* out) {
    return write_file(image, false, out);
}

bool write_stored(const Image& image, std::vector<uint8_t>* out) {
    return write_file(image, true, out);
}

namespace {

bool write_file(const Image& image, bool stored, std::vector<uint8_t>* out) {
    const Header& header = image.header;
    if (!valid_header(header))
        return false;
    if (header.color_type == ColorType::palette &&
        (image.palette.empty() || image.palette.size() > k_max_palette_entries))
        return false;
    const std::size_t row = row_bytes(header, {});
    if (row == 0 || image.rows.size() / row != header.height || image.rows.size() % row != 0)
        return false;

    const bool interlaced = header.interlace == Interlace::adam7;
    const uint32_t pass_count = interlaced ? k_adam7_pass_count : 1;
    const uint32_t bits = bits_per_pixel(header);
    std::vector<uint8_t> filtered;
    std::vector<uint8_t> current(row);
    std::vector<uint8_t> previous(row);
    std::vector<uint8_t> candidate(row);
    for (uint32_t p = 0; p < pass_count; ++p) {
        const Pass& pass = interlaced ? k_adam7_passes[p] : k_whole_image;
        const uint32_t pass_width = pass_extent(header.width, pass.x0, pass.dx);
        const uint32_t pass_height = pass_extent(header.height, pass.y0, pass.dy);
        if (pass_width == 0 || pass_height == 0)
            continue;
        const std::size_t pass_bytes = packed_bytes(pass_width, bits);
        if (filtered.size() + (pass_bytes + 1) * pass_height > k_max_filtered_bytes)
            return false;
        std::memset(previous.data(), 0, pass_bytes);
        for (uint32_t y = 0; y < pass_height; ++y) {
            const std::size_t image_y = pass.y0 + static_cast<std::size_t>(y) * pass.dy;
            gather_row(image.rows.data() + image_y * row, pass, pass_width, bits, current.data());
            append_scanline(
                header, stored, current.data(), previous.data(), pass_bytes, candidate, filtered
            );
            std::memcpy(previous.data(), current.data(), pass_bytes);
        }
    }

    uLongf packed_size = compressBound(static_cast<uLong>(filtered.size()));
    std::vector<uint8_t> packed(packed_size);
    if (compress2(
            packed.data(),
            &packed_size,
            filtered.data(),
            static_cast<uLong>(filtered.size()),
            stored ? Z_NO_COMPRESSION : Z_BEST_COMPRESSION
        ) != Z_OK)
        return false;

    out->assign(k_signature, k_signature + k_signature_bytes);
    uint8_t ihdr[k_ihdr_bytes] = {};
    write_be32(ihdr + k_ihdr_width, header.width);
    write_be32(ihdr + k_ihdr_height, header.height);
    ihdr[k_ihdr_bit_depth] = header.bit_depth;
    ihdr[k_ihdr_color_type] = static_cast<uint8_t>(header.color_type);
    ihdr[k_ihdr_compression] = k_compression_deflate;
    ihdr[k_ihdr_filter] = k_filter_method_adaptive;
    ihdr[k_ihdr_interlace] = static_cast<uint8_t>(header.interlace);
    append_chunk(*out, "IHDR", ihdr, sizeof ihdr);
    if (header.color_type == ColorType::palette) {
        std::vector<uint8_t> entries;
        for (const Rgb& entry : image.palette) {
            entries.push_back(entry.r);
            entries.push_back(entry.g);
            entries.push_back(entry.b);
        }
        append_chunk(*out, "PLTE", entries.data(), entries.size());
    }
    for (std::size_t offset = 0; offset < packed_size; offset += k_idat_chunk_bytes) {
        const std::size_t size =
            packed_size - offset < k_idat_chunk_bytes ? packed_size - offset : k_idat_chunk_bytes;
        append_chunk(*out, "IDAT", packed.data() + offset, size);
    }
    append_chunk(*out, "IEND", nullptr, 0);
    return true;
}

} // namespace

} // namespace oa::formats::png
