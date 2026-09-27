// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Sightings and the contact scan's mincloakdistance pass in a running match:
// each player's seen and radar lists after a rebuild, and a cloaked unit that
// stays hidden from an enemy its owner sees outside mincloakdistance but
// loses the cloak for 90 ticks once one is inside.
#include "combat_fixture.hpp"

#include <cstdint>
#include <iostream>
#include <vector>

namespace {

using namespace combat_fixture;
namespace detection = oa::sim::detection;

constexpr int16_t min_cloak_distance = 64;

// Units see three sight cells (96 world units) around their own.
Options sighted() {
    Options options;
    options.sight_cells = 7;
    return options;
}

// The fixture's type can cloak, and both players can pay the upkeep. The
// game has no outcome, so the economy that runs the upkeep keeps settling
// with no enemy left; a decided game settles none from its countdown on.
void allow_cloak(Fixture& f) {
    f.match->disable_scenario();
    auto& def = f.match->state().unit_defs[1];
    def.abilities |= OA_UNIT_DEF_ABILITY_CAN_CLOAK;
    def.min_cloak_distance = min_cloak_distance;
    def.cloak_cost = def.cloak_cost_moving = 10.0F;
    for (uint8_t player = 0; player < 2; ++player)
        f.match->state().game.players[player].energy = 900.0F;
}

// Holds fire: a shot would hold the shooter's cloak off by itself.
sim::unit_spawn::Slot& idle(Fixture& f, uint8_t player, uint32_t x, uint32_t z) {
    auto& slot = f.spawn(player, x, z);
    slot.unit->flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
    return slot;
}

bool cloaked(const sim::unit_spawn::Slot& slot) {
    return (slot.record.state_flags & OA_UNIT_STATE_CLOAKED) != 0;
}

bool locked(const sim::unit_spawn::Slot& slot) {
    return (slot.record.flags & OA_UNIT_FLAG_CLOAK_LOCKED) != 0;
}

std::vector<uint16_t> seen(const Fixture& f, uint8_t player) {
    const auto& sightings = f.match->sightings(player);
    return {sightings.seen, sightings.seen + sightings.seen_count};
}

std::vector<uint16_t> radar(const Fixture& f, uint8_t player) {
    const auto& sightings = f.match->sightings(player);
    return {sightings.radar, sightings.radar + sightings.radar_count};
}

using Slots = std::vector<uint16_t>;

// Player 0 at (64,64) with a switched-on targeting facility; player 1 with
// one unit 40 east of it, one 40 south flagged off the seen lists, and one
// in the far corner out of sight.
void sightings_after_a_rebuild() {
    Fixture f(sighted());
    f.match->state().unit_defs[1].flags |= OA_UNIT_DEF_FLAG_TARGETING_UPGRADE;
    auto& own = idle(f, 0, 64, 64);
    auto& east = idle(f, 1, 104, 64);
    auto& south = idle(f, 1, 64, 104);
    auto& corner = idle(f, 1, 240, 240);
    own.record.state_flags |= OA_UNIT_STATE_ACTIVE;
    south.unit->flags |= detection::unsighted;
    f.run(29);
    CHECK(f.match->sightings(0).seen_count == 0 && f.match->sightings(0).refreshed_tick == 0);
    f.run(1);
    CHECK(f.match->unit_visible(0, east.unit_index) && f.match->unit_visible(0, south.unit_index));
    CHECK(!f.match->unit_visible(0, corner.unit_index));
    CHECK(f.match->sightings(0).refreshed_tick == 30);
    CHECK(seen(f, 0) == Slots{east.unit_index});
    // Radar contacts are the viewpoint's (player 0's) scan of tick 1, which
    // stamped the two units in its line of sight.
    CHECK((radar(f, 0) == Slots{east.unit_index, south.unit_index}));
    CHECK(f.match->sightings(0).radar_fallback == 1);
    // Player 1 sees player 0's unit, and the viewpoint's own units carry the
    // radar bit for everyone.
    CHECK(seen(f, 1) == Slots{own.unit_index});
    CHECK(radar(f, 1) == Slots{own.unit_index});
    CHECK(f.match->sightings(1).radar_fallback == 0);
    std::cout << "sightings after a rebuild passed\n";
}

// The enemy stands 100 away: seen by the cloaker's owner but outside 64.
void cloak_holds_outside_the_distance() {
    Fixture f(sighted());
    allow_cloak(f);
    auto& cloaker = idle(f, 0, 64, 64);
    auto& enemy = idle(f, 1, 164, 64);
    f.match->issue_cloak(cloaker.unit_index, true);
    // Tick 1 runs Cloak_On, then the first settlement pays the upkeep.
    f.run(1);
    CHECK((cloaker.record.flags & detection::cloak_ordered) != 0);
    CHECK(cloaked(cloaker));
    CHECK(
        f.services.attachment_notices ==
        std::vector<uint32_t>{sim::unit_activation::cloak_notification}
    );
    CHECK(!f.match->unit_visible(1, cloaker.unit_index));
    const auto energy = f.match->state().game.players[0].energy;
    f.run(89);
    CHECK(seen(f, 0) == Slots{enemy.unit_index});
    CHECK(cloaked(cloaker) && !locked(cloaker) && cloaker.record.decloak_until_tick == 0);
    CHECK(seen(f, 1).empty());
    CHECK(!f.match->unit_visible(1, cloaker.unit_index));
    CHECK(f.match->state().game.players[0].energy < energy);
    std::cout << "cloak holds outside the distance passed\n";
}

// The enemy stands 40 away. The scan of tick 30 (after that tick's upkeep)
// locks the cloak until tick 120; the settlement of tick 60 drops it. With
// the enemy gone, the cloak returns at the first settlement from tick 150.
void enemy_inside_the_distance_decloaks() {
    Fixture f(sighted());
    allow_cloak(f);
    auto& cloaker = idle(f, 0, 64, 64);
    auto& enemy = idle(f, 1, 104, 64);
    f.match->issue_cloak(cloaker.unit_index, true);
    f.run(1);
    CHECK(cloaked(cloaker) && !locked(cloaker));
    f.run(29);
    CHECK(seen(f, 0) == Slots{enemy.unit_index});
    CHECK(cloaked(cloaker) && locked(cloaker));
    CHECK(cloaker.record.decloak_until_tick == 30 + detection::decloak_hold_ticks);
    f.run(30);
    CHECK(!cloaked(cloaker) && locked(cloaker));
    CHECK(cloaker.record.decloak_until_tick == 60 + detection::decloak_hold_ticks);
    CHECK(f.match->unit_visible(1, cloaker.unit_index));
    kill(f, enemy, cloaker);
    // Tick 90: the lock from tick 60 still blocks the upkeep; the scan then
    // clears it and finds no enemy.
    f.run(30);
    CHECK(seen(f, 0).empty());
    CHECK(!cloaked(cloaker) && !locked(cloaker));
    f.run(59);
    CHECK(!cloaked(cloaker));
    f.run(1);
    CHECK(cloaked(cloaker));
    std::cout << "enemy inside the distance decloaks passed\n";
}

// Cloak_Off clears the order; the next settlement drops the cloak.
void cloak_off_order() {
    Fixture f(sighted());
    allow_cloak(f);
    auto& cloaker = idle(f, 0, 64, 64);
    f.match->issue_cloak(cloaker.unit_index, true);
    f.run(1);
    CHECK(cloaked(cloaker));
    f.match->issue_cloak(cloaker.unit_index, false);
    f.run(1);
    CHECK((cloaker.record.flags & detection::cloak_ordered) == 0);
    CHECK(cloaked(cloaker));
    f.run(29);
    CHECK(!cloaked(cloaker));
    std::cout << "cloak off order passed\n";
}

} // namespace

int main() {
    try {
        sightings_after_a_rebuild();
        cloak_holds_outside_the_distance();
        enemy_inside_the_distance_decloaks();
        cloak_off_order();
    } catch (const std::exception& error) {
        std::cerr << "cloak: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
