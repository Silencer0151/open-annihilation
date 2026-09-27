// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The deaths a match shares with the other players and the deaths it takes
// from them: MultiplayerHooks::unit_killed carries the outcome the kill
// handler settles for a unit simulated here, before the unit's orders are
// cleared and it is torn down, and nothing for a unit simulated elsewhere;
// Match::apply_kill tears a unit down with the attacker, Killed pieces,
// explosion and wreck level another player's machine settled; and
// Match::kill_unit with death kind 0 kills a unit whose slot another player's
// new unit takes by its own health. A last attacker whose player's slot has
// gone free is credited nowhere.
#include "combat_fixture.hpp"
#include "oa/data/unit_definitions.hpp"
#include "oa/sim/weapon_execution/weapon_launch.hpp"

#include <cstdint>
#include <cstring>
#include <iostream>
#include <string_view>
#include <utility>
#include <vector>

namespace {

using namespace combat_fixture;
using sim::match_runtime::DeathKind;
using sim::match_runtime::KillOutcome;

constexpr int16_t full_health = 1000;
constexpr uint8_t no_player = 10;
constexpr uint8_t replaced_unit_death = 0; // a unit whose slot another player's new unit takes
constexpr uint32_t root_piece = 0;
constexpr uint8_t cargo_death = static_cast<uint8_t>(DeathKind::cargo);
constexpr uint8_t self_destruct_death = static_cast<uint8_t>(DeathKind::self_destruct);
// The route the test's health_route hook gives a source unit's events.
constexpr sim::unit_health::RouteIdentity route_base = 1000;
// The type's corpse steps along featuredead to the heap, and the heap to
// nothing, as a solar collector's does.
constexpr uint16_t dead_feature = 0;
constexpr uint16_t heap_feature = 1;
constexpr uint16_t no_further_feature = 0xffff;
// A blast's screen shake as detonation_shakes_the_screen reads it: TESTBLAST
// shakes by 6 for half a second (7 ticks), TESTSELFD not at all.
constexpr int32_t blast_shake_ticks = 7;
constexpr int32_t blast_shake_magnitude = 6;
// A corpse's smoke column puffs every 15 ticks; a blast's own smoke does not.
constexpr int32_t wreck_smoke_interval = 15;

// Blast weapons with edge effectiveness 1, as the death-path tests arm them.
constexpr std::string_view blast_tdf = R"([TESTBLAST]
{
ID=2; areaofeffect=96; edgeeffectiveness=1; explosiongaf=fx; explosionart=explode4;
shakemagnitude=6; shakeduration=0.5;
[DAMAGE] { default=300; }
}
[TESTSELFD]
{
ID=3; areaofeffect=128; edgeeffectiveness=1; explosiongaf=fx; explosionart=explode5;
[DAMAGE] { default=500; }
}
)";

// The explode4 and explode5 blasts and the wreck's "smoke 1" column.
struct Art {
    formats::gaf::Sequence explode4 = frames();
    formats::gaf::Sequence explode5 = frames();
    formats::gaf::Sequence smoke = frames();

    static formats::gaf::Sequence frames() {
        formats::gaf::Sequence sequence{};
        sequence.frames.resize(4);
        for (auto& frame : sequence.frames)
            frame.duration = 2;
        return sequence;
    }
};

// Dry high ground, where corpses fit and smoke, the corpse chain and the
// Killed script.
Options death_options(const Art& art) {
    FeatureDef dead{};
    dead.footprint_x = dead.footprint_z = 1;
    dead.dead_feature = heap_feature;
    FeatureDef heap{};
    heap.footprint_x = heap.footprint_z = 1;
    heap.dead_feature = no_further_feature;
    Options options;
    options.high_ground_from = 0;
    options.killed_script = true;
    options.features = {dead, heap};
    options.effect_sequence =
        [&art](std::string_view, std::string_view entry) -> const formats::gaf::Sequence* {
        if (entry == "explode4")
            return &art.explode4;
        if (entry == "explode5")
            return &art.explode5;
        return entry == "smoke 1" ? &art.smoke : nullptr;
    };
    return options;
}

