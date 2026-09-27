// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/sprite_animation.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>

namespace animation = oa::sim::sprite_animation;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

oa::formats::gaf::Sequence
sequence(uint16_t repeat_word, std::initializer_list<uint16_t> durations) {
    oa::formats::gaf::Sequence value;
    value.repeat_flags = repeat_word;
    for (const auto duration : durations) {
        oa::formats::gaf::Frame frame;
        frame.duration = duration;
        value.frames.push_back(frame);
    }
    return value;
}

void initialization_and_low_byte_repeat() {
    auto value = sequence(0x0100, {3, 0xabcd});
    animation::Cursor cursor;
    animation::initialize(cursor, &value, 1);
    require(
        cursor.frame_index == 1 && cursor.remaining_ticks == 0xabcd && cursor.repeat_flag == 0,
        "initialize fields or low-byte repeat flag disagreed"
    );

    animation::initialize(cursor, &value, 7);
    require(
        cursor.frame_index == 0 && cursor.remaining_ticks == 3,
        "out-of-range initial frame did not reset to zero"
    );
    animation::initialize(cursor, &value, -65535);
    require(
        cursor.frame_index == 1 && cursor.remaining_ticks == 0xabcd,
        "signed comparison before 16-bit initial-frame narrowing disagreed"
    );
    animation::initialize(cursor, nullptr, 0);
    require(
        !cursor.active() && cursor.remaining_ticks == 0xffff,
        "inactive frame_duration duration sentinel disagreed"
    );
}

void current_frame_record() {
    auto value = sequence(1, {3, 5});
    value.frames[0].width = 11;
    value.frames[0].origin_x = -2;
    value.frames[1].width = 17;
    value.frames[1].origin_y = 9;

    animation::Cursor cursor;
    animation::initialize(cursor, &value, 1);
    const auto* frame = animation::current_frame(cursor);
    require(
        frame == &value.frames[1] && frame->width == 17 && frame->origin_y == 9 &&
            cursor.remaining_ticks == 5 && cursor.repeat_flag == 1,
        "current_frame did not return the current frame record"
    );

    animation::initialize(cursor, &value, 0);
    require(
        animation::current_frame(cursor) == &value.frames[0] &&
            animation::current_frame(cursor)->width == 11 &&
            animation::current_frame(cursor)->origin_x == -2,
        "current_frame frame-index selection disagreed"
    );

    animation::initialize(cursor, nullptr, 0);
    require(
        animation::current_frame(cursor) == nullptr, "inactive cursor did not return a null frame"
    );

    animation::initialize(cursor, &value, 0);
    cursor.frame_index = 9;
    require(
        animation::current_frame(cursor) == nullptr && cursor.sequence == &value,
        "out-of-range frame index should not read past the frame table"
    );
}

void single_tick_behavior() {
    auto once = sequence(0, {2, 4});
    animation::Cursor cursor;
    animation::initialize(cursor, &once, 0);
    require(
        !animation::tick(cursor) && cursor.frame_index == 0 && cursor.remaining_ticks == 1,
        "timer-only tick disagreed"
    );
    require(
        animation::tick(cursor) && cursor.frame_index == 1 && cursor.remaining_ticks == 4,
        "frame-changing tick disagreed"
    );
    cursor.remaining_ticks = 0;
    require(
        animation::tick(cursor) && !cursor.active(),
        "non-repeating completion did not clear sequence"
    );

    auto repeating = sequence(0x1201, {1, 1});
    animation::initialize(cursor, &repeating, 1);
    require(
        animation::tick(cursor) && cursor.active() && cursor.frame_index == 0,
        "nonzero low repeat byte did not wrap"
    );
}

void signed_elapsed_and_wrapping() {
    auto value = sequence(1, {3, 5, 7});
    animation::Cursor cursor;
    animation::initialize(cursor, &value, 0);
    require(
        animation::advance_elapsed_bounded(cursor, 9, 8) == animation::ElapsedStatus::advanced &&
            cursor.frame_index == 2 && cursor.remaining_ticks == 6,
        "advance_elapsed multi-frame carry disagreed"
    );

    cursor.remaining_ticks = 1;
    require(
        animation::advance_elapsed_bounded(cursor, -1, 8) == animation::ElapsedStatus::unchanged &&
            cursor.remaining_ticks == 2,
        "signed negative elapsed delta did not use 16-bit subtraction"
    );

    auto once = sequence(0, {1, 1});
    animation::initialize(cursor, &once, 1);
    require(
        animation::advance_elapsed_bounded(cursor, 1, 8) == animation::ElapsedStatus::completed &&
            !cursor.active(),
        "elapsed completion did not clear non-repeating sequence"
    );

    auto boundary = sequence(1, {1, 0x7fff});
    animation::initialize(cursor, &boundary, 0);
    cursor.remaining_ticks = 0x8000;
    require(
        animation::advance_elapsed_bounded(cursor, -1, 4) == animation::ElapsedStatus::advanced &&
            cursor.frame_index == 0 && cursor.remaining_ticks == 1,
        "signed boundary and duration-addition wrapping disagreed"
    );

    animation::initialize(cursor, &value, 0);
    animation::advance_elapsed_unbounded(cursor, 4);
    require(
        cursor.frame_index == 1 && cursor.remaining_ticks == 4,
        "unbounded elapsed path disagreed on valid durations"
    );
}

void malformed_zero_duration_is_bounded() {
    auto malformed = sequence(1, {0, 0});
    animation::Cursor cursor;
    animation::initialize(cursor, &malformed, 0);
    require(
        animation::advance_elapsed_bounded(cursor, 1, 5) ==
                animation::ElapsedStatus::advance_limit &&
            cursor.active() && cursor.frame_index == 1 && cursor.remaining_ticks == 0xffff,
        "zero-duration repeat did not report the caller-selected bound"
    );

    animation::initialize(cursor, &malformed, 0);
    require(
        animation::advance_elapsed_bounded(cursor, 1, 0) ==
                animation::ElapsedStatus::advance_limit &&
            cursor.frame_index == 0 && cursor.remaining_ticks == 0xffff,
        "zero advance limit changed the frame before reporting exhaustion"
    );

    auto one_frame = sequence(1, {0});
    animation::initialize(cursor, &one_frame, 0);
    require(
        animation::advance_elapsed_bounded(cursor, 123, 0) == animation::ElapsedStatus::unchanged &&
            cursor.remaining_ticks == 0,
        "advance_elapsed should ignore sequences with one frame"
    );
}

} // namespace

int main() {
    try {
        initialization_and_low_byte_repeat();
        current_frame_record();
        single_tick_behavior();
        signed_elapsed_and_wrapping();
        malformed_zero_duration_is_bounded();
    } catch (const std::exception& error) {
        std::cerr << "sprite-animation test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sprite-animation tests passed\n";
    return 0;
}
