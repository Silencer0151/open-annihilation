// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/pcx.hpp"
#include "oa/base/bytes.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <new>
#include <string>

namespace oa::present {
namespace {

// Header field offsets.
constexpr std::size_t header_manufacturer = 0;
constexpr std::size_t header_version = 1;
constexpr std::size_t header_encoding = 2;
constexpr std::size_t header_bits_per_pixel = 3;
constexpr std::size_t header_x_min = 4;
constexpr std::size_t header_y_min = 6;
constexpr std::size_t header_x_max = 8;
constexpr std::size_t header_y_max = 10;
constexpr std::size_t header_h_dpi = 12;
constexpr std::size_t header_v_dpi = 14;
constexpr std::size_t header_ega_palette = 16;
constexpr std::size_t header_ega_palette_size = 48;
constexpr std::size_t header_planes = 65;
constexpr std::size_t header_bytes_per_line = 66;

constexpr uint8_t pcx_encoding_rle = 1;
constexpr uint8_t pcx_bits_per_pixel = 8;
constexpr uint8_t pcx_single_plane = 1;
constexpr int32_t pcx_max_run = 0x3F;
constexpr int32_t rgb_triple = 3;

// BMP header fields.
constexpr uint16_t bmp_signature = 0x4D42; // "BM"
constexpr uint16_t bmp_planes = 1;
constexpr uint16_t bmp_bits_per_pixel = 8;
constexpr uint32_t bmp_palette_colors = OA_PALETTE_COLORS;
constexpr int32_t bmp_row_alignment = 4;

using base::bytes::load_le16;
using base::bytes::store_le16;
using base::bytes::store_le32;

int32_t stream_read(ByteStream& stream, void* data, int32_t size) noexcept {
    return stream.read ? stream.read(stream.user, data, size) : 0;
}

int32_t stream_write(ByteStream& stream, const void* data, int32_t size) noexcept {
    return stream.write ? stream.write(stream.user, data, size) : 0;
}

bool stream_seek(ByteStream& stream, int32_t position) noexcept {
    return stream.seek && stream.seek(stream.user, position) == 0;
}

bool has_signature(const uint8_t* header) noexcept {
    return header[header_manufacturer] == pcx_manufacturer && header[header_version] == pcx_version;
}

// Width and height from the header's inclusive bounds.
bool header_extent(const uint8_t* header, int32_t& width, int32_t& height) noexcept {
    width = static_cast<int32_t>(load_le16(header + header_x_max)) -
            load_le16(header + header_x_min) + 1;
    height = static_cast<int32_t>(load_le16(header + header_y_max)) -
             load_le16(header + header_y_min) + 1;
    return width > 0 && height > 0 && static_cast<int64_t>(width) * height <= pcx_max_pixels;
}

int32_t reader_read(void* user, void* data, int32_t size) {
    auto& reader = *static_cast<MemoryReader*>(user);
    if (size <= 0 || reader.position >= reader.data.size())
        return 0;
    const auto count =
        std::min(static_cast<std::size_t>(size), reader.data.size() - reader.position);
    std::memcpy(data, reader.data.data() + reader.position, count);
    reader.position += count;
    return static_cast<int32_t>(count);
}

int32_t reader_seek(void* user, int32_t position) {
    auto& reader = *static_cast<MemoryReader*>(user);
    if (position < 0 || static_cast<std::size_t>(position) > reader.data.size())
        return -1;
    reader.position = static_cast<std::size_t>(position);
    return 0;
}

int32_t reader_tell(void* user) {
    return static_cast<int32_t>(static_cast<MemoryReader*>(user)->position);
}

int32_t reader_length(void* user) {
    const auto size = static_cast<MemoryReader*>(user)->data.size();
    return size > static_cast<std::size_t>(std::numeric_limits<int32_t>::max())
               ? -1
               : static_cast<int32_t>(size);
}

int32_t writer_write(void* user, const void* data, int32_t size) {
    auto& writer = *static_cast<MemoryWriter*>(user);
    if (size <= 0)
        return 0;
    const auto count = static_cast<std::size_t>(size);
    if (writer.position > writer.limit || count > writer.limit - writer.position)
        return 0;
    if (writer.bytes.size() < writer.position + count)
        writer.bytes.resize(writer.position + count, 0);
    std::memcpy(writer.bytes.data() + writer.position, data, count);
    writer.position += count;
    return size;
}

int32_t writer_seek(void* user, int32_t position) {
    auto& writer = *static_cast<MemoryWriter*>(user);
    if (position < 0 || static_cast<std::size_t>(position) > writer.limit)
        return -1;
    writer.position = static_cast<std::size_t>(position);
    if (writer.bytes.size() < writer.position)
        writer.bytes.resize(writer.position, 0);
    return 0;
}

int32_t writer_tell(void* user) {
    return static_cast<int32_t>(static_cast<MemoryWriter*>(user)->position);
}

int32_t writer_length(void* user) {
    return static_cast<int32_t>(static_cast<MemoryWriter*>(user)->bytes.size());
}

} // namespace

ByteStream memory_reader_stream(MemoryReader& reader) noexcept {
    ByteStream stream;
    stream.user = &reader;
    stream.read = reader_read;
    stream.seek = reader_seek;
    stream.tell = reader_tell;
    stream.length = reader_length;
    return stream;
}

ByteStream memory_writer_stream(MemoryWriter& writer) noexcept {
    ByteStream stream;
    stream.user = &writer;
    stream.write = writer_write;
    stream.seek = writer_seek;
    stream.tell = writer_tell;
    stream.length = writer_length;
    return stream;
}

const char* pcx_status_text(PcxStatus status) noexcept {
    switch (status) {
    case PcxStatus::ok:
        return "ok";
    case PcxStatus::no_stream:
        return "no stream";
    case PcxStatus::short_header:
        return "PCX data is shorter than its 128-byte header";
    case PcxStatus::bad_signature:
        return "not a version 5 PCX (bad manufacturer/version bytes)";
    case PcxStatus::bad_bounds:
        return "PCX bounds are inverted or exceed the image size limit";
    case PcxStatus::short_color_map:
        return "PCX data is too short to hold its 768-byte color map";
    case PcxStatus::truncated_pixels:
        return "PCX pixel data ends before the last row";
    case PcxStatus::write_failed:
        return "image stream write failed";
    }
    return "unknown PCX status";
}

bool decode_pcx_row(ByteStream& stream, uint8_t* row, int32_t width) noexcept {
    uint8_t* out = row;
    while (width > 0) {
        uint8_t byte = 0;
        if (stream_read(stream, &byte, 1) != 1)
            return false;
        if ((byte & pcx_run_marker) != pcx_run_marker) {
            *out++ = byte;
            --width;
            continue;
        }
        auto count = static_cast<uint8_t>(byte & pcx_run_length_mask);
        width -= count;
        if (width < 0)
            count = static_cast<uint8_t>(count + width);
        if (stream_read(stream, &byte, 1) != 1)
            return false;
        std::memset(out, byte, count);
        out += count;
    }
    return true;
}

int32_t encode_pcx_run(ByteStream& stream, uint8_t value, int32_t count) noexcept {
    if (count == 1 && (value & pcx_run_marker) != pcx_run_marker) {
        stream_write(stream, &value, 1);
        return 1;
    }
    int32_t written = 0;
    while (count > 0) {
        const int32_t run = std::min(count, pcx_max_run);
        const auto marker = static_cast<uint8_t>(run | pcx_run_marker);
        stream_write(stream, &marker, 1);
        stream_write(stream, &value, 1);
        count -= run;
        written += 2;
    }
    return written;
}

int32_t encode_pcx_row(ByteStream& stream, const uint8_t* row, int32_t width) noexcept {
    int32_t written = 0;
    uint8_t value = row[0];
    int32_t run = 1;
    for (int32_t i = 1; i < width; ++i) {
        if (row[i] == value) {
            ++run;
            continue;
        }
        written += encode_pcx_run(stream, value, run);
        value = row[i];
        run = 1;
    }
    return written + encode_pcx_run(stream, value, run);
}

PcxStatus load_pcx_image(ByteStream& stream, PcxImage& image) {
    image = PcxImage{};
    uint8_t header[pcx_header_size]{};
    if (stream_read(stream, header, pcx_header_size) != pcx_header_size)
        return PcxStatus::short_header;
    if (!has_signature(header))
        return PcxStatus::bad_signature;
    int32_t width = 0;
    int32_t height = 0;
    if (!header_extent(header, width, height))
        return PcxStatus::bad_bounds;
    const int32_t length = stream.length ? stream.length(stream.user) : -1;
    if (length < pcx_header_size + pcx_color_map_size)
        return PcxStatus::short_color_map;
    image.width = width;
    image.height = height;
    image.pixels.assign(static_cast<std::size_t>(width) * static_cast<std::size_t>(height), 0);
    if (!stream_seek(stream, length - pcx_color_map_size) ||
        stream_read(stream, image.color_map, pcx_color_map_size) != pcx_color_map_size)
        return PcxStatus::short_color_map;
    if (!stream_seek(stream, pcx_header_size))
        return PcxStatus::truncated_pixels;
    uint8_t* row = image.pixels.data();
    for (int32_t y = 0; y < height; ++y, row += width)
        if (!decode_pcx_row(stream, row, width))
            return PcxStatus::truncated_pixels;
    return PcxStatus::ok;
}

PcxStatus write_pcx_image(
    ByteStream& stream,
    const uint8_t* pixels,
    int32_t width,
    int32_t height,
    const uint8_t* color_map
) noexcept {
    uint8_t header[pcx_header_size]{};
    header[header_manufacturer] = pcx_manufacturer;
    header[header_version] = pcx_version;
    header[header_encoding] = pcx_encoding_rle;
    header[header_bits_per_pixel] = pcx_bits_per_pixel;
    store_le16(header + header_x_max, static_cast<uint16_t>(width - 1));
    store_le16(header + header_y_max, static_cast<uint16_t>(height - 1));
    store_le16(header + header_h_dpi, static_cast<uint16_t>(width));
    store_le16(header + header_v_dpi, static_cast<uint16_t>(height));
    std::memcpy(header + header_ega_palette, color_map, header_ega_palette_size);
    header[header_planes] = pcx_single_plane;
    store_le16(header + header_bytes_per_line, static_cast<uint16_t>(width));
    if (stream_write(stream, header, pcx_header_size) != pcx_header_size)
        return PcxStatus::write_failed;
    const uint8_t* row = pixels;
    for (int32_t y = 0; y < height; ++y, row += width)
        encode_pcx_row(stream, row, width);
    stream_write(stream, &pcx_palette_marker, 1);
    if (stream_write(stream, color_map, pcx_color_map_size) != pcx_color_map_size)
        return PcxStatus::write_failed;
    return PcxStatus::ok;
}

PcxStatus save_pcx_surface(
    ByteStream& stream, const DisplayContext& display, const Surface& surface
) noexcept {
    uint8_t color_map[pcx_color_map_size];
    for (int i = 0; i < OA_PALETTE_COLORS; ++i) {
        color_map[i * rgb_triple] = display.palette.entries[i].r;
        color_map[i * rgb_triple + 1] = display.palette.entries[i].g;
        color_map[i * rgb_triple + 2] = display.palette.entries[i].b;
    }
    return write_pcx_image(stream, surface.pixels, surface.width, surface.height, color_map);
}

namespace {

// Room for a numbered file's number: at least four digits, and up to ten with
// a sign.
constexpr std::size_t numbered_file_number_bytes = 12;

struct HighestFrame {
    std::size_t prefix_length{};
    int32_t highest{};
};

void keep_highest_frame(void* user, const char* name) {
    auto& state = *static_cast<HighestFrame*>(user);
    if (std::strlen(name) < state.prefix_length)
        return;
    const auto number = static_cast<int32_t>(std::atoi(name + state.prefix_length));
    if (number > state.highest)
        state.highest = number;
}

} // namespace

bool save_numbered_pcx(
    const DisplayContext& display,
    const char* directory,
    const char* prefix,
    const NumberedFileHost& host
) noexcept {
    const std::size_t length = std::strlen(directory);
    const char* separator = length != 0 && directory[length - 1] != '\\' ? "\\" : "";
    if (display.use_active_surface == 0 || display.active_surface == nullptr)
        return false;
    // The paths are kept whole, however long the directory is; with no memory
    // to spell them, nothing is saved.
    std::string path;
    try {
        const std::string stem = std::string(directory) + separator + prefix;
        path = stem + "*.pcx";
        HighestFrame highest{std::strlen(prefix), 0};
        if (host.list != nullptr)
            host.list(host.context, path.c_str(), keep_highest_frame, &highest);
        char number[numbered_file_number_bytes];
        std::snprintf(number, sizeof number, "%04i", highest.highest + 1);
        path = stem + number + ".pcx";
    } catch (const std::bad_alloc&) {
        return false;
    }
    ByteStream stream{};
    if (host.open == nullptr || !host.open(host.context, path.c_str(), &stream))
        return false;
    const PcxStatus status = save_pcx_surface(stream, display, *display.active_surface);
    if (stream.close != nullptr)
        stream.close(stream.user);
    return status == PcxStatus::ok;
}

PcxStatus load_pcx_surface(ByteStream& stream, SurfaceBuffer& surface, Palette* palette) {
    uint8_t header[pcx_header_size]{};
    if (stream_read(stream, header, pcx_header_size) != pcx_header_size)
        return PcxStatus::short_header;
    if (!has_signature(header))
        return PcxStatus::bad_signature;
    int32_t width = 0;
    int32_t height = 0;
    if (!header_extent(header, width, height))
        return PcxStatus::bad_bounds;
    surface = create_surface(width, height);
    if (!stream_seek(stream, 0))
        return PcxStatus::short_header;
    PcxImage image;
    const PcxStatus status = load_pcx_image(stream, image);
    if (status != PcxStatus::ok)
        return status;
    std::memcpy(surface.pixels.data(), image.pixels.data(), image.pixels.size());
    if (palette) {
        for (int i = 0; i < OA_PALETTE_COLORS; ++i) {
            palette->entries[i].r = image.color_map[i * rgb_triple];
            palette->entries[i].g = image.color_map[i * rgb_triple + 1];
            palette->entries[i].b = image.color_map[i * rgb_triple + 2];
            palette->entries[i].flags = 0;
        }
    }
    return PcxStatus::ok;
}

PcxStatus load_pcx_palette(ByteStream& stream, Palette& palette) {
    uint8_t header[pcx_header_size]{};
    if (stream_read(stream, header, pcx_header_size) != pcx_header_size)
        return PcxStatus::short_header;
    if (!has_signature(header))
        return PcxStatus::bad_signature;
    if (!stream_seek(stream, 0))
        return PcxStatus::short_header;
    PcxImage image;
    const PcxStatus status = load_pcx_image(stream, image);
    if (status != PcxStatus::ok && status != PcxStatus::truncated_pixels)
        return status;
    for (int i = 0; i < OA_PALETTE_COLORS; ++i) {
        palette.entries[i].r = image.color_map[i * rgb_triple];
        palette.entries[i].g = image.color_map[i * rgb_triple + 1];
        palette.entries[i].b = image.color_map[i * rgb_triple + 2];
        palette.entries[i].flags = 0;
    }
    return PcxStatus::ok;
}

void bmp_strip_writer_init(BmpStripWriter& writer) noexcept {
    writer.stream = nullptr;
}

void bmp_strip_writer_close(BmpStripWriter& writer) noexcept {
    if (writer.stream && writer.stream->close)
        writer.stream->close(writer.stream->user);
}

bool bmp_strip_writer_begin(
    BmpStripWriter& writer,
    ByteStream* stream,
    const DisplayContext& display,
    int32_t width,
    int32_t height
) noexcept {
    writer.width = width;
    writer.height = height;
    writer.stream = stream;
    if (!stream)
        return false;
    uint8_t file_header[bmp_file_header_size]{};
    store_le16(file_header, bmp_signature);
    store_le32(file_header + 10, static_cast<uint32_t>(bmp_pixel_offset));
    if (stream_write(*stream, file_header, bmp_file_header_size) != bmp_file_header_size)
        return false;
    uint8_t info[bmp_info_header_size + bmp_color_table_size]{};
    store_le32(info, bmp_info_header_size);
    store_le32(info + 4, static_cast<uint32_t>(width));
    store_le32(info + 8, static_cast<uint32_t>(height));
    store_le16(info + 12, bmp_planes);
    store_le16(info + 14, bmp_bits_per_pixel);
    store_le32(info + 24, bmp_pixels_per_meter);
    store_le32(info + 28, bmp_pixels_per_meter);
    store_le32(info + 32, bmp_palette_colors);
    store_le32(info + 36, bmp_palette_colors);
    uint8_t* quad = info + bmp_info_header_size;
    for (const PaletteEntry& entry : display.palette.entries) {
        quad[0] = entry.b;
        quad[1] = entry.g;
        quad[2] = entry.r;
        quad += 4;
    }
    constexpr int32_t info_size = bmp_info_header_size + bmp_color_table_size;
    if (stream_write(*stream, info, info_size) != info_size)
        return false;
    writer.data_offset = stream->tell ? stream->tell(stream->user) : -1;
    return true;
}

bool bmp_strip_writer_write_rows(
    BmpStripWriter& writer, const Surface& surface, int32_t rows, int32_t y, int32_t source_row
) noexcept {
    if (!writer.stream)
        return false;
    ByteStream& stream = *writer.stream;
    const int32_t stride = (surface.width + bmp_row_alignment - 1) & ~(bmp_row_alignment - 1);
    if (!stream_seek(stream, (writer.height - rows - y) * stride + writer.data_offset))
        return false;
    std::vector<uint8_t> padded(static_cast<std::size_t>(std::max(stride, 0)), 0);
    const auto copied = static_cast<std::size_t>(std::clamp(surface.pitch, 0, std::max(stride, 0)));
    for (int32_t i = rows - 1; i >= 0; --i) {
        const uint8_t* row =
            surface.pixels + static_cast<std::ptrdiff_t>(surface.pitch) * (i + source_row);
        // Bytes past the pitch would come from the next row; padded with zero.
        std::memcpy(padded.data(), row, copied);
        if (stream_write(stream, padded.data(), stride) != stride)
            return false;
    }
    return true;
}

bool write_surface_rows(ByteStream& stream, const Surface& surface) noexcept {
    if (stream.seek == nullptr || stream.write == nullptr || stream.seek(stream.user, 0) != 0) {
        return false;
    }
    uint8_t header[8];
    for (int i = 0; i < 4; ++i) {
        header[i] = static_cast<uint8_t>(static_cast<uint32_t>(surface.width) >> (8 * i));
        header[4 + i] = static_cast<uint8_t>(static_cast<uint32_t>(surface.height) >> (8 * i));
    }
    bool ok =
        stream.write(stream.user, header, sizeof header) == static_cast<int32_t>(sizeof header);
    for (int32_t row = 0; row < surface.height; ++row) {
        const uint8_t* pixels = surface.pixels + static_cast<std::ptrdiff_t>(row) * surface.pitch;
        ok = stream.write(stream.user, pixels, surface.width) == surface.width && ok;
    }
    return ok;
}

bool read_surface_rows(ByteStream& stream, SurfaceBuffer& surface) {
    if (stream.seek == nullptr || stream.read == nullptr || stream.seek(stream.user, 0) != 0) {
        return false;
    }
    uint8_t header[8];
    if (stream.read(stream.user, header, sizeof header) < static_cast<int32_t>(sizeof header)) {
        return false;
    }
    const auto width = static_cast<int32_t>(
        header[0] | (header[1] << 8) | (header[2] << 16) | (static_cast<uint32_t>(header[3]) << 24)
    );
    const auto height = static_cast<int32_t>(
        header[4] | (header[5] << 8) | (header[6] << 16) | (static_cast<uint32_t>(header[7]) << 24)
    );
    if (width < 0 || height < 0 || static_cast<int64_t>(width) * height > pcx_max_pixels) {
        return false;
    }
    // An image with no pixels is empty, whatever its other side claims.
    if (width == 0 || height == 0) {
        surface = SurfaceBuffer{};
        return true;
    }
    surface = create_surface(width, height);
    for (int32_t row = 0; row < height; ++row) {
        uint8_t* pixels =
            surface.surface.pixels + static_cast<std::ptrdiff_t>(row) * surface.surface.pitch;
        if (stream.read(stream.user, pixels, width) < width) {
            surface = SurfaceBuffer{};
            return false;
        }
    }
    return true;
}

} // namespace oa::present
