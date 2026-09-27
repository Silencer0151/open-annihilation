// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The checks are asserts; keep them in release builds, where NDEBUG is set.
#undef NDEBUG
#include <cassert>

#include "oa/sim/weapon_execution.hpp"

#include "oa/sim/combat_state.hpp"

#include <vector>
using namespace oa::sim::weapon_execution;
namespace flags = oa::sim::combat_state;

struct Fixture final : Host {
    std::array<bool, 3> target{true, true, true}, reach{true, true, true}, fire{true, true, true};
    std::array<bool, 3> ready{true, true, true};
    bool reserve = true;
    std::vector<int> aim, shots, stock;
    float reserved_energy{}, reserved_metal{};
    int payments{};

    bool resolve_target(uint8_t n, Target& t) override {
        t.point.fixed = {4, 5, 6};
        t.unit_identity = 9;
        return target[n];
    }

    Point source_position() override { return {{1, 2, 3}}; }

    bool ballistic_aim = true;

    bool begin_turret_aim(uint8_t n, const Target&) override {
        aim.push_back(10 + n);
        return ballistic_aim;
    }

    void begin_vlaunch_aim(uint8_t n) override { aim.push_back(20 + n); }

    bool aim_ready(uint8_t n) override { return ready[n]; }

    bool can_reach(uint8_t n, const Point&, const Target&) override { return reach[n]; }

    TurretAim aim_now = TurretAim::on_target;

    TurretAim turret_aim(uint8_t, const Target&) override { return aim_now; }

    bool fire_projectile(uint8_t n, const Target&) override {
        shots.push_back(n);
        return fire[n];
    }

    void stockpile_consumed(uint8_t n) override { stock.push_back(n); }

    bool can_pay_shot_cost(float energy, float metal) override {
        reserved_energy = energy;
        reserved_metal = metal;
        return reserve;
    }

    void pay_shot_cost(float energy, float metal) override {
        assert(energy == reserved_energy && metal == reserved_metal);
        ++payments;
    }
};

