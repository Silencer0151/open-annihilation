// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// 8-bit PCX images (screenshots, backgrounds, palettes) and the streamed BMP
// screenshot writer. Images are read and written through the ByteStream
// boundary below.

#include "oa/present/display.hpp"
#include "oa/present/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::present {

inline constexpr int32_t pcx_header_size = 0x80;
inline constexpr int32_t pcx_color_map_size = 0x300; // 256 packed RGB triples
inline constexpr uint8_t pcx_manufacturer = 0x0A;
inline constexpr uint8_t pcx_version = 5;
inline constexpr uint8_t pcx_run_marker = 0xC0; // top two bits flag a run byte
inline constexpr uint8_t pcx_run_length_mask = 0x3F;
inline constexpr uint8_t pcx_palette_marker = 0x0C; // precedes the trailing color map
// Loader bound; the game's largest image is 640x480.
inline constexpr int64_t pcx_max_pixels = 16 * 1024 * 1024;

// Sequential byte stream. Every callback is optional; a missing one fails.
struct ByteStream {
    void* user = nullptr;
    // Copies up to `size` bytes to `data`; returns the count copied.
    int32_t (*read)(void* user, void* data, int32_t size) = nullptr;
    // Writes `size` bytes; returns the count written.
    int32_t (*write)(void* user, const void* data, int32_t size) = nullptr;
    // Moves to an absolute position; returns 0 on success.
    int32_t (*seek)(void* user, int32_t position) = nullptr;
    // Current position, or -1.
    int32_t (*tell)(void* user) = nullptr;
    // Total length, or -1.
    int32_t (*length)(void* user) = nullptr;
    // Releases the stream.
    void (*close)(void* user) = nullptr;
};

// Read-only stream over caller-owned bytes.
struct MemoryReader {
    std::span<const uint8_t> data;
    std::size_t position = 0;
};

// Growable stream; seeking past the end zero-fills. `limit` bounds growth.
struct MemoryWriter {
    std::vector<uint8_t> bytes;
    std::size_t position = 0;
    std::size_t limit = 64 * 1024 * 1024;
};

/// Returns a read-only stream (read, seek, tell, length) over a memory reader.
///
/// @param[in,out] reader reader whose position the stream advances; must outlive the stream
/// @return the stream; it has no write or close callback
[[nodiscard]] ByteStream memory_reader_stream(MemoryReader& reader) noexcept;

/// Returns a write stream (write, seek, tell, length) over a growable memory writer.
///
/// Writes past `limit` fail; seeking past the end zero-fills.
///
/// @param[in,out] writer writer that receives the bytes; must outlive the stream
/// @return the stream; it has no read or close callback
[[nodiscard]] ByteStream memory_writer_stream(MemoryWriter& writer) noexcept;

enum class PcxStatus : uint8_t {
    ok,
    no_stream,
    short_header,
    bad_signature,   // manufacturer/version bytes are not 0x0A 0x05
    bad_bounds,      // max < min, or the image exceeds pcx_max_pixels
    short_color_map, // stream shorter than header + color map
    truncated_pixels,
    write_failed,
};

/// Returns a one-line description of a PCX status.
///
/// @param status status to describe
/// @return static text; "unknown PCX status" for values outside the enum
[[nodiscard]] const char* pcx_status_text(PcxStatus status) noexcept;

// Decoded image: pixels, color map, width and height.
struct PcxImage {
    std::vector<uint8_t> pixels; // width * height, pitch == width
    uint8_t color_map[pcx_color_map_size]{};
    int32_t width = 0;
    int32_t height = 0;
};

/// Decodes one run-length encoded row of pixels.
///
/// A run longer than the remaining width is clipped.
///
/// @param[in,out] stream stream positioned at the row
/// @param[out] row `width` output pixels
/// @param width row width in pixels
/// @return false when the stream ends early
bool decode_pcx_row(ByteStream& stream, uint8_t* row, int32_t width) noexcept;

