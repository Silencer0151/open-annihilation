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

namespace oa::sim::script_state {

// Names in this namespace describe the unit-script payload 3.1c writes to and
// reads from save files. All byte offsets are relative to the start of that
// payload, excluding the enclosing save-stream section.
namespace layout {
inline constexpr std::size_t axis_count = 3;
inline constexpr std::size_t context_count = 8;
inline constexpr std::size_t stack_slot_count = 32;

inline constexpr std::size_t dword_bytes = 4;
inline constexpr std::size_t context_word_count = 41;
inline constexpr std::size_t context_bytes = context_word_count * dword_bytes;
inline constexpr std::size_t core_bytes = 0x528;
inline constexpr std::size_t piece_word_count = 27;
inline constexpr std::size_t piece_bytes = piece_word_count * dword_bytes;

inline constexpr std::size_t script_identity_offset = 0;
inline constexpr std::size_t contexts_offset = dword_bytes;
inline constexpr std::size_t active_context_count_offset =
    contexts_offset + context_count * context_bytes;
inline constexpr std::size_t statics_offset = core_bytes;

enum class ContextWord : std::size_t {
    execution_state = 0,
    program_counter = 1,
    stack_pointer = 2,
    sleep_remaining = 3,
    wait_piece = 4,
    wait_axis = 5,
    child_context = 6,
    signal_mask = 7,
    return_callback = 8,
    first_stack_slot = 9,
};

enum class PieceWord : std::size_t {
    move_target_x = 0,
    move_speed_x = 3,
    turn_target_x = 6,
    turn_speed_x = 9,
    spin_target_speed_x = 12,
    spin_acceleration_x = 15,
    position_x = 18,
    angle_x = 21,
    visible = 24,
    cached = 25,
    shaded = 26,
};

/// Returns the payload offset of one context record.
///
/// @param index context index, 0..context_count-1
/// @return byte offset from the start of the payload
[[nodiscard]] constexpr std::size_t context_offset(std::size_t index) noexcept {
    return contexts_offset + index * context_bytes;
}

/// Returns the payload offset of one word of a context record.
///
/// @param index context index, 0..context_count-1
/// @param word word within the context
/// @return byte offset from the start of the payload
[[nodiscard]] constexpr std::size_t
context_word_offset(std::size_t index, ContextWord word) noexcept {
    return context_offset(index) + static_cast<std::size_t>(word) * dword_bytes;
}

/// Returns the payload offset of one word of a piece record.
///
/// @param piece_base byte offset of the piece record
/// @param first_axis_word the field's word for the X axis, or a flag word
/// @param axis axis added to the word, 0..2
/// @return byte offset from the start of the payload
[[nodiscard]] constexpr std::size_t piece_word_offset(
    std::size_t piece_base, PieceWord first_axis_word, std::size_t axis = 0
) noexcept {
    return piece_base + (static_cast<std::size_t>(first_axis_word) + axis) * dword_bytes;
}
} // namespace layout

// Safety limits of the codec.
inline constexpr std::size_t max_static_count = 65'536;
inline constexpr std::size_t max_piece_count = 4'096;

struct ContextRecord {
    // Raw execution-state word. Known VM state values are interpreted
    // by script-vm; the save codec intentionally preserves unknown values.
    uint32_t execution_state = 0;
    uint32_t program_counter = 0;
    int32_t stack_pointer = -1;
    // Scheduler ticks remaining, not milliseconds.
    int32_t sleep_remaining = 0;
    uint32_t wait_piece = 0;
    uint32_t wait_axis = 0;
    uint32_t child_context = 0;
    uint32_t signal_mask = 0;
    std::array<int32_t, layout::stack_slot_count> slots{};

