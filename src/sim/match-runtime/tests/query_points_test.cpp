// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit query points and the automatic target pick in a running match: the
// sweet-spot box (started at the piece origin and halved toward zero), a
// carried unit's attitude, the fire-at-will gate of the automatic target
// pick, and the unsigned angles the weapon tick hands the Aim script.
#include "combat_fixture.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/weapon_execution/retaliation.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

constexpr uint32_t fire_order(uint32_t order) {
    return order << OA_UNIT_FLAG_FIRE_ORDER_SHIFT;
}

void set_fire_order(sim::unit_spawn::Slot& slot, uint32_t order) {
    slot.record.flags = (slot.record.flags & ~OA_UNIT_FLAG_FIRE_ORDER_MASK) | fire_order(order);
}

uint32_t offset(uint32_t base, int32_t delta) {
    return base + static_cast<uint32_t>(delta);
}

// The root piece's vertices all lie off the origin, so the box 3.1c grows
// from (0,0,0) differs from the vertices' own box. A second model with
// an odd X extent shows the halving rounding toward zero.
void sweet_spot_box_starts_at_the_origin() {
    Fixture f;
    f.model->objects[0].vertices = {{fx(4) + 3, fx(6), fx(-8)}, {fx(10), fx(12), fx(-2)}};
    auto& unit = f.spawn(0, 64, 64);
    auto* instance = f.match->instance(unit.unit_index);
    CHECK(instance != nullptr);
    const auto& vertices = instance->model().pieces()[0].transformed_vertices;
    CHECK(vertices.size() == 2 && vertices[0].x == -(fx(4) + 3) && vertices[1].x == -fx(10));
    const auto position = unit.unit->position;
    // X box [-(10<<16), 0]: -327680 / 2. Y box [0, 12<<16], Z box [0, 8<<16].
    const auto centre = instance->piece_box_center(0);
    CHECK(centre[0] == offset(position[0], -fx(5)));
    CHECK(centre[1] == offset(position[1], fx(6)));
    CHECK(centre[2] == offset(position[2], fx(4)));
    // Without a SweetSpot script the piece stays 0.
    CHECK(instance->sweet_spot_world() == centre);
    CHECK(instance->piece_box_center(1) == position);

    f.model->objects[0].vertices = {{-3, 0, 0}};
    auto& odd = f.spawn(0, 96, 64);
    const auto odd_centre = f.match->instance(odd.unit_index)->piece_box_center(0);
    // X box [0, 3]: 3 / 2 truncates to 1.
    CHECK(odd_centre[0] == offset(odd.unit->position[0], 1));
    std::cout << "sweet spot box starts at the origin passed\n";
}

void carried_attitude_adds_the_piece_rotation() {
    Fixture f;
    auto& carrier = f.spawn(0, 64, 64);
    auto* instance = f.match->instance(carrier.unit_index);
    carrier.record.bank = 0x100;
    carrier.record.heading = 0x7000;
    carrier.record.pitch = -0x10;
    instance->model().pieces()[0].rotation = {0x10, static_cast<int16_t>(0x9000), 0x20};
    const auto attitude = instance->piece_attitude(0);
    CHECK(attitude.xy == 0x110);
    CHECK(attitude.xz == 0); // 0x7000 + 0x9000 wraps
    CHECK(attitude.yz == 0x10);
    std::cout << "carried attitude adds the piece rotation passed\n";
}

// The automatic pick: (flags & 0x300000) == 0x200000, else no target.
void automatic_target_needs_fire_at_will() {
    Options options;
    options.sight_cells = 7;
    Fixture f(options);
    f.match->state().game.console_flags |= OA_CONSOLE_FLAG_SHOOT_ALL;
    auto& own = f.spawn(0, 64, 64);
    auto& enemy = f.spawn(1, 104, 64);
    set_fire_order(own, 0);
    set_fire_order(enemy, 0);
    f.run(30);
    set_fire_order(own, 2);
    CHECK(f.match->find_automatic_target(*own.unit) == enemy.unit);
    for (const uint32_t order : {0u, 1u, 3u}) {
        set_fire_order(own, order);
        CHECK(f.match->find_automatic_target(*own.unit) == nullptr);
    }
    std::cout << "automatic target needs fire at will passed\n";
}