/// Writes one run of copies of a value.
///
/// A single byte below 0xC0 is written literally; everything else, even a
/// run of two, becomes marker/value pairs of at most 63 pixels.
///
/// @param[in,out] stream output stream
/// @param value pixel value
/// @param count run length in pixels
/// @return bytes produced (write failures are not detected)
int32_t encode_pcx_run(ByteStream& stream, uint8_t value, int32_t count) noexcept;

/// Run-length encodes one row.
///
/// @param[in,out] stream output stream
/// @param row row pixels
/// @param width row width in pixels
/// @return bytes produced (write failures are not detected)
/// @quirk row[0] is always read and emitted, even for a width below 1.
int32_t encode_pcx_row(ByteStream& stream, const uint8_t* row, int32_t width) noexcept;

/// Reads a PCX header, the trailing color map and every row from a stream.
///
/// Only the signature bytes are checked: encoding, depth, planes and
/// bytes-per-line are ignored, and rows are decoded `width` bytes wide.
///
/// @param[in,out] stream seekable stream with a length callback, positioned at the header
/// @param[out] image decoded image; reset first
/// @return ok, or the first check that failed
/// @quirk The color map is the last 0x300 bytes whether or not a 0x0C marker
///     precedes it.
[[nodiscard]] PcxStatus load_pcx_image(ByteStream& stream, PcxImage& image);

/// Writes an 8-bit PCX: header, encoded rows, 0x0C marker and color map.
///
/// @param[in,out] stream output stream
/// @param pixels width * height pixels, rows `width` bytes apart
/// @param width width in pixels
/// @param height height in rows
/// @param color_map 256 packed RGB triples
/// @return ok, or write_failed when the header or color map write fails
/// @quirk The DPI fields carry width and height, and the header's 16-colour
///     map is the first 48 bytes of `color_map`.
[[nodiscard]] PcxStatus write_pcx_image(
    ByteStream& stream,
    const uint8_t* pixels,
    int32_t width,
    int32_t height,
    const uint8_t* color_map
) noexcept;

/// Saves a surface as a PCX with the display palette.
///
/// @param[in,out] stream output stream
/// @param display display whose palette becomes the color map
/// @param surface surface to save
/// @return the write_pcx_image status
/// @quirk Rows are taken `width` bytes apart; the surface pitch is ignored.
[[nodiscard]] PcxStatus save_pcx_surface(
    ByteStream& stream, const DisplayContext& display, const Surface& surface
) noexcept;

// Directory boundary of the numbered writer: the find-first/next walk over a
// wildcard path and the file the picture goes to.
struct NumberedFileHost {
    void* context = nullptr;
    // Calls `visit` with each name (no directory part) matching `pattern`.
    void (*list)(
        void* context, const char* pattern, void (*visit)(void* user, const char* name), void* user
    ) = nullptr;
    // Opens `path` for writing; false when it cannot be created.
    bool (*open)(void* context, const char* path, ByteStream* stream) = nullptr;
};

inline constexpr std::size_t numbered_pcx_path_bytes = 0x104;

/// Writes the display's active surface as the PCX numbered one past the highest already there.
///
/// Existing <directory><prefix>*.pcx names are listed and the new file is
/// <directory><prefix>NNNN.pcx, with a backslash added after a directory that
/// lacks one. The stream is closed afterwards.
///
/// @param display display whose active surface and palette are saved
/// @param directory target directory; may be empty
/// @param prefix file name prefix
/// @param host directory listing and file creation boundary
/// @return false while the display draws to its back buffer, when the file
///     cannot be opened or the write fails
/// @quirk The number is read with atoi right after the prefix, so names whose
///     digits do not follow the prefix count as 0.
bool save_numbered_pcx(
    const DisplayContext& display,
    const char* directory,
    const char* prefix,
    const NumberedFileHost& host
) noexcept;

