// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/command.hpp"
#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/test/check.hpp"
using namespace oa::sim::match_runtime;

int main() {
    CommandSource source{true, true, true, OA_UNIT_FLAG_HAS_WEAPONS, 0, 0, 0, 0, 2};
    CommandTarget target{false, 0x10000001u, 0, 20, 10};
    OA_CHECK(resolve_combat_command(2, source, {}, 10) == "Move_Ground");
    OA_CHECK(resolve_combat_command(3, source, {}, 10) == "Suppress");
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Attack_Chase");
    OA_CHECK(combat_order_kind("Suppress") == 46);
    OA_CHECK(combat_order_kind("AttackSpecial") == 9);
    source.object_present = false;
    OA_CHECK(resolve_combat_command(2, source, {}, 10) == "QMove");
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    source.unit_flags |= OA_UNIT_FLAG_BUILDING;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Attack_NoMove");
    source.primary_weapon_flags = 0x20000;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    target.unit_flags = 0x10000002u;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Attack_NoMove");
    source.primary_weapon_flags = 0;
    target.height = -20;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    source.secondary_weapon_flags = 0x10000;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Attack_NoMove");
    source.secondary_slot_flags = 0;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    target.height = 20;
    source.type_flags = 0x1000;
    source.secondary_slot_flags = 2;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    // A primary weapon with the surface-fire key lets a hovering unit with a
    // water weapon attack at or above sea level (weapons.surface-fire).
    source.primary_surface_fire = true;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Attack_NoMove");
    source.primary_weapon_flags = 0x10000;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Attack_NoMove");
    source.primary_surface_fire = false;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    source.primary_weapon_flags = 0;
    // Below sea level the key changes nothing: a weapon must still be a water weapon.
    target.height = -20;
    source.secondary_weapon_flags = 0;
    source.primary_surface_fire = true;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    source.primary_surface_fire = false;
    source.secondary_weapon_flags = 0x10000;
    target.height = 20;
    source.type_flags = 0x800;
    source.secondary_weapon_flags = 0;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "AirToGround");
    source.type_primary_weapon_flags = 0x100;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "AirStrike");
    target.type_flags = 0x800;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    source.type_primary_weapon_flags = 0;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "AirToAir");
    target.type_flags = 0;
    source.type_flags |= OA_UNIT_DEF_FLAG_HOVER_ATTACK;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "AirToGroundHover");
    target.allied = true;
    source.type_flags = 0;
    OA_CHECK(resolve_combat_command(3, source, target, 10) == "Suppress");
    target.unit_flags = 0;
    OA_CHECK(resolve_combat_command(3, source, target, 10).empty());
    source.unit_flags = 0;
    source.type_flags = OA_UNIT_DEF_FLAG_KAMIKAZE;
    OA_CHECK(resolve_combat_command(3, source, {}, 10) == "Attack_Kamikaze");
    source.can_attack = false;
    OA_CHECK(resolve_combat_command(3, source, {}, 10).empty());

    const RepairEligibility repair{2, 0, 0, 50, 100, 0, 20, 30, 10};
    OA_CHECK(can_repair_target(repair));
    OA_CHECK(!can_repair_target(RepairEligibility{0, 0, 0, 50, 100, 0, 20, 30, 10}));
    OA_CHECK(!can_repair_target(RepairEligibility{2, 0, 0, 100, 100, 0, 20, 30, 10}));
    OA_CHECK(!can_repair_target(RepairEligibility{2, 0, 0, 50, 100, 2, 20, 30, 10}));

    const LoadEligibility load{1, 0, 10, 5, 0, 0, true, 3, 0, -1, 0, 6 << 16, 0, 5};
    OA_CHECK(can_load_target(load));
    OA_CHECK(
        !can_load_target(LoadEligibility{0, 0, 10, 5, 0, 0, true, 3, 0, -1, 0, 6 << 16, 0, 5})
    );
    OA_CHECK(
        !can_load_target(LoadEligibility{1, 0, 10, 5, 10, 0, true, 3, 0, -1, 0, 6 << 16, 0, 5})
    );
    OA_CHECK(
        !can_load_target(LoadEligibility{1, 0, 10, 5, 0, 8, true, 3, 0, -1, 0, 6 << 16, 0, 5})
    );

    // Targeted-move resolution (command 2 with a target).
    const auto make_source = [](bool can_capture,
                                bool can_reclaim,
                                bool can_guard,
                                bool can_load,
                                bool aircraft,
                                bool flies) {
        CommandSource s;
        s.can_move = true;
        s.object_present = true;
        s.type_flags = aircraft ? 0x800u : 0u;
        const auto cap = pack_command_capabilities(
            false,
            can_guard,
            false,
            true,
            can_load,
            can_reclaim,
            false,
            can_capture,
            flies,
            false,
            false
        );
        s.abilities_byte0 = cap.abilities_byte0;
        s.abilities_byte1 = cap.abilities_byte1;
        s.flags_byte1 = cap.flags_byte1;
        s.waterline = 0;
        s.capacity = 10;
        s.size = 5;
        return s;
    };
    const auto make_target = [](bool allied, bool air_base = false) {
        CommandTarget t;
        t.allied = allied;
        t.unit_flags = OA_UNIT_FLAG_LIVE;
        t.object_present = true;
        t.health = 50;
        t.max_health = 100;
        t.height = 10;
        t.model_height = 0;
        t.occupancy = 0;
        t.flags_byte1 = air_base ? 0x02u : 0u;
        t.abilities_byte2 = 0;
        t.footprint = 3;
        t.waterline = -1;
        t.y = 0;
        t.y_offset = 6 << 16;
        t.progress = 0.0F;
        return t;
    };
    const auto kind = [](std::string_view n) { return combat_order_kind(n); };
    const auto resolve = [&](const CommandSource& s, const CommandTarget& t) {
        return kind(resolve_combat_command(2, s, t, 5));
    };
    OA_CHECK(
        resolve(make_source(true, false, false, false, false, false), make_target(false)) ==
        capture_kind
    );
    OA_CHECK(
        resolve(make_source(false, true, false, false, false, false), make_target(false)) ==
        reclaim_unit_kind
    );
    OA_CHECK(
        resolve(make_source(false, true, false, false, false, true), make_target(false)) == 59
    );
    // allied repair: health < max_health and progress == 0 -> RepairUnit.
    OA_CHECK(
        resolve(make_source(false, true, false, false, false, false), make_target(true)) ==
        repair_unit_kind
    );
    auto help_target = make_target(true);
    help_target.progress = 1.0F;
    OA_CHECK(
        resolve(make_source(false, true, false, false, false, false), help_target) ==
        help_build_kind
    );
    // aircraft + allied air base -> VTOL_Landing.
    OA_CHECK(
        resolve(make_source(false, false, false, false, true, false), make_target(true, true)) ==
        oa::sim::ground_orders::vtol_landing_kind
    );
    // can_load -> Ground_Pickup / VTOL_Pickup (target is not an air base).
    OA_CHECK(
        resolve(make_source(false, false, false, true, false, false), make_target(true)) ==
        ground_pickup_kind
    );
    OA_CHECK(resolve(make_source(false, false, false, true, true, false), make_target(true)) == 57);
    // can_guard + allied -> Follow_Ground / VTOL_Follow.
    OA_CHECK(
        resolve(make_source(false, false, true, false, false, false), make_target(true)) ==
        follow_ground_kind
    );
    OA_CHECK(resolve(make_source(false, false, true, false, true, false), make_target(true)) == 49);
    // no special -> Move_Ground / VTOL_Move.
    OA_CHECK(
        resolve(make_source(false, false, false, false, false, false), make_target(false)) == 26
    );
    OA_CHECK(
        resolve(make_source(false, false, false, false, true, false), make_target(false)) == 55
    );

    // Mission-table indices of the names the command resolver returns, as the
    // sorted mission table orders them:
    // a structure's move is QMove, not Move_Ground.
    OA_CHECK(combat_order_kind("") == 0);
    OA_CHECK(combat_order_kind("QMove") == 30 && combat_order_kind("Move_Ground") == 26);
    OA_CHECK(combat_order_kind("Attack_Kamikaze") == 7 && combat_order_kind("Attack_NoMove") == 8);
    OA_CHECK(combat_order_kind("VTOL_Pickup") == 57 && combat_order_kind("VTOL_RepairUnit") == 61);
    return oa::test::check_exit_status();
}
