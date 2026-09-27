// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/gaf.hpp"

#include <cstddef>
#include <cstdint>
#include <limits>

namespace oa::sim::sprite_animation {

struct Cursor {
    uint16_t frame_index = 0;
    uint16_t remaining_ticks = 0;
    uint8_t repeat_flag = 0;
    const formats::gaf::Sequence* sequence = nullptr;

    /// Returns whether the cursor is playing a sequence.
    [[nodiscard]] bool active() const noexcept { return sequence != nullptr; }
};

/// Returns the duration of the cursor's current frame.
///
/// Only the low 16-bit duration is read; parsed Frame::duration already holds
/// that word.
///
/// @param cursor cursor to read
/// @return the duration in animation ticks, 0xffff for an inactive cursor, 0 for a frame index outside the parsed frames
[[nodiscard]] uint16_t frame_duration(const Cursor& cursor) noexcept;

/// Returns the cursor's current frame record.
///
/// The index is the unsigned cursor word and is not compared with the frame
/// count.
///
/// @param cursor cursor to read
/// @return the frame, or null for an inactive cursor or an index outside the parsed frames
[[nodiscard]] const formats::gaf::Frame* current_frame(const Cursor& cursor) noexcept;

/// Starts a cursor on a sequence.
///
/// The signed initial frame is compared with the frame count before it is
/// narrowed to 16 bits; an index at or above the frame count becomes zero. The
/// duration is loaded from that frame and the repeat flag is the low byte of
/// repeat_flags. A duration looked up outside the parsed frames is 0.
///
/// @param[out] cursor cursor to start
/// @param sequence sequence to play, or null for an inactive cursor
/// @param initial_frame first frame
/// @quirk A negative initial frame wraps to a 16-bit index outside the frames.
void initialize(
    Cursor& cursor, const formats::gaf::Sequence* sequence, int32_t initial_frame
) noexcept;

/// Advances the cursor by one animation tick.
///
/// When fewer than two ticks remain the frame changes; a repeating sequence
/// wraps and a non-repeating one completes, clearing the sequence.
///
/// @param[in,out] cursor cursor to advance
/// @return true when the frame changes or a non-repeating animation completes, false while only its timer changes
[[nodiscard]] bool tick(Cursor& cursor) noexcept;

enum class ElapsedStatus {
    unchanged,
    advanced,
    completed,
    advance_limit,
};

/// Advances the cursor by an elapsed tick count, stopping after a frame-advance limit.
///
/// Keeps the signed-16 timer arithmetic of advance_elapsed_unbounded but stops
/// after max_frame_advances, so malformed repeating sequences whose durations
/// never make the signed timer positive are observable to callers. Sequences
/// with at most one frame are left alone.
///
/// @param[in,out] cursor cursor to advance
/// @param elapsed_ticks signed 16-bit ticks subtracted from the timer with wrapping
/// @param max_frame_advances largest number of frame changes allowed
/// @return unchanged, advanced, completed (a non-repeating sequence ended) or advance_limit
[[nodiscard]] ElapsedStatus advance_elapsed_bounded(
    Cursor& cursor, int16_t elapsed_ticks, std::size_t max_frame_advances
) noexcept;

/// Advances the cursor by an elapsed tick count with the game's unbounded loop.
///
/// The timer drops by the signed 16-bit delta with 16-bit wrapping and frames
/// advance while the timer, read as signed 16-bit, is below one. Sequences with
/// at most one frame are left alone.
///
/// @param[in,out] cursor cursor to advance
/// @param elapsed_ticks signed 16-bit ticks subtracted from the timer
/// @quirk A malformed repeating sequence with non-positive durations never returns; use
///        advance_elapsed_bounded at untrusted-data boundaries.
void advance_elapsed_unbounded(Cursor& cursor, int16_t elapsed_ticks) noexcept;

} // namespace oa::sim::sprite_animation