int main() {
    // Projectile-constructor selection from the weapon flags.
    assert(select_fire_mode(flags::weapon_turret_flag) == FireMode::turret);
    assert(select_fire_mode(flags::weapon_vlaunch_flag) == FireMode::vertical_launch);
    assert(select_fire_mode(flags::weapon_line_of_sight_flag) == FireMode::line);
    assert(select_fire_mode(flags::weapon_selfprop_flag) == FireMode::line);
    assert(select_fire_mode(0x100) == FireMode::dropped);
    assert(select_fire_mode(0) == FireMode::none);
    assert(select_fire_mode(flags::weapon_ballistic_flag) == FireMode::none);
    assert(
        select_fire_mode(flags::weapon_turret_flag | flags::weapon_vlaunch_flag) == FireMode::turret
    );
    // Line-of-sight or selfprop before ballistic.
    assert(projectile_route(0) == ProjectileRoute::none);
    assert(projectile_route(flags::weapon_vlaunch_flag) == ProjectileRoute::none);
    assert(projectile_route(flags::weapon_dropped_flag) == ProjectileRoute::none);
    assert(projectile_route(flags::weapon_start_smoke_flag) == ProjectileRoute::none);
    assert(projectile_route(flags::weapon_turret_flag) == ProjectileRoute::none);
    assert(projectile_route(flags::weapon_ballistic_flag) == ProjectileRoute::ballistic);
    assert(
        projectile_route(flags::weapon_dropped_flag | flags::weapon_ballistic_flag) ==
        ProjectileRoute::ballistic
    );
    assert(
        projectile_route(flags::weapon_turret_flag | flags::weapon_ballistic_flag) ==
        ProjectileRoute::ballistic
    );
    assert(
        projectile_route(~(flags::weapon_line_of_sight_flag | flags::weapon_selfprop_flag)) ==
        ProjectileRoute::ballistic
    );
    assert(projectile_route(flags::weapon_line_of_sight_flag) == ProjectileRoute::line);
    assert(projectile_route(flags::weapon_selfprop_flag) == ProjectileRoute::line);
    assert(
        projectile_route(flags::weapon_line_of_sight_flag | flags::weapon_ballistic_flag) ==
        ProjectileRoute::line
    );
    assert(
        projectile_route(flags::weapon_selfprop_flag | flags::weapon_ballistic_flag) ==
        ProjectileRoute::line
    );
    assert(
        projectile_route(~(flags::weapon_line_of_sight_flag | flags::weapon_ballistic_flag)) ==
        ProjectileRoute::line
    );
    // Turret slew feasibility.
    assert(turret_within_tolerance(TurretSlewInput{500, 500, 0, 0, 100, 100, false}));
    assert(!turret_within_tolerance(TurretSlewInput{500, 500, 0, 0, 600, 0, false}));
    assert(!turret_within_tolerance(TurretSlewInput{500, 500, 0, 0, 0, 600, false}));
    // Zero yaw limit falls back to 150 stopped / 2000 moving.
    assert(turret_within_tolerance(TurretSlewInput{0, 0, 0, 0, 100, 100, false}));
    assert(!turret_within_tolerance(TurretSlewInput{0, 0, 0, 0, 200, 0, false}));
    assert(turret_within_tolerance(TurretSlewInput{0, 0, 0, 0, 200, 0, true}));
    // Zero pitch limit inherits the yaw limit.
    assert(!turret_within_tolerance(TurretSlewInput{500, 0, 0, 0, 0, 600, false}));
    assert(reload_ticks_after_shot(100, 25, 10, 100) == 82); // 100*.70*.? 120-2 = 82
    WeaponDefinition direct{true, vlaunch_flag, 100, 0, 2.0f, 3.0f};
    WeaponDefinition ballistic{true, turret_flag | stockpile_flag | commandfire_flag, 77, 0, 0, 0};
    // The reload counts down in the unit's canonical slots.
    std::array<oa::UnitWeapon, weapon_slot_count> records{};
    records[0].reload = 1;
    UnitState u;
    u.maximum_health = 100;
    u.veteran_level = 25;
    u.health = 10;
    u.slots[0] = {&direct, &records[0], 0, enabled_flag};
    u.slots[1] = {&ballistic, &records[1], 2, enabled_flag};
    u.slots[2].record = &records[2];
    Fixture h;
    h.target[2] = false;
    u.slots[2].flags = enabled_flag | aimed_flag;
    const auto r = tick_weapons(u, h);
    assert(r.slots[0] == SlotResult::fired && records[0].reload == 82);
    assert(r.slots[1] == SlotResult::fired && u.slots[1].stockpile_count == 1);
    assert(r.slots[2] == SlotResult::target_lost && !(u.slots[2].flags & aimed_flag));
    constexpr uint16_t both_shots = shot_fired_event | commandfire_shot_event;
    assert((u.shot_event_bits & both_shots) == both_shots && h.aim == std::vector<int>({20, 11}));
    assert(h.shots == std::vector<int>({0, 1}) && h.stock == std::vector<int>({1}));
    assert(h.reserved_energy == 2.0f && h.reserved_metal == 3.0f && h.payments == 1);
    // The vertical-launch and turret constructors clear the aim bit after a shot, so both slots run
    // their Aim script again before the next one.
    assert(!(u.slots[0].flags & aimed_flag) && !(u.slots[1].flags & aimed_flag));
    // The turret waits while the Aim script it started has not returned nonzero:
    // no shot, and the aim bit stays so the script is not restarted.
    h.aim.clear();
    h.shots.clear();
    h.ready[1] = false;
    const auto aiming = tick_weapons(u, h);
    assert(aiming.slots[1] == SlotResult::waiting && (u.slots[1].flags & aimed_flag));
    assert(u.slots[1].stockpile_count == 1 && h.aim == std::vector<int>({20, 11}));
    (void)tick_weapons(u, h);
    assert(h.aim == std::vector<int>({20, 11}) && h.shots.empty());
    h.ready[1] = true;
    assert(tick_weapons(u, h).slots[1] == SlotResult::fired && u.slots[1].stockpile_count == 0);
    u.slots[1].flags = enabled_flag;
    u.slots[1].stockpile_count = 1;
    h.ballistic_aim = false;
    (void)tick_weapons(u, h);
    assert(!(u.slots[1].flags & aimed_flag));
    // The turret with the slot aimed and ready: a solution outside tolerance
    // drops the aim; no solution also raises target_unreachable_event. Neither fires.
    h.ballistic_aim = true;
    u.slots[1].flags = enabled_flag | aimed_flag;
    u.shot_event_bits = 0;
    h.aim_now = TurretAim::off_target;
    auto missed = tick_weapons(u, h);
    assert(missed.slots[1] == SlotResult::waiting && !(u.slots[1].flags & aimed_flag));
    assert((u.shot_event_bits & target_unreachable_event) == 0 && u.slots[1].stockpile_count == 1);
    u.slots[1].flags = enabled_flag | aimed_flag;
    h.aim_now = TurretAim::unsolved;
    missed = tick_weapons(u, h);
    assert(missed.slots[1] == SlotResult::waiting && !(u.slots[1].flags & aimed_flag));
    assert(u.shot_event_bits & target_unreachable_event);
    h.aim_now = TurretAim::on_target;
    records[0].reload = 0;
    h.reach[0] = false;
    u.shot_event_bits = 0;
    (void)tick_weapons(u, h);
    assert(u.shot_event_bits & target_unreachable_event);
    // Payment gate: a rejected debit skips the projectile constructor and the
    // shot/reload bookkeeping.
    h.reach[0] = true;
    h.reserve = false;
    h.shots.clear();
    records[0].reload = 0;
    u.shot_event_bits = 0;
    const auto payments = h.payments;
    const auto blocked = tick_weapons(u, h);
    assert(blocked.slots[0] == SlotResult::insufficient_resources);
    assert(h.shots.empty() && u.shot_event_bits == 0 && records[0].reload == 0);
    assert(h.reserved_energy == 2.0f && h.reserved_metal == 3.0f && h.payments == payments);
    // The debit comes only after the constructor fired: a refused shot costs nothing.
    h.reserve = true;
    h.fire[0] = false;
    const auto refused = tick_weapons(u, h);
    assert(refused.slots[0] == SlotResult::projectile_rejected && h.shots == std::vector<int>({0}));
    assert(h.payments == payments && records[0].reload == 0);
    // One child once current >= anchor + burstrate. A skipped span
    // still emits one; the next due tick stays one interval ahead.
    const auto waiting = burst_fire_step(3, 10, 0, 0);
    assert(waiting.shots == 0 && waiting.remaining == 3 && waiting.anchor == 0);
    assert(waiting.next_due == 10 && waiting.has_next);
    assert(burst_fire_step(3, 10, 0, 9).shots == 0);
    const auto first = burst_fire_step(3, 10, 0, 10);
    assert(first.shots == 1 && first.remaining == 2 && first.anchor == 10);
    assert(first.next_due == 20 && first.has_next);
    const auto skipped = burst_fire_step(3, 10, 0, 25);
    assert(skipped.shots == 1 && skipped.remaining == 2 && skipped.anchor == 10);
    assert(skipped.next_due == 20);
    const auto follow = burst_fire_step(skipped.remaining, 10, skipped.anchor, 26);
    assert(follow.shots == 1 && follow.remaining == 1 && follow.anchor == 20);
    assert(follow.next_due == 30 && follow.has_next);
    const auto last = burst_fire_step(1, 10, 20, 30);
    assert(last.shots == 1 && last.remaining == 0 && last.anchor == 30 && !last.has_next);
    const auto idle = burst_fire_step(0, 10, 30, 40);
    assert(idle.shots == 0 && idle.remaining == 0 && idle.anchor == 30 && !idle.has_next);
    const auto same = burst_fire_step(2, 0, 5, 5);
    assert(same.shots == 1 && same.remaining == 1 && same.anchor == 5 && same.next_due == 5);
    const auto next_tick = burst_fire_step(same.remaining, 0, same.anchor, 6);
    assert(next_tick.shots == 1 && next_tick.remaining == 0 && !next_tick.has_next);
    const auto wrapped = burst_fire_step(1, 32, 0xfffffff0u, 0xfffffff8u);
    assert(wrapped.shots == 1 && wrapped.anchor == 0x10u && wrapped.remaining == 0);
    const auto before_wrap = burst_fire_step(1, 32, 0xfffffff0u, 5u);
    assert(before_wrap.shots == 0 && before_wrap.next_due == 0x10u && before_wrap.has_next);
}