// The type leaves the corpse chain's first feature.
void leave_corpses(Fixture& f) {
    f.match->state().unit_defs[1].corpse = static_cast<int16_t>(dead_feature);
}

void arm_death_blasts(Fixture& f) {
    const auto parsed = data::unit_definitions::parse_tdf(blast_tdf);
    CHECK(parsed);
    CHECK(sim::combat_state::install_weapon_tdf(f.weapons, parsed.value) == 2);
    sim::weapon_execution::store_weapon_defs(f.weapons, f.match->state().game.weapon_defs);
    f.def.explode_as = "TESTBLAST";
    f.def.self_destruct_as = "TESTSELFD";
}

// A unit that neither fires on its own nor chases.
sim::unit_spawn::Slot& idle(Fixture& f, uint8_t player, uint32_t x, uint32_t z) {
    auto& slot = f.spawn(player, x, z);
    slot.unit->flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
    return slot;
}

bool live(const sim::unit_spawn::Slot& slot) {
    return (slot.unit->flags & OA_UNIT_FLAG_LIVE) != 0;
}

void mirror_player(Fixture& f, uint8_t player) {
    f.match->simulation().players[player].status = OA_PLAYER_STATUS_MIRRORED;
}

// The smoke columns over corpses among a layer's emitters.
uint32_t smoke_columns(const sim::effect_particles::Layer& layer) {
    uint32_t columns = 0;
    for (uint16_t index = 0; index < layer.count; ++index) {
        const auto& emitter =
            layer.emitters[(layer.head + index) % sim::effect_particles::layer_capacity];
        if (emitter.kind == sim::effect_particles::EmitterKind::smoke &&
            emitter.interval == wreck_smoke_interval)
            ++columns;
    }
    return columns;
}

bool same(const KillOutcome& left, const KillOutcome& right) {
    return left.kind == right.kind && left.killed_percent == right.killed_percent &&
           left.wreck_level == right.wreck_level;
}

// A unit left to die by the weapon kind with the given health and previous
// 30-tick health percentage.
void doom(sim::unit_spawn::Slot& unit, int16_t health, uint8_t previous_percent) {
    unit.unit->health = health;
    unit.unit->previous_health_percent = previous_percent;
}

// A death shared through unit_killed, and what the unit's record and queue
// held when it was shared.
struct SharedDeath {
    uint16_t unit{};
    KillOutcome outcome{};
    bool live{};
    uint16_t type_index{};
    uint32_t last_attacker_id{};
    uint8_t last_attacker_owner{};
    bool had_orders{};
    uint16_t owner_units{};
};

// A health event shared for a unit simulated elsewhere, with its route.
struct SharedHit {
    sim::unit_health::RouteIdentity route{};
    sim::unit_health::HealthEvent event{};
};

struct Shared {
    sim::match_runtime::Match* match{};
    std::vector<SharedDeath> deaths;
    std::vector<SharedHit> hits;
};

void share_deaths(Fixture& f, Shared& shared) {
    shared.match = f.match.get();
    f.match->multiplayer.context = &shared;
    f.match->multiplayer.unit_killed =
        [](void* context, uint16_t unit, const KillOutcome& outcome) {
            auto& self = *static_cast<Shared*>(context);
            auto& slot = self.match->world().slots[unit];
            self.deaths.push_back(
                {unit,
                 outcome,
                 (slot.unit->flags & OA_UNIT_FLAG_LIVE) != 0,
                 slot.record.type_index,
                 slot.record.last_attacker_id,
                 slot.record.last_attacker_owner,
                 self.match->orders(unit).primary != nullptr,
                 self.match->state().game.players[slot.record.owner_index].unit_count}
            );
        };
}

// Records the health events shared for units simulated elsewhere; each goes
// out on route_base plus its source unit's slot.
void share_hits(Fixture& f, Shared& shared) {
    f.match->multiplayer.context = &shared;
    f.match->multiplayer.health_route = [](void*, uint16_t source_unit) {
        return route_base + source_unit;
    };
    f.match->multiplayer.health_shared = [](void* context,
                                            sim::unit_health::RouteIdentity route,
                                            const sim::unit_health::HealthEvent& event) {
        static_cast<Shared*>(context)->hits.push_back({route, event});
    };
}

