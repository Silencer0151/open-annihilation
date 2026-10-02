// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/model_runtime/model_host.hpp"

#include <bit>
#include <cstdint>

namespace oa::sim::model_runtime {
namespace {

// The VM checks the axis operand against 0..2; any other axis names nothing.
int32_t* component(oa::formats::objects3d::FixedVector3& value, uint32_t axis) noexcept {
    switch (axis) {
    case 0:
        return &value.x;
    case 1:
        return &value.y;
    case 2:
        return &value.z;
    default:
        return nullptr;
    }
}

int16_t* angle(RotationWords& value, uint32_t axis) noexcept {
    // COB axis 0 is the piece's yz rotation word, axis 1 its xz word and
    // axis 2 its xy word.
    switch (axis) {
    case 0:
        return &value.yz;
    case 1:
        return &value.xz;
    case 2:
        return &value.xy;
    default:
        return nullptr;
    }
}

uint16_t piece_flags(const Instance& instance, uint32_t piece) noexcept {
    const auto* state = instance.find_piece(piece);
    return state ? state->flags : 0;
}
} // namespace

int32_t ModelHost::piece_position(uint32_t piece, uint32_t axis) const {
    auto* state = instance_.find_piece(piece);
    const auto* stored = state ? component(state->translation, axis) : nullptr;
    return stored ? *stored : 0;
}

int32_t ModelHost::piece_angle(uint32_t piece, uint32_t axis) const {
    auto* state = instance_.find_piece(piece);
    const auto* stored = state ? angle(state->rotation, axis) : nullptr;
    return stored ? static_cast<int32_t>(static_cast<uint16_t>(*stored)) : 0;
}

uint32_t ModelHost::piece_visible(uint32_t piece) const {
    return (piece_flags(instance_, piece) & static_cast<uint16_t>(PieceFlag::visible)) != 0;
}

uint32_t ModelHost::piece_cached(uint32_t piece) const {
    return (piece_flags(instance_, piece) & static_cast<uint16_t>(PieceFlag::cached)) != 0;
}

uint32_t ModelHost::piece_shaded(uint32_t piece) const {
    return (piece_flags(instance_, piece) & static_cast<uint16_t>(PieceFlag::shaded)) != 0;
}

void ModelHost::set_piece_position(uint32_t piece, uint32_t axis, int32_t value) {
    auto* state = instance_.find_piece(piece);
    auto* stored = state ? component(state->translation, axis) : nullptr;
    if (stored && *stored != value) {
        *stored = value;
        state->transform_marker = 0;
        instance_.transforms_dirty_ = true;
    }
}

void ModelHost::set_piece_angle(uint32_t piece, uint32_t axis, uint32_t value) {
    auto* state = instance_.find_piece(piece);
    const auto normalized = std::bit_cast<int16_t>(static_cast<uint16_t>(value));
    auto* stored = state ? angle(state->rotation, axis) : nullptr;
    if (stored && *stored != normalized) {
        *stored = normalized;
        state->transform_marker = 0;
        instance_.transforms_dirty_ = true;
    }
}

void ModelHost::set_piece_visible(uint32_t piece, uint32_t visible) {
    auto* state = instance_.find_piece(piece);
    if (!state)
        return;
    const auto current = (state->flags & static_cast<uint16_t>(PieceFlag::visible)) != 0;
    if (visible != static_cast<uint32_t>(current)) {
        state->flags = static_cast<uint16_t>(
            (state->flags & ~static_cast<uint16_t>(PieceFlag::visible)) |
            ((visible & 1U) * static_cast<uint16_t>(PieceFlag::visible))
        );
        state->transform_marker = 0;
    }
}

void ModelHost::set_piece_cached(uint32_t piece, uint32_t cached) {
    auto* state = instance_.find_piece(piece);
    if (!state)
        return;
    state->flags = static_cast<uint16_t>(
        (state->flags & ~static_cast<uint16_t>(PieceFlag::cached)) |
        ((cached & 1U) * static_cast<uint16_t>(PieceFlag::cached))
    );
}

void ModelHost::set_piece_shaded(uint32_t piece, uint32_t shaded) {
    auto* state = instance_.find_piece(piece);
    if (!state)
        return;
    state->flags = static_cast<uint16_t>(
        (state->flags & ~static_cast<uint16_t>(PieceFlag::shaded)) |
        ((shaded & 1U) * static_cast<uint16_t>(PieceFlag::shaded))
    );
}

} // namespace oa::sim::model_runtime
