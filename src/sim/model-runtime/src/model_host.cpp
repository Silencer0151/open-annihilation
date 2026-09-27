// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/model_runtime/model_host.hpp"

#include <bit>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace oa::sim::model_runtime {
namespace {

const int32_t& component(const oa::formats::objects3d::FixedVector3& value, uint32_t axis) {
    switch (axis) {
    case 0:
        return value.x;
    case 1:
        return value.y;
    case 2:
        return value.z;
    default:
        throw std::out_of_range("COB axis is out of range");
    }
}

int32_t& component(oa::formats::objects3d::FixedVector3& value, uint32_t axis) {
    return const_cast<int32_t&>(component(std::as_const(value), axis));
}

const int16_t& angle(const RotationWords& value, uint32_t axis) {
    // COB axis 0 is the piece's yz rotation word, axis 1 its xz word and
    // axis 2 its xy word.
    switch (axis) {
    case 0:
        return value.yz;
    case 1:
        return value.xz;
    case 2:
        return value.xy;
    default:
        throw std::out_of_range("COB axis is out of range");
    }
}

int16_t& angle(RotationWords& value, uint32_t axis) {
    return const_cast<int16_t&>(angle(std::as_const(value), axis));
}
} // namespace

int32_t ModelHost::piece_position(uint32_t piece, uint32_t axis) const {
    return component(instance_.piece_for_script_index(piece).translation, axis);
}

int32_t ModelHost::piece_angle(uint32_t piece, uint32_t axis) const {
    return static_cast<int32_t>(
        static_cast<uint16_t>(angle(instance_.piece_for_script_index(piece).rotation, axis))
    );
}

uint32_t ModelHost::piece_visible(uint32_t piece) const {
    return (instance_.piece_for_script_index(piece).flags &
            static_cast<uint16_t>(PieceFlag::visible)) != 0;
}

uint32_t ModelHost::piece_cached(uint32_t piece) const {
    return (instance_.piece_for_script_index(piece).flags &
            static_cast<uint16_t>(PieceFlag::cached)) != 0;
}

uint32_t ModelHost::piece_shaded(uint32_t piece) const {
    return (instance_.piece_for_script_index(piece).flags &
            static_cast<uint16_t>(PieceFlag::shaded)) != 0;
}

void ModelHost::set_piece_position(uint32_t piece, uint32_t axis, int32_t value) {
    auto& state = instance_.piece_for_script_index(piece);
    auto& stored = component(state.translation, axis);
    if (stored != value) {
        stored = value;
        state.transform_marker = 0;
        instance_.transforms_dirty_ = true;
    }
}

void ModelHost::set_piece_angle(uint32_t piece, uint32_t axis, uint32_t value) {
    auto& state = instance_.piece_for_script_index(piece);
    const auto normalized = std::bit_cast<int16_t>(static_cast<uint16_t>(value));
    auto& stored = angle(state.rotation, axis);
    if (stored != normalized) {
        stored = normalized;
        state.transform_marker = 0;
        instance_.transforms_dirty_ = true;
    }
}

void ModelHost::set_piece_visible(uint32_t piece, uint32_t visible) {
    auto& state = instance_.piece_for_script_index(piece);
    const auto current = (state.flags & static_cast<uint16_t>(PieceFlag::visible)) != 0;
    if (visible != current) {
        state.flags = static_cast<uint16_t>(
            (state.flags & ~static_cast<uint16_t>(PieceFlag::visible)) |
            ((visible & 1U) * static_cast<uint16_t>(PieceFlag::visible))
        );
        state.transform_marker = 0;
    }
}

void ModelHost::set_piece_cached(uint32_t piece, uint32_t cached) {
    auto& state = instance_.piece_for_script_index(piece);
    state.flags = static_cast<uint16_t>(
        (state.flags & ~static_cast<uint16_t>(PieceFlag::cached)) |
        ((cached & 1U) * static_cast<uint16_t>(PieceFlag::cached))
    );
}

void ModelHost::set_piece_shaded(uint32_t piece, uint32_t shaded) {
    auto& state = instance_.piece_for_script_index(piece);
    state.flags = static_cast<uint16_t>(
        (state.flags & ~static_cast<uint16_t>(PieceFlag::shaded)) |
        ((shaded & 1U) * static_cast<uint16_t>(PieceFlag::shaded))
    );
}

} // namespace oa::sim::model_runtime