// A player-1 unit simulated here dies in its own tick at the percentage and
// wreck level its Killed script settles: the death is shared once, while the
// unit is still live with its type, its last attacker and its orders and
// before its owner's unit count drops. Then it is torn down with its orders
// cleared; wreck level 3 steps past the heap, so it leaves nothing.
void local_death_is_shared_before_teardown() {
    const Art art;
    Fixture f(death_options(art));
    leave_corpses(f);
    auto& victim = idle(f, 1, 64, 64);
    auto& attacker = idle(f, 0, 200, 64);
    f.run(1);
    Shared shared;
    share_deaths(f, shared);
    (void)f.match->issue_ground_move(victim.unit_index, {200 << 16, 0, 200 << 16}, false);
    CHECK(f.match->orders(victim.unit_index).primary != nullptr);
    auto& game = f.match->state().game;
    const auto units = game.players[1].unit_count;
    const auto wrecks = f.match->wrecks().size();
    doom(victim, -full_health, 100);
    victim.record.damage_kind = weapon_hit;
    victim.record.last_attacker_id = attacker.unit_index;
    victim.record.last_attacker_owner = 0;
    victim.unit->flags |= OA_UNIT_FLAG_DEATH_PENDING;

    f.run(1);
    CHECK(shared.deaths.size() == 1);
    const auto& death = shared.deaths[0];
    CHECK(death.unit == victim.unit_index);
    CHECK(same(death.outcome, {DeathKind::weapon, 100, 3}));
    CHECK(death.live && death.type_index == 1);
    CHECK(death.last_attacker_id == attacker.unit_index && death.last_attacker_owner == 0);
    CHECK(death.had_orders && death.owner_units == units);
    CHECK(!live(victim) && f.match->orders(victim.unit_index).primary == nullptr);
    CHECK(game.players[1].unit_count == units - 1);
    CHECK(f.match->wrecks().size() == wrecks);
    // The Killed query ran once and picked corpsetype 3.
    CHECK(f.services.explosions.size() == 1 && f.services.explosions[0].flags == 3);
    std::cout << "local death is shared before teardown passed\n";
}

// The outcome shared follows the kill handler's rules: a dismissed unit
// leaves its plain corpse with no Killed percentage; captured, reclaimed and
// cancelled units, and a unit still above 0 health, neither explode nor leave
// a wreck; an unfinished unit keeps its percentage but leaves no wreck
// whatever Killed picks; kind 0 takes the percentage from the unit's own
// health like any other.
void shared_outcome_follows_the_kill_rules() {
    struct Row {
        uint8_t kind{};
        int16_t health{};
        uint8_t previous_percent{};
        bool unfinished{};
        KillOutcome expected{};
    };

    const auto kind = [](DeathKind death) { return static_cast<uint8_t>(death); };
    const Row rows[] = {
        {kind(DeathKind::dismissed), full_health, 100, false, {DeathKind::dismissed, 0, 1}},
        {kind(DeathKind::reclaim), -500, 100, false, {DeathKind::reclaim, 0, 0}},
        {kind(DeathKind::captured), -23000, 100, false, {DeathKind::captured, 0, 0}},
        {kind(DeathKind::cancelled), -500, 100, false, {DeathKind::cancelled, 0, 0}},
        {weapon_hit, 10, 100, false, {DeathKind::weapon, 0, 0}},
        // (1000 * 100 / 1000 + 100) / 2 = 100: corpsetype 3, dropped.
        {weapon_hit, -full_health, 100, true, {DeathKind::weapon, 100, 0}},
        // (200 * 100 / 1000 + 60) / 2 = 40: corpsetype 2.
        {kind(DeathKind::self_destruct), -200, 60, false, {DeathKind::self_destruct, 40, 2}},
        // (0 + 60) / 2 = 30: corpsetype 2.
        {replaced_unit_death, 0, 60, false, {DeathKind{}, 30, 2}},
    };
    const Art art;
    Fixture f(death_options(art));
    leave_corpses(f);
    // Four units a player, both players simulated here.
    std::vector<sim::unit_spawn::Slot*> units;
    for (uint32_t row = 0; row < std::size(rows); ++row)
        units.push_back(
            &idle(f, static_cast<uint8_t>(row / 4), 24 + 48 * (row % 4), 64 + 48 * (row / 4))
        );
    f.run(1);
    Shared shared;
    share_deaths(f, shared);
    for (uint32_t row = 0; row < std::size(rows); ++row) {
        auto& unit = *units[row];
        doom(unit, rows[row].health, rows[row].previous_percent);
        if (rows[row].unfinished)
            unit.record.build_remaining = 0.5F;
        f.match->kill_unit(unit.unit_index, rows[row].kind);
        CHECK(!live(unit));
        CHECK(shared.deaths.size() == row + 1);
        CHECK(shared.deaths[row].unit == unit.unit_index);
        CHECK(same(shared.deaths[row].outcome, rows[row].expected));
    }
    std::cout << "shared outcome follows the kill rules passed\n";
}