// The target pick skips a candidate whose type lacks OA_UNIT_DEF_FLAG_SHOOT_ME
// unless the owner is a status-2 player or "ShootAll" set
// OA_CONSOLE_FLAG_SHOOT_ALL in Game.console_flags.
void shoot_all_targets_skipped_types() {
    Options options;
    options.sight_cells = 7;
    Fixture f(options);
    auto& own = f.spawn(0, 64, 64);
    auto& enemy = f.spawn(1, 104, 64);
    set_fire_order(own, 0);
    set_fire_order(enemy, 0);
    f.run(30);
    set_fire_order(own, 2);
    CHECK(f.match->find_automatic_target(*own.unit) == nullptr);
    f.match->state().game.console_flags |= OA_CONSOLE_FLAG_SHOOT_ALL;
    CHECK(f.match->find_automatic_target(*own.unit) == enemy.unit);
    f.match->state().game.console_flags &= static_cast<uint16_t>(~OA_CONSOLE_FLAG_SHOOT_ALL);
    CHECK(f.match->find_automatic_target(*own.unit) == nullptr);
    std::cout << "shoot all targets skipped types passed\n";
}

struct ForwardedStart {
    uint32_t count{};
    uint16_t unit{};
    int16_t function{};
    uint8_t arguments{};
    std::array<uint32_t, 4> locals{};
};

// AimPrimary stores its first argument; a target due east gives a heading
// below zero as a signed word, which the Aim script receives zero-extended.
// The weapon tick shares the same start through share_named_script_start:
// AimPrimary's COB index (1) with two arguments, heading then pitch.
void aim_script_gets_unsigned_angles() {
    Fixture f;
    f.script->code[1] = sim::script_vm::opcode::push_local;
    f.script->code[2] = 0;
    ForwardedStart forwarded;
    f.match->multiplayer.context = &forwarded;
    f.match->multiplayer.script_started = [](void* context,
                                             uint16_t unit,
                                             int16_t function,
                                             uint8_t arguments,
                                             const std::array<uint32_t, 4>& locals) {
        auto& start = *static_cast<ForwardedStart*>(context);
        if (start.count++ != 0)
            return;
        start.unit = unit;
        start.function = function;
        start.arguments = arguments;
        start.locals = locals;
    };
    auto& shooter = f.spawn(0, 64, 64);
    auto& target = f.spawn(1, 160, 64);
    set_fire_order(shooter, 0);
    set_fire_order(target, 0);
    sim::weapon_execution::aim_slot_at_unit(shooter.record, target.record, 0);
    f.run(1);
    const auto heading = static_cast<uint16_t>(
        base::game_math::direction(fx(64) - fx(160), 0) - shooter.record.heading
    );
    CHECK(heading >= 0x8000);
    const auto stored = f.match->instance(shooter.unit_index)->script()->vm().static_value(0);
    CHECK(stored.has_value() && *stored == static_cast<int32_t>(heading));
    CHECK(static_cast<uint16_t>(shooter.record.weapons[0].aim_heading) == heading);
    CHECK(forwarded.count >= 1 && forwarded.unit == shooter.unit_index && forwarded.function == 1);
    CHECK(forwarded.arguments == 2 && forwarded.locals[0] == heading);
    CHECK(forwarded.locals[1] == static_cast<uint16_t>(shooter.record.weapons[0].aim_pitch));
    CHECK(forwarded.locals[2] == 0 && forwarded.locals[3] == 0);
    std::cout << "aim script gets unsigned angles passed\n";
}

} // namespace

int main() {
    try {
        sweet_spot_box_starts_at_the_origin();
        carried_attitude_adds_the_piece_rotation();
        automatic_target_needs_fire_at_will();
        shoot_all_targets_skipped_types();
        aim_script_gets_unsigned_angles();
    } catch (const std::exception& error) {
        std::cerr << "query points: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
