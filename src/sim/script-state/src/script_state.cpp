// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/script_state.hpp"

#include <bit>
#include <limits>
#include <utility>

namespace oa::sim::script_state {
namespace {

[[nodiscard]] constexpr uint32_t unsigned_bits(int32_t value) noexcept {
    return std::bit_cast<uint32_t>(value);
}

[[nodiscard]] constexpr int32_t signed_bits(uint32_t value) noexcept {
    return std::bit_cast<int32_t>(value);
}

void append_u32(std::vector<uint8_t>& output, uint32_t value) {
    output.push_back(static_cast<uint8_t>(value));
    output.push_back(static_cast<uint8_t>(value >> 8U));
    output.push_back(static_cast<uint8_t>(value >> 16U));
    output.push_back(static_cast<uint8_t>(value >> 24U));
}

class Reader {
  public:

    explicit Reader(std::span<const uint8_t> bytes) : bytes_(bytes) {}

    [[nodiscard]] uint32_t u32() noexcept {
        // Exact-size validation precedes parsing, so every read is in range.
        const auto value = static_cast<uint32_t>(bytes_[offset_]) |
                           (static_cast<uint32_t>(bytes_[offset_ + 1]) << 8U) |
                           (static_cast<uint32_t>(bytes_[offset_ + 2]) << 16U) |
                           (static_cast<uint32_t>(bytes_[offset_ + 3]) << 24U);
        offset_ += layout::dword_bytes;
        return value;
    }

    [[nodiscard]] int32_t i32() noexcept { return signed_bits(u32()); }

  private:

    std::span<const uint8_t> bytes_;
    std::size_t offset_ = 0;
};

[[nodiscard]] DecodeResult failure(DecodeErrorCode code, std::string message) {
    return DecodeResult{std::nullopt, DecodeError{code, std::move(message)}};
}

} // namespace

std::optional<std::size_t>
encoded_size(std::size_t static_count, std::size_t piece_count) noexcept {
    if (static_count > max_static_count || piece_count > max_piece_count) {
        return std::nullopt;
    }

    constexpr auto maximum = std::numeric_limits<std::size_t>::max();
    if (static_count > (maximum - layout::core_bytes) / layout::dword_bytes) {
        return std::nullopt;
    }
    const auto through_statics = layout::core_bytes + static_count * layout::dword_bytes;
    if (piece_count > (maximum - through_statics) / layout::piece_bytes) {
        return std::nullopt;
    }
    return through_statics + piece_count * layout::piece_bytes;
}

std::vector<uint8_t> encode(const State& state) {
    const auto size = encoded_size(state.statics.size(), state.pieces.size());
    if (!size.has_value())
        return {};

    std::vector<uint8_t> output;
    output.reserve(*size);
    append_u32(output, state.script_identity_token);

    for (const auto& context : state.contexts) {
        append_u32(output, context.execution_state);
        append_u32(output, context.program_counter);
        append_u32(output, unsigned_bits(context.stack_pointer));
        append_u32(output, unsigned_bits(context.sleep_remaining));
        append_u32(output, context.wait_piece);
        append_u32(output, context.wait_axis);
        append_u32(output, context.child_context);
        append_u32(output, context.signal_mask);
        // The save holds every context word as it stands, except the return
        // callback, which is always zero.
        append_u32(output, 0);
        for (const auto slot : context.slots) {
            append_u32(output, unsigned_bits(slot));
        }
    }
    append_u32(output, state.active_context_count);

    for (const auto value : state.statics) {
        append_u32(output, unsigned_bits(value));
    }

    for (const auto& piece : state.pieces) {
        for (const auto& axis : piece.axes) {
            append_u32(output, unsigned_bits(axis.move_target));
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, unsigned_bits(axis.move_speed_per_tick));
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, axis.turn_target);
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, unsigned_bits(axis.turn_speed_per_tick));
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, unsigned_bits(axis.spin_target_speed_per_tick));
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, unsigned_bits(axis.spin_acceleration_per_update));
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, unsigned_bits(axis.position));
        }
        for (const auto& axis : piece.axes) {
            append_u32(output, axis.angle);
        }
        append_u32(output, piece.visible);
        append_u32(output, piece.cached);
        append_u32(output, piece.shaded);
    }

    return output;
}

