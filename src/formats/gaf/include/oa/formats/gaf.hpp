// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::formats::gaf {

// TA's GAF version word. The game does not reject other values, and stock
// GAFs also store zero, so parse() retains the header word verbatim.
inline constexpr uint32_t version_ta = 0x00010100U;

// On-disk records: header, sequence header, frame-list item and frame info.
// Multi-byte fields are little-endian.
namespace on_disk {

inline constexpr std::size_t sequence_name_bytes = 32;

struct Header {
    uint32_t version = 0;
    uint32_t sequence_count = 0;
    // Zero in every shipped GAF. Kept as read; 3.1c does not interpret it.
    uint32_t reserved = 0;
};

struct SequenceHeader {
    uint16_t frame_count = 0;
    uint16_t repeat_flags = 0;
    // Zero in every shipped GAF. Kept as read and not interpreted.
    uint32_t reserved = 0;
    std::array<uint8_t, sequence_name_bytes> name{};
};

struct FrameListItem {
    uint32_t frame_offset = 0;
    // Full on-disk word. The game observes only the low 16 bits.
    uint32_t duration = 0;
};

struct FrameInfo {
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    uint8_t transparency_index = 0;
    uint8_t compressed = 0;
    // Two separate bytes, not one uint16 layer count: the game takes byte 10
    // as the layer count and byte 11 as a child's special-render flag.
    uint8_t layer_count = 0;
    uint8_t special_render_flag = 0;
    // Zero in every shipped GAF. Kept as read and not interpreted.
    uint32_t reserved = 0;
    uint32_t data_offset = 0;
    // ? The word a loaded frame keeps its auxiliary plane in. The file's value
    // is not an offset and varies between shipped frames; kept as read and
    // not interpreted.
    uint32_t aux_plane_slot = 0;
};

} // namespace on_disk

namespace layout {
inline constexpr std::size_t header_bytes = sizeof(on_disk::Header);
inline constexpr std::size_t sequence_header_bytes = sizeof(on_disk::SequenceHeader);
inline constexpr std::size_t sequence_name_bytes = on_disk::sequence_name_bytes;
inline constexpr std::size_t frame_list_item_bytes = sizeof(on_disk::FrameListItem);
inline constexpr std::size_t frame_record_bytes = sizeof(on_disk::FrameInfo);
inline constexpr std::size_t frame_layer_count_offset = offsetof(on_disk::FrameInfo, layer_count);
inline constexpr std::size_t frame_special_render_offset =
    offsetof(on_disk::FrameInfo, special_render_flag);
} // namespace layout

static_assert(layout::header_bytes == 12);
static_assert(layout::sequence_header_bytes == 40);
static_assert(layout::sequence_name_bytes == 32);
static_assert(layout::frame_list_item_bytes == 8);
static_assert(layout::frame_record_bytes == 24);
static_assert(layout::frame_layer_count_offset == 10);
static_assert(layout::frame_special_render_offset == 11);
static_assert(offsetof(on_disk::Header, sequence_count) == 4);
static_assert(offsetof(on_disk::SequenceHeader, name) == 8);
static_assert(offsetof(on_disk::Header, reserved) == 8);
static_assert(offsetof(on_disk::SequenceHeader, reserved) == 4);
static_assert(offsetof(on_disk::FrameListItem, duration) == 4);
static_assert(offsetof(on_disk::FrameInfo, reserved) == 12);
static_assert(offsetof(on_disk::FrameInfo, data_offset) == 16);
static_assert(offsetof(on_disk::FrameInfo, aux_plane_slot) == 20);

namespace limit {
inline constexpr std::size_t input_bytes = 256U * 1024U * 1024U;
inline constexpr std::size_t sequences = 4096;
inline constexpr std::size_t frames_per_sequence = 4096;
inline constexpr std::size_t total_frame_records = 131072;
inline constexpr std::size_t nesting_depth = 32;
inline constexpr std::size_t pixels_per_frame = 64U * 1024U * 1024U;
inline constexpr std::size_t total_decoded_bytes = 256U * 1024U * 1024U;
} // namespace limit

