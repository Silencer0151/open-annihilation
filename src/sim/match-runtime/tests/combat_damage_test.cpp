// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The match's damage path over the retaliation and paralysis ports: an idle
// unit hit by an enemy in range attacks it, a busy one does not, and a
// paralyzer hit stops a unit firing and taking orders for the clamped
// duration while it keeps steering along a route it already had.
#include "combat_fixture.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;

void idle_unit_attacks_its_attacker() {
    Fixture f;
    auto& victim = f.spawn(0, 64, 64);
    auto& attacker = f.spawn(1, 160, 64);
    f.run(1);
    CHECK(victim.unit->primary == nullptr);
    f.match->apply_damage_event(victim, &attacker, 1, weapon_hit, 0);
    // A hit stores 0xF0 in Unit.damage_countdown.
    CHECK(victim.record.damage_countdown == 0xf0);
    CHECK(head_is(victim, attack_chase_order));
    // The attack order carries the notice-muting bit and is the head the
    // notice reads.
    CHECK(f.services.notices == 0);
    CHECK(
        victim.record.last_attacker_id == attacker.unit_index &&
        victim.record.damage_kind == weapon_hit
    );
    f.run(30);
    CHECK(f.shots_from(victim) > 0);
    std::cout << "idle unit attacks its attacker passed\n";
}

void busy_unit_does_not_chase() {
    Fixture f;
    auto& victim = f.spawn(0, 64, 64);
    auto& attacker = f.spawn(1, 160, 64);
    f.run(1);
    (void)f.match->issue_ground_move(victim.unit_index, {200 << 16, 0, 200 << 16}, false);
    CHECK(head_is(victim, sim::ground_orders::move_ground_kind));
    f.match->apply_damage_event(victim, &attacker, 1, weapon_hit, 0);
    CHECK(head_is(victim, sim::ground_orders::move_ground_kind));
    CHECK(victim.unit->primary->next == nullptr);
    // The tracked weapon still turns on the attacker, and the move order lets
    // the notice through.
    CHECK(victim.record.weapons[0].target_a == static_cast<int16_t>(attacker.unit_index));
    CHECK(f.services.notices == 1);
    std::cout << "busy unit does not chase passed\n";
}

// A unit attacking its attacker is paralyzed: the paralyze order goes in
// front of the attack order, the weapon loses its target and hits do not
// re-aim it until the order has waited out its duration. The attack order is
// head again but stays parked on the events it waited for (Attack_Chase
// phase 1), while the paralyze order's teardown frees the slot (weapon stand
// down); the attacker's next hit re-aims it (the damage reaction) and the
// unit fires.
void paralysis_stops_and_resumes() {
    Fixture f;
    auto& victim = f.spawn(0, 64, 64);
    auto& attacker = f.spawn(1, 160, 64);
    f.run(1);
    f.match->apply_damage_event(victim, &attacker, 1, weapon_hit, 0);
    f.run(30);
    CHECK(head_is(victim, attack_chase_order) && f.shots_from(victim) > 0);

    // A second hit before the order runs extends the head paralyze order.
    f.match->apply_damage_event(victim, &attacker, 40, paralyzer_hit, 0);
    CHECK(head_is(victim, paralyze_order));
    CHECK(
        victim.unit->primary->next != nullptr &&
        victim.unit->primary->next->kind == attack_chase_order
    );
    f.match->apply_damage_event(victim, &attacker, 20, paralyzer_hit, 0);
    CHECK(victim.unit->primary->next->kind == attack_chase_order);
    CHECK(victim.unit->health == 999);

    f.run(1);
    const uint32_t start = f.match->simulation().tick;
    auto& gun = victim.record.weapons[0];
    CHECK(victim.record.state_flags & sim::unit_health::paralyzed_state_flag);
    CHECK(gun.target_a == 0 && !(gun.flags & OA_UNIT_WEAPON_RETALIATE));
    CHECK(victim.unit->primary->wake_tick == start + 60);
    const auto shots = f.shots_from(victim);

    // Hits while paralyzed neither chase nor re-aim.
    f.match->apply_damage_event(victim, &attacker, 1, weapon_hit, 0);
    CHECK(head_is(victim, paralyze_order) && gun.target_a == 0);
    f.run(58);
    CHECK(f.shots_from(victim) == shots);
    CHECK(head_is(victim, paralyze_order));
    CHECK(victim.record.state_flags & sim::unit_health::paralyzed_state_flag);

    f.run(2);
    CHECK(!(victim.record.state_flags & sim::unit_health::paralyzed_state_flag));
    CHECK(head_is(victim, attack_chase_order));
    f.run(30);
    CHECK(f.shots_from(victim) > shots);
    std::cout << "paralysis stops and resumes passed\n";
}