// Units simulated elsewhere die here without sharing anything: a dying copy
// in its tick, a slot's stale occupant through kill_unit and a player's sweep
// of the commander rule. With no hooks a local death is torn down as before.
void death_simulated_elsewhere_is_not_shared() {
    const Art art;
    Fixture f(death_options(art));
    leave_corpses(f);
    auto& copy = idle(f, 1, 64, 64);
    auto& stale = idle(f, 1, 128, 64);
    auto& swept = idle(f, 1, 64, 128);
    f.run(1);
    mirror_player(f, 1);
    Shared shared;
    share_deaths(f, shared);
    doom(copy, 0, 100);
    copy.unit->flags |= OA_UNIT_FLAG_DEATH_PENDING;
    f.run(1);
    CHECK(!live(copy));
    doom(stale, 0, 100);
    f.match->kill_unit(stale.unit_index, replaced_unit_death);
    CHECK(!live(stale));
    f.match->destroy_player_units(1);
    CHECK(!live(swept));
    CHECK(shared.deaths.empty());

    Fixture plain(death_options(art));
    leave_corpses(plain);
    auto& unit = idle(plain, 0, 64, 64);
    plain.run(1);
    const auto wrecks = plain.match->wrecks().size();
    // (0 + 40) / 2 = 20: corpsetype 1, the corpse itself.
    doom(unit, 0, 40);
    unit.record.damage_kind = weapon_hit;
    unit.unit->flags |= OA_UNIT_FLAG_DEATH_PENDING;
    plain.run(1);
    CHECK(!live(unit));
    CHECK(plain.match->wrecks().size() == wrecks + 1);
    CHECK(plain.match->wrecks().back().feature == dead_feature);
    std::cout << "death simulated elsewhere is not shared passed\n";
}

// A last attacker whose player's slot has gone free credits nobody: the
// death is shared with the attacker but no owner for it, the loss is counted
// and that player gains no kill.
void attacker_of_a_free_slot_is_not_credited() {
    const Art art;
    Fixture f(death_options(art));
    leave_corpses(f);
    auto& victim = idle(f, 0, 64, 64);
    auto& attacker = idle(f, 1, 200, 96);
    f.run(1);
    Shared shared;
    share_deaths(f, shared);
    auto& game = f.match->state().game;
    game.players[1].status = OA_PLAYER_STATUS_FREE;
    const auto kills = game.players[1].kills;
    const auto losses = game.players[0].losses;
    doom(victim, -full_health, 100);
    victim.record.last_attacker_id = attacker.unit_index;
    victim.record.last_attacker_owner = 1;
    f.match->kill_unit(victim.unit_index, weapon_hit);
    CHECK(!live(victim));
    CHECK(shared.deaths.size() == 1);
    CHECK(shared.deaths[0].last_attacker_id == attacker.unit_index);
    CHECK(shared.deaths[0].last_attacker_owner == no_player);
    CHECK(game.players[0].losses == losses + 1 && game.players[1].kills == kills);
    std::cout << "attacker of a free slot is not credited passed\n";
}