struct Frame {
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    uint8_t transparency_index = 0;
    bool compressed = false;
    uint8_t layer_count = 0;
    // Frame byte 11. The game draws a child frame with this byte set with
    // special blending, which this renderer does not implement.
    uint8_t special_render_flag = 0;
    uint32_t reserved = 0;       // on_disk::FrameInfo::reserved, as read
    uint32_t aux_plane_slot = 0; // ? on_disk::FrameInfo::aux_plane_slot, as read
    uint16_t duration = 0;
    // Simple frames contain width*height palette indices. Composite frames
    // contain layers instead. Coverage distinguishes compressed transparent
    // skip commands from literal pixels equal to the transparency index.
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> coverage;
    std::vector<Frame> layers;
};

struct Sequence {
    std::string name;
    uint16_t repeat_flags = 0;
    uint32_t reserved = 0; // on_disk::SequenceHeader::reserved, as read
    std::vector<Frame> frames;
};

struct Archive {
    uint32_t version = 0;
    uint32_t raw_sequence_count = 0;
    int16_t header_sequence_count = 0;
    uint32_t reserved = 0; // on_disk::Header::reserved, as read
    std::vector<Sequence> sequences;
};

enum class ErrorCode {
    none,
    input_limit,
    truncated,
    offset_out_of_range,
    sequence_limit,
    frame_limit,
    nesting_limit,
    pointer_cycle,
    pixel_limit,
    malformed_compression,
    unsupported_special_render,
};

struct Error {
    ErrorCode code = ErrorCode::none;
    std::size_t offset = 0;
    std::string message;
};

struct ParseResult {
    std::optional<Archive> archive;
    std::optional<Error> error;

    /// Returns whether an archive was parsed.
    [[nodiscard]] bool ok() const noexcept { return archive.has_value(); }
};

struct RenderedFrame {
    uint16_t width = 0;
    uint16_t height = 0;
    int16_t origin_x = 0;
    int16_t origin_y = 0;
    uint8_t transparency_index = 0;
    std::vector<uint8_t> pixels;
    // One byte per pixel. A set byte means the normal renderer performed an
    // overwrite at that position, including compressed literal/repeat values
    // equal to transparency_index.
    std::vector<uint8_t> coverage;
};

struct RenderResult {
    std::optional<RenderedFrame> frame;
    std::optional<Error> error;

    /// Returns whether a frame was rendered.
    [[nodiscard]] bool ok() const noexcept { return frame.has_value(); }
};

/// Parses the pointer-based GAF structure.
///
/// The sequence count is the signed low 16 bits of the header word, frame
/// counts are uint16 and byte 10 of a frame is its layer count. Pixel data
/// is decoded with a coverage mask; special-render blending is deferred to
/// the renderer. Every offset, row, run, pointer cycle and size is checked
/// against the limit namespace.
///
/// @param bytes the whole file
/// @return the archive, or the first error and its byte offset
[[nodiscard]] ParseResult parse(std::span<const uint8_t> bytes);

/// Returns one frame of a sequence.
///
/// @param sequence parsed sequence, or null
/// @param index frame index; compared signed against the uint16 frame count
/// @return the frame, or null for a negative or too-large index or a null sequence
[[nodiscard]] const Frame* frame_at(const Sequence* sequence, int32_t index) noexcept;

/// Renders a frame and its layers as the game draws them normally.
///
/// Layers are drawn recursively into a frame-sized palette-index canvas with
/// signed origins subtracted from the parent's hotspot and clipped to it. The
/// frame's own special-render flag is ignored, as 3.1c ignores it when it
/// draws a frame.
///
/// @param frame parsed frame
/// @return pixels and coverage, or unsupported_special_render when a child
///         needs special blending
[[nodiscard]] RenderResult render_normal(const Frame& frame);

} // namespace oa::formats::gaf
