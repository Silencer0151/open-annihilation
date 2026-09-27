// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/sprite_animation.hpp"

#include <bit>
#include <cstdint>

namespace oa::sim::sprite_animation {
namespace {

[[nodiscard]] int16_t signed_word(uint16_t value) noexcept {
    return std::bit_cast<int16_t>(value);
}

[[nodiscard]] uint16_t wrapped_subtract(uint16_t left, int16_t right) noexcept {
    return static_cast<uint16_t>(
        static_cast<uint32_t>(left) - static_cast<uint32_t>(static_cast<uint16_t>(right))
    );
}

[[nodiscard]] bool advance_frame(Cursor& cursor) noexcept {
    cursor.frame_index = static_cast<uint16_t>(cursor.frame_index + 1U);
    if (cursor.frame_index >= cursor.sequence->frames.size()) {
        if (cursor.repeat_flag == 0) {
            cursor.sequence = nullptr;
            return false;
        }
        cursor.frame_index = 0;
    }
    return true;
}

} // namespace

uint16_t frame_duration(const Cursor& cursor) noexcept {
    if (cursor.sequence == nullptr)
        return std::numeric_limits<uint16_t>::max();
    if (cursor.frame_index >= cursor.sequence->frames.size())
        return 0;
    return cursor.sequence->frames[cursor.frame_index].duration;
}

const formats::gaf::Frame* current_frame(const Cursor& cursor) noexcept {
    if (cursor.sequence == nullptr)
        return nullptr;
    if (cursor.frame_index >= cursor.sequence->frames.size())
        return nullptr;
    return &cursor.sequence->frames[cursor.frame_index];
}

void initialize(
    Cursor& cursor, const formats::gaf::Sequence* sequence, int32_t initial_frame
) noexcept {
    cursor.sequence = sequence;
    if (sequence == nullptr) {
        cursor.frame_index = 0;
        cursor.remaining_ticks = frame_duration(cursor);
        cursor.repeat_flag = 0;
        return;
    }
    const auto frame_count = static_cast<int32_t>(sequence->frames.size());
    cursor.frame_index =
        initial_frame < frame_count ? static_cast<uint16_t>(initial_frame) : uint16_t{0};
    cursor.remaining_ticks = frame_duration(cursor);
    cursor.repeat_flag = static_cast<uint8_t>(sequence->repeat_flags);
}

bool tick(Cursor& cursor) noexcept {
    if (cursor.sequence == nullptr)
        return false;
    if (cursor.remaining_ticks >= 2) {
        cursor.remaining_ticks = static_cast<uint16_t>(cursor.remaining_ticks - 1U);
        return false;
    }
    if (!advance_frame(cursor))
        return true;
    cursor.remaining_ticks = frame_duration(cursor);
    return true;
}

ElapsedStatus advance_elapsed_bounded(
    Cursor& cursor, int16_t elapsed_ticks, std::size_t max_frame_advances
) noexcept {
    if (cursor.sequence == nullptr || cursor.sequence->frames.size() <= 1) {
        return ElapsedStatus::unchanged;
    }
    cursor.remaining_ticks = wrapped_subtract(cursor.remaining_ticks, elapsed_ticks);
    bool advanced = false;
    std::size_t advances = 0;
    while (signed_word(cursor.remaining_ticks) < 1) {
        if (advances == max_frame_advances)
            return ElapsedStatus::advance_limit;
        ++advances;
        advanced = true;
        if (!advance_frame(cursor))
            return ElapsedStatus::completed;
        cursor.remaining_ticks =
            static_cast<uint16_t>(cursor.remaining_ticks + frame_duration(cursor));
    }
    return advanced ? ElapsedStatus::advanced : ElapsedStatus::unchanged;
}

void advance_elapsed_unbounded(Cursor& cursor, int16_t elapsed_ticks) noexcept {
    if (cursor.sequence == nullptr || cursor.sequence->frames.size() <= 1)
        return;
    cursor.remaining_ticks = wrapped_subtract(cursor.remaining_ticks, elapsed_ticks);
    while (signed_word(cursor.remaining_ticks) < 1) {
        if (!advance_frame(cursor))
            return;
        cursor.remaining_ticks =
            static_cast<uint16_t>(cursor.remaining_ticks + frame_duration(cursor));
    }
}

} // namespace oa::sim::sprite_animation
