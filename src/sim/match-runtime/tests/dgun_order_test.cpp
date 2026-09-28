// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The commander's D-gun in the running match: AttackSpecial aims the third,
// commandfire weapon at its target; the weapon tick fires it like any slot
// with a target, pays its energy and raises the commandfire shot event,
// which ends the attack order. Only the weapon sweep of a computer player
// aims a commandfire weapon by itself.
#include "combat_fixture.hpp"
#include "oa/sim/match_runtime/attack_orders.hpp"
#include "oa/sim/weapon_execution/retaliation.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;

constexpr uint8_t dgun_slot = 2;
constexpr uint8_t dgun_weapon = 2; // registry index
constexpr float dgun_energy = 400.0F;
constexpr uint32_t dgun_reload_ticks = 36; // reloadtime 1.2 at 30 ticks a second
constexpr float start_energy = 1000.0F;

int32_t tertiary_shots(Fixture& f, const sim::unit_spawn::Slot& slot) {
    auto* instance = f.match->instance(slot.unit_index);
    CHECK(instance && instance->script());
    return instance->script()->vm().static_value(2).value_or(-1);
}

int32_t dgun_projectiles(Fixture& f, const sim::unit_spawn::Slot& source) {
    const auto& world = f.match->state();
    int32_t count = 0;
    for (int32_t i = 0; i < world.game.projectile_count; ++i)
        if (world.projectiles[i].def == oa_ref_from_index(dgun_weapon) &&
            world.projectiles[i].source == oa_unit_ref_from_slot(source.unit_index))
            ++count;
    return count;
}

void fund(Fixture& f) {
    for (uint8_t player = 0; player < 2; ++player) {
        auto& record = f.match->state().game.players[player];
        record.energy = record.energy_storage = start_energy;
        record.metal = record.metal_storage = start_energy;
    }
}

// An enemy that cannot shoot back, so no retaliation order follows the D-gun's.
sim::unit_spawn::Slot& unarmed_enemy(Fixture& f, uint32_t x, uint32_t z) {
    auto& enemy = f.spawn(1, x, z);
    for (auto& weapon : enemy.record.weapons)
        weapon.flags = static_cast<uint8_t>(weapon.flags & ~OA_UNIT_WEAPON_ENABLED);
    return enemy;
}

bool attacking(const sim::unit_spawn::Slot& slot) {
    for (const auto* order = slot.unit->primary; order != nullptr; order = order->next)
        if (order->kind == sim::match_runtime::attack_chase_kind ||
            order->kind == sim::match_runtime::attack_special_kind)
            return true;
    return false;
}

// A human commander shooting with its first gun: the commandfire gun stays
// quiet until AttackSpecial names a target beyond its reach; the commander
// closes in, the first gun still firing, and the D-gun fires once.
void attack_special_fires_the_dgun_once() {
    Options options;
    options.dgun = true;
    Fixture f(options);
    fund(f);
    auto& commander = f.spawn(0, 40, 128);
    auto& enemy = unarmed_enemy(f, 180, 128);
    CHECK((commander.record.weapons[dgun_slot].flags & OA_UNIT_WEAPON_ENABLED) != 0);
    const auto* dgun = world_weapon_def(&f.match->state(), commander.record.weapons[dgun_slot].def);
    CHECK(dgun != nullptr && (dgun->flags & OA_WEAPON_FLAG_COMMAND_FIRE) != 0);
    CHECK(dgun->energy_per_shot == dgun_energy);

    // The first gun already shoots at the enemy; nothing aims the D-gun.
    sim::weapon_execution::aim_slot_at_unit(commander.record, enemy.record, 0);
    f.run(60);
    CHECK(f.shots_from(commander) > 0);
    CHECK(tertiary_shots(f, commander) == 0);

    auto& player = f.match->state().game.players[0];
    player.energy = start_energy;
    const auto primary_before = f.shots_from(commander);
    const std::array<uint32_t, 3> enemy_position = enemy.unit->position;
    const sim::ground_orders::Point at_enemy{
        std::bit_cast<int32_t>(enemy_position[0]),
        std::bit_cast<int32_t>(enemy_position[1]),
        std::bit_cast<int32_t>(enemy_position[2])
    };
    auto& order =
        f.match->issue_attack_special(commander.unit_index, at_enemy, false, enemy.unit_index);
    CHECK(order.kind == sim::match_runtime::attack_special_kind);
    uint32_t fired_at = 0;
    for (uint32_t tick = 1; tick <= 300 && fired_at == 0; ++tick) {
        f.run(1);
        if (tertiary_shots(f, commander) != 0)
            fired_at = tick;
    }
    CHECK(fired_at != 0);
    CHECK(tertiary_shots(f, commander) == 1 && dgun_projectiles(f, commander) == 1);
    // The first gun kept firing while the commander closed in.
    CHECK(f.shots_from(commander) > primary_before);
    CHECK(player.energy == start_energy - dgun_energy);
    CHECK(enemy.unit->record.type_index != 0);

    // The commandfire shot event ends the order, and the order's end frees
    // the slots, so the D-gun stays quiet past its reload.
    f.run(2);
    CHECK(!attacking(commander));
    CHECK(commander.record.weapons[dgun_slot].target_a == 0);
    f.run(3 * dgun_reload_ticks);
    CHECK(tertiary_shots(f, commander) == 1);
    CHECK(player.energy == start_energy - dgun_energy);
    std::cout << "AttackSpecial fired the D-gun once, " << fired_at << " ticks after the order\n";
}

// Without the energy for a shot the D-gun waits, aimed, and fires once the
// energy is there.
void dgun_waits_for_energy() {
    Options options;
    options.dgun = true;
    Fixture f(options);
    fund(f);
    auto& commander = f.spawn(0, 100, 128);
    auto& enemy = unarmed_enemy(f, 160, 128);
    auto& player = f.match->state().game.players[0];
    player.energy = dgun_energy - 1.0F;
    const std::array<uint32_t, 3> enemy_position = enemy.unit->position;
    const sim::ground_orders::Point at_enemy{
        std::bit_cast<int32_t>(enemy_position[0]),
        std::bit_cast<int32_t>(enemy_position[1]),
        std::bit_cast<int32_t>(enemy_position[2])
    };
    (void)f.match->issue_attack_special(commander.unit_index, at_enemy, false, enemy.unit_index);
    f.run(60);
    CHECK(tertiary_shots(f, commander) == 0 && attacking(commander));
    player.energy = dgun_energy;
    f.run(10);
    CHECK(tertiary_shots(f, commander) == 1 && player.energy == 0.0F);
    std::cout << "the D-gun waited for its energy\n";
}

} // namespace

int main() {
    try {
        attack_special_fires_the_dgun_once();
        dgun_waits_for_energy();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