// The SelfDestruct order's death is shared as a self-destruct; a commander
// simulated here is shared first under the commander rule, then each unit
// its sweep leaves dying as a self-destruct in its own tick.
void self_destruct_and_sweep_are_shared() {
    const Art art;
    {
        Fixture f(death_options(art));
        auto& unit = idle(f, 0, 64, 64);
        f.run(1);
        Shared shared;
        share_deaths(f, shared);
        (void)f.match->insert_ground_order(unit.unit_index, sim::match_runtime::self_destruct_kind);
        f.run(1);
        CHECK(!live(unit));
        CHECK(shared.deaths.size() == 1 && shared.deaths[0].unit == unit.unit_index);
        CHECK(same(shared.deaths[0].outcome, {DeathKind::self_destruct, 100, 3}));
    }
    Fixture f(death_options(art));
    f.match->state().game.session_rules = 1;
    std::strcpy(f.match->state().game.sides[0].commander, "testunit");
    auto& commander = idle(f, 0, 64, 64);
    auto& first = idle(f, 0, 128, 64);
    auto& second = idle(f, 0, 64, 128);
    f.run(1);
    Shared shared;
    share_deaths(f, shared);
    doom(commander, -full_health, 100);
    f.match->kill_unit(commander.unit_index, weapon_hit);
    CHECK(!live(commander) && live(first) && live(second));
    CHECK(shared.deaths.size() == 1 && shared.deaths[0].unit == commander.unit_index);
    CHECK(shared.deaths[0].outcome.kind == DeathKind::weapon);
    f.run(1);
    CHECK(!live(first) && !live(second));
    CHECK(shared.deaths.size() == 3);
    CHECK(shared.deaths[1].unit == first.unit_index && shared.deaths[2].unit == second.unit_index);
    for (std::size_t death = 1; death < shared.deaths.size(); ++death)
        CHECK(same(shared.deaths[death].outcome, {DeathKind::self_destruct, 100, 3}));
    std::cout << "self-destruct and sweep are shared passed\n";
}

// A match with the death blasts armed, a mirrored player-1 victim holding an
// order and a player-0 attacker out of the blast's reach; the counts a
// shared kill changes, taken before it.
struct Scene {
    Art art;
    Fixture f{death_options(art)};
    sim::unit_spawn::Slot* victim{};
    sim::unit_spawn::Slot* attacker{};
    Shared shared;
    std::size_t wrecks{};
    int32_t blasts{};
    uint32_t corpse_smoke{};
    uint16_t veteran{};

    Scene() {
        arm_death_blasts(f);
        leave_corpses(f);
        victim = &idle(f, 1, 64, 64);
        attacker = &idle(f, 0, 200, 96);
        f.run(1);
        (void)f.match->issue_ground_move(victim->unit_index, {200 << 16, 0, 200 << 16}, false);
        mirror_player(f, 1);
        share_deaths(f, shared);
        wrecks = f.match->wrecks().size();
        blasts = effects().explosion_count;
        corpse_smoke = columns();
        veteran = attacker->record.veteran_level;
    }

    const sim::effect_particles::EffectWorld& effects() const {
        return std::as_const(*f.match).effects();
    }

    uint32_t columns() const {
        return smoke_columns(effects().layers[sim::effect_particles::layer_smoke]);
    }

    oa::Game& game() { return f.match->state().game; }

    void apply(const KillOutcome& outcome, uint16_t attacker_unit, uint8_t attacker_owner) {
        f.match->apply_kill(victim->unit_index, outcome, attacker_unit, attacker_owner);
    }

    // The blast art of the explosion the kill logged, or null for none.
    const formats::gaf::Sequence* blast() const {
        if (effects().explosion_count != blasts + 1)
            return nullptr;
        return effects().explosions[blasts].sprite.sequence;
    }
};

