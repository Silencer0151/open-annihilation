// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/script_state.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <limits>
#include <span>
#include <stdexcept>
#include <string_view>
#include <vector>

namespace {

using oa::sim::script_state::AxisRecord;
using oa::sim::script_state::ContextRecord;
using oa::sim::script_state::DecodeErrorCode;
using oa::sim::script_state::PieceRecord;
using oa::sim::script_state::State;
namespace layout = oa::sim::script_state::layout;

void require(bool condition, std::string_view message) {
    if (!condition) {
        throw std::runtime_error(std::string(message));
    }
}

uint32_t read_u32(std::span<const uint8_t> bytes, std::size_t offset) {
    require(offset + layout::dword_bytes <= bytes.size(), "test read is in range");
    return static_cast<uint32_t>(bytes[offset]) | (static_cast<uint32_t>(bytes[offset + 1]) << 8U) |
           (static_cast<uint32_t>(bytes[offset + 2]) << 16U) |
           (static_cast<uint32_t>(bytes[offset + 3]) << 24U);
}

void write_u32(std::span<uint8_t> bytes, std::size_t offset, uint32_t value) {
    require(offset + layout::dword_bytes <= bytes.size(), "test write is in range");
    bytes[offset] = static_cast<uint8_t>(value);
    bytes[offset + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[offset + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[offset + 3] = static_cast<uint8_t>(value >> 24U);
}

State patterned_state() {
    State state;
    state.script_identity_token = 0x11223344U;
    state.active_context_count = 3;
    for (std::size_t index = 0; index < state.contexts.size(); ++index) {
        auto& context = state.contexts[index];
        context.execution_state = static_cast<uint32_t>(index + 1);
        context.program_counter = static_cast<uint32_t>(100 + index);
        context.stack_pointer = static_cast<int32_t>(index) - 1;
        context.sleep_remaining = -static_cast<int32_t>(20 + index);
        context.wait_piece = static_cast<uint32_t>(30 + index);
        context.wait_axis = static_cast<uint32_t>(index % layout::axis_count);
        context.child_context = static_cast<uint32_t>(7 - index);
        context.signal_mask = 0x80000000U >> index;
        for (std::size_t slot = 0; slot < context.slots.size(); ++slot) {
            context.slots[slot] = static_cast<int32_t>(index * 1000 + slot) - 40;
        }
    }
    state.statics = {-1, 0x12345678};
    state.pieces.resize(2);
    for (std::size_t piece_index = 0; piece_index < state.pieces.size(); ++piece_index) {
        auto& piece = state.pieces[piece_index];
        for (std::size_t axis_index = 0; axis_index < piece.axes.size(); ++axis_index) {
            auto& axis = piece.axes[axis_index];
            const auto base = static_cast<int32_t>(1000 * piece_index + 100 * axis_index);
            axis.move_target = base + 1;
            axis.move_speed_per_tick = base + 2;
            axis.turn_target = static_cast<uint32_t>(base + 3);
            axis.turn_speed_per_tick = base + 4;
            axis.spin_target_speed_per_tick = base + 5;
            axis.spin_acceleration_per_update = base + 6;
            axis.position = base + 7;
            axis.angle = static_cast<uint32_t>(base + 8);
        }
        piece.visible = static_cast<uint32_t>(10 + piece_index);
        piece.cached = static_cast<uint32_t>(20 + piece_index);
        piece.shaded = static_cast<uint32_t>(30 + piece_index);
    }
    return state;
}

void test_exact_layout_and_round_trip() {
    const auto state = patterned_state();
    const auto bytes = oa::sim::script_state::encode(state);
    const auto expected_size = layout::core_bytes + state.statics.size() * layout::dword_bytes +
                               state.pieces.size() * layout::piece_bytes;
    require(bytes.size() == expected_size, "encoded size follows the payload formula");
    require(
        read_u32(bytes, layout::script_identity_offset) == 0x11223344U,
        "identity begins the payload in little-endian order"
    );

    const auto callback_offset =
        layout::context_word_offset(0, layout::ContextWord::return_callback);
    require(read_u32(bytes, callback_offset) == 0, "the return callback word is saved as zero");
    require(
        read_u32(bytes, layout::context_offset(1)) == 2,
        "context records are contiguous 0xa4-byte records"
    );
    require(
        read_u32(bytes, layout::active_context_count_offset) == 3,
        "active context count terminates the core block"
    );
    require(
        read_u32(bytes, layout::statics_offset) == 0xffffffffU,
        "static values immediately follow the core block"
    );

    const auto piece_base = layout::statics_offset + state.statics.size() * layout::dword_bytes;
    require(
        read_u32(
            bytes, layout::piece_word_offset(piece_base, layout::PieceWord::move_target_x, 2)
        ) == 201U,
        "move targets are grouped across all three axes"
    );
    require(
        read_u32(
            bytes, layout::piece_word_offset(piece_base, layout::PieceWord::move_speed_x, 1)
        ) == 102U,
        "move speeds follow the target axis group"
    );
    require(
        read_u32(bytes, layout::piece_word_offset(piece_base, layout::PieceWord::position_x, 2)) ==
            207U,
        "live positions occupy piece words 18 through 20"
    );
    require(
        read_u32(bytes, layout::piece_word_offset(piece_base, layout::PieceWord::shaded)) == 30U,
        "piece flags terminate the 27-word record"
    );

    const auto decoded = oa::sim::script_state::decode(
        bytes, state.script_identity_token, state.statics.size(), state.pieces.size()
    );
    require(decoded.ok(), "valid exact payload decodes");
    require(*decoded.state == state, "state survives an exact round trip");
}

void test_callback_word_is_not_restored() {
    const auto original = patterned_state();
    auto bytes = oa::sim::script_state::encode(original);
    const auto callback_offset =
        layout::context_word_offset(4, layout::ContextWord::return_callback);
    write_u32(bytes, callback_offset, 0xdeadbeefU);

    const auto decoded = oa::sim::script_state::decode(
        bytes, original.script_identity_token, original.statics.size(), original.pieces.size()
    );
    require(decoded.ok(), "restore accepts a callback word before clearing it");
    const auto reencoded = oa::sim::script_state::encode(*decoded.state);
    require(
        read_u32(reencoded, callback_offset) == 0,
        "a restored return callback word is saved as zero again"
    );
}

void test_exact_size_identity_and_count_bounds() {
    const auto state = patterned_state();
    const auto bytes = oa::sim::script_state::encode(state);

    auto oversized = state;
    oversized.pieces.resize(oa::sim::script_state::max_piece_count + 1);
    require(
        oa::sim::script_state::encode(oversized).empty(),
        "a piece count over the codec bound encodes to no bytes"
    );

    const auto truncated = oa::sim::script_state::decode(
        std::span(bytes).first(bytes.size() - 1),
        state.script_identity_token,
        state.statics.size(),
        state.pieces.size()
    );
    require(
        !truncated.ok() && truncated.error->code == DecodeErrorCode::size_mismatch,
        "truncated payload is rejected before parsing"
    );

    auto extended = bytes;
    extended.push_back(0);
    const auto trailing = oa::sim::script_state::decode(
        extended, state.script_identity_token, state.statics.size(), state.pieces.size()
    );
    require(
        !trailing.ok() && trailing.error->code == DecodeErrorCode::size_mismatch,
        "trailing payload bytes are rejected"
    );

    const auto wrong_identity = oa::sim::script_state::decode(
        bytes, state.script_identity_token + 1, state.statics.size(), state.pieces.size()
    );
    require(
        !wrong_identity.ok() && wrong_identity.error->code == DecodeErrorCode::identity_mismatch,
        "payload identity must match the currently loaded COB"
    );

    const auto too_many_statics = oa::sim::script_state::decode(
        {}, state.script_identity_token, oa::sim::script_state::max_static_count + 1, 0
    );
    require(
        !too_many_statics.ok() && too_many_statics.error->code == DecodeErrorCode::count_limit,
        "static count bound is checked before allocation"
    );
    const auto too_many_pieces = oa::sim::script_state::decode(
        {}, state.script_identity_token, 0, oa::sim::script_state::max_piece_count + 1
    );
    require(
        !too_many_pieces.ok() && too_many_pieces.error->code == DecodeErrorCode::count_limit,
        "piece count bound is checked before allocation"
    );
    require(
        !oa::sim::script_state::encoded_size(std::numeric_limits<std::size_t>::max(), 0),
        "unbounded count cannot overflow encoded-size arithmetic"
    );
}

void test_raw_execution_words_are_preserved() {
    auto state = patterned_state();
    state.active_context_count = 0xffffffffU;
    state.contexts[0].execution_state = 0xfedcba98U;
    state.contexts[0].stack_pointer = std::numeric_limits<int32_t>::max();
    state.pieces[0].axes[0].turn_target = 0xffffffffU;

    const auto bytes = oa::sim::script_state::encode(state);
    const auto decoded = oa::sim::script_state::decode(
        bytes, state.script_identity_token, state.statics.size(), state.pieces.size()
    );
    require(
        decoded.ok() && *decoded.state == state,
        "codec preserves raw words instead of inventing semantic validation"
    );
}

void test_claim_context() {
    const std::vector<uint32_t> entries{0x111u, 0u};
    auto state = patterned_state();
    const auto occupied = state;
    require(
        oa::sim::script_state::claim_context(state, 0, entries) == -1,
        "a nonzero execution-state word keeps the context occupied"
    );
    require(state == occupied, "a failed claim does not mutate script state");

    state.contexts[2].execution_state = 0;
    state.contexts[2].program_counter = 9;
    state.contexts[2].stack_pointer = 4;
    state.contexts[2].sleep_remaining = 77;
    state.contexts[2].wait_piece = 8;
    state.contexts[2].wait_axis = 2;
    state.contexts[2].child_context = 3;
    state.contexts[2].signal_mask = 0xabcdu;
    state.contexts[2].slots[0] = 55;
    state.contexts[2].slots[31] = -19;
    state.contexts[7].execution_state = 0;
    state.contexts[7].sleep_remaining = 66;
    state.contexts[7].slots[3] = 44;
    const auto ready = state;

    require(
        oa::sim::script_state::claim_context(state, -1, entries) == -1,
        "a negative script index is rejected"
    );
    require(
        oa::sim::script_state::claim_context(state, 2, entries) == -1,
        "a script index equal to the entry-table length is rejected"
    );
    require(
        oa::sim::script_state::claim_context(state, 0, {}) == -1,
        "an empty entry table rejects every index"
    );
    require(state == ready, "an out-of-range script index does not mutate script state");

    require(
        oa::sim::script_state::claim_context(state, 1, entries) == 2,
        "the lowest free context is claimed"
    );
    const auto& claimed = state.contexts[2];
    require(
        claimed.execution_state == oa::sim::script_state::active_execution_state,
        "claim stores the active execution-state word"
    );
    require(claimed.program_counter == 0, "the COB entry word is copied raw, including zero");
    require(
        claimed.stack_pointer == oa::sim::script_state::claimed_stack_pointer,
        "claim sets the stack pointer to -1"
    );
    require(
        claimed.signal_mask == oa::sim::script_state::claimed_signal_mask,
        "claim sets the signal mask to 1"
    );
    require(claimed.sleep_remaining == 77, "claim leaves sleep remaining unchanged");
    require(
        claimed.wait_piece == 8 && claimed.wait_axis == 2, "claim leaves wait fields unchanged"
    );
    require(claimed.child_context == 3, "claim leaves the child context unchanged");
    require(
        claimed.slots[0] == 55 && claimed.slots[31] == -19, "claim leaves stack slots unchanged"
    );
    require(
        state.active_context_count == ready.active_context_count + 1,
        "claim increments the active count"
    );
    require(
        state.script_identity_token == ready.script_identity_token,
        "claim leaves the identity token unchanged"
    );
    require(
        state.statics == ready.statics && state.pieces == ready.pieces,
        "claim leaves statics and pieces unchanged"
    );
    for (std::size_t index = 0; index < state.contexts.size(); ++index) {
        if (index == 2) {
            continue;
        }
        require(
            state.contexts[index] == ready.contexts[index],
            "claim changes only the selected context"
        );
    }

    require(
        oa::sim::script_state::claim_context(state, 0, entries) == 7,
        "a later free context is the next claim"
    );
    require(state.contexts[7].program_counter == 0x111u, "each claim copies its own entry word");
    require(
        state.contexts[7].sleep_remaining == 66, "the second claim also leaves sleep unchanged"
    );
    require(state.contexts[7].slots[3] == 44, "the second claim leaves stack slots unchanged");
    require(
        state.active_context_count == ready.active_context_count + 2,
        "each successful claim increments the stored count"
    );

    state.active_context_count = 0xffffffffU;
    state.contexts[0].execution_state = 0;
    require(
        oa::sim::script_state::claim_context(state, 0, entries) == 0,
        "a zero execution-state word is free even when other words are stale"
    );
    require(state.active_context_count == 0, "the active count wraps as a 32-bit add");
    require(
        state.contexts[0].program_counter == 0x111u, "the wrapped claim still copies the entry word"
    );

    const auto persisted = oa::sim::script_state::decode(
        oa::sim::script_state::encode(state),
        state.script_identity_token,
        state.statics.size(),
        state.pieces.size()
    );
    require(
        persisted.ok() && *persisted.state == state,
        "a claimed context remains an exact raw payload"
    );
}

} // namespace

int main() {
    try {
        test_exact_layout_and_round_trip();
        test_callback_word_is_not_restored();
        test_exact_size_identity_and_count_bounds();
        test_raw_execution_words_are_preserved();
        test_claim_context();
    } catch (const std::exception& error) {
        std::cerr << "script-state test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "script-state tests passed\n";
    return 0;
}