DecodeResult decode(
    std::span<const uint8_t> bytes,
    uint32_t expected_script_identity_token,
    std::size_t static_count,
    std::size_t piece_count
) {
    const auto size = encoded_size(static_count, piece_count);
    if (!size.has_value()) {
        if (static_count > max_static_count || piece_count > max_piece_count) {
            return failure(
                DecodeErrorCode::count_limit,
                "script state static or piece count exceeds the portable limit"
            );
        }
        return failure(
            DecodeErrorCode::size_overflow, "script state size overflows the host size type"
        );
    }
    if (bytes.size() != *size) {
        return failure(
            DecodeErrorCode::size_mismatch,
            "script state payload length does not match its COB counts"
        );
    }

    Reader reader(bytes);
    const auto identity = reader.u32();
    if (identity != expected_script_identity_token) {
        return failure(
            DecodeErrorCode::identity_mismatch,
            "script state identity token does not match the loaded COB"
        );
    }

    State state;
    state.script_identity_token = identity;
    for (auto& context : state.contexts) {
        context.execution_state = reader.u32();
        context.program_counter = reader.u32();
        context.stack_pointer = reader.i32();
        context.sleep_remaining = reader.i32();
        context.wait_piece = reader.u32();
        context.wait_axis = reader.u32();
        context.child_context = reader.u32();
        context.signal_mask = reader.u32();
        // Restore discards the saved return callback word.
        static_cast<void>(reader.u32());
        for (auto& slot : context.slots) {
            slot = reader.i32();
        }
    }
    state.active_context_count = reader.u32();

    state.statics.resize(static_count);
    for (auto& value : state.statics) {
        value = reader.i32();
    }

    state.pieces.resize(piece_count);
    for (auto& piece : state.pieces) {
        for (auto& axis : piece.axes) {
            axis.move_target = reader.i32();
        }
        for (auto& axis : piece.axes) {
            axis.move_speed_per_tick = reader.i32();
        }
        for (auto& axis : piece.axes) {
            axis.turn_target = reader.u32();
        }
        for (auto& axis : piece.axes) {
            axis.turn_speed_per_tick = reader.i32();
        }
        for (auto& axis : piece.axes) {
            axis.spin_target_speed_per_tick = reader.i32();
        }
        for (auto& axis : piece.axes) {
            axis.spin_acceleration_per_update = reader.i32();
        }
        for (auto& axis : piece.axes) {
            axis.position = reader.i32();
        }
        for (auto& axis : piece.axes) {
            axis.angle = reader.u32();
        }
        piece.visible = reader.u32();
        piece.cached = reader.u32();
        piece.shaded = reader.u32();
    }

    return DecodeResult{std::move(state), std::nullopt};
}

int32_t claim_context(
    State& state, int32_t script_index, std::span<const uint32_t> script_entry_points
) noexcept {
    if (script_index < 0 || static_cast<std::size_t>(script_index) >= script_entry_points.size()) {
        return -1;
    }

    for (std::size_t index = 0; index < state.contexts.size(); ++index) {
        auto& context = state.contexts[index];
        if (context.execution_state != 0) {
            continue;
        }
        context.execution_state = active_execution_state;
        context.program_counter = script_entry_points[static_cast<std::size_t>(script_index)];
        context.stack_pointer = claimed_stack_pointer;
        context.signal_mask = claimed_signal_mask;
        ++state.active_context_count;
        return static_cast<int32_t>(index);
    }
    return -1;
}

} // namespace oa::sim::script_state