// A kill the victim's machine settled at percentage 60 and wreck level 1:
// the victim takes that machine's attacker, loses its orders and is torn
// down; its Killed script runs once for its pieces with 60 (corpsetype 3),
// but the wreck is the level shared, the plain corpse with its smoke column;
// it explodes with its explodeas blast, and the kill is counted for the
// attacker and its owner. Nothing is shared back.
void shared_kill_applies_the_settled_outcome() {
    Scene s;
    auto& game = s.game();
    const auto kills = game.players[0].kills;
    const auto losses = game.players[1].losses;
    s.apply({DeathKind::weapon, 60, 1}, s.attacker->unit_index, 0);
    CHECK(
        s.victim->record.last_attacker_id == s.attacker->unit_index &&
        s.victim->record.last_attacker_owner == 0
    );
    CHECK(!live(*s.victim));
    CHECK(s.f.match->orders(s.victim->unit_index).primary == nullptr);
    CHECK(s.f.match->wrecks().size() == s.wrecks + 1);
    CHECK(s.f.match->wrecks().back().feature == dead_feature);
    CHECK(s.columns() == s.corpse_smoke + 1);
    const auto& pieces = s.f.services.explosions;
    CHECK(pieces.size() == 1);
    CHECK(pieces[0].unit == s.victim->unit_index && pieces[0].piece == root_piece);
    CHECK(pieces[0].flags == killed_corpsetype(60) && pieces[0].flags == 3);
    CHECK(s.blast() == &s.art.explode4);
    CHECK(game.shake_duration == blast_shake_ticks);
    CHECK(game.shake_amplitude_x == blast_shake_magnitude);
    CHECK(game.players[0].kills == kills + 1 && game.players[1].losses == losses + 1);
    CHECK(s.attacker->record.veteran_level == s.veteran + 1);
    CHECK(s.shared.deaths.empty());
    std::cout << "shared kill applies the settled outcome passed\n";
}

// Each part of a shared outcome on its own.
void shared_kill_variants() {
    {
        // Percentage 0: no Killed pieces and no explosion; the heap at level 2.
        Scene s;
        s.apply({DeathKind::weapon, 0, 2}, s.attacker->unit_index, 0);
        CHECK(!live(*s.victim) && s.f.services.explosions.empty());
        CHECK(s.effects().explosion_count == s.blasts);
        CHECK(s.f.match->wrecks().size() == s.wrecks + 1);
        CHECK(s.f.match->wrecks().back().feature == heap_feature);
    }
    {
        // A self-destruct explodes with selfdestructas, which does not shake.
        Scene s;
        s.apply({DeathKind::self_destruct, 60, 0}, s.victim->unit_index, 1);
        CHECK(!live(*s.victim) && s.f.services.explosions.size() == 1);
        CHECK(s.blast() == &s.art.explode5);
        CHECK(s.game().shake_duration == 0 && s.game().shake_amplitude_x == 0);
        CHECK(s.f.match->wrecks().size() == s.wrecks);
    }
    {
        // A dismissed unit's corpse raises no smoke.
        Scene s;
        s.apply({DeathKind::dismissed, 0, 1}, 0, no_player);
        CHECK(s.f.match->wrecks().size() == s.wrecks + 1);
        CHECK(s.columns() == s.corpse_smoke);
    }
    {
        // An unfinished unit plays its Killed pieces but does not explode,
        // and its death counts as a loss with no kill.
        Scene s;
        s.victim->record.build_remaining = 0.5F;
        const auto kills = s.game().players[0].kills;
        const auto losses = s.game().players[1].losses;
        s.apply({DeathKind::weapon, 100, 0}, s.attacker->unit_index, 0);
        CHECK(!live(*s.victim) && s.f.services.explosions.size() == 1);
        CHECK(s.effects().explosion_count == s.blasts);
        CHECK(s.game().players[0].kills == kills && s.game().players[1].losses == losses + 1);
    }
    {
        // With no attacker the loss is counted and nobody is credited.
        Scene s;
        const auto kills = s.game().players[0].kills;
        const auto losses = s.game().players[1].losses;
        s.apply({DeathKind::weapon, 50, 1}, 0, no_player);
        CHECK(s.victim->record.last_attacker_id == 0);
        CHECK(s.victim->record.last_attacker_owner == no_player);
        CHECK(s.game().players[0].kills == kills && s.game().players[1].losses == losses + 1);
        CHECK(s.attacker->record.veteran_level == s.veteran);
    }
    {
        // A unit no longer live is left alone.
        Scene s;
        s.apply({DeathKind::weapon, 50, 1}, s.attacker->unit_index, 0);
        const auto wrecks = s.f.match->wrecks().size();
        const auto pieces = s.f.services.explosions.size();
        const auto blasts = s.effects().explosion_count;
        const auto losses = s.game().players[1].losses;
        const auto kills = s.game().players[0].kills;
        s.apply({DeathKind::weapon, 90, 2}, 0, no_player);
        CHECK(s.victim->record.last_attacker_id == s.attacker->unit_index);
        CHECK(s.victim->record.last_attacker_owner == 0);
        CHECK(s.f.match->wrecks().size() == wrecks && s.f.services.explosions.size() == pieces);
        CHECK(s.effects().explosion_count == blasts);
        CHECK(s.game().players[1].losses == losses && s.game().players[0].kills == kills);
    }
    {
        // A reclaim credits the attacker with the built share of the metal
        // cost.
        Scene s;
        s.f.def.build_cost_metal = 100;
        const auto metal = s.attacker->record.economy.metal.produced;
        s.apply({DeathKind::reclaim, 0, 0}, s.attacker->unit_index, 0);
        CHECK(s.attacker->record.economy.metal.produced == metal + 100.0F);
    }
    std::cout << "shared kill variants passed\n";
}

