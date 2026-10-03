// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The economy and wind rules a mod's profile may set, in a running match:
// computer players' income multipliers at every production credit and at
// feature reclaim, shared income left out of the produced totals, cloaking
// only once a unit is built, and wind drawn from the shared generator. Each
// rule at its 3.1c baseline plays exactly as 3.1c does.
#include "combat_fixture.hpp"

#include "oa/sim/match_runtime/rule_state.hpp"
#include "oa/sim/unit_activation.hpp"
#include "oa/sim/unit_health.hpp"
#include "oa/sim/world_environment/wind.hpp"

#include <array>
#include <bit>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <iostream>
#include <span>
#include <stdexcept>
#include <vector>

namespace {

using namespace combat_fixture;
namespace match_rules = oa::data::match_rules;
namespace runtime = oa::sim::match_runtime;
namespace environment = oa::sim::world_environment;
namespace features = oa::sim::feature_runtime;
using Scales = oa::sim::unit_health::ComputerIncomeScales;

constexpr uint8_t computer = OA_PLAYER_STATUS_COMPUTER;
constexpr uint8_t human = OA_PLAYER_STATUS_LOCAL;
constexpr uint32_t active_state = 1; // Unit.state_flags: the unit is switched on

// Where a production credit comes from in the economy tick.
enum class Site : uint8_t {
    mobile_negative_use,   // a mobile unit with negative EnergyUse
    building_negative_use, // a switched-on building with negative EnergyUse
    extractor,             // a powered metal extractor
    metal_maker,           // a powered metal maker
    wind,                  // a wind generator
    tidal,                 // a tidal generator
    make,                  // a finished unit's EnergyMake and MetalMake
};

constexpr std::array sites{
    Site::mobile_negative_use,
    Site::building_negative_use,
    Site::extractor,
    Site::metal_maker,
    Site::wind,
    Site::tidal,
    Site::make,
};

// What one economy tick credited a player.
struct Credited {
    float energy{};
    float metal{};
};

// The 3.1c income at each site before scaling: energy, then metal.
Credited site_amounts(Site site) {
    switch (site) {
    case Site::mobile_negative_use:
    case Site::building_negative_use:
        return {20.0F, 0.0F};
    case Site::extractor:
        return {0.0F, 30.0F};
    case Site::metal_maker:
        return {0.0F, 4.0F};
    case Site::wind:
        return {4.0F * (1500.0F / 5000.0F), 0.0F};
    case Site::tidal:
        return {5.0F * 0.5F, 0.0F};
    case Site::make:
        return {600.0F, 3.0F};
    }
    return {};
}

/// Runs one economy tick of player 1 with one unit producing at a site.
///
/// @param rules the match's rules
/// @param status player 1's Player.status
/// @param difficulty Game.difficulty
/// @param site where the unit's income comes from
/// @return what the tick credited player 1
Credited
site_income(const match_rules::MatchRules& rules, uint8_t status, int32_t difficulty, Site site) {
    Options options;
    options.rules = rules;
    // A pinned wind: strength 1500 of 5000, normalized 0.3.
    options.minimum_wind = options.maximum_wind = 1500;
    Fixture f(options);
    f.match->simulation().tick = 1;
    f.match->refresh_wind();
    f.match->set_difficulty(difficulty);
    f.match->simulation().players[1].status = status;
    f.match->state().game.tidal_strength = 0.5F;
    auto& unit = f.spawn(1, 64, 64).record;
    auto& def = f.match->state().unit_defs[1];
    unit.flags &= ~OA_UNIT_FLAG_BUILDING;
    unit.state_flags |= active_state;
    unit.build_remaining = 1.0F;
    switch (site) {
    case Site::mobile_negative_use:
        def.energy_use = -20.0F;
        break;
    case Site::building_negative_use:
        unit.flags |= OA_UNIT_FLAG_BUILDING;
        def.energy_use = -20.0F;
        break;
    case Site::extractor:
        unit.flags |= OA_UNIT_FLAG_BUILDING;
        def.extracts_metal = 3.0F;
        unit.extracted_metal = 30.0F;
        break;
    case Site::metal_maker:
        unit.flags |= OA_UNIT_FLAG_BUILDING;
        def.energy_use = 10.0F;
        def.makes_metal = 4;
        break;
    case Site::wind:
        unit.flags |= OA_UNIT_FLAG_BUILDING;
        def.wind_generator = 4.0F;
        break;
    case Site::tidal:
        unit.flags |= OA_UNIT_FLAG_BUILDING;
        def.tidal_generator = 5.0F;
        break;
    case Site::make:
        unit.state_flags &= ~active_state;
        unit.build_remaining = 0.0F;
        def.energy_make = 600.0F;
        def.metal_make = 3.0F;
        break;
    }
    f.match->update_player_economy(1);
    const auto& player = f.match->state().game.players[1];
    return {player.energy_produced, player.metal_produced};
}

/// Tells whether two floats hold the same bits.
///
/// @param left a float
/// @param right a float
/// @return true when every bit agrees
bool same_bits(float left, float right) {
    return std::bit_cast<uint32_t>(left) == std::bit_cast<uint32_t>(right);
}

/// Returns an amount scaled as a computer player's credit is.
///
/// @param amount the credit before scaling
/// @param scale the multiplier
/// @return the amount widened, multiplied as a double and rounded to float
float scaled(float amount, double scale) {
    return static_cast<float>(static_cast<double>(amount) * scale);
}

/// The income multipliers at their baseline credit exactly what 3.1c does at
/// every production site, for computer and human players at every difficulty.
void production_baseline_plays_3_1c() {
    match_rules::MatchRules baseline{};
    baseline.ai.income_multipliers.enabled = true;
    for (const auto site : sites)
        for (const uint8_t status : {human, computer})
            for (int32_t difficulty = 0; difficulty <= 3; ++difficulty) {
                const auto classic = site_income({}, status, difficulty, site);
                const auto profiled = site_income(baseline, status, difficulty, site);
                CHECK(same_bits(classic.energy, profiled.energy));
                CHECK(same_bits(classic.metal, profiled.metal));
            }
    // 3.1c's own scaling: easy 0.5, medium 0.7, hard unscaled.
    const auto medium = site_income({}, computer, 1, Site::make);
    CHECK(medium.energy == scaled(600.0F, 0.7) && medium.metal == scaled(3.0F, 0.7));
    const auto hard = site_income({}, computer, 2, Site::make);
    CHECK(hard.energy == 600.0F && hard.metal == 3.0F);
    std::cout << "production baseline plays 3.1c passed\n";
}

/// The production multipliers scale a computer player's credit at every
/// production site by its difficulty's entry, hard included; a human player
/// and feature reclaim's multipliers do not enter.
void production_multipliers_scale_every_site() {
    const std::array<Scales, 2> lists{Scales{4.0, 2.0, 1.0}, Scales{0.25, 3.0, 8.0}};
    for (const auto& list : lists) {
        match_rules::MatchRules rules{};
        rules.ai.income_multipliers.enabled = true;
        rules.ai.income_multipliers.production = list;
        rules.ai.income_multipliers.reclaim = {9.0, 9.0, 9.0};
        for (const auto site : sites) {
            const auto amounts = site_amounts(site);
            for (int32_t difficulty = 0; difficulty <= 3; ++difficulty) {
                const auto scale = list[oa::sim::unit_health::computer_income_index(difficulty)];
                const auto income = site_income(rules, computer, difficulty, site);
                CHECK(same_bits(income.energy, scaled(amounts.energy, scale)));
                CHECK(same_bits(income.metal, scaled(amounts.metal, scale)));
                const auto own = site_income(rules, human, difficulty, site);
                CHECK(same_bits(own.energy, amounts.energy));
                CHECK(same_bits(own.metal, amounts.metal));
            }
        }
    }
    std::cout << "production multipliers scale every site passed\n";
}

// Sequence refs of the reclaim fixture's feature.
constexpr oa_ref32 standing = 1, reclaiming = 2;

/// Answers the reclaim fixture's sequence frames: one standing frame and a
/// four-frame reclamate sequence.
///
/// @param sequence sequence ref
/// @param frame frame index
/// @param[out] out the frame
/// @return false past the last frame or for another sequence
bool heap_frames(oa_ref32 sequence, uint16_t frame, features::FeatureSequenceFrame& out) {
    if (sequence != standing && sequence != reclaiming)
        return false;
    out.frame_count = sequence == standing ? 1 : 4;
    out.repeat = sequence == standing ? 1 : 0;
    if (frame >= out.frame_count)
        return false;
    out.duration = 3;
    out.width = 16;
    out.height = 32;
    out.origin_x = 8;
    out.origin_y = 30;
    return true;
}

/// Reclaims a heap of 250 energy and 30 metal with a unit of player 1.
///
/// @param rules the match's rules
/// @param status player 1's Player.status
/// @param difficulty Game.difficulty
/// @return what the reclaim credited the unit
Credited reclaim_heap(const match_rules::MatchRules& rules, uint8_t status, int32_t difficulty) {
    Options options;
    options.rules = rules;
    FeatureDef heap{};
    heap.footprint_x = heap.footprint_z = 1;
    heap.flags = OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_RECLAIMABLE | OA_FEATURE_FLAG_BLOCKING;
    heap.seq_name = standing;
    heap.seq_name_reclamate = reclaiming;
    heap.dead_feature = heap.burnt_feature = heap.reclamate_feature = features::no_feature;
    heap.energy = 250.0F;
    heap.metal = 30.0F;
    std::snprintf(heap.name, sizeof heap.name, "%s", "Heap");
    options.features = {heap};
    options.feature_words = {{std::size_t{4 * 16 + 4}, uint16_t{0}}};
    options.feature_sequence_frame = heap_frames;
    Fixture f(options);
    f.match->set_difficulty(difficulty);
    f.match->simulation().players[1].status = status;
    auto& unit = f.spawn(1, 64, 96).record;
    CHECK(f.match->reclaim_feature(unit, {(4 * 16 + 8) << 16, 0, (4 * 16 + 8) << 16}));
    return {unit.economy.energy.produced, unit.economy.metal.produced};
}

/// The reclaim multipliers scale what a computer player's unit reclaims from
/// a feature, energy and metal; the production multipliers do not enter, and
/// at their baseline reclaim plays as 3.1c does.
void reclaim_multipliers_scale_feature_reclaim() {
    match_rules::MatchRules baseline{};
    baseline.ai.income_multipliers.enabled = true;
    match_rules::MatchRules rules{};
    rules.ai.income_multipliers.enabled = true;
    rules.ai.income_multipliers.production = {7.0, 7.0, 7.0};
    rules.ai.income_multipliers.reclaim = {4.0, 2.0, 1.5};
    for (int32_t difficulty = 0; difficulty <= 3; ++difficulty) {
        for (const uint8_t status : {human, computer}) {
            const auto classic = reclaim_heap({}, status, difficulty);
            const auto profiled = reclaim_heap(baseline, status, difficulty);
            CHECK(same_bits(classic.energy, profiled.energy));
            CHECK(same_bits(classic.metal, profiled.metal));
        }
        const auto scale = rules.ai.income_multipliers
                               .reclaim[oa::sim::unit_health::computer_income_index(difficulty)];
        const auto income = reclaim_heap(rules, computer, difficulty);
        CHECK(income.energy == scaled(250.0F, scale) && income.metal == scaled(30.0F, scale));
        const auto own = reclaim_heap(rules, human, difficulty);
        CHECK(own.energy == 250.0F && own.metal == 30.0F);
    }
    const auto easy = reclaim_heap({}, computer, 0);
    CHECK(easy.energy == 125.0F && easy.metal == 15.0F);
    std::cout << "reclaim multipliers scale feature reclaim passed\n";
}

// The produced totals and stores of player 0 after an economy tick in which
// it made 7 energy and 3 metal and received 50 energy and 100 metal from
// allies.
struct SharedTick {
    double energy_total{};
    double metal_total{};
    float energy_produced{};
    float metal_produced{};
    float energy{};
    float metal{};
};

/// Runs one economy tick of player 0 with shared income in its staging block.
///
/// @param exclude whether the shared-income rule is on
/// @param shared_energy energy received from allies this tick
/// @param shared_metal metal received from allies this tick
/// @return the totals and stores after the tick
SharedTick shared_tick(bool exclude, float shared_energy, float shared_metal) {
    Options options;
    options.rules.economy.stats_exclude_shared_income.enabled = exclude;
    Fixture f(options);
    auto& world = f.match->state();
    auto& player = world.game.players[0];
    auto* staging = oa::world_player_economy(&world, &player);
    CHECK(staging != nullptr);
    auto& def = world.unit_defs[1];
    def.energy_make = 7.0F;
    def.metal_make = 3.0F;
    (void)f.spawn(0, 64, 64);
    player.energy = 0.0F;
    player.metal = 0.0F;
    player.energy_produced_total = 0.1;
    player.metal_produced_total = 1e9 + 0.3;
    // An ally's share credits the staging block, as the share path does.
    staging->energy.produced = shared_energy;
    staging->metal.produced = shared_metal;
    f.match->update_player_economy(0);
    // The staging block rolled its receipts over as income of the tick.
    CHECK(staging->energy.produced == 0.0F && staging->metal.produced == 0.0F);
    return {
        player.energy_produced_total,
        player.metal_produced_total,
        player.energy_produced,
        player.metal_produced,
        player.energy,
        player.metal
    };
}

/// Shared income stays income but leaves the produced totals, subtracted at
/// double precision from what 3.1c accumulates; without receipts the rule
/// changes nothing.
void shared_income_leaves_the_produced_totals() {
    const auto classic = shared_tick(false, 50.0F, 100.0F);
    const auto excluded = shared_tick(true, 50.0F, 100.0F);
    // 3.1c: the tick's produced amount, receipts included, is added in float.
    CHECK(classic.energy_total == static_cast<double>(57.0F + static_cast<float>(0.1)));
    CHECK(classic.metal_total == static_cast<double>(103.0F + static_cast<float>(1e9 + 0.3)));
    CHECK(excluded.energy_total == classic.energy_total - 50.0);
    CHECK(excluded.metal_total == classic.metal_total - 100.0);
    // Income and stores keep the receipts.
    CHECK(excluded.energy_produced == 57.0F && excluded.metal_produced == 103.0F);
    CHECK(classic.energy_produced == 57.0F && classic.metal_produced == 103.0F);
    CHECK(excluded.energy == classic.energy && excluded.metal == classic.metal);
    CHECK(excluded.energy == 57.0F && excluded.metal == 103.0F);
    // No receipts: the rule leaves the totals exactly as 3.1c does.
    const auto quiet = shared_tick(false, 0.0F, 0.0F);
    const auto quiet_excluded = shared_tick(true, 0.0F, 0.0F);
    CHECK(
        std::bit_cast<uint64_t>(quiet.energy_total) ==
        std::bit_cast<uint64_t>(quiet_excluded.energy_total)
    );
    CHECK(
        std::bit_cast<uint64_t>(quiet.metal_total) ==
        std::bit_cast<uint64_t>(quiet_excluded.metal_total)
    );
    std::cout << "shared income leaves the produced totals passed\n";
}

// Player 0's cloak after one economy tick.
struct CloakTick {
    bool cloaked{};
    float energy_requested{};
};

/// Runs one economy tick of player 0 with a cloaking unit that has some build
/// left.
///
/// @param after_build whether the cloak-after-build rule is on
/// @param build_remaining the unit's build fraction left
/// @return whether the unit cloaked and what player 0 requested
CloakTick cloak_tick(bool after_build, float build_remaining) {
    Options options;
    options.rules.units.init_cloaked_after_build.enabled = after_build;
    Fixture f(options);
    auto& world = f.match->state();
    world.unit_defs[1].cloak_cost = 10.0F;
    auto& slot = f.spawn(0, 64, 64);
    slot.record.flags |= OA_UNIT_FLAG_CLOAK_RUNNING;
    slot.record.build_remaining = build_remaining;
    auto& player = world.game.players[0];
    player.energy = 500.0F;
    f.match->update_player_economy(0);
    return {
        (slot.record.state_flags & oa::sim::unit_activation::cloaked_mask) != 0,
        player.energy_requested
    };
}

/// Under the cloak-after-build rule a unit with build left neither cloaks nor
/// pays; once built it cloaks as in 3.1c. Without the rule a nanoframe cloaks
/// and pays.
void cloak_waits_for_the_build() {
    const auto frame = cloak_tick(false, 0.5F);
    CHECK(frame.cloaked && frame.energy_requested == 10.0F);
    const auto held = cloak_tick(true, 0.5F);
    CHECK(!held.cloaked && held.energy_requested == 0.0F);
    const auto built = cloak_tick(true, 0.0F);
    CHECK(built.cloaked && built.energy_requested == 10.0F);
    const auto classic_built = cloak_tick(false, 0.0F);
    CHECK(classic_built.cloaked && classic_built.energy_requested == 10.0F);
    // The rule tests the build fraction's bits, so a negative zero still
    // counts as build left.
    const auto negative_zero = cloak_tick(true, -0.0F);
    CHECK(!negative_zero.cloaked && negative_zero.energy_requested == 0.0F);
    CHECK(cloak_tick(false, -0.0F).cloaked);
    std::cout << "cloak waits for the build passed\n";
}

// The wind fields of the game record.
struct Wind {
    uint32_t change_tick{};
    int32_t strength{};
    uint16_t direction{};
    float factor{};
    uint32_t changed{};
};

/// Reads the game record's wind fields.
///
/// @param f the fixture
/// @return the fields
Wind wind_of(Fixture& f) {
    const auto& game = f.match->state().game;
    return {
        game.wind_change_tick,
        game.wind_strength,
        game.wind_direction,
        game.wind_factor,
        game.wind_changed
    };
}

/// Returns options for a match with wind between 1000 and 3000.
///
/// @param rng the deterministic-wind rule's generator; null leaves the rule off
/// @return the options
Options wind_options(const match_rules::EconomyDeterministicWindRng* rng) {
    Options options;
    options.minimum_wind = 1000;
    options.maximum_wind = 3000;
    if (rng != nullptr) {
        options.rules.economy.deterministic_wind.enabled = true;
        options.rules.economy.deterministic_wind.rng = *rng;
    }
    return options;
}

/// Plays a match's wind against the shared scheduler run on its own, from a
/// seed, tick by tick.
///
/// @param f the fixture, at tick 0
/// @param seed the seed the match should use
/// @param ticks ticks to play
void wind_follows_the_shared_scheduler(Fixture& f, uint32_t seed, uint32_t ticks) {
    environment::WindState reference{};
    reference.minimum_strength = 1000;
    reference.maximum_strength = 3000;
    environment::WindGenerator generator{};
    CHECK(
        environment::initialize_shared_wind(reference, generator, seed) ==
        environment::WindRefresh::waiting
    );
    uint32_t changes = 0;
    for (uint32_t tick = 1; tick <= ticks; ++tick) {
        f.run(1);
        reference.current_tick = tick;
        if (environment::refresh_shared_wind(reference, generator, seed) ==
            environment::WindRefresh::changed)
            ++changes;
        const auto wind = wind_of(f);
        CHECK(wind.change_tick == reference.change_deadline);
        CHECK(wind.strength == reference.strength && wind.direction == reference.direction);
        CHECK(same_bits(wind.factor, reference.normalized_strength));
        CHECK(wind.changed == reference.changed);
    }
    CHECK(changes > 1);
}

/// Counts the rand() steps from one state of the match's local stream to
/// another.
///
/// @param from the earlier state
/// @param to the later state
/// @return the steps between them
uint32_t local_steps(uint32_t from, uint32_t to) {
    for (uint32_t steps = 0; steps < 1'000'000; ++steps) {
        if (from == to)
            return steps;
        from = from * 214013u + 2531011u;
    }
    throw std::runtime_error("the local stream did not reach the state");
}

/// Under the shared generator the wind follows the generator seeded with the
/// match's seed when no player is the host, and keeps its generator as rule
/// state.
void shared_wind_without_a_host() {
    const auto rng = match_rules::EconomyDeterministicWindRng::mt19937_host_id;
    Fixture f(wind_options(&rng));
    CHECK(f.match->fault() == nullptr);
    const auto* table = runtime::find_rule_state(f.match->rule_state(), "wind-generator");
    CHECK(
        table != nullptr &&
        table->bytes(table->context).size() == sizeof(environment::WindGenerator)
    );
    CHECK(f.match->fold_rule_state(0x1234) != 0x1234);
    // The fixture's match seed is 1.
    wind_follows_the_shared_scheduler(f, 1, 1500);
    std::cout << "shared wind without a host passed\n";
}

/// The shared generator's wind takes no draw from the match's own streams:
/// 3.1c's wind takes one rand() step per change, and its strength and
/// direction draws make the shared stream depend on the map's wind limits.
void shared_wind_leaves_the_match_streams() {
    const auto rng = match_rules::EconomyDeterministicWindRng::mt19937_host_id;
    Fixture classic(wind_options(nullptr));
    Fixture shared(wind_options(&rng));
    auto calm_options = wind_options(&rng);
    calm_options.minimum_wind = calm_options.maximum_wind = 0;
    Fixture calm(calm_options);
    auto classic_calm_options = wind_options(nullptr);
    classic_calm_options.minimum_wind = classic_calm_options.maximum_wind = 0;
    Fixture classic_calm(classic_calm_options);
    const auto start = classic.match->lcg_state();
    CHECK(shared.match->lcg_state() == start);
    uint32_t classic_changes = 0;
    for (uint32_t tick = 0; tick < 1500; ++tick) {
        classic.run(1);
        shared.run(1);
        calm.run(1);
        classic_calm.run(1);
        if (wind_of(classic).changed != 0)
            ++classic_changes;
    }
    CHECK(classic_changes > 1);
    CHECK(
        local_steps(start, classic.match->lcg_state()) ==
        local_steps(start, shared.match->lcg_state()) + classic_changes
    );
    CHECK(shared.match->random_state() == calm.match->random_state());
    CHECK(classic.match->random_state() != classic_calm.match->random_state());
    std::cout << "shared wind leaves the match streams passed\n";
}

/// The host's network id seeds the generator at the first change.
void shared_wind_seeded_by_the_host() {
    const auto rng = match_rules::EconomyDeterministicWindRng::mt19937_host_id;
    Fixture f(wind_options(&rng));
    auto& world = f.match->state();
    auto& host = world.game.players[1];
    host.machine_group = environment::host_machine_group;
    host.player_id = 0x5150;
    host.info = 2;
    world.player_info[1].state = environment::host_setup_state;
    wind_follows_the_shared_scheduler(f, 0x5150, 800);
    std::cout << "shared wind seeded by the host passed\n";
}

/// The generator's rule state restores from a save's bytes, and refuses
/// bytes that are not one generator in range.
void shared_wind_generator_restores() {
    const auto rng = match_rules::EconomyDeterministicWindRng::mt19937_host_id;
    Fixture played(wind_options(&rng));
    played.run(700);
    const auto* source = runtime::find_rule_state(played.match->rule_state(), "wind-generator");
    CHECK(source != nullptr);
    const auto bytes = source->bytes(source->context);
    std::vector<uint8_t> saved(bytes.begin(), bytes.end());
    Fixture loaded(wind_options(&rng));
    const auto* target = runtime::find_rule_state(loaded.match->rule_state(), "wind-generator");
    CHECK(target != nullptr);
    CHECK(target->restore(target->context, saved));
    const auto restored = target->bytes(target->context);
    CHECK(std::equal(restored.begin(), restored.end(), saved.begin(), saved.end()));
    CHECK(loaded.match->fold_rule_state(0) == played.match->fold_rule_state(0));
    CHECK(!target->restore(target->context, std::span(saved).first(saved.size() - 1)));
    auto out_of_range = saved;
    const uint32_t past = environment::wind_generator_words + 1;
    std::memcpy(out_of_range.data() + sizeof(uint32_t), &past, sizeof past);
    CHECK(!target->restore(target->context, out_of_range));
    CHECK(loaded.match->fold_rule_state(0) == played.match->fold_rule_state(0));
    std::cout << "shared wind generator restores passed\n";
}

/// The deterministic-wind rule at its baseline (the local streams) keeps no
/// rule state and plays 3.1c's wind and streams.
void local_wind_baseline_plays_3_1c() {
    const auto local = match_rules::EconomyDeterministicWindRng::local_random;
    Fixture classic(wind_options(nullptr));
    Fixture profiled(wind_options(&local));
    CHECK(profiled.match->rule_state().count == 0);
    CHECK(profiled.match->fold_rule_state(0x1234) == 0x1234);
    for (uint32_t tick = 0; tick < 1500; ++tick) {
        classic.run(1);
        profiled.run(1);
        const auto a = wind_of(classic);
        const auto b = wind_of(profiled);
        CHECK(a.change_tick == b.change_tick && a.strength == b.strength);
        CHECK(a.direction == b.direction && same_bits(a.factor, b.factor));
        CHECK(a.changed == b.changed);
        CHECK(classic.match->lcg_state() == profiled.match->lcg_state());
        CHECK(classic.match->random_state() == profiled.match->random_state());
    }
    std::cout << "local wind baseline plays 3.1c passed\n";
}

} // namespace

int main() {
    try {
        production_baseline_plays_3_1c();
        production_multipliers_scale_every_site();
        reclaim_multipliers_scale_feature_reclaim();
        shared_income_leaves_the_produced_totals();
        cloak_waits_for_the_build();
        shared_wind_without_a_host();
        shared_wind_leaves_the_match_streams();
        shared_wind_seeded_by_the_host();
        shared_wind_generator_restores();
        local_wind_baseline_plays_3_1c();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "economy rules tests passed\n";
    return 0;
}
