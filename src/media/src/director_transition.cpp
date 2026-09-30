// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The director's transitions: integer blends of two RGB frames
// (transition.hpp states each formula).

#include "oa/media/director/transition.hpp"

#include <cstddef>
#include <cstdint>
#include <cstring>
#include <span>

namespace oa::media::director {
namespace {

using oa::formats::oascript::TransitionKind;

/// Bytes a pixel takes: red, green and blue.
inline constexpr size_t bytes_per_pixel = 3;
/// The full weight of a blend: one frame counts 256 / 256.
inline constexpr uint32_t blend_scale = 256;
/// Half the full weight, added before dividing to round to the nearest.
inline constexpr uint32_t blend_rounding = blend_scale / 2;
/// The fade's scale: the first 256 steps darken, the next 256 brighten.
inline constexpr uint32_t fade_scale = blend_scale * 2;

/// Returns floor(scale * (step + 1) / (steps + 1)): progress on a scale.
///
/// @param scale the value progress 1 would give
/// @param step the step, below steps
/// @param steps the transition's frames
/// @return the scaled progress, below scale
uint64_t scaled_progress(uint64_t scale, uint32_t step, uint32_t steps) noexcept {
    return scale * (uint64_t{step} + 1) / (uint64_t{steps} + 1);
}

/// Blends every byte of two frames with weights out of 256.
///
/// @param outgoing the outgoing frame
/// @param outgoing_weight its weight
/// @param incoming the incoming frame
/// @param incoming_weight its weight
/// @param[out] frame the blended frame
void weigh_bytes(
    std::span<const uint8_t> outgoing,
    uint32_t outgoing_weight,
    std::span<const uint8_t> incoming,
    uint32_t incoming_weight,
    std::span<uint8_t> frame
) noexcept {
    for (size_t index{}; index < frame.size(); ++index) {
        const uint32_t sum{
            uint32_t{outgoing[index]} * outgoing_weight +
            uint32_t{incoming[index]} * incoming_weight + blend_rounding
        };
        frame[index] = static_cast<uint8_t>(sum / blend_scale);
    }
}

/// Copies the pixels [first, end) of one row from a source frame.
///
/// @param source the frame copied from
/// @param[out] frame the frame copied to
/// @param row_start the row's first byte in both frames
/// @param first the first column copied
/// @param end the column after the last copied
void copy_columns(
    std::span<const uint8_t> source,
    std::span<uint8_t> frame,
    size_t row_start,
    size_t first,
    size_t end
) noexcept {
    if (end <= first)
        return;
    const size_t offset{row_start + first * bytes_per_pixel};
    std::memcpy(frame.data() + offset, source.data() + offset, (end - first) * bytes_per_pixel);
}

/// Draws a wipe: columns left of `edge` incoming, the rest outgoing.
///
/// @param outgoing the outgoing frame
/// @param incoming the incoming frame
/// @param width the frames' width in pixels
/// @param height the frames' height in pixels
/// @param edge the first outgoing column
/// @param[out] frame the blended frame
void draw_wipe(
    std::span<const uint8_t> outgoing,
    std::span<const uint8_t> incoming,
    size_t width,
    size_t height,
    size_t edge,
    std::span<uint8_t> frame
) noexcept {
    const size_t row_bytes{width * bytes_per_pixel};
    for (size_t row{}; row < height; ++row) {
        copy_columns(incoming, frame, row * row_bytes, 0, edge);
        copy_columns(outgoing, frame, row * row_bytes, edge, width);
    }
}

/// Draws a checkerboard step: in each square of the filling parity, the
/// columns left of `filled` are incoming; the squares of the other parity
/// are incoming when `other_full`, outgoing otherwise.
///
/// @param outgoing the outgoing frame
/// @param incoming the incoming frame
/// @param width the frames' width in pixels
/// @param height the frames' height in pixels
/// @param cell the squares' side in pixels
/// @param filling_parity 0 when the even squares fill, 1 when the odd ones do
/// @param filled the columns of each filling square already incoming
/// @param other_full the squares of the other parity are all incoming
/// @param[out] frame the blended frame
void draw_checkerboard(
    std::span<const uint8_t> outgoing,
    std::span<const uint8_t> incoming,
    size_t width,
    size_t height,
    size_t cell,
    size_t filling_parity,
    size_t filled,
    bool other_full,
    std::span<uint8_t> frame
) noexcept {
    const size_t row_bytes{width * bytes_per_pixel};
    const std::span<const uint8_t> other{other_full ? incoming : outgoing};
    for (size_t row{}; row < height; ++row) {
        const size_t row_start{row * row_bytes};
        const size_t square_row{row / cell};
        for (size_t left{}; left < width; left += cell) {
            const size_t right{left + cell < width ? left + cell : width};
            const size_t square_column{left / cell};
            if ((square_column + square_row) % 2 != filling_parity) {
                copy_columns(other, frame, row_start, left, right);
                continue;
            }
            const size_t edge{left + filled < right ? left + filled : right};
            copy_columns(incoming, frame, row_start, left, edge);
            copy_columns(outgoing, frame, row_start, edge, right);
        }
    }
}

} // namespace

uint32_t checkerboard_cell(uint32_t height) noexcept {
    const uint32_t cell{height / checkerboard_rows};
    return cell > 0 ? cell : 1;
}

bool blend_transition(
    TransitionKind kind,
    uint32_t step,
    uint32_t steps,
    std::span<const uint8_t> outgoing,
    std::span<const uint8_t> incoming,
    uint32_t width,
    uint32_t height,
    std::span<uint8_t> frame
) noexcept {
    const uint64_t frame_bytes{uint64_t{width} * height * bytes_per_pixel};
    if (steps == 0 || step >= steps || outgoing.size() != frame_bytes ||
        incoming.size() != frame_bytes || frame.size() != frame_bytes)
        return false;
    switch (kind) {
    case TransitionKind::dissolve: {
        const auto weight{static_cast<uint32_t>(scaled_progress(blend_scale, step, steps))};
        weigh_bytes(outgoing, blend_scale - weight, incoming, weight, frame);
        return true;
    }
    case TransitionKind::fade: {
        const auto level{static_cast<uint32_t>(scaled_progress(fade_scale, step, steps))};
        if (level < blend_scale)
            weigh_bytes(outgoing, blend_scale - level, incoming, 0, frame);
        else
            weigh_bytes(outgoing, 0, incoming, level - blend_scale, frame);
        return true;
    }
    case TransitionKind::wipe:
        draw_wipe(outgoing, incoming, width, height, scaled_progress(width, step, steps), frame);
        return true;
    case TransitionKind::checkerboard: {
        const uint32_t cell{checkerboard_cell(height)};
        // 2p = 2 (step + 1) / (steps + 1); below 1 the even squares fill.
        const uint64_t twice_done{2 * (uint64_t{step} + 1)};
        const uint64_t whole{uint64_t{steps} + 1};
        if (twice_done < whole) {
            const uint64_t filled{uint64_t{cell} * twice_done / whole};
            draw_checkerboard(outgoing, incoming, width, height, cell, 0, filled, false, frame);
        } else {
            const uint64_t filled{uint64_t{cell} * (twice_done - whole) / whole};
            draw_checkerboard(outgoing, incoming, width, height, cell, 1, filled, true, frame);
        }
        return true;
    }
    }
    return false;
}

} // namespace oa::media::director
