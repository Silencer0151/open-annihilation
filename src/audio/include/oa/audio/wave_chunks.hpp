// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace oa::audio {

// Read cursor over a whole sound file held in memory. Reads past the end copy
// what remains and leave the rest of the destination untouched.
struct WaveCursor {
    const uint8_t* data{};
    uint32_t size{};
    uint32_t position{};
};

/// Creates a cursor at the start of a file held in memory.
///
/// @param data File bytes; must outlive the cursor.
/// @param size File length in bytes.
/// @return Cursor at position 0.
[[nodiscard]] WaveCursor wave_cursor(const uint8_t* data, uint32_t size) noexcept;

/// Moves the cursor to an absolute position; positions past the end are allowed.
///
/// @param[in,out] cursor Cursor to move.
/// @param position Byte offset from the start of the file.
void wave_seek(WaveCursor& cursor, uint32_t position) noexcept;

/// Copies bytes from the cursor and advances it.
///
/// @param[in,out] cursor Cursor to read from.
/// @param[out] destination Buffer of at least `bytes` bytes; bytes beyond the
///             end of the file are left untouched.
/// @param bytes Number of bytes wanted.
/// @return Number of bytes copied.
uint32_t wave_read(WaveCursor& cursor, void* destination, uint32_t bytes) noexcept;

/// Returns the bytes left after the cursor.
///
/// @param cursor Cursor to query.
/// @return Remaining byte count, 0 at or past the end.
[[nodiscard]] uint32_t wave_remaining(const WaveCursor& cursor) noexcept;

// Container layouts recognised by the sound loader.
enum class WaveContainer : int32_t {
    raw = 0,  // headerless 11025 Hz, 8-bit, mono
    digi = 1, // DIGI/HSHD/SDAT: rate at 0x16, samples at 0x28
    riff = 2, // RIFF/WAVE with "fmt " and "data" chunks
};

inline constexpr uint32_t default_sample_rate = 11025;
// The DIGI header records 11025 Hz material as 11000.
inline constexpr uint32_t digi_nominal_rate = 11000;
inline constexpr uint32_t digi_rate_offset = 0x16;
inline constexpr uint32_t digi_samples_offset = 0x28;
inline constexpr uint32_t riff_minimum_format_bytes = 0x10;

struct PcmFormat {
    uint32_t sample_rate{};
    uint16_t bits{};
    uint16_t channels{};
};

// Format and sample span chosen by the loader. data_bytes is not checked
// against the file length; the consumer rejects short reads.
struct WaveLayout {
    WaveContainer container{WaveContainer::raw};
    PcmFormat format{};
    uint32_t data_offset{};
    uint32_t data_bytes{};
};

/// Classifies the container from its leading tags.
///
/// @param[in,out] cursor Cursor over the file; left after the last tag read.
/// @return The container kind; raw when no tags match.
/// @quirk A DIGI file whose later tags do not match is tested against RIFF
///        using the last tag read.
[[nodiscard]] WaveContainer detect_wave_container(WaveCursor& cursor) noexcept;

/// Finds a RIFF chunk by tag, walking the chunk list from offset 12.
///
/// @param[in,out] cursor Cursor over the file; left at the matching chunk's payload.
/// @param tag Four-character chunk tag.
/// @return Size of the first matching chunk, 0 when absent.
/// @quirk Chunks are not padded to even sizes.
[[nodiscard]] uint32_t find_wave_chunk(WaveCursor& cursor, const char tag[4]) noexcept;

/// Detects the container, reads its format, and leaves the cursor at the first sample.
///
/// The format-selection half of the sound loader.
///
/// @param[in,out] cursor Cursor over the whole file.
/// @param[out] layout Container, format and sample span.
/// @return False for a RIFF file with a short "fmt " chunk or an empty "data" chunk.
[[nodiscard]] bool describe_wave(WaveCursor& cursor, WaveLayout& layout) noexcept;

} // namespace oa::audio