void paralysis_is_clamped() {
    Fixture f;
    auto& victim = f.spawn(0, 64, 64);
    auto& attacker = f.spawn(1, 160, 64);
    f.run(1);
    f.match->apply_damage_event(victim, &attacker, 3000, paralyzer_hit, 0);
    f.run(1);
    const uint32_t start = f.match->simulation().tick;
    CHECK(head_is(victim, paralyze_order));
    CHECK(victim.unit->primary->wake_tick == start + sim::unit_health::paralysis_wait_limit);
    f.run(sim::unit_health::paralysis_wait_limit + 1);
    CHECK(!(victim.record.state_flags & sim::unit_health::paralyzed_state_flag));
    CHECK(!head_is(victim, paralyze_order));

    // Immune types ignore the hit.
    f.types[1].simulation.flags |= OA_UNIT_DEF_FLAG_IMMUNE_TO_PARALYZER;
    f.match->reload_unit_defs();
    f.match->apply_damage_event(victim, &attacker, 30, paralyzer_hit, 0);
    CHECK(!head_is(victim, paralyze_order));
    std::cout << "paralysis clamp passed\n";
}

// 3.1c drives every unit's movement object whatever order heads the queue,
// and inserting the paralyze order leaves the preempted
// order's goal installed. A unit paralyzed mid-move therefore keeps steering
// along that route and brakes where it ends instead of sliding on; the Move
// order finishes after the paralysis.
void paralysis_keeps_the_route() {
    Fixture f;
    auto& mover = f.spawn(0, 40, 64);
    f.run(1);
    (void)f.match->issue_ground_move(mover.unit_index, {140 << 16, 0, 64 << 16}, false);
    f.run(30);
    const uint32_t paralyzed_at = mover.unit->position[0];
    CHECK(paralyzed_at > (40u << 16) && paralyzed_at < (120u << 16));
    f.match->apply_damage_event(mover, nullptr, 90, paralyzer_hit, 0);
    CHECK(head_is(mover, paralyze_order));
    CHECK(
        mover.unit->primary->next != nullptr &&
        mover.unit->primary->next->kind == sim::ground_orders::move_ground_kind
    );
    f.run(80);
    CHECK(head_is(mover, paralyze_order));
    CHECK(mover.record.state_flags & sim::unit_health::paralyzed_state_flag);
    const uint32_t stopped_at = mover.unit->position[0];
    const auto x = static_cast<int32_t>(stopped_at >> 16);
    CHECK(x > 120 && x < 160);
    f.run(5);
    CHECK(mover.unit->position[0] == stopped_at);
    f.run(30);
    CHECK(!(mover.record.state_flags & sim::unit_health::paralyzed_state_flag));
    CHECK(mover.unit->primary == nullptr);

    // An idle unit has no route and stays where it was hit.
    auto& idle = f.spawn(0, 64, 160);
    f.run(1);
    const std::array<uint32_t, 3> rest = idle.unit->position;
    f.match->apply_damage_event(idle, nullptr, 40, paralyzer_hit, 0);
    f.run(30);
    CHECK(head_is(idle, paralyze_order) && idle.unit->position == rest);
    std::cout << "paralysis keeps the route passed\n";
}

