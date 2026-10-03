// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/command.hpp"
#include "oa/sim/match_runtime/attack_orders.hpp"
#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/data/mission_types.hpp"
#include "oa/core/unit_def.h"
#include <cstdint>

namespace oa::sim::match_runtime {
namespace {
constexpr uint8_t command_can_capture = 0x10; // second byte of UnitDef.abilities
constexpr uint8_t command_can_reclaim = 0x04; // second byte of UnitDef.abilities
constexpr uint8_t command_can_guard = 0x20;   // first byte of UnitDef.abilities
constexpr uint32_t type_aircraft = OA_UNIT_DEF_FLAG_CAN_FLY;

// Move command with a target: capture/reclaim/repair/load/guard resolution.
std::string_view
targeted_move(const CommandSource& source, const CommandTarget& target, uint8_t sea_level) {
    const bool enemy = !target.allied;
    const bool flies = (source.flags_byte1 & 8u) != 0;
    const bool aircraft = (source.type_flags & type_aircraft) != 0;
    if ((source.abilities_byte1 & command_can_capture) && enemy)
        return "Capture";
    if ((source.abilities_byte1 & command_can_reclaim) && enemy)
        return flies ? "VTOL_ReclaimUnit" : "ReclaimUnit";
    if (target.allied) {
        const RepairEligibility repair{
            source.abilities_byte1,
            source.type_flags,
            source.waterline,
            target.health,
            target.max_health,
            target.occupancy,
            target.model_height,
            target.height,
            sea_level
        };
        if (can_repair_target(repair)) {
            if (target.progress != 0.0F)
                return flies ? "VTOL_HelpBuild" : "HelpBuild";
            if (target.health < target.max_health)
                return flies ? "VTOL_RepairUnit" : "RepairUnit";
        }
    }
    if (aircraft && target.allied && (target.flags_byte1 & 2u))
        return "VTOL_Landing";
    const LoadEligibility load{
        source.abilities_byte1,
        source.flags_byte1,
        source.capacity,
        source.size,
        source.loaded_count,
        target.abilities_byte2,
        target.object_present,
        target.footprint,
        target.occupancy,
        target.waterline,
        target.y,
        target.y_offset,
        target.progress,
        sea_level
    };
    if (can_load_target(load))
        return aircraft ? "VTOL_Pickup" : "Ground_Pickup";
    if ((source.abilities_byte0 & command_can_guard) && target.allied)
        return aircraft ? "VTOL_Follow" : "Follow_Ground";
    return aircraft ? "VTOL_Move" : "Move_Ground";
}
} // namespace

std::string_view resolve_combat_command(
    uint8_t command,
    const CommandSource& source,
    const std::optional<CommandTarget>& target,
    uint8_t sea_level
) {
    constexpr uint32_t active = OA_UNIT_FLAG_LIVE, armed = OA_UNIT_FLAG_HAS_WEAPONS,
                       stationary = OA_UNIT_FLAG_BUILDING;
    constexpr uint32_t aircraft = OA_UNIT_DEF_FLAG_CAN_FLY, hover = OA_UNIT_DEF_FLAG_CAN_HOVER,
                       hover_attack = OA_UNIT_DEF_FLAG_HOVER_ATTACK,
                       kamikaze = OA_UNIT_DEF_FLAG_KAMIKAZE;
    constexpr uint32_t water_weapon = OA_WEAPON_FLAG_WATER_WEAPON,
                       to_air_weapon = OA_WEAPON_FLAG_TO_AIR_WEAPON,
                       dropped_weapon = OA_WEAPON_FLAG_DROPPED;
    if (target && !(target->unit_flags & active))
        return {};
    if (command == 2) {
        if (!source.can_move)
            return {};
        if (!source.object_present)
            return "QMove";
        if (target)
            return targeted_move(source, *target, sea_level);
        return source.type_flags & aircraft ? "VTOL_Move" : "Move_Ground";
    }
    if (command != 3)
        return {};
    if (!source.can_attack)
        return {};
    if (source.unit_flags & armed) {
        if (!target || target->allied) {
            if (source.primary_weapon_flags & to_air_weapon)
                return {};
            if (!(source.type_flags & aircraft))
                return "Suppress";
            return source.type_primary_weapon_flags & dropped_weapon ? "AirStrike" : "AirToGround";
        }
        if ((target->unit_flags & 3) != 2 && (source.primary_weapon_flags & to_air_weapon))
            return {};
        const auto top = static_cast<int32_t>(target->height) + target->model_height;
        const bool primary_water = (source.primary_weapon_flags & water_weapon) != 0;
        const bool secondary_water =
            (source.secondary_slot_flags & 2) && (source.secondary_weapon_flags & water_weapon);
        if (top < sea_level && !primary_water && !secondary_water)
            return {};
        if (top >= sea_level && (source.type_flags & hover) && !source.primary_surface_fire &&
            (primary_water || secondary_water))
            return {};
        if (!(source.type_flags & aircraft)) {
            if (source.object_present)
                return "Attack_Chase";
            if (source.unit_flags & stationary)
                return "Attack_NoMove";
        } else {
            const bool dropped = (source.type_primary_weapon_flags & dropped_weapon) != 0;
            const bool airborne = (target->type_flags & aircraft) != 0;
            if (dropped && !airborne)
                return "AirStrike";
            if (!dropped && airborne)
                return "AirToAir";
            if (airborne || (source.type_flags & hover_attack)) {
                if (airborne || !(source.type_flags & hover_attack))
                    return {};
                return "AirToGroundHover";
            }
            return "AirToGround";
        }
    }
    return source.type_flags & kamikaze ? "Attack_Kamikaze" : "";
}

uint8_t combat_order_kind(std::string_view name) {
    return data::mission_types::index_for_name(name);
}

bool can_repair_target(const RepairEligibility& e) noexcept {
    // Gate on source repair capability, target damage, occupancy, then
    // the aircraft/waterline sea-level pairs.
    if ((e.source_abilities_byte1 & 2) == 0)
        return false;
    if (e.target_health == e.target_max_health)
        return false;
    if (e.target_occupancy == 2)
        return false;
    const bool aircraft = (e.source_type_flags & OA_UNIT_DEF_FLAG_CAN_FLY) != 0;
    const auto target_top =
        static_cast<int32_t>(e.target_height) + static_cast<int32_t>(e.target_y);
    if (aircraft && (e.source_type_flags & OA_UNIT_DEF_FLAG_AMPHIBIOUS) == 0)
        if (target_top < static_cast<int32_t>(e.sea_level))
            return false;
    if (!aircraft) {
        const auto floor =
            static_cast<int32_t>(e.sea_level) - static_cast<int32_t>(e.source_waterline);
        if (target_top < floor)
            return false;
    }
    return true;
}

bool can_load_target(const LoadEligibility& e) noexcept {
    // Target loadable, source carrier, spare capacity, footprint fit,
    // occupancy, air/ground, above-sea, and finished-build gates.
    if ((e.target_abilities_byte2 & 8) != 0)
        return false;
    if ((e.source_abilities_byte1 & 1) == 0)
        return false;
    if (e.loaded_count >= e.source_capacity)
        return false;
    if (!e.target_present)
        return false;
    if (e.target_footprint > e.source_size)
        return false;
    if (e.target_occupancy == 2)
        return false;
    if ((e.source_flags_byte1 & 8) == 0 && e.target_waterline >= 0)
        return false;
    const auto target_top =
        static_cast<int32_t>(e.target_y_offset) + static_cast<int32_t>(e.target_y);
    if (target_top <= static_cast<int32_t>(e.sea_level) << 16)
        return false;
    if (e.target_progress != 0.0F)
        return false;
    return true;
}
} // namespace oa::sim::match_runtime
