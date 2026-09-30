// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The match's event hooks (Match::event_hooks) only read. The synthetic
// skirmish of match-determinism plays out tick for tick the same with every
// hook set as without, to the digests that test pins; in it each kind of
// event is reported with the units and shots it names, and each death once
// while the dying unit still names its owner. A shot launched from another
// machine's report is reported with the reported aim, which its record does
// not keep; burst shots and meteors, creations and completions are reported
// where they happen.
#include "combat_fixture.hpp"

#include "oa/base/game_loop.hpp"
#include "oa/sim/match_runtime/event_hooks.hpp"
#include "oa/sim/match_runtime/match_trace.hpp"
#include "oa/sim/state_hash.hpp"
#include "oa/sim/weapon_execution/interceptor.hpp"
#include "oa/sim/weapon_execution/projectile_pool.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <vector>

namespace {

using namespace combat_fixture;
namespace trace = oa::sim::trace;
using sim::match_runtime::EventHooks;
using sim::match_runtime::KillOutcome;
using sim::match_runtime::ShotSource;

constexpr uint32_t match_ticks = 360;
constexpr uint32_t halfway_tick = 180;

// match-determinism's pinned digests (tests/determinism_test.cpp); the run
// with hooks must reach the same.
constexpr uint64_t pinned_halfway_state = 0x60f37308a7eb5e14ull;
constexpr uint64_t pinned_final_state = 0xf7ca3a754b0db9c8ull;
constexpr uint64_t pinned_final_total = 0x0e75170de962eee6ull;

// The fixture's gun, and the kinds apply_damage_event takes.
constexpr uint32_t gun_registry_index = 1;
constexpr uint8_t weapon_kind = 1;

constexpr int32_t fx(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

struct CreatedReport {
    uint16_t unit{};
    uint16_t type_index{};
    uint8_t owner{};
};

struct FinishedReport {
    uint16_t unit{};
    uint16_t builder{};
    bool finished_at_call{};
};

struct ShotReport {
    ShotSource source{};
    oa_ref32 def{};
    oa_ref32 origin_unit{};
    std::optional<FixedVec3> aim;
    FixedVec3 record_target{};
    uint16_t target_unit{};
};

struct DetonationReport {
    oa_ref32 def{};
    uint16_t direct_unit{};
};

struct DamageReport {
    uint16_t target{};
    uint16_t source{};
    int16_t amount{};
    uint8_t kind{};
    int16_t health_at_call{};
};

struct DeathReport {
    uint16_t unit{};
    uint8_t owner{};
    uint16_t type_index{};
    bool live_at_call{};
    bool settled_elsewhere{};
};

// Keeps what every hook reported.
struct Recorder {
    std::vector<CreatedReport> created;
    std::vector<FinishedReport> finished;
    std::vector<ShotReport> shots;
    std::vector<DetonationReport> detonations;
    std::vector<DamageReport> damage;
    std::vector<DeathReport> deaths;

    /// Returns hooks that report into this recorder.
    ///
    /// @return every entry set, with this recorder as the context
    EventHooks hooks() {
        EventHooks hooks;
        hooks.context = this;
        hooks.unit_created = [](void* context, const World& world, uint16_t unit) {
            const auto& record = world.units[unit];
            static_cast<Recorder*>(context)->created.push_back(
                {unit, record.type_index, record.owner_index}
            );
        };
        hooks.unit_finished =
            [](void* context, const World& world, uint16_t unit, uint16_t builder) {
                static_cast<Recorder*>(context)->finished.push_back(
                    {unit, builder, world.units[unit].build_remaining == 0.0F}
                );
            };
        hooks.shot_placed = [](void* context,
                               const World&,
                               const Projectile& shot,
                               ShotSource source,
                               const FixedVec3* aim,
                               uint16_t target_unit) {
            ShotReport report{
                source, shot.def, shot.source, std::nullopt, shot.target, target_unit
            };
            if (aim != nullptr)
                report.aim = *aim;
            static_cast<Recorder*>(context)->shots.push_back(report);
        };
        hooks.shot_detonated =
            [](void* context, const World&, const Projectile& shot, uint16_t direct_unit) {
                static_cast<Recorder*>(context)->detonations.push_back({shot.def, direct_unit});
            };
        hooks.unit_damaged = [](void* context,
                                const World& world,
                                uint16_t target,
                                uint16_t source,
                                int16_t amount,
                                uint8_t kind) {
            static_cast<Recorder*>(context)->damage.push_back(
                {target, source, amount, kind, world.units[target].health}
            );
        };
        hooks.unit_died = [](void* context,
                             const World& world,
                             uint16_t unit,
                             const KillOutcome&,
                             bool settled_elsewhere) {
            const auto& record = world.units[unit];
            static_cast<Recorder*>(context)->deaths.push_back(
                {unit,
                 record.owner_index,
                 record.type_index,
                 (record.flags & OA_UNIT_FLAG_LIVE) != 0,
                 settled_elsewhere}
            );
        };
        return hooks;
    }

    /// Counts the shots reported from one source.
    ///
    /// @param source how the shots came to be
    /// @return the number reported
    std::size_t shots_from(ShotSource source) const {
        return static_cast<std::size_t>(
            std::count_if(shots.begin(), shots.end(), [source](const ShotReport& shot) {
                return shot.source == source;
            })
        );
    }
};

/// Tells whether two points are the same.
///
/// @param a first point
/// @param b second point
/// @return true when every coordinate is equal
bool same_point(const FixedVec3& a, const FixedVec3& b) {
    return a.x == b.x && a.y == b.y && a.z == b.z;
}

/// Tells whether a unit slot holds a live unit.
///
/// @param world canonical state
/// @param slot unit slot
/// @return true when the slot has a type and the live flag
bool live(const World& world, uint32_t slot) {
    return world.units[slot].type_index != 0 && (world.units[slot].flags & OA_UNIT_FLAG_LIVE) != 0;
}

/// Returns the total section digest of the match's current state.
///
/// @param match match to sample
/// @param[in,out] sides scratch table of one entry per unit slot
/// @return the total section's 64-bit digest
uint64_t total_digest(const sim::match_runtime::Match& match, std::vector<trace::UnitSide>& sides) {
    const auto& world = match.state();
    sides.assign(world.unit_slot_count, {});
    sim::match_runtime::fill_trace_sides(match, sides);
    const trace::RandomState random{match.random_state(), match.lcg_state()};
    const auto digest = trace::tick_digest(world, sides.data(), random);
    return digest.sections[static_cast<size_t>(trace::Section::total)].value;
}

/// Digests the match state as the savegame check does, with a default clock,
/// the camera at the origin and no meteor storm.
///
/// @param match match to digest
/// @return the match-state digest
uint64_t state_digest(sim::match_runtime::Match& match) {
    const base::game_loop::Timing timing{};
    return trace::match_state_hash(match, timing, 0, 0, nullptr);
}

struct Run {
    std::vector<uint64_t> totals; // each tick's total section digest
    uint64_t halfway_state{};
    uint64_t final_state{};
    std::vector<uint16_t> units; // the six spawned slots, in spawn order
    std::vector<uint8_t> owners; // their owners
    uint16_t gunner{};
    uint16_t target{};
};

/// Plays match-determinism's skirmish: player 0's first two units attack
/// player 1's first, which attacks player 0's first back; player 1's second
/// unit marches on player 0's side; each player's third unit patrols across
/// the map.
///
/// @param hooks event hooks set before the first unit is created
/// @return each tick's total digest, both state digests and the units
Run play(const EventHooks& hooks) {
    Fixture f;
    f.match->event_hooks = hooks;
    auto& gunner = f.spawn(0, 48, 48);
    auto& second = f.spawn(0, 48, 112);
    auto& scout = f.spawn(0, 64, 208);
    auto& target = f.spawn(1, 176, 64);
    auto& marcher = f.spawn(1, 208, 128);
    auto& patroller = f.spawn(1, 200, 208);
    Run run;
    for (const auto* slot : {&gunner, &second, &scout, &target, &marcher, &patroller}) {
        run.units.push_back(slot->unit_index);
        run.owners.push_back(slot->record.owner_index);
    }
    run.gunner = gunner.unit_index;
    run.target = target.unit_index;
    CHECK(f.match->issue_attack(run.gunner, run.target, true));
    CHECK(f.match->issue_attack(second.unit_index, run.target, true));
    CHECK(f.match->issue_attack(run.target, run.gunner, true));
    f.match->issue_ground_move(marcher.unit_index, {96 << 16, 0, 128 << 16}, false);
    f.match->issue_patrol(scout.unit_index, {200 << 16, 0, 40 << 16}, false);
    f.match->issue_patrol(patroller.unit_index, {40 << 16, 0, 176 << 16}, false);
    std::vector<trace::UnitSide> sides;
    for (uint32_t tick = 1; tick <= match_ticks; ++tick) {
        f.run(1);
        run.totals.push_back(total_digest(*f.match, sides));
        if (tick == halfway_tick)
            run.halfway_state = state_digest(*f.match);
    }
    run.final_state = state_digest(*f.match);
    CHECK(!live(f.match->state(), run.gunner) && !live(f.match->state(), run.target));
    return run;
}

/// The skirmish with every hook set plays exactly as without, to
/// match-determinism's pins.
void hooks_change_nothing() {
    Recorder recorder;
    const Run plain = play({});
    const Run hooked = play(recorder.hooks());
    CHECK(plain.totals == hooked.totals);
    CHECK(plain.halfway_state == hooked.halfway_state);
    CHECK(plain.final_state == hooked.final_state);
    CHECK(hooked.halfway_state == pinned_halfway_state);
    CHECK(hooked.final_state == pinned_final_state);
    CHECK(hooked.totals.back() == pinned_final_total);
    std::cout << "hooks change nothing passed\n";
}

/// Tells whether a slot is one of the run's units.
///
/// @param run the run
/// @param unit slot to find
/// @return true when the run spawned it
bool spawned(const Run& run, uint16_t unit) {
    return std::find(run.units.begin(), run.units.end(), unit) != run.units.end();
}

/// Every kind of event of the skirmish is reported with the units it names.
void skirmish_reports_every_kind() {
    Recorder recorder;
    const Run run = play(recorder.hooks());

    // Each spawned unit once, in order, with its type and owner filled; the
    // fixture's type is mobile, so none is finished at creation.
    CHECK(recorder.created.size() == run.units.size());
    for (std::size_t i = 0; i < run.units.size(); ++i)
        CHECK(
            recorder.created[i].unit == run.units[i] && recorder.created[i].type_index == 1 &&
            recorder.created[i].owner == run.owners[i]
        );
    CHECK(recorder.finished.empty());

    // The guns' shots, each from a spawned unit, aimed at a point and a unit.
    CHECK(recorder.shots_from(ShotSource::weapon) > 0);
    CHECK(recorder.shots.size() == recorder.shots_from(ShotSource::weapon));
    for (const auto& shot : recorder.shots)
        CHECK(
            shot.def == oa_ref_from_index(gun_registry_index) &&
            spawned(run, static_cast<uint16_t>(oa_unit_slot_from_ref(shot.origin_unit))) &&
            shot.aim && same_point(*shot.aim, shot.record_target) && spawned(run, shot.target_unit)
        );

    // Detonations of the gun, damage to spawned units by spawned units.
    CHECK(!recorder.detonations.empty());
    for (const auto& detonation : recorder.detonations)
        CHECK(detonation.def == oa_ref_from_index(gun_registry_index));
    CHECK(!recorder.damage.empty());
    for (const auto& hit : recorder.damage)
        CHECK(
            hit.kind == weapon_kind && hit.amount > 0 && spawned(run, hit.target) &&
            spawned(run, hit.source) && hit.health_at_call > 0
        );

    // Each death once, while the unit is still live and names its owner.
    CHECK(!recorder.deaths.empty());
    std::vector<uint16_t> dead;
    for (const auto& death : recorder.deaths) {
        CHECK(std::find(dead.begin(), dead.end(), death.unit) == dead.end());
        dead.push_back(death.unit);
        const auto index = static_cast<std::size_t>(
            std::find(run.units.begin(), run.units.end(), death.unit) - run.units.begin()
        );
        CHECK(index < run.units.size());
        CHECK(
            death.owner == run.owners[index] && death.type_index == 1 && death.live_at_call &&
            !death.settled_elsewhere
        );
    }
    CHECK(std::find(dead.begin(), dead.end(), run.gunner) != dead.end());
    CHECK(std::find(dead.begin(), dead.end(), run.target) != dead.end());
    std::cout << "skirmish reports every kind passed (" << recorder.shots.size() << " shots, "
              << recorder.detonations.size() << " detonations, " << recorder.damage.size()
              << " hits, " << recorder.deaths.size() << " deaths)\n";
}

/// Returns the gun's shared description for a shot of the fixture's unit.
///
/// @param f the fixture
/// @param source firing unit
/// @param target unit aimed at
/// @param aim point aimed at
/// @return the shot as another machine reports it
sim::match_runtime::ShotEvent
gun_shot(Fixture& f, uint16_t source, uint16_t target, const FixedVec3& aim) {
    sim::match_runtime::ShotEvent shot{};
    shot.start = f.match->state().units[source].position;
    shot.target = aim;
    shot.weapon_id = f.match->state().game.weapon_defs[gun_registry_index].weapon_id;
    shot.target_unit = target;
    shot.source_unit = source;
    shot.slot = 0;
    return shot;
}

/// A shot launched from another machine's report is reported with the
/// reported aim and target unit, also when its record keeps an earlier shot's
/// point, and exactly once.
void applied_shot_reports_its_aim() {
    Fixture f;
    Recorder recorder;
    f.match->event_hooks = recorder.hooks();
    const auto shooter = f.spawn(1, 48, 48).unit_index;
    const auto aimed = f.spawn(0, 176, 64).unit_index;
    f.match->simulation().players[1].status = OA_PLAYER_STATUS_MIRRORED;
    auto& world = f.match->state();
    CHECK(oa::world_weapon_def(&world, oa_ref_from_index(gun_registry_index)) != nullptr);

    // Ballistic: the constructor leaves the record's target as it found it.
    auto& gun = world.game.weapon_defs[gun_registry_index];
    const auto line_flags = gun.flags;
    gun.flags |= OA_WEAPON_FLAG_BALLISTIC;
    const FixedVec3 earlier{fx(7), fx(8), fx(9)};
    world.projectiles[world.game.projectile_count].target = earlier;
    const FixedVec3 aim{fx(170), fx(32), fx(60)};
    const auto count = world.game.projectile_count;
    f.match->apply_shot(gun_shot(f, shooter, aimed, aim));
    CHECK(world.game.projectile_count == count + 1);
    CHECK(same_point(world.projectiles[count].target, earlier));
    CHECK(recorder.shots.size() == 1);
    const auto& ballistic = recorder.shots.front();
    CHECK(
        ballistic.source == ShotSource::weapon && ballistic.aim && same_point(*ballistic.aim, aim)
    );
    CHECK(ballistic.target_unit == aimed);
    CHECK(ballistic.origin_unit == oa::oa_unit_ref_from_slot(shooter));

    // A line shot keeps its aim in the record too.
    gun.flags = line_flags;
    const FixedVec3 second_aim{fx(160), fx(32), fx(70)};
    f.match->apply_shot(gun_shot(f, shooter, aimed, second_aim));
    CHECK(recorder.shots.size() == 2);
    const auto& line = recorder.shots.back();
    CHECK(
        line.aim && same_point(*line.aim, second_aim) && same_point(line.record_target, second_aim)
    );

    // A report the match cannot launch reports nothing.
    f.match->apply_shot(gun_shot(f, 0, aimed, aim));
    CHECK(recorder.shots.size() == 2);
    std::cout << "applied shot reports its aim passed\n";
}

/// Meteors are reported without an aim, burst shots with the spawner's aim
/// unless a mirrored player's ballistic spawner lost it.
void meteors_and_bursts_are_reported() {
    Fixture f;
    Recorder recorder;
    f.match->event_hooks = recorder.hooks();
    auto& world = f.match->state();
    const auto gun = oa_ref_from_index(gun_registry_index);
    const FixedVec3 sky{fx(100), fx(400), fx(100)};
    const FixedVec3 fall{0, fx(-4), 0};
    CHECK(f.match->launch_meteor(gun, sky, fall, false));
    CHECK(recorder.shots.size() == 1);
    CHECK(
        recorder.shots.front().source == ShotSource::meteor && !recorder.shots.front().aim &&
        recorder.shots.front().target_unit == 0 && recorder.shots.front().def == gun
    );
    sim::weapon_execution::retire_projectile(world, world.projectiles[0]);
    sim::weapon_execution::compact_projectiles(world);

    // A spawner with two burst shots left, aimed at a point, owned by player
    // 1; each tick it copies itself as one burst shot.
    const auto spawner = [&](const FixedVec3& aim) -> Projectile& {
        auto* shot = sim::weapon_execution::allocate_projectile(world);
        CHECK(shot != nullptr);
        const FixedVec3 at{fx(128), fx(60), fx(128)};
        sim::weapon_execution::init_projectile_record(
            world, *shot, gun, at, &aim, world.game.tick, nullptr, 0
        );
        shot->owner_index = 1;
        shot->burst_remaining = 2;
        shot->lifetime_tick = world.game.tick + 200;
        return *shot;
    };
    const FixedVec3 aim{fx(20), fx(0), fx(30)};
    (void)spawner(aim);
    f.run(1);
    CHECK(recorder.shots_from(ShotSource::burst) == 1);
    CHECK(recorder.shots.back().aim && same_point(*recorder.shots.back().aim, aim));

    // The same spawner of a mirrored player's ballistic gun reports no aim.
    recorder.shots.clear();
    for (int32_t i = 0; i < world.game.projectile_count; ++i)
        sim::weapon_execution::retire_projectile(world, world.projectiles[i]);
    sim::weapon_execution::compact_projectiles(world);
    world.game.weapon_defs[gun_registry_index].flags |= OA_WEAPON_FLAG_BALLISTIC;
    f.match->simulation().players[1].status = OA_PLAYER_STATUS_MIRRORED;
    const auto* owner = oa::world_player(&world, 1);
    CHECK(owner != nullptr && owner->in_use != 0 && owner->status == OA_PLAYER_STATUS_MIRRORED);
    (void)spawner(aim);
    f.run(1);
    CHECK(recorder.shots_from(ShotSource::burst) == 1);
    CHECK(!recorder.shots.back().aim);
    std::cout << "meteors and bursts are reported passed\n";
}

// The start of a player: its commander is the fixture's type.
struct Start : sim::unit_spawn::StartHost {
    uint16_t commander_type_for_side(uint8_t) override { return 1; }

    void report_missing_start_position(int32_t) override {
        throw std::runtime_error("unexpected missing start position");
    }

    void set_camera_position(int32_t, int32_t, uint32_t) override {}
};

/// Units created and finished are reported: a start commander, a building
/// created finished (by itself), and a frame its builder's link finishes,
/// once, even when a state record marked it finished first.
void creations_and_completions_are_reported() {
    Fixture f;
    Recorder recorder;
    f.match->event_hooks = recorder.hooks();
    auto& world = f.match->state();

    const std::array<sim::unit_spawn::StartMarker, 1> markers{{{1, 0, 96, 96}}};
    Start start;
    const auto started = f.match->start_player(0, {1, 0, 0, 100}, markers, 0, 256, 128, start);
    CHECK(started.unit != nullptr);
    const auto commander = static_cast<uint16_t>(oa::world_unit_slot(&world, started.unit));
    CHECK(recorder.created.size() == 1 && recorder.created.front().unit == commander);
    CHECK(recorder.finished.empty());

    // A frame, then its builder's link: finished once, by the builder.
    auto* frame = f.match->create({0, 1, {fx(48), fx(32), fx(48)}, false, 1, 0});
    CHECK(frame != nullptr && frame->record.build_remaining != 0.0F);
    CHECK(recorder.created.size() == 2 && recorder.created.back().unit == frame->unit_index);
    CHECK(recorder.finished.empty());
    f.match->finish_unit(frame->unit_index, commander);
    CHECK(recorder.finished.size() == 1);
    CHECK(
        recorder.finished.front().unit == frame->unit_index &&
        recorder.finished.front().builder == commander && recorder.finished.front().finished_at_call
    );
    f.match->finish_unit(frame->unit_index, commander);
    CHECK(recorder.finished.size() == 1);

    // A frame a state record from elsewhere already marked finished is still
    // reported when its builder's link arrives.
    auto* copied = f.match->create({0, 1, {fx(80), fx(32), fx(48)}, false, 1, 0});
    CHECK(copied != nullptr);
    copied->record.build_remaining = 0.0F;
    f.match->finish_unit(copied->unit_index, commander);
    CHECK(recorder.finished.size() == 2 && recorder.finished.back().unit == copied->unit_index);

    // A building (bmcode 0) created finished names itself as its builder,
    // after its creation, and a link from elsewhere does not finish it again.
    world.unit_defs[1].bm_code = 0;
    auto* building = f.match->create({1, 1, {fx(176), fx(32), fx(176)}, true, 1, 0});
    world.unit_defs[1].bm_code = 1;
    CHECK(building != nullptr);
    CHECK(recorder.created.size() == 4 && recorder.created.back().unit == building->unit_index);
    CHECK(recorder.finished.size() == 3);
    CHECK(
        recorder.finished.back().unit == building->unit_index &&
        recorder.finished.back().builder == building->unit_index
    );
    f.match->finish_unit(building->unit_index, building->unit_index);
    CHECK(recorder.finished.size() == 3);
    std::cout << "creations and completions are reported passed\n";
}

/// A death settled elsewhere and a paralysis are reported as such.
void settled_deaths_and_other_kinds_are_reported() {
    Fixture f;
    Recorder recorder;
    f.match->event_hooks = recorder.hooks();
    auto& victim = f.spawn(1, 176, 64);
    auto& killer = f.spawn(0, 48, 48);
    f.match->apply_damage_event(victim, &killer, 5, paralyzer_hit, 0);
    CHECK(recorder.damage.size() == 1);
    CHECK(
        recorder.damage.front().target == victim.unit_index &&
        recorder.damage.front().source == killer.unit_index &&
        recorder.damage.front().amount == 5 && recorder.damage.front().kind == paralyzer_hit
    );
    f.match->simulation().players[1].status = OA_PLAYER_STATUS_MIRRORED;
    const KillOutcome outcome{sim::match_runtime::DeathKind::weapon, 50, 1};
    f.match->apply_kill(victim.unit_index, outcome, killer.unit_index, 0);
    CHECK(recorder.deaths.size() == 1);
    const auto& death = recorder.deaths.front();
    CHECK(
        death.unit == victim.unit_index && death.owner == 1 && death.type_index == 1 &&
        death.live_at_call && death.settled_elsewhere
    );
    // A dead unit dies once and takes no damage.
    f.match->apply_kill(victim.unit_index, outcome, killer.unit_index, 0);
    f.match->apply_damage_event(victim, &killer, 5, weapon_kind, 0);
    CHECK(recorder.deaths.size() == 1 && recorder.damage.size() == 1);
    std::cout << "settled deaths and other kinds are reported passed\n";
}

} // namespace

int main() {
    try {
        hooks_change_nothing();
        skirmish_reports_every_kind();
        applied_shot_reports_its_aim();
        meteors_and_bursts_are_reported();
        creations_and_completions_are_reported();
        settled_deaths_and_other_kinds_are_reported();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    return 0;
}