// A shared kill lets go of the carry links before the dying unit's Killed
// script runs. A carried unit leaves its carrier, which lives on. A
// transport's cargo, simulated elsewhere too, takes the lethal cargo hit from
// the shared attacker, which goes out as a health event on the attacker's
// route, and is set down at 0 health but alive, since damage never kills a
// unit simulated elsewhere; when the transport self-destructed, the hit is a
// self-destruct from the transport. Each Killed script runs once, and no
// death is shared.
void shared_kill_lets_go_of_carried_units_first() {
    const Art art;
    {
        Fixture f(death_options(art));
        leave_corpses(f);
        auto& carrier = idle(f, 1, 64, 64);
        auto& passenger = idle(f, 1, 80, 64);
        auto& transport = idle(f, 1, 64, 160);
        auto& cargo = idle(f, 1, 80, 160);
        auto& attacker = idle(f, 0, 200, 200);
        f.run(1);
        f.match->set_carry_link(passenger.unit_index, carrier.unit_index, -1, 1);
        f.match->set_carry_link(cargo.unit_index, transport.unit_index, -1, 1);
        CHECK(passenger.record.attach_parent != 0 && cargo.record.attach_parent != 0);
        mirror_player(f, 1);
        Shared shared;
        share_deaths(f, shared);
        share_hits(f, shared);
        const auto& pieces = f.services.explosions;

        f.match->apply_kill(
            passenger.unit_index, {DeathKind::weapon, 60, 1}, attacker.unit_index, 0
        );
        CHECK(!live(passenger) && live(carrier));
        CHECK(passenger.record.attach_parent == 0 && carrier.record.attach_first_child == 0);
        CHECK(pieces.size() == 1 && pieces[0].unit == passenger.unit_index);
        CHECK(!pieces[0].carried);

        f.match->apply_kill(
            transport.unit_index, {DeathKind::weapon, 60, 1}, attacker.unit_index, 0
        );
        CHECK(!live(transport));
        CHECK(pieces.size() == 2 && pieces[1].unit == transport.unit_index);
        CHECK(!pieces[1].carrying);
        CHECK(cargo.record.attach_parent == 0 && live(cargo) && cargo.unit->health == 0);
        CHECK(cargo.record.damage_kind == cargo_death);
        CHECK(
            cargo.record.last_attacker_id == attacker.unit_index &&
            cargo.record.last_attacker_owner == 0
        );
        CHECK(shared.hits.size() == 1);
        const auto& hit = shared.hits[0];
        CHECK(hit.route == route_base + attacker.unit_index);
        CHECK(hit.event.target == cargo.unit_index && hit.event.source == attacker.unit_index);
        CHECK(hit.event.kind == cargo_death);
        CHECK(shared.deaths.empty());
    }
    Fixture f(death_options(art));
    leave_corpses(f);
    auto& ferry = idle(f, 1, 64, 64);
    auto& crew = idle(f, 1, 80, 64);
    f.run(1);
    f.match->set_carry_link(crew.unit_index, ferry.unit_index, -1, 1);
    CHECK(crew.record.attach_parent != 0);
    mirror_player(f, 1);
    Shared shared;
    share_deaths(f, shared);
    share_hits(f, shared);
    f.match->apply_kill(ferry.unit_index, {DeathKind::self_destruct, 60, 0}, ferry.unit_index, 1);
    CHECK(!live(ferry) && crew.record.attach_parent == 0 && live(crew));
    CHECK(crew.record.damage_kind == self_destruct_death);
    CHECK(shared.hits.size() == 1);
    CHECK(shared.hits[0].event.source == ferry.unit_index);
    CHECK(shared.hits[0].event.kind == self_destruct_death);
    const auto& pieces = f.services.explosions;
    CHECK(pieces.size() == 1 && pieces[0].unit == ferry.unit_index && !pieces[0].carrying);
    CHECK(shared.deaths.empty());
    std::cout << "shared kill lets go of carried units first passed\n";
}

