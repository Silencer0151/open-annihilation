// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_effects/effects.hpp"
#include <bit>

namespace oa::sim::unit_effects {
namespace {
constexpr uint32_t no_debris = 0x20U;

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) + std::bit_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return std::bit_cast<int32_t>(std::bit_cast<uint32_t>(a) - std::bit_cast<uint32_t>(b));
}
} // namespace

Runtime::Runtime(Host& h, Sink& s) : host_(h), sink_(s) {
}

void Runtime::emit_sfx(sim::unit_spawn::Slot& slot, uint32_t piece, int32_t raw) {
    if (!slot.unit || !host_.visible(slot))
        return;
    host_.refresh_transform(slot);
    Event e{};
    e.unit = slot.unit_index;
    e.piece = piece;
    e.code = static_cast<uint32_t>(raw);
    if ((static_cast<uint32_t>(raw) & 0x100U) != 0)
        e.first = host_.piece_origin(slot, piece);
    else {
        e.first = host_.piece_start(slot, piece);
        e.second = host_.piece_end(slot, piece);
    }
    // The game subtracts model Z offsets from unit Z.
    e.first[0] = wrap_add(e.first[0], static_cast<int32_t>(slot.unit->position[0]));
    e.first[1] = wrap_add(e.first[1], static_cast<int32_t>(slot.unit->position[1]));
    e.first[2] = wrap_sub(static_cast<int32_t>(slot.unit->position[2]), e.first[2]);
    if ((static_cast<uint32_t>(raw) & 0x100U) == 0) {
        e.second[0] = wrap_add(e.second[0], static_cast<int32_t>(slot.unit->position[0]));
        e.second[1] = wrap_add(e.second[1], static_cast<int32_t>(slot.unit->position[1]));
        e.second[2] = wrap_sub(static_cast<int32_t>(slot.unit->position[2]), e.second[2]);
    }
    switch (raw) {
    case 0:
        e.kind = EventKind::thrust_flame;
        e.flags = 6;
        break;
    case 1:
        e.kind = EventKind::thrust_flame;
        e.flags = 7;
        break;
    case 2:
        e.kind = EventKind::wake;
        e.flags = 16;
        break;
    case 3:
        e.kind = EventKind::wake;
        e.flags = 8;
        break;
    case 4:
        e.kind = EventKind::wake;
        e.flags = 16;
        std::swap(e.first, e.second);
        break;
    case 5:
        e.kind = EventKind::wake;
        e.flags = 8;
        std::swap(e.first, e.second);
        break;
    case 0x101:
        e.kind = EventKind::white_smoke;
        break;
    case 0x102:
        e.kind = EventKind::black_smoke;
        break;
    case 0x103:
        e.kind = EventKind::sub_bubbles;
        e.second = e.first;
        e.second[1] = host_.sea_level_fixed();
        break;
    default:
        return;
    }
    sink_.effect(e);
}

void Runtime::explode_piece(sim::unit_spawn::Slot& slot, uint32_t piece, int32_t raw) {
    const auto flags = static_cast<uint32_t>(raw);
    if (!slot.unit)
        return;
    if ((flags & no_debris) == 0) {
        Event e{};
        e.kind = EventKind::debris_piece;
        e.unit = slot.unit_index;
        e.piece = piece;
        e.flags = flags;
        e.first = {
            static_cast<int32_t>(host_.random_bounded(3000)),
            static_cast<int32_t>(host_.random_bounded(3000)),
            static_cast<int32_t>(host_.random_bounded(3000))
        };
        e.velocity = {
            static_cast<int32_t>((20 - host_.random_bounded(40)) * 0x4000),
            static_cast<int32_t>(host_.random_bounded(10) << 16),
            static_cast<int32_t>((20 - host_.random_bounded(40)) * 0x4000)
        };
        e.descriptor_flags = static_cast<uint8_t>(
            ((flags >> 4U) & 1U) | ((flags >> 2U) & 2U) | ((flags & 1U) << 2U) |
            ((flags & 4U) << 1U) | ((flags & 2U) << ((flags & 1U) ? 4U : 3U))
        );
        e.lifetime = 900;
        host_.set_piece_visible(slot, piece, false);
        sink_.effect(e);
    }
    if ((flags & 0x3f00U) != 0) {
        const auto pos = host_.piece_world(slot, piece);
        for (uint32_t i = 0; i < 6; ++i)
            if ((flags & (0x100U << i)) != 0) {
                Event e{};
                e.kind = EventKind::explosion_sprite;
                e.unit = slot.unit_index;
                e.piece = piece;
                e.code = i;
                e.flags = 0x100U << i;
                e.first = pos;
                sink_.effect(e);
            }
    }
}

} // namespace oa::sim::unit_effects