    bool operator==(const ContextRecord&) const = default;
};

struct AxisRecord {
    // Linear fields use the game's integer model-position units.
    int32_t move_target = 0;
    int32_t move_speed_per_tick = 0;
    // Angles wrap in the low 16 bits; 65,536 units form one revolution.
    uint32_t turn_target = 0;
    int32_t turn_speed_per_tick = 0;
    int32_t spin_target_speed_per_tick = 0;
    // Applied once per motion update.
    int32_t spin_acceleration_per_update = 0;
    int32_t position = 0;
    uint32_t angle = 0;

    bool operator==(const AxisRecord&) const = default;
};

struct PieceRecord {
    std::array<AxisRecord, layout::axis_count> axes{};
    uint32_t visible = 0;
    uint32_t cached = 0;
    uint32_t shaded = 0;

    bool operator==(const PieceRecord&) const = default;
};

struct State {
    // The script's identity (the loaded COB file's hash); a restore requires
    // it to match the loaded script's.
    uint32_t script_identity_token = 0;
    std::array<ContextRecord, layout::context_count> contexts{};
    uint32_t active_context_count = 0;
    std::vector<int32_t> statics;
    std::vector<PieceRecord> pieces;

    bool operator==(const State&) const = default;
};

enum class DecodeErrorCode {
    none,
    count_limit,
    size_overflow,
    size_mismatch,
    identity_mismatch,
};

struct DecodeError {
    DecodeErrorCode code = DecodeErrorCode::none;
    std::string message;
};

struct DecodeResult {
    std::optional<State> state;
    std::optional<DecodeError> error;

    /// Returns whether a state was decoded.
    [[nodiscard]] bool ok() const noexcept { return state.has_value(); }
};

/// Returns the exact payload size for a script: 0x528 + statics * 4 + pieces * 0x6c.
///
/// @param static_count number of script statics
/// @param piece_count number of model pieces
/// @return the size in bytes, or nullopt when a codec limit or size_t
///         arithmetic bound would be exceeded
[[nodiscard]] std::optional<std::size_t>
encoded_size(std::size_t static_count, std::size_t piece_count) noexcept;

/// Serializes one payload in the 3.1c save layout.
///
/// The callback word of every context is written as zero.
///
/// @param state state to serialize
/// @return the payload bytes, or no bytes when a vector count exceeds the codec bounds
[[nodiscard]] std::vector<uint8_t> encode(const State& state);

/// Parses one payload.
///
/// Decode requires the exact size and returns no partial state on failure.
///
/// @param bytes payload bytes
/// @param expected_script_identity_token hash of the loaded COB file, from the
///        enclosing unit save state
/// @param static_count number of statics of the loaded script
/// @param piece_count number of pieces of the loaded model
/// @return the state, or the reason it was refused
[[nodiscard]] DecodeResult decode(
    std::span<const uint8_t> bytes,
    uint32_t expected_script_identity_token,
    std::size_t static_count,
    std::size_t piece_count
);

// Execution-state word written by claim_context. script-vm treats it as running;
// this component still stores the word raw.
inline constexpr uint32_t active_execution_state = 0x01000000U;
// Stack pointer and signal mask written by the same claim.
inline constexpr int32_t claimed_stack_pointer = -1;
inline constexpr uint32_t claimed_signal_mask = 1U;

/// Claims the first of the eight contexts whose execution-state word is zero.
///
/// A successful claim copies the entry word into the program counter without
/// checking it against code, sets the execution state, stack pointer and
/// signal mask to the constants above, and increments active_context_count
/// with 32-bit wrap. Sleep, wait, child and stack slots are left as they were,
/// and the count is not recomputed. Context word 8, the return callback, is
/// not represented: ContextRecord has no callback field and encode stores
/// zero there.
///
/// @param[in,out] state state whose contexts and active count change
/// @param script_index script whose entry point is used
/// @param script_entry_points the loaded COB's entry-point table; its length is
///        the script count of the COB header
/// @return the claimed context index, or -1 with no change when the index is
///         negative or outside the table, or every context is occupied
[[nodiscard]] int32_t claim_context(
    State& state, int32_t script_index, std::span<const uint32_t> script_entry_points
) noexcept;

} // namespace oa::sim::script_state
