// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// GAF animation files as presentation records. The file's 32-bit offsets
// become pointers: the relocated records are built beside a copy of the file,
// pixels and row-RLE streams stay in the copy, and each frame or child header
// becomes a Sprite pointing into it.

#include "oa/present/surface.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::present {

inline constexpr int32_t gaf_name_size = 32;
inline constexpr size_t gaf_header_size = 0x0C;
inline constexpr size_t gaf_sequence_size = 0x28; // frame slots follow the record
inline constexpr size_t gaf_frame_slot_size = 8;
inline constexpr size_t gaf_frame_header_size = 0x18;
// Loader bounds; the game's largest GAF is 5.5 MB with 5183 headers, and a
// mod's effects file can pass 80 MB. The byte bound is the GAF reader's
// input bound (oa::formats::gaf::limit::input_bytes).
inline constexpr size_t gaf_max_bytes = 256 * 1024 * 1024;
inline constexpr size_t gaf_max_headers = 256 * 1024;

// One entry of a sequence's frame list.
struct GafFrameSlot {
    Sprite* frame = nullptr;
    uint32_t duration = 0; // ticks; readers use the low word
};

struct GafSequence {
    uint16_t frame_count = 0;
    // Loaded as stored. A non-zero low byte makes the sequence loop when a
    // feature animation plays it (see formats::gaf::Sequence::repeat_flags).
    uint16_t repeat_flags = 0;
    uint32_t reserved_after_repeat_flags = 0; // no known use: loaded as stored, never read
    char name[gaf_name_size]{};               // NUL-padded
    GafFrameSlot* frames = nullptr;           // frame_count slots
};

// The records point into this record's own vectors: moving it keeps them
// valid, copying it does not.
struct GafSprites {
    uint32_t version = 0;
    uint32_t sequence_count = 0;                // only the signed low word is walked
    uint32_t reserved_after_sequence_count = 0; // no known use: loaded as stored, never read
    std::vector<uint8_t> bytes;                 // the file
    std::vector<GafSequence> sequences;         // one per walked entry
    std::vector<GafFrameSlot> slots;
    std::vector<Sprite> sprites;   // frame and child headers
    std::vector<Sprite*> children; // composite child tables
};

enum class GafStatus : uint8_t {
    ok,
    too_large, // above gaf_max_bytes or gaf_max_headers
    short_header,
    sequence_outside, // a sequence record or its frame list leaves the file
    header_outside,   // a frame or child header leaves the file
    children_outside, // a child table leaves the file
    shared_header,    // a header reached twice
    nested_children,  // a child with children of its own
    pixels_outside,   // raw pixels leave the file
    malformed_rows,   // a row-RLE stream leaves the file or a row stops short of its width
};

/// Returns a one-line description of a GAF load status.
///
/// @param status status to describe
/// @return static text; "unknown GAF status" for values outside the enum
[[nodiscard]] const char* gaf_status_text(GafStatus status) noexcept;

/// Relocates a loaded GAF into sequences, frame slots and sprites.
///
/// The entry table after the file header names each sequence, and each frame
/// slot names a frame header whose data offset leads to its pixels, its
/// row-RLE stream or, with a non-zero child count, a table of child headers
/// whose own data offsets are relocated too. Only the signed low word of the
/// header's sequence count is walked, and children are relocated one level
/// deep. The frame header's last word is not an offset and never becomes
/// `aux`. The caller supplies the bytes, and every offset is checked before
/// use.
///
/// @param file GAF file bytes; copied into `gaf`
/// @param[out] gaf relocated records; reset first and left partly built on failure
/// @return ok, or the first check that failed
[[nodiscard]] GafStatus relocate_gaf(std::span<const uint8_t> file, GafSprites& gaf);

/// Returns one frame of a sequence.
///
/// @param sequence sequence to read; may be null
/// @param index frame index
/// @return the frame, or null for a null sequence or an index outside [0, frame_count)
[[nodiscard]] Sprite* gaf_frame(const GafSequence* sequence, int32_t index) noexcept;

/// Finds a sequence by name.
///
/// @param gaf relocated GAF
/// @param name sequence name, compared ignoring ASCII case within the 32-byte
///     name field; null finds nothing
/// @return the first matching sequence, or null
[[nodiscard]] const GafSequence*
find_gaf_sequence(const GafSprites& gaf, const char* name) noexcept;

} // namespace oa::present