// A unit still in a slot another player's new unit takes dies through
// kill_unit with kind 0 by its own health: a copy at 0 health with a previous
// 60% dies at (0 + 60) / 2 = 30, which its Killed script turns into the heap,
// explodes and counts no statistics; a copy above 0 health vanishes with no
// Killed, no explosion and no wreck. Neither is shared; a unit simulated here
// that dies this way is, with kind 0.
void stale_occupant_dies_by_its_own_health() {
    const Art art;
    Fixture f(death_options(art));
    arm_death_blasts(f);
    leave_corpses(f);
    auto& copy = idle(f, 1, 64, 64);
    auto& whole = idle(f, 1, 128, 64);
    auto& attacker = idle(f, 0, 200, 96);
    auto& own = idle(f, 0, 64, 160);
    f.run(1);
    mirror_player(f, 1);
    Shared shared;
    share_deaths(f, shared);
    const auto& effects = std::as_const(*f.match).effects();
    auto& game = f.match->state().game;
    const auto kills = game.players[0].kills;
    const auto losses = game.players[1].losses;
    const auto veteran = attacker.record.veteran_level;
    const auto wrecks = f.match->wrecks().size();
    const auto blasts = effects.explosion_count;
    doom(copy, 0, 60);
    copy.record.last_attacker_id = attacker.unit_index;
    copy.record.last_attacker_owner = 0;
    f.match->kill_unit(copy.unit_index, replaced_unit_death);
    CHECK(!live(copy));
    const auto& pieces = f.services.explosions;
    CHECK(pieces.size() == 1 && pieces[0].flags == killed_corpsetype(30) && pieces[0].flags == 2);
    CHECK(f.match->wrecks().size() == wrecks + 1);
    CHECK(f.match->wrecks().back().feature == heap_feature);
    CHECK(effects.explosion_count == blasts + 1);
    CHECK(effects.explosions[blasts].sprite.sequence == &art.explode4);
    CHECK(game.players[0].kills == kills && game.players[1].losses == losses);
    CHECK(attacker.record.veteran_level == veteran);

    f.match->kill_unit(whole.unit_index, replaced_unit_death);
    CHECK(!live(whole) && pieces.size() == 1);
    CHECK(effects.explosion_count == blasts + 1 && f.match->wrecks().size() == wrecks + 1);
    CHECK(shared.deaths.empty());

    doom(own, 0, 60);
    f.match->kill_unit(own.unit_index, replaced_unit_death);
    CHECK(!live(own));
    CHECK(shared.deaths.size() == 1 && shared.deaths[0].unit == own.unit_index);
    CHECK(same(shared.deaths[0].outcome, {DeathKind{}, 30, 2}));
    std::cout << "stale occupant dies by its own health passed\n";
}

} // namespace

int main() {
    try {
        local_death_is_shared_before_teardown();
        shared_outcome_follows_the_kill_rules();
        death_simulated_elsewhere_is_not_shared();
        attacker_of_a_free_slot_is_not_credited();
        self_destruct_and_sweep_are_shared();
        shared_kill_applies_the_settled_outcome();
        shared_kill_variants();
        shared_kill_lets_go_of_carried_units_first();
        stale_occupant_dies_by_its_own_health();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
