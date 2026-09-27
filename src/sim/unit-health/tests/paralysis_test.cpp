// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_health/paralysis.hpp"

#include "oa/core/player.h"
#include "oa/core/unit_def.h"

#include <cstdio>
#include <cstdlib>

using namespace oa;
using namespace oa::sim::unit_health;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

// Player 0 (local) owns live unit 1 of type 1.
World* make_world() {
    World* w = world_create();
    WorldCapacity cap{4, 2, 0};
    if (w == nullptr || !world_alloc_tables(w, &cap))
        std::abort();
    Player& player = w->game.players[0];
    player.in_use = 1;
    player.status = OA_PLAYER_STATUS_LOCAL;
    Unit& unit = w->units[1];
    unit.owner = 1;
    unit.def = 2;
    unit.type_index = 1;
    unit.id = 1;
    unit.flags = OA_UNIT_FLAG_LIVE;
    return w;
}

void action_gates() {
    World* w = make_world();
    Unit& unit = w->units[1];
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::insert);
    CHECK(paralyze_action(*w, unit, true) == ParalyzeAction::extend);

    w->unit_defs[1].flags = OA_UNIT_DEF_FLAG_IMMUNE_TO_PARALYZER;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::none);
    w->unit_defs[1].flags = 0;

    w->game.players[0].status = OA_PLAYER_STATUS_COMPUTER;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::insert);
    w->game.players[0].status = OA_PLAYER_STATUS_MIRRORED;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::none);
    w->game.players[0].status = OA_PLAYER_STATUS_LOCAL;
    w->game.players[0].in_use = 0;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::none);
    w->game.players[0].in_use = 1;

    unit.flags |= OA_UNIT_FLAG_DEATH_PENDING;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::none);
    unit.flags = 0;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::none);
    unit.flags = OA_UNIT_FLAG_LIVE;
    unit.owner = 0;
    CHECK(paralyze_action(*w, unit, false) == ParalyzeAction::none);
    world_destroy(w);
}

// Effects the engine hands back, in the order it runs them.
struct Recorder {
    uint8_t cleared[8]{};
    int cleared_count = 0;
    int releases = 0;
    int flag_calls = 0;
    bool flag_on = false;
    int release_before_flag = -1; // release count seen by the last flag call
};

ParalysisHooks hooks_for(Recorder& r) {
    ParalysisHooks hooks;
    hooks.context = &r;
    hooks.target_cleared = [](void* context, Unit&, uint8_t slot) {
        auto& rec = *static_cast<Recorder*>(context);
        rec.cleared[rec.cleared_count++] = slot;
    };
    hooks.set_state_flags = [](void* context, Unit& unit, uint8_t mask, bool on) {
        auto& rec = *static_cast<Recorder*>(context);
        ++rec.flag_calls;
        rec.flag_on = on;
        rec.release_before_flag = rec.releases;
        unit.state_flags =
            static_cast<uint8_t>(on ? unit.state_flags | mask : unit.state_flags & ~mask);
    };
    hooks.release_order_goal = [](void* context) { ++static_cast<Recorder*>(context)->releases; };
    return hooks;
}

