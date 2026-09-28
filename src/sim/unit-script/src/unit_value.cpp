// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_script.hpp"

#include <bit>
#include <cmath>

namespace oa::sim::unit_script {
namespace {

uint32_t bit(uint8_t flags, uint32_t shift) {
    return (static_cast<uint32_t>(flags) >> shift) & 1U;
}

// COB packs a map position as x high word, z low word (both 16.16 high words).
// The unpack rounds x up by one unit when the z half is negative.
struct PackedXz {
    int32_t x;
    int32_t z;
};

PackedXz unpack_xz(uint32_t packed) {
    auto x = packed & 0xffff0000U;
    const auto z = packed << 16;
    if (std::bit_cast<int32_t>(z) < 0)
        x += 0x10000U;
    return {std::bit_cast<int32_t>(x), std::bit_cast<int32_t>(z)};
}

uint32_t pack_xz(int32_t x, int32_t z) {
    return static_cast<uint32_t>(z >> 16) + (std::bit_cast<uint32_t>(x) & 0xffff0000U);
}

// A unit referenced by id: its slot must hold a live unit.
const Unit* live_unit(const World* world, uint32_t id) {
    const auto slot = id & 0xffffU;
    if (slot == 0)
        return nullptr;
    const Unit* unit = world_unit_at(world, slot);
    return unit != nullptr && (unit->flags & OA_UNIT_FLAG_LIVE) != 0 ? unit : nullptr;
}

} // namespace

int32_t unit_script_get_value(
    World* world,
    Unit* unit,
    int32_t selector,
    int32_t first,
    int32_t second,
    const UnitValueServices& services
) {
    const auto argument = std::bit_cast<uint32_t>(first);
    uint32_t value = 0;
    switch (static_cast<UnitValue>(selector)) {
    case UnitValue::activation:
        value = bit(unit->state_flags, 0);
        break;
    case UnitValue::standing_move_orders:
        value = (unit->flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT;
        break;
    case UnitValue::standing_fire_orders:
        value = (unit->flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
        break;
    case UnitValue::health: {
        const UnitDef* def = world_unit_def_of(world, unit);
        if (def != nullptr && def->max_damage != 0)
            value =
                static_cast<uint32_t>(static_cast<int32_t>(unit->health) * 100) / def->max_damage;
        break;
    }
    case UnitValue::in_build_stance:
        value = bit(unit->build_flags, 0);
        break;
    case UnitValue::busy:
        value = bit(unit->build_flags, 1);
        break;
    case UnitValue::piece_xz: {
        const auto at = services.piece_world(services.context, world, unit, argument);
        value = pack_xz(at.x, at.z);
        break;
    }
    case UnitValue::piece_y: {
        const oa_fixed height = services.piece_world(services.context, world, unit, argument).y;
        value = std::bit_cast<uint32_t>(height);
        break;
    }
    case UnitValue::unit_xz:
        if (const Unit* target = live_unit(world, argument))
            value = pack_xz(target->position.x, target->position.z);
        break;
    case UnitValue::unit_y:
        if (const Unit* target = live_unit(world, argument)) {
            const oa_fixed height = target->position.y;
            value = std::bit_cast<uint32_t>(height);
        }
        break;
    case UnitValue::unit_height:
        if (const Unit* target = live_unit(world, argument))
            if (const UnitDef* def = world_unit_def_of(world, target)) {
                const oa_fixed model_height = def->model_height;
                value = std::bit_cast<uint32_t>(model_height);
            }
        break;
    case UnitValue::xz_atan: {
        const auto at = unpack_xz(argument);
        const auto heading = services.direction(services.context, at.x, at.z);
        value = static_cast<uint16_t>(heading - unit->heading);
        break;
    }
    case UnitValue::xz_hypot: {
        const auto at = unpack_xz(argument);
        value = services.distance(services.context, at.x, at.z);
        break;
    }
    case UnitValue::atan:
        value = services.direction(services.context, first, second);
        break;
    case UnitValue::hypot:
        value = services.distance(services.context, first, second);
        break;
    case UnitValue::ground_height: {
        const auto at = unpack_xz(argument);
        value = std::bit_cast<uint32_t>(services.ground_height(services.context, world, at.x, at.z))
                << 16;
        break;
    }
    case UnitValue::build_percent_left:
        // 1 - trunc(remaining * -99): 0 when finished, else 1..100.
        if (unit->build_remaining != 0.0F) {
            const double scaled = static_cast<double>(unit->build_remaining) * -99.0;
            if (std::isfinite(scaled) && scaled > -2147483648.0 && scaled < 2147483648.0)
                value = 1U - static_cast<uint32_t>(static_cast<int32_t>(scaled));
        }
        break;
    case UnitValue::yard_open:
        value = bit(unit->build_flags, 2);
        break;
    case UnitValue::bugger_off:
        value = bit(unit->build_flags, 3);
        break;
    case UnitValue::armored:
        value = bit(unit->state_flags, 1);
        break;
    }
    return std::bit_cast<int32_t>(value);
}

void unit_script_set_value(
    World* world, Unit* unit, int32_t selector, int32_t value, const UnitValueServices& services
) {
    const auto low_bit = static_cast<uint8_t>(std::bit_cast<uint32_t>(value) & 1U);
    switch (static_cast<UnitValue>(selector)) {
    case UnitValue::activation:
        services.set_state_flag(services.context, world, unit, state_flag_activated, value != 0);
        break;
    case UnitValue::in_build_stance:
        unit->build_flags =
            static_cast<uint8_t>((unit->build_flags & ~build_flag_in_build_stance) | low_bit);
        break;
    case UnitValue::busy:
        unit->build_flags =
            static_cast<uint8_t>((unit->build_flags & ~build_flag_busy) | (low_bit << 1));
        break;
    case UnitValue::yard_open:
        services.set_yard_open(services.context, world, unit, value);
        break;
    case UnitValue::bugger_off:
        unit->build_flags =
            static_cast<uint8_t>((unit->build_flags & ~build_flag_bugger_off) | (low_bit << 3));
        break;
    case UnitValue::armored:
        services.set_state_flag(services.context, world, unit, state_flag_armored, value != 0);
        break;
    default:
        break;
    }
    unit->events = static_cast<uint16_t>(unit->events | event_script_state_changed);
}

} // namespace oa::sim::unit_script
