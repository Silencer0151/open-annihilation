// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <optional>
#include <string_view>

namespace oa::sim::match_runtime {
// Bits of an order's command flags (Match::OrderRecordView::command_flags).
inline constexpr uint8_t command_counts_builds =
    0x01; // its second parameter shows on the build button
inline constexpr uint8_t command_has_target = 0x02; // the order keeps its target unit
inline constexpr uint8_t command_has_point = 0x04;  // the order keeps its destination point
inline constexpr uint8_t command_queue_tail =
    0x10;                                         // the order the next queued order is placed after
inline constexpr uint8_t command_unqueued = 0x20; // given without the queue key
inline constexpr uint8_t command_overlay = 0x40;  // dropped from the queue head by the next insert

// Projections consumed by command cases 2 (move) and 3 (attack) of the
// command resolver.
struct CommandSource {
    bool can_move{}, can_attack{}, object_present{};
    uint32_t unit_flags{}, type_flags{}, primary_weapon_flags{}, secondary_weapon_flags{},
        type_primary_weapon_flags{};
    uint8_t secondary_slot_flags{};
    // Packed command-capability bytes (the low two bytes of UnitDef.abilities)
    // projected by the caller from the FBI loader's record: the first carries
    // can_attack/can_guard/can_patrol/can_move, the second can_load/
    // can_reclaim(0x06)/can_resurrect/can_capture.
    uint8_t abilities_byte0{}, abilities_byte1{}, flags_byte1{};
    int16_t waterline{};        // FBI waterline; can_repair_target's source_waterline
    uint8_t capacity{}, size{}; // UnitDef.transport_capacity, transport_size
    uint32_t loaded_count{};    // units the source carries
    // The primary weapon has the surface-fire key (weapons.surface-fire): a
    // hovering unit with a water weapon may then attack a target at or above
    // sea level.
    bool primary_surface_fire{};
};

struct CommandTarget {
    bool allied{};
    uint32_t unit_flags{}, type_flags{};
    // The whole parts of Unit.position.y and UnitDef.model_height.
    int16_t height{}, model_height{};
    // Targeted-move fields for the repair and load predicates and the resolution.
    bool object_present{};
    int16_t health{};          // Unit.health
    int32_t max_health{};      // UnitDef.max_damage
    uint8_t occupancy{};       // Unit.flags & OA_UNIT_FLAG_OCCUPANCY_MASK
    uint8_t flags_byte1{};     // second byte of UnitDef.flags (bit1 air base)
    uint8_t abilities_byte2{}; // third byte of UnitDef.abilities (bit3 no_load)
    int16_t footprint{};       // UnitDef.footprint_x
    int16_t waterline{};       // UnitDef.min_water_depth
    uint32_t y{};              // Unit.position.y, raw 16.16
    uint32_t y_offset{};       // UnitDef.model_height, raw 16.16
    float progress{};          // Unit.build_remaining (0.0 = finished)
};

// The FBI loader packs the capability booleans into the first three bytes of
// UnitDef.abilities and the second byte of UnitDef.flags, which the command
// resolver and the eligibility predicates read. can_reclaim sets bits 1 and 2
// (0x06) of the second abilities byte.
struct CommandCapabilities {
    uint8_t abilities_byte0{}, abilities_byte1{}, flags_byte1{}, abilities_byte2{};
};

/// Packs FBI capability booleans into the type's command bytes as the FBI
/// loader does.
///
/// @param can_attack canattack; abilities byte 0, bit 0x10.
/// @param can_guard canguard; abilities byte 0, bit 0x20.
/// @param can_patrol canpatrol; abilities byte 0, bit 0x40.
/// @param can_move canmove; abilities byte 0, bit 0x80.
/// @param can_load canload; abilities byte 1, bit 0x01.
/// @param can_reclaim canreclaim; abilities byte 1, bits 0x06.
/// @param can_resurrect canresurrect; abilities byte 1, bit 0x08.
/// @param can_capture cancapture; abilities byte 1, bit 0x10.
/// @param can_fly canfly; flags byte 1, bit 0x08.
/// @param is_airbase isairbase; flags byte 1, bit 0x02.
/// @param no_transport cantbetransported; abilities byte 2, bit 0x08.
/// @return The four packed bytes.
inline CommandCapabilities pack_command_capabilities(
    bool can_attack,
    bool can_guard,
    bool can_patrol,
    bool can_move,
    bool can_load,
    bool can_reclaim,
    bool can_resurrect,
    bool can_capture,
    bool can_fly,
    bool is_airbase,
    bool no_transport
) noexcept {
    CommandCapabilities capabilities;
    capabilities.abilities_byte0 = static_cast<uint8_t>(
        (can_attack ? 0x10u : 0u) | (can_guard ? 0x20u : 0u) | (can_patrol ? 0x40u : 0u) |
        (can_move ? 0x80u : 0u)
    );
    capabilities.abilities_byte1 = static_cast<uint8_t>(
        (can_load ? 0x01u : 0u) | (can_reclaim ? 0x06u : 0u) | (can_resurrect ? 0x08u : 0u) |
        (can_capture ? 0x10u : 0u)
    );
    capabilities.flags_byte1 =
        static_cast<uint8_t>((can_fly ? 0x08u : 0u) | (is_airbase ? 0x02u : 0u));
    capabilities.abilities_byte2 = static_cast<uint8_t>(no_transport ? 0x08u : 0u);
    return capabilities;
}

/// Resolves a move or attack command to the name of the mission it gives.
///
/// Case 2 (move) picks QMove, a targeted capture/reclaim/help-build/repair/
/// landing/load/guard mission or a plain move; case 3 (attack) applies the
/// water-weapon, aircraft, alliance and availability gates.
///
/// @param command Command case: 2 move, 3 attack; another gives no mission.
/// @param source The commanded unit's projection.
/// @param target The unit under the cursor, if any; an inactive target gives
///     no mission.
/// @param sea_level Map sea level in whole height units.
/// @return The mission name, or an empty name (mission index zero) for none.
std::string_view resolve_combat_command(
    uint8_t command,
    const CommandSource& source,
    const std::optional<CommandTarget>& target,
    uint8_t sea_level
);

/// Looks up the mission-table index of a name resolve_combat_command returns.
///
/// @param name Mission name.
/// @return The mission kind, 0 for none.
uint8_t combat_order_kind(std::string_view name);

// Inputs of can_repair_target: the source's repair ability bit, its
// aircraft/repair-air type flags and its water depth, and the target
// health/occupancy/height against sea level. The repair missions pass the
// source's UnitDef.max_water_depth as its water depth; the command resolver
// passes its FBI waterline.
struct RepairEligibility {
    uint8_t source_abilities_byte1{}; // second byte of UnitDef.abilities
    uint32_t source_type_flags{};     // UnitDef.flags
    int16_t source_waterline{};       // max_water_depth, or the FBI waterline (see above)
    int16_t target_health{};          // Unit.health
    int32_t target_max_health{};      // UnitDef.max_damage
    uint8_t target_occupancy{};       // Unit.flags & OA_UNIT_FLAG_OCCUPANCY_MASK
    int16_t target_height{};          // whole part of UnitDef.model_height
    int16_t target_y{};               // whole part of Unit.position.y
    uint8_t sea_level{};              // Game.sea_level
};

/// Tells whether the source can repair this target.
///
/// Gates, in order: the source's repair bit, target damage, target
/// occupancy (not in the air), then the sea-level test of an aircraft
/// without repair-air or the waterline test of anything else.
///
/// @param eligibility Source and target projection.
/// @return True when the repair is allowed.
[[nodiscard]] bool can_repair_target(const RepairEligibility& eligibility) noexcept;

// Inputs of can_load_target. loaded_count is the source's child count; the
// caller supplies it from the attachment links.
struct LoadEligibility {
    uint8_t source_abilities_byte1{}; // second byte of UnitDef.abilities; bit0 can_load
    uint8_t source_flags_byte1{};     // second byte of UnitDef.flags; bit3 aircraft
    uint8_t source_capacity{};        // UnitDef.transport_capacity
    uint8_t source_size{};            // UnitDef.transport_size
    uint32_t loaded_count{};          // units the source carries
    uint8_t target_abilities_byte2{}; // third byte of UnitDef.abilities; bit3 no_load
    bool target_present{};            // the target has a movement object (Unit.movement)
    int16_t target_footprint{};       // UnitDef.footprint_x
    uint8_t target_occupancy{};       // Unit.flags & OA_UNIT_FLAG_OCCUPANCY_MASK
    int16_t target_waterline{};       // UnitDef.min_water_depth
    uint32_t target_y{};              // Unit.position.y, raw 16.16
    uint32_t target_y_offset{};       // UnitDef.model_height, raw 16.16
    float target_progress{};          // Unit.build_remaining (0.0 = finished)
    uint8_t sea_level{};              // Game.sea_level
};

/// Tells whether the source can load this target.
///
/// Gates, in order: the target is transportable, the source can load and has
/// spare capacity, the target exists and fits, is not in the air, is under
/// water only for an air transport, stands above sea level and is finished.
///
/// @param eligibility Source and target projection.
/// @return True when the load is allowed.
[[nodiscard]] bool can_load_target(const LoadEligibility& eligibility) noexcept;
} // namespace oa::sim::match_runtime
