// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/gaf.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <limits>
#include <string>
#include <unordered_set>
#include <utility>

namespace oa::formats::gaf {
namespace {

// Row-command bits of compressed frames. Bit 0 is a transparent skip. Otherwise
// bit 1 selects a repeated byte; a clear pair is a literal run. Counted runs
// store length-1 in the high bits.
namespace compression {
constexpr uint8_t transparent_flag = 0x01;
constexpr uint8_t repeat_flag = 0x02;
constexpr unsigned transparent_shift = 1;
constexpr unsigned counted_shift = 2;
constexpr std::size_t counted_bias = 1;
} // namespace compression

constexpr std::size_t pointer_bytes = sizeof(uint32_t);
constexpr std::size_t row_length_bytes = sizeof(uint16_t);

[[nodiscard]] bool fits(std::size_t offset_value, std::size_t count, std::size_t size) noexcept {
    return offset_value <= size && count <= size - offset_value;
}

[[nodiscard]] uint16_t le16(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<uint16_t>(bytes[at]) |
           static_cast<uint16_t>(static_cast<uint16_t>(bytes[at + 1]) << 8U);
}

[[nodiscard]] uint32_t le32(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    return static_cast<uint32_t>(bytes[at]) | (static_cast<uint32_t>(bytes[at + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[at + 2]) << 16U) |
           (static_cast<uint32_t>(bytes[at + 3]) << 24U);
}

[[nodiscard]] int16_t signed16(uint16_t value) noexcept {
    return std::bit_cast<int16_t>(value);
}

class ByteCursor {
  public:

    ByteCursor(std::span<const uint8_t> bytes, std::size_t at) noexcept : bytes_(bytes), at_(at) {}

    [[nodiscard]] uint8_t u8() noexcept { return bytes_[at_++]; }

    [[nodiscard]] uint16_t u16() noexcept {
        const auto value = le16(bytes_, at_);
        at_ += sizeof(uint16_t);
        return value;
    }

    [[nodiscard]] uint32_t u32() noexcept {
        const auto value = le32(bytes_, at_);
        at_ += sizeof(uint32_t);
        return value;
    }

    [[nodiscard]] int16_t i16() noexcept { return signed16(u16()); }

    void copy(std::span<uint8_t> out) noexcept {
        std::copy_n(
            bytes_.begin() + static_cast<std::ptrdiff_t>(at_),
            static_cast<std::ptrdiff_t>(out.size()),
            out.begin()
        );
        at_ += out.size();
    }

  private:

    std::span<const uint8_t> bytes_;
    std::size_t at_ = 0;
};

/// Decodes the file header; the caller has checked that it fits.
[[nodiscard]] on_disk::Header read_header(std::span<const uint8_t> bytes) noexcept {
    ByteCursor cursor(bytes, 0);
    on_disk::Header header;
    header.version = cursor.u32();
    header.sequence_count = cursor.u32();
    header.reserved = cursor.u32();
    return header;
}

/// Decodes the sequence header at `at`; the caller has checked that it fits.
[[nodiscard]] on_disk::SequenceHeader
read_sequence_header(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    ByteCursor cursor(bytes, at);
    on_disk::SequenceHeader header;
    header.frame_count = cursor.u16();
    header.repeat_flags = cursor.u16();
    header.reserved = cursor.u32();
    cursor.copy(header.name);
    return header;
}

[[nodiscard]] on_disk::FrameListItem
read_frame_list_item(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    ByteCursor cursor(bytes, at);
    on_disk::FrameListItem item;
    item.frame_offset = cursor.u32();
    item.duration = cursor.u32();
    return item;
}

/// Decodes the frame record at `at`; the caller has checked that it fits.
[[nodiscard]] on_disk::FrameInfo
read_frame_info(std::span<const uint8_t> bytes, std::size_t at) noexcept {
    ByteCursor cursor(bytes, at);
    on_disk::FrameInfo info;
    info.width = cursor.u16();
    info.height = cursor.u16();
    info.origin_x = cursor.i16();
    info.origin_y = cursor.i16();
    info.transparency_index = cursor.u8();
    info.compressed = cursor.u8();
    info.layer_count = cursor.u8();
    info.special_render_flag = cursor.u8();
    info.reserved = cursor.u32();
    info.data_offset = cursor.u32();
    info.aux_plane_slot = cursor.u32();
    return info;
}

class Parser {
  public:

    explicit Parser(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    /// Parses the whole file into an archive, or returns the first error.
    [[nodiscard]] ParseResult run() {
        if (bytes_.size() > limit::input_bytes)
            return fail(ErrorCode::input_limit, 0, "GAF input exceeds 256 MiB limit");
        if (!fits(0, layout::header_bytes, bytes_.size()))
            return fail(ErrorCode::truncated, 0, "truncated GAF header");

        const auto header = read_header(bytes_);
        Archive archive;
        archive.version = header.version;
        archive.raw_sequence_count = header.sequence_count;
        archive.header_sequence_count = signed16(static_cast<uint16_t>(archive.raw_sequence_count));
        archive.reserved = header.reserved;

        // The count is the header word's low 16 bits, signed; zero or less loads none.
        const std::size_t count = archive.header_sequence_count > 0
                                      ? static_cast<std::size_t>(archive.header_sequence_count)
                                      : 0U;
        if (count > limit::sequences)
            return fail(
                ErrorCode::sequence_limit,
                offsetof(on_disk::Header, sequence_count),
                "GAF sequence count exceeds portable limit"
            );
        if (!fits(layout::header_bytes, count * pointer_bytes, bytes_.size())) {
            return fail(
                ErrorCode::truncated, layout::header_bytes, "truncated GAF sequence pointer table"
            );
        }
        archive.sequences.reserve(count);
        for (std::size_t index = 0; index < count; ++index) {
            const auto pointer_at = layout::header_bytes + index * pointer_bytes;
            Sequence sequence;
            const auto sequence_at = static_cast<std::size_t>(le32(bytes_, pointer_at));
            if (!parse_sequence(sequence_at, sequence))
                return ParseResult{std::nullopt, error_};
            archive.sequences.push_back(std::move(sequence));
        }
        return ParseResult{std::move(archive), std::nullopt};
    }

  private:

    std::span<const uint8_t> bytes_;
    std::optional<Error> error_;
    std::size_t total_frame_records_ = 0;
    std::size_t total_decoded_bytes_ = 0;
    std::unordered_set<std::size_t> active_frame_offsets_;

    [[nodiscard]] ParseResult fail(ErrorCode code, std::size_t at, std::string message) {
        return ParseResult{std::nullopt, Error{code, at, std::move(message)}};
    }

    [[nodiscard]] bool set_error(ErrorCode code, std::size_t at, std::string message) {
        error_ = Error{code, at, std::move(message)};
        return false;
    }

    /// Parses the sequence header at `at` and every frame it lists into `output`.
    [[nodiscard]] bool parse_sequence(std::size_t at, Sequence& output) {
        if (!fits(at, layout::sequence_header_bytes, bytes_.size())) {
            return set_error(
                ErrorCode::offset_out_of_range, at, "GAF sequence header is outside input"
            );
        }
        const auto record = read_sequence_header(bytes_, at);
        const auto frame_count = static_cast<std::size_t>(record.frame_count);
        if (frame_count > limit::frames_per_sequence ||
            frame_count > limit::total_frame_records - total_frame_records_) {
            return set_error(ErrorCode::frame_limit, at, "GAF frame count exceeds portable limit");
        }
        const auto table_at = at + layout::sequence_header_bytes;
        if (!fits(table_at, frame_count * layout::frame_list_item_bytes, bytes_.size())) {
            return set_error(ErrorCode::truncated, table_at, "truncated GAF frame table");
        }
        output.repeat_flags = record.repeat_flags;
        output.reserved = record.reserved;
        const auto terminator = std::find(record.name.begin(), record.name.end(), uint8_t{0});
        output.name.assign(
            reinterpret_cast<const char*>(record.name.data()),
            static_cast<std::size_t>(terminator - record.name.begin())
        );
        output.frames.reserve(frame_count);
        for (std::size_t index = 0; index < frame_count; ++index) {
            const auto item_at = table_at + index * layout::frame_list_item_bytes;
            const auto item = read_frame_list_item(bytes_, item_at);
            Frame frame;
            const auto frame_at = static_cast<std::size_t>(item.frame_offset);
            // Only the duration word's low 16 bits are used.
            const auto duration = static_cast<uint16_t>(item.duration);
            if (!parse_frame(frame_at, duration, 1, frame))
                return false;
            output.frames.push_back(std::move(frame));
        }
        return true;
    }

    /// Parses the frame record at `at`, its layers or its pixels into `output`.
    [[nodiscard]] bool
    parse_frame(std::size_t at, uint16_t duration, std::size_t depth, Frame& output) {
        if (depth > limit::nesting_depth)
            return set_error(ErrorCode::nesting_limit, at, "GAF layer nesting exceeds limit");
        if (total_frame_records_ >= limit::total_frame_records)
            return set_error(
                ErrorCode::frame_limit, at, "GAF total frame-record count exceeds limit"
            );
        if (!active_frame_offsets_.insert(at).second)
            return set_error(ErrorCode::pointer_cycle, at, "GAF layer pointer cycle");

        struct ActiveGuard {
            std::unordered_set<std::size_t>& set;
            std::size_t value;

            ~ActiveGuard() { set.erase(value); }
        } guard{active_frame_offsets_, at};

        ++total_frame_records_;
        if (!fits(at, layout::frame_record_bytes, bytes_.size()))
            return set_error(
                ErrorCode::offset_out_of_range, at, "GAF frame record is outside input"
            );

        const auto info = read_frame_info(bytes_, at);
        output.width = info.width;
        output.height = info.height;
        output.origin_x = info.origin_x;
        output.origin_y = info.origin_y;
        output.transparency_index = info.transparency_index;
        output.compressed = info.compressed != 0;
        output.layer_count = info.layer_count;
        output.special_render_flag = info.special_render_flag;
        output.reserved = info.reserved;
        output.aux_plane_slot = info.aux_plane_slot;
        output.duration = duration;
        const auto data_at = static_cast<std::size_t>(info.data_offset);

        const auto pixel_count =
            static_cast<std::size_t>(output.width) * static_cast<std::size_t>(output.height);
        if (pixel_count > limit::pixels_per_frame)
            return set_error(ErrorCode::pixel_limit, at, "GAF frame pixels exceed portable limit");

        if (output.layer_count != 0) {
            const auto layer_count = static_cast<std::size_t>(output.layer_count);
            if (!fits(data_at, layer_count * pointer_bytes, bytes_.size()))
                return set_error(
                    ErrorCode::offset_out_of_range,
                    data_at,
                    "GAF layer pointer table is outside input"
                );
            output.layers.reserve(layer_count);
            for (std::size_t index = 0; index < layer_count; ++index) {
                Frame layer;
                const auto layer_at =
                    static_cast<std::size_t>(le32(bytes_, data_at + index * pointer_bytes));
                if (!parse_frame(layer_at, 0, depth + 1, layer))
                    return false;
                output.layers.push_back(std::move(layer));
            }
            return true;
        }

        constexpr std::size_t decoded_bytes_per_pixel = 2;
        if (pixel_count >
            (limit::total_decoded_bytes - total_decoded_bytes_) / decoded_bytes_per_pixel) {
            return set_error(
                ErrorCode::pixel_limit, at, "GAF decoded buffers exceed portable limit"
            );
        }
        total_decoded_bytes_ += pixel_count * decoded_bytes_per_pixel;
        output.pixels.assign(pixel_count, output.transparency_index);
        output.coverage.assign(pixel_count, 0);
        if (pixel_count == 0)
            return true;
        if (!output.compressed) {
            if (!fits(data_at, pixel_count, bytes_.size()))
                return set_error(ErrorCode::truncated, data_at, "truncated raw GAF pixels");
            std::copy_n(
                bytes_.begin() + static_cast<std::ptrdiff_t>(data_at),
                static_cast<std::ptrdiff_t>(pixel_count),
                output.pixels.begin()
            );
            for (std::size_t index = 0; index < pixel_count; ++index) {
                output.coverage[index] =
                    output.pixels[index] != output.transparency_index ? 1U : 0U;
            }
            return true;
        }
        return decode_compressed(data_at, output);
    }

    /// Decodes the row-compressed pixels at `at` into `output`'s pixels and coverage.
    [[nodiscard]] bool decode_compressed(std::size_t at, Frame& output) {
        std::size_t row_at = at;
        const auto width = static_cast<std::size_t>(output.width);
        for (std::size_t y = 0; y < output.height; ++y) {
            if (!fits(row_at, row_length_bytes, bytes_.size()))
                return set_error(
                    ErrorCode::truncated, row_at, "truncated compressed GAF row length"
                );
            const auto encoded_bytes = static_cast<std::size_t>(le16(bytes_, row_at));
            row_at += row_length_bytes;
            if (!fits(row_at, encoded_bytes, bytes_.size()))
                return set_error(ErrorCode::truncated, row_at, "truncated compressed GAF row data");
            const auto row_end = row_at + encoded_bytes;
            if (encoded_bytes == 0)
                continue;
            std::size_t x = 0;
            while (row_at < row_end && x < width) {
                const auto command = bytes_[row_at++];
                std::size_t count = 0;
                if ((command & compression::transparent_flag) != 0) {
                    count = static_cast<std::size_t>(command >> compression::transparent_shift);
                    x += std::min(count, width - x);
                } else if ((command & compression::repeat_flag) != 0) {
                    count = static_cast<std::size_t>(command >> compression::counted_shift) +
                            compression::counted_bias;
                    if (row_at >= row_end)
                        return set_error(
                            ErrorCode::malformed_compression,
                            row_at - 1,
                            "repeated GAF run lacks its value"
                        );
                    const auto value = bytes_[row_at++];
                    count = std::min(count, width - x);
                    std::fill_n(
                        output.pixels.begin() + static_cast<std::ptrdiff_t>(y * width + x),
                        static_cast<std::ptrdiff_t>(count),
                        value
                    );
                    std::fill_n(
                        output.coverage.begin() + static_cast<std::ptrdiff_t>(y * width + x),
                        static_cast<std::ptrdiff_t>(count),
                        uint8_t{1}
                    );
                    x += count;
                } else {
                    count = static_cast<std::size_t>(command >> compression::counted_shift) +
                            compression::counted_bias;
                    count = std::min(count, width - x);
                    if (count > row_end - row_at)
                        return set_error(
                            ErrorCode::malformed_compression,
                            row_at - 1,
                            "literal GAF run lacks visible bytes"
                        );
                    std::copy_n(
                        bytes_.begin() + static_cast<std::ptrdiff_t>(row_at),
                        static_cast<std::ptrdiff_t>(count),
                        output.pixels.begin() + static_cast<std::ptrdiff_t>(y * width + x)
                    );
                    std::fill_n(
                        output.coverage.begin() + static_cast<std::ptrdiff_t>(y * width + x),
                        static_cast<std::ptrdiff_t>(count),
                        uint8_t{1}
                    );
                    row_at += count;
                    x += count;
                }
            }
            // A zero byte count encodes a fully transparent row. A nonempty
            // row takes commands until they cover the frame's width, and those
            // commands must lie within the row's byte count.
            if (x != width)
                return set_error(
                    ErrorCode::malformed_compression,
                    row_at,
                    "nonempty compressed GAF row does not cover its width"
                );
            row_at = row_end;
        }
        return true;
    }
};

[[nodiscard]] bool render_into(
    const Frame& source,
    RenderedFrame& destination,
    int32_t left,
    int32_t top,
    Error& error,
    std::size_t depth
) {
    if (depth > limit::nesting_depth) {
        error = {ErrorCode::nesting_limit, 0, "GAF render nesting exceeds limit"};
        return false;
    }
    if (source.layer_count != source.layers.size()) {
        error = {ErrorCode::frame_limit, 0, "GAF model layer count does not match layers"};
        return false;
    }
    if (!source.layers.empty()) {
        for (const auto& layer : source.layers) {
            if (layer.special_render_flag != 0) {
                error = {
                    ErrorCode::unsupported_special_render,
                    0,
                    "GAF child requires unsupported special blending"
                };
                return false;
            }
            const auto child_left =
                left + static_cast<int32_t>(source.origin_x) - static_cast<int32_t>(layer.origin_x);
            const auto child_top =
                top + static_cast<int32_t>(source.origin_y) - static_cast<int32_t>(layer.origin_y);
            if (!render_into(layer, destination, child_left, child_top, error, depth + 1))
                return false;
        }
        return true;
    }
    const auto expected = static_cast<std::size_t>(source.width) * source.height;
    if (source.pixels.size() != expected || source.coverage.size() != expected) {
        error = {
            ErrorCode::pixel_limit, 0, "GAF model pixel/coverage count does not match dimensions"
        };
        return false;
    }
    const auto destination_width = static_cast<int32_t>(destination.width);
    const auto destination_height = static_cast<int32_t>(destination.height);
    for (int32_t y = 0; y < static_cast<int32_t>(source.height); ++y) {
        const auto destination_y = top + y;
        if (destination_y < 0 || destination_y >= destination_height)
            continue;
        for (int32_t x = 0; x < static_cast<int32_t>(source.width); ++x) {
            const auto destination_x = left + x;
            if (destination_x < 0 || destination_x >= destination_width)
                continue;
            const auto source_index =
                static_cast<std::size_t>(y) * source.width + static_cast<std::size_t>(x);
            const auto pixel = source.pixels[source_index];
            if (source.coverage[source_index] == 0)
                continue;
            const auto destination_index =
                static_cast<std::size_t>(destination_y) * destination.width +
                static_cast<std::size_t>(destination_x);
            destination.pixels[destination_index] = pixel;
            destination.coverage[destination_index] = 1;
        }
    }
    return true;
}

} // namespace

ParseResult parse(std::span<const uint8_t> bytes) {
    return Parser(bytes).run();
}

const Frame* frame_at(const Sequence* sequence, int32_t index) noexcept {
    if (sequence == nullptr || index < 0)
        return nullptr;
    // A nonnegative index is compared with the frame count as unsigned.
    if (static_cast<uint32_t>(index) >= sequence->frames.size())
        return nullptr;
    return &sequence->frames[static_cast<std::size_t>(index)];
}

RenderResult render_normal(const Frame& frame) {
    const auto pixel_count = static_cast<std::size_t>(frame.width) * frame.height;
    if (pixel_count > limit::pixels_per_frame) {
        return {std::nullopt, Error{ErrorCode::pixel_limit, 0, "GAF render pixels exceed limit"}};
    }
    RenderedFrame rendered{
        frame.width,
        frame.height,
        frame.origin_x,
        frame.origin_y,
        frame.transparency_index,
        std::vector<uint8_t>(pixel_count, frame.transparency_index),
        std::vector<uint8_t>(pixel_count, 0)
    };
    Error error;
    // A top-level frame's own special-render flag is ignored; only a child's
    // flag selects special rendering.
    if (!render_into(frame, rendered, 0, 0, error, 1))
        return {std::nullopt, std::move(error)};
    return {std::move(rendered), std::nullopt};
}

} // namespace oa::formats::gaf