/// Loads a PCX into a new surface and optionally returns its palette.
///
/// @param[in,out] stream seekable stream positioned at the header
/// @param[out] surface new surface of the image's size; allocated once the
///     header is valid, filled only on success
/// @param[out] palette receives the color map with zero flags; may be null
/// @return ok, or the first check that failed
[[nodiscard]] PcxStatus
load_pcx_surface(ByteStream& stream, SurfaceBuffer& surface, Palette* palette);

/// Loads a PCX only for its color map, expanded to palette entries.
///
/// @param[in,out] stream seekable stream positioned at the header
/// @param[out] palette receives the color map with zero flags
/// @return ok (also when the pixel rows are truncated), or the first header
///     or color map check that failed
[[nodiscard]] PcxStatus load_pcx_palette(ByteStream& stream, Palette& palette);

// Streamed bottom-up 8-bit BMP written in horizontal strips (screenshots).
struct BmpStripWriter {
    int32_t width = 0;
    int32_t height = 0;
    int32_t data_offset = 0; // stream position of the first pixel byte
    ByteStream* stream = nullptr;
};

inline constexpr int32_t bmp_file_header_size = 14;
inline constexpr int32_t bmp_info_header_size = 40;
inline constexpr int32_t bmp_color_table_size = 0x400;
inline constexpr int32_t bmp_pixel_offset =
    bmp_file_header_size + bmp_info_header_size + bmp_color_table_size;
inline constexpr int32_t bmp_pixels_per_meter = 3000;

/// Clears the writer's stream slot.
///
/// @param[out] writer writer to reset
void bmp_strip_writer_init(BmpStripWriter& writer) noexcept;

/// Closes the writer's stream if one is open.
///
/// @param[in,out] writer writer whose stream is closed
/// @quirk The stream slot is not cleared afterwards, as in the game.
void bmp_strip_writer_close(BmpStripWriter& writer) noexcept;

/// Starts a BMP on a stream: file and info headers plus the display palette.
///
/// The file-size field is left zero; each colour-table reserved byte is
/// written as zero. The pixel data offset is taken
/// from the stream position afterwards.
///
/// @param[in,out] writer writer to start; keeps the size and stream
/// @param stream output stream; null fails
/// @param display display whose palette becomes the colour table
/// @param width image width in pixels
/// @param height image height in rows
/// @return false without a stream or when a header write fails
bool bmp_strip_writer_begin(
    BmpStripWriter& writer,
    ByteStream* stream,
    const DisplayContext& display,
    int32_t width,
    int32_t height
) noexcept;

/// Writes a strip of surface rows into the bottom-up image.
///
/// Each row is written as its 4-byte-aligned width from the surface, bytes
/// past the pitch as zero.
///
/// @param[in,out] writer started writer
/// @param surface surface to copy from
/// @param rows number of rows in the strip
/// @param y image row of the strip's first row, counted from the top
/// @param source_row surface row of the strip's first row
/// @return false without a stream or when a seek or write fails
bool bmp_strip_writer_write_rows(
    BmpStripWriter& writer, const Surface& surface, int32_t rows, int32_t y, int32_t source_row
) noexcept;

/// Writes a surface as a u32 width and u32 height header and `height` rows of `width` bytes.
///
/// Writing starts at the stream's first byte. Write failures are reported.
///
/// @param[in,out] stream seekable output stream
/// @param surface surface to write
/// @return false without seek or write callbacks, or on a failed seek or short write
bool write_surface_rows(ByteStream& stream, const Surface& surface) noexcept;

/// Reads a surface written by write_surface_rows.
///
/// @param[in,out] stream seekable input stream; read from its first byte
/// @param[out] surface new surface; empty on a short row
/// @return false without seek or read callbacks, on a short header or row, or
///     for a size that is negative or above pcx_max_pixels
[[nodiscard]] bool read_surface_rows(ByteStream& stream, SurfaceBuffer& surface);

} // namespace oa::present