// BeCarried's first step releases the tracked weapons. The
// handler's own copy of the slot flags must not restore the cleared bit when
// it writes them back.
void carried_unit_keeps_released_weapons() {
    Fixture f;
    auto& cargo = f.spawn(0, 64, 64);
    auto& carrier = f.spawn(0, 80, 64);
    auto& enemy = f.spawn(1, 200, 64);
    f.run(1);
    f.match->set_carry_link(cargo.unit_index, carrier.unit_index, -1, 1);
    CHECK(cargo.record.attach_parent != 0);
    auto& gun = cargo.record.weapons[0];
    gun.flags |= OA_UNIT_WEAPON_RETALIATE;
    gun.target_a = static_cast<int16_t>(enemy.unit_index);
    (void)f.match->insert_ground_order(cargo.unit_index, sim::match_runtime::be_carried_kind);
    f.run(1);
    CHECK(head_is(cargo, sim::match_runtime::be_carried_kind));
    CHECK(!(gun.flags & OA_UNIT_WEAPON_RETALIATE) && gun.target_a == 0);
    std::cout << "carried unit keeps released weapons passed\n";
}

// A shot striking a unit directly (areaofeffect under 17) damages it alone,
// then marks its shooter's reaction byte (the high byte of Unit.events): 0x40
// when the shot's owner is not the unit's, 0x20 when it is (strike_unit
// through the hit reaction).
void direct_hit_marks_the_shooter() {
    Fixture f;
    auto& gun = const_cast<sim::combat_state::WeaponDefinition&>(f.weapons.definition(1));
    gun.default_damage = 50;
    gun.areaofeffect = 16;
    sim::weapon_execution::store_weapon_defs(f.weapons, f.match->state().game.weapon_defs);
    auto& shooter = f.spawn(0, 64, 64);
    auto& enemy = f.spawn(1, 160, 64);
    auto& friendly = f.spawn(0, 64, 160);
    const auto strike = [&](sim::unit_spawn::Slot& target) {
        shooter.record.events = static_cast<uint16_t>(shooter.record.events & 0x00ffu);
        oa::Projectile shot{};
        shot.def = oa::oa_ref_from_index(1);
        shot.source = oa::oa_unit_ref_from_slot(shooter.unit_index);
        shot.owner_index = shooter.record.owner_index;
        shot.position = target.record.position;
        const auto health = target.unit->health;
        f.match->detonate(shot, &target);
        CHECK(target.unit->health < health);
        return static_cast<uint8_t>(shooter.record.events >> 8);
    };
    CHECK(strike(enemy) == 0x40);
    CHECK(strike(friendly) == 0x20);
    std::cout << "direct hit marks the shooter passed\n";
}

// The death handler reports a player once its unit count (Player.unit_count)
// drops to zero, for the host's departure or elimination notice.
void last_unit_death_is_reported() {
    Fixture f;
    auto& first = f.spawn(1, 64, 64);
    auto& second = f.spawn(1, 160, 64);
    auto& killer = f.spawn(0, 64, 160);

    struct Seen {
        int calls = 0;
        uint8_t player = 0xff;
    } seen;

    f.match->last_unit = {&seen, [](void* context, uint8_t player) {
                              auto& record = *static_cast<Seen*>(context);
                              ++record.calls;
                              record.player = player;
                          }};
    kill(f, first, killer);
    CHECK(seen.calls == 0 && oa::world_player(&f.match->state(), 1)->unit_count == 1);
    kill(f, second, killer);
    CHECK(seen.calls == 1 && seen.player == 1);
    CHECK(oa::world_player(&f.match->state(), 1)->unit_count == 0);
    std::cout << "last unit death is reported passed\n";
}

} // namespace

int main() {
    try {
        idle_unit_attacks_its_attacker();
        busy_unit_does_not_chase();
        paralysis_stops_and_resumes();
        paralysis_is_clamped();
        paralysis_keeps_the_route();
        carried_unit_keeps_released_weapons();
        direct_hit_marks_the_shooter();
        last_unit_death_is_reported();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