void mission_steps() {
    World* w = make_world();
    Unit& unit = w->units[1];
    unit.weapons[0].target_a = 5;
    unit.weapons[0].target_b = 7;
    unit.weapons[0].flags = OA_UNIT_WEAPON_ENABLED;
    unit.weapons[1].target_a = 3;
    unit.weapons[1].target_b = OA_UNIT_TARGET_IS_UNIT;
    unit.weapons[1].flags = OA_UNIT_WEAPON_ENABLED | OA_UNIT_WEAPON_RETALIATE;
    unit.weapons[2].target_b = OA_UNIT_TARGET_IS_UNIT;
    unit.weapons[2].flags = OA_UNIT_WEAPON_RETALIATE;
    unit.state_flags = OA_UNIT_STATE_ACTIVE;
    Recorder r;
    const auto hooks = hooks_for(r);

    int32_t duration = 5000;
    auto step = paralyzed_mission_step(unit, duration, hooks);
    CHECK(step.result == mission_result_waiting && step.wait_ticks == paralysis_wait_limit);
    CHECK(duration == 0);
    // The tracked slot clears first, then the plain sweep reaches slot 0.
    CHECK(r.cleared_count == 2 && r.cleared[0] == 1 && r.cleared[1] == 0);
    CHECK(r.releases == 1 && r.flag_calls == 1 && r.flag_on && r.release_before_flag == 1);
    CHECK(unit.state_flags == (OA_UNIT_STATE_ACTIVE | paralyzed_state_flag));
    CHECK(unit.weapons[1].flags == OA_UNIT_WEAPON_ENABLED);
    CHECK(unit.weapons[0].target_a == 0 && unit.weapons[0].target_b == OA_UNIT_TARGET_IS_UNIT);
    CHECK(unit.weapons[1].target_a == 0 && unit.weapons[1].target_b == OA_UNIT_TARGET_IS_UNIT);
    CHECK(unit.weapons[2].flags == OA_UNIT_WEAPON_RETALIATE);

    // Damage during the wait extends the duration: the next step waits again, shorter.
    duration += 100;
    step = paralyzed_mission_step(unit, duration, hooks);
    CHECK(step.result == mission_result_waiting && step.wait_ticks == 100 && r.cleared_count == 2);
    CHECK(duration == 0 && r.releases == 2 && r.flag_calls == 2);

    step = paralyzed_mission_step(unit, duration, hooks);
    CHECK(step.result == mission_result_finished && step.wait_ticks == 0 && r.cleared_count == 2);
    CHECK(r.releases == 2 && r.flag_calls == 3 && !r.flag_on);
    CHECK(unit.state_flags == OA_UNIT_STATE_ACTIVE);
    world_destroy(w);
}

// Without hooks the engine still sets and clears the paralyzed bit itself.
void unhooked_state_flag() {
    Unit unit{};
    unit.state_flags = OA_UNIT_STATE_ACTIVE;
    int32_t duration = 30;
    auto step = paralyzed_mission_step(unit, duration, ParalysisHooks{});
    CHECK(
        step.result == mission_result_waiting &&
        unit.state_flags == (OA_UNIT_STATE_ACTIVE | paralyzed_state_flag)
    );
    step = paralyzed_mission_step(unit, duration, ParalysisHooks{});
    CHECK(step.result == mission_result_finished && unit.state_flags == OA_UNIT_STATE_ACTIVE);
}

void target_clearing() {
    Unit unit{};
    Recorder r;
    const auto hooks = hooks_for(r);
    unit.weapons[0].target_b = OA_UNIT_TARGET_IS_UNIT;
    CHECK(!clear_weapon_target(unit, 0, hooks));
    unit.weapons[0].target_a = 9;
    CHECK(clear_weapon_target(unit, 0, hooks));
    CHECK(unit.weapons[0].target_a == 0 && unit.weapons[0].target_b == OA_UNIT_TARGET_IS_UNIT);
    CHECK(r.cleared_count == 1 && r.cleared[0] == 0);
    CHECK(!clear_weapon_target(unit, 3, hooks));

    unit.weapons[1].target_a = 4;
    unit.weapons[1].flags = OA_UNIT_WEAPON_ENABLED | OA_UNIT_WEAPON_RETALIATE;
    unit.weapons[2].target_a = 4;
    unit.weapons[2].flags = OA_UNIT_WEAPON_ENABLED;
    CHECK(clear_tracked_weapon_targets(unit, 2, hooks) == 0);
    CHECK(unit.weapons[2].target_a == 4);
    CHECK(clear_tracked_weapon_targets(unit, 3, hooks) == 0x2);
    CHECK(unit.weapons[1].target_a == 0 && unit.weapons[1].flags == OA_UNIT_WEAPON_ENABLED);
    CHECK(unit.weapons[2].target_a == 4);
    CHECK(r.cleared_count == 2 && r.cleared[1] == 1);
    CHECK(clear_tracked_weapon_targets(unit, 4, hooks) == 0);

    // A tracked slot without a target only loses the bit.
    unit.weapons[0].flags = OA_UNIT_WEAPON_ENABLED | OA_UNIT_WEAPON_RETALIATE;
    CHECK(clear_tracked_weapon_targets(unit, 0, ParalysisHooks{}) == 0);
    CHECK(unit.weapons[0].flags == OA_UNIT_WEAPON_ENABLED && r.cleared_count == 2);
}
} // namespace

int main() {
    action_gates();
    mission_steps();
    unhooked_state_flag();
    target_clearing();
    if (failures != 0) {
        std::fprintf(stderr, "%d failures\n", failures);
        return 1;
    }
    std::puts("paralysis tests passed");
    return 0;
}
