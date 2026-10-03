// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/unit_spawn/legacy_views.hpp"
#include "oa/sim/ground_orders/orders.hpp"
#include <array>
#include <cstdint>

namespace oa::sim::match_runtime {
// Mission kinds: indices into the sorted mission table.
inline constexpr uint8_t building_build_kind = 12;      // BuildingBuild
inline constexpr uint8_t follow_ground_kind = 18;       // Follow_Ground
inline constexpr uint8_t get_built_kind = 19;           // GetBuilt
inline constexpr uint8_t help_build_kind = 23;          // HelpBuild
inline constexpr uint8_t mobile_build_kind = 25;        // MobileBuild
inline constexpr uint8_t patrol_kind = 29;              // Patrol
inline constexpr uint8_t capture_kind = 14;             // Capture
inline constexpr uint8_t ground_pickup_kind = 20;       // Ground_Pickup
inline constexpr uint8_t ground_unload_kind = 21;       // Ground_Unload
inline constexpr uint8_t reclaim_kind = 32;             // Reclaim
inline constexpr uint8_t reclaim_unit_kind = 33;        // ReclaimUnit
inline constexpr uint8_t repair_patrol_kind = 34;       // RepairPatrol
inline constexpr uint8_t repair_unit_kind = 35;         // RepairUnit
inline constexpr uint8_t be_carried_kind = 11;          // BeCarried
inline constexpr uint8_t vtol_get_repaired_kind = 50;   // VTOL_GetRepaired
inline constexpr uint8_t self_repair_kind = 40;         // SelfRepair
inline constexpr uint8_t repair_unit_no_move_kind = 36; // RepairUnitNoMove
inline constexpr uint8_t vtol_help_build_kind = 51;     // VTOL_HelpBuild
inline constexpr uint8_t vtol_mobile_build_kind = 54;   // VTOL_MobileBuild
inline constexpr uint8_t vtol_patrol_kind = 56;         // VTOL_Patrol
inline constexpr uint8_t vtol_reclaim_kind = 58;        // VTOL_Reclaim
inline constexpr uint8_t vtol_reclaim_unit_kind = 59;   // VTOL_ReclaimUnit
inline constexpr uint8_t vtol_repair_patrol_kind = 60;  // VTOL_RepairPatrol
inline constexpr uint8_t vtol_repair_unit_kind = 61;    // VTOL_RepairUnit

struct VtolRepairStep {
    uint32_t result{};
    uint32_t announce{};
    bool wait{};
};

/// Runs one step of VTOL_GetRepaired.
///
/// Without a target it announces speech category 7 and fails. Phase 0 waits
/// 30 ticks while health is below maximum, then advances; phase 1 announces
/// category 10 and finishes.
///
/// @param has_target Whether the order still has its repair pad.
/// @param phase The order's phase.
/// @param health The aircraft's health.
/// @param maximum Its type's maximum health.
/// @return The step result (1 next phase, 2 keep waiting, 5 done, 7 invalid,
///     8 failed), the speech category to announce (0 none) and whether to
///     wait 30 ticks.
/// @quirk Health is sign-extended and then compared unsigned, so a negative
///     health counts as fully repaired.
inline VtolRepairStep
vtol_get_repaired(bool has_target, uint8_t phase, int16_t health, uint32_t maximum) noexcept {
    if (!has_target)
        return {8, 7, false};
    if (phase == 0) {
        const auto current = static_cast<uint32_t>(static_cast<int32_t>(health));
        if (current < maximum)
            return {2, 0, true};
        return {1, 0, false};
    }
    if (phase == 1)
        return {5, 10, false};
    return {7, 0, false};
}

inline constexpr uint32_t mission_finished = 5;

/// Runs the blank mission record (table entry 0), which finishes at once.
///
/// @return 5 (done).
inline constexpr uint32_t blank_mission_step() noexcept {
    return mission_finished;
}

// Descriptor words of mission kinds, as mission_descriptor_table holds them.
inline constexpr uint32_t follow_ground_flags = 0x200u;
inline constexpr uint32_t building_build_flags = 0x10010cu;
inline constexpr uint32_t mobile_build_flags = 0x100508u;
inline constexpr uint32_t get_built_flags = 0x224u;
inline constexpr uint32_t patrol_flags = 0x412u;
inline constexpr uint32_t help_build_flags = 0x100208u;
inline constexpr uint32_t repair_unit_flags = 0x100200u;
inline constexpr uint32_t reclaim_unit_flags = 0x100200u; // same queue descriptor as RepairUnit
inline constexpr uint32_t capture_flags = 0x200u;
inline constexpr uint32_t ground_pickup_flags = 0x200u;
inline constexpr uint32_t ground_unload_flags = 0x400u;

inline constexpr std::size_t mission_kind_count = 68;
// Sorted mission descriptor table: the descriptor word of every mission
// kind's table entry (preserve, command and order flags, low byte first), as
// order creation reads it.
inline constexpr std::array<uint32_t, mission_kind_count> mission_descriptor_table = {
    0x00000000u, //  0
    0x00010060u, //  1 Activate
    0x00000600u, //  2 AirStrike
    0x00000200u, //  3 AirToAir
    0x00000200u, //  4 AirToGround
    0x00000200u, //  5 AirToGroundHover
    0x00000280u, //  6 Attack_Chase
    0x00000600u, //  7 Attack_Kamikaze
    0x00000280u, //  8 Attack_NoMove
    0x00000680u, //  9 AttackSpecial
    0x00000004u, // 10 AttackUType
    0x00000024u, // 11 BeCarried
    0x0010010cu, // 12 BuildingBuild
    0x000c0140u, // 13 BuildWeapon
    0x00000200u, // 14 Capture
    0x00010060u, // 15 Cloak_Off
    0x00010060u, // 16 Cloak_On
    0x00010060u, // 17 Deactivate
    0x00000200u, // 18 Follow_Ground
    0x00000224u, // 19 GetBuilt
    0x00000200u, // 20 Ground_Pickup
    0x00000400u, // 21 Ground_Unload
    0x00000020u, // 22 Guard_NoMove
    0x00100208u, // 23 HelpBuild
    0x00000004u, // 24 MakeSelectable
    0x00100508u, // 25 MobileBuild
    0x00000402u, // 26 Move_Ground
    0x00000024u, // 27 Paralyze
    0x00000000u, // 28 Park
    0x00000412u, // 29 Patrol
    0x00000400u, // 30 QMove
    0x00000400u, // 31 QPatrol
    0x00100800u, // 32 Reclaim
    0x00100200u, // 33 ReclaimUnit
    0x00000412u, // 34 RepairPatrol
    0x00100200u, // 35 RepairUnit
    0x00000200u, // 36 RepairUnitNoMove
    0x00000200u, // 37 Resurrect
    0x00040040u, // 38 SelfDestruct
    0x00000000u, // 39 SelfDestructFG
    0x01000204u, // 40 SelfRepair
    0x00020000u, // 41 Standby
    0x01020000u, // 42 Standby_Mine
    0x00010060u, // 43 Standing_FireOrder
    0x00010060u, // 44 Standing_MoveOrder
    0x00000000u, // 45 Stop
    0x00000410u, // 46 Suppress
    0x00000600u, // 47 Teleport
    0x00000000u, // 48 VTOL_Evade
    0x00000200u, // 49 VTOL_Follow
    0x00000200u, // 50 VTOL_GetRepaired
    0x00100208u, // 51 VTOL_HelpBuild
    0x00000400u, // 52 VTOL_LandIfCan
    0x00000600u, // 53 VTOL_Landing
    0x00100508u, // 54 VTOL_MobileBuild
    0x00000402u, // 55 VTOL_Move
    0x00000412u, // 56 VTOL_Patrol
    0x00000200u, // 57 VTOL_Pickup
    0x00100800u, // 58 VTOL_Reclaim
    0x00100200u, // 59 VTOL_ReclaimUnit
    0x00000412u, // 60 VTOL_RepairPatrol
    0x00100200u, // 61 VTOL_RepairUnit
    0x00000600u, // 62 VTOL_SeekAttack
    0x00000600u, // 63 VTOL_SeekGuard
    0x00020000u, // 64 VTOL_Standby
    0x00000400u, // 65 VTOL_Unload
    0x00000004u, // 66 Wait
    0x00000204u, // 67 WaitForAttack
};

// The construction family's copy of an order's words.
struct ConstructionOrderState {
    sim::simulation_state::Unit* target{}; // the order's target unit
    int32_t type_index{};                  // first parameter
    int32_t remaining{};                   // second parameter: BuildingBuild repeat count
    int32_t blocked_retries{};             // third parameter
    int8_t pad_piece{-1};                  // QueryBuildInfo piece, kept as a signed char
    // Quarter turns from south the building is placed facing
    // (units.build-rotation; Match::set_build_facing); 0 in 3.1c.
    uint8_t facing{};
};

/// Snaps a build site's x and z to the footprint-centred cell grid, as
/// MobileBuild's first phase and mission unit placement do; y is left
/// unchanged.
///
/// @param[in,out] destination Signed 16.16 site; x and z are snapped.
/// @param footprint_x Footprint width in cells.
/// @param footprint_z Footprint depth in cells.
void snap_build_position(
    sim::ground_orders::Point& destination, int16_t footprint_x, int16_t footprint_z
);

} // namespace oa::sim::match_runtime
