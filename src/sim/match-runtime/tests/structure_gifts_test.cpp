// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The structure gift rate limit (sharing.structure-gift-rate-limit): its
// window, its waiting batches and their encoding, and a match that holds
// back the structures a player gives through the share panel.
#include "combat_fixture.hpp"

#include "oa/sim/match_runtime/rule_state.hpp"
#include "oa/sim/match_runtime/structure_gifts.hpp"

#include <array>
#include <cstdint>
#include <iostream>
#include <span>
#include <vector>

namespace {

using namespace combat_fixture;
namespace runtime = oa::sim::match_runtime;

/// The rules with the gift rate limit on at its usual values.
///
/// @return the rules
oa::data::match_rules::MatchRules gift_rules() {
    oa::data::match_rules::MatchRules rules{};
    rules.sharing.structure_gift_rate_limit.enabled = true;
    return rules;
}

void window_and_batches() {
    runtime::StructureGiftState state{};
    runtime::note_structure_gift(state, 100, 4);
    runtime::note_structure_gift(state, 150, 0); // nothing went: nothing remembered
    runtime::note_structure_gift(state, 200, 3);
    CHECK(state.run_count == 2 && runtime::recent_structure_gifts(state) == 7);
    // A gift leaves the window once the window's length has passed.
    runtime::prune_structure_gifts(state, 999, 900);
    CHECK(runtime::recent_structure_gifts(state) == 7);
    runtime::prune_structure_gifts(state, 1000, 900);
    CHECK(state.run_count == 1 && runtime::recent_structure_gifts(state) == 3);
    runtime::prune_structure_gifts(state, 1100, 900);
    CHECK(state.run_count == 0);

    // A full memory folds its oldest gift into the next.
    for (uint32_t i = 0; i < runtime::structure_gift_run_capacity + 1; ++i)
        runtime::note_structure_gift(state, i, 1);
    CHECK(state.run_count == runtime::structure_gift_run_capacity);
    CHECK(runtime::recent_structure_gifts(state) == runtime::structure_gift_run_capacity + 1);
    CHECK(state.runs[0].tick == 1 && state.runs[0].count == 2);
    state = {};

    // Batches wait until their due tick, earliest first.
    const std::array<runtime::StructureGift, 2> first{{{5, 1, 0, 1}, {6, 1, 0, 1}}};
    const std::array<runtime::StructureGift, 1> second{{{7, 2, 0, 2}}};
    CHECK(runtime::defer_structure_gifts(state, 900, first) == 2);
    CHECK(runtime::defer_structure_gifts(state, 950, second) == 1);
    std::array<runtime::StructureGift, runtime::structure_gift_capacity> taken{};
    CHECK(runtime::take_due_structure_gifts(state, 899, taken) == 0);
    CHECK(runtime::take_due_structure_gifts(state, 900, taken) == 2);
    CHECK(taken[0].unit == 5 && taken[1].unit == 6 && taken[1].recipient == 1);
    CHECK(runtime::take_due_structure_gifts(state, 900, taken) == 0);

    // The encoding keeps the window and the waiting batches, and refuses a
    // state that does not hold together.
    runtime::note_structure_gift(state, 40, 9);
    const auto bytes = runtime::encode_structure_gifts(state);
    CHECK(bytes.size() == 8 + 8 + 8 + 6);
    const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
    runtime::StructureGiftState read{};
    CHECK(runtime::decode_structure_gifts(read, saved));
    CHECK(read.run_count == 1 && read.runs[0].tick == 40 && read.runs[0].count == 9);
    CHECK(read.batch_count == 1 && read.batches[0].due_tick == 950 && read.batches[0].count == 1);
    CHECK(read.waiting_count == 1 && read.waiting[0].unit == 7 && read.waiting[0].type == 2);
    CHECK(read.waiting[0].recipient == 2);
    std::vector<uint8_t> short_bytes(saved.begin(), saved.end() - 1);
    CHECK(!runtime::decode_structure_gifts(read, short_bytes));
    std::vector<uint8_t> wrong_count = saved;
    wrong_count[8 + 8 + 4] = 2; // the batch claims two structures
    CHECK(!runtime::decode_structure_gifts(read, wrong_count));
    CHECK(read.run_count == 1 && read.waiting_count == 1);
    const std::array<uint8_t, 8> empty{};
    CHECK(runtime::decode_structure_gifts(read, empty));
    CHECK(read.run_count == 0 && read.batch_count == 0 && read.waiting_count == 0);
    std::cout << "window and batches passed\n";
}

/// A match whose type is a structure, with both players' slots in use.
struct GiftMatch {
    Fixture f;

    explicit GiftMatch(const oa::data::match_rules::MatchRules& rules)
        : f([&] {
              Options options;
              options.rules = rules;
              options.per_player_limit = 24;
              return options;
          }()) {
        f.match->state().unit_defs[1].bm_code = 0;
        for (uint8_t player = 0; player < 2; ++player)
            f.match->state().game.players[player].in_use = 1;
    }

    /// Creates a structure of player 0.
    ///
    /// @param x whole world units
    /// @return its slot
    uint16_t structure(uint32_t x) { return f.spawn(0, x, 40).unit_index; }

    /// Gives units to player 1 through the share panel.
    ///
    /// @param units their slots
    /// @return what became of the structures
    runtime::ShareGiftOutcome give(std::span<const uint16_t> units) {
        f.match->begin_share_gift();
        for (const auto unit : units)
            f.match->share_gift_unit(unit, 1);
        return f.match->end_share_gift();
    }

    /// Tells whether a unit is still player 0's live unit.
    ///
    /// @param unit its slot
    /// @return true while it stands for player 0
    bool kept(uint16_t unit) {
        const auto& record = f.match->state().units[unit];
        return (record.flags & OA_UNIT_FLAG_LIVE) != 0 &&
               (record.flags & OA_UNIT_FLAG_DEATH_PENDING) == 0 && record.owner_index == 0;
    }

    /// Counts player 1's live units.
    ///
    /// @return the count
    uint32_t received() {
        uint32_t count = 0;
        const auto& world = f.match->state();
        for (uint32_t slot = 0; slot < world.unit_slot_count; ++slot) {
            const auto& unit = world.units[slot];
            if ((unit.flags & OA_UNIT_FLAG_LIVE) != 0 && unit.owner_index == 1)
                ++count;
        }
        return count;
    }
};

void without_the_rule() {
    GiftMatch m(oa::data::match_rules::MatchRules{});
    CHECK(m.f.match->rule_state().count == 0);
    std::vector<uint16_t> units;
    for (uint32_t i = 0; i < 12; ++i)
        units.push_back(m.structure(20 + 16 * (i % 12)));
    const auto outcome = m.give(units);
    CHECK(outcome.given == 0 && outcome.waiting == 0);
    // Every structure went across at once.
    for (const auto unit : units)
        CHECK(!m.kept(unit));
    CHECK(m.received() == 12);
    std::cout << "without the rule passed\n";
}

void structures_wait() {
    GiftMatch m(gift_rules());
    CHECK(m.f.match->rule_state().count == 1);
    CHECK(runtime::find_rule_state(m.f.match->rule_state(), runtime::structure_gifts_table_name));
    std::vector<uint16_t> first;
    for (uint32_t i = 0; i < 10; ++i)
        first.push_back(m.structure(20 + 20 * i));
    // Ten structures at once: within the limit, they go across.
    auto outcome = m.give(first);
    CHECK(outcome.given == 10 && outcome.waiting == 0);
    CHECK(m.received() == 10);
    // Two more while those ten count: they wait 900 ticks (30 seconds).
    const std::array<uint16_t, 2> second{m.structure(30), m.structure(60)};
    m.f.run(5);
    const uint32_t gave_at = m.f.match->state().game.tick;
    outcome = m.give(second);
    CHECK(outcome.given == 0 && outcome.waiting == 2 && outcome.wait_seconds == 30);
    CHECK(m.kept(second[0]) && m.kept(second[1]));
    // A mobile unit is not held back.
    m.f.match->state().unit_defs[1].bm_code = 1;
    const std::array<uint16_t, 1> tank{m.structure(90)};
    outcome = m.give(tank);
    CHECK(outcome.given == 0 && outcome.waiting == 0 && !m.kept(tank[0]));
    m.f.match->state().unit_defs[1].bm_code = 0;
    // One that dies while it waits is left out.
    m.f.match->state().units[second[1]].flags |= OA_UNIT_FLAG_DEATH_PENDING;
    m.f.run(900 - 1);
    CHECK(m.f.match->state().game.tick == gave_at + 899);
    CHECK(m.kept(second[0]));
    m.f.run(1);
    CHECK(!m.kept(second[0]));
    CHECK(m.received() == 12);
    std::cout << "structures wait passed\n";
}

void each_parameter() {
    {
        // immediate-max 0: every structure gift waits.
        auto rules = gift_rules();
        rules.sharing.structure_gift_rate_limit.immediate_max = 0;
        GiftMatch m(rules);
        const std::array<uint16_t, 1> one{m.structure(20)};
        const auto outcome = m.give(one);
        CHECK(outcome.waiting == 1 && m.kept(one[0]));
    }
    {
        // defer-ticks 60: the batch waits two seconds.
        auto rules = gift_rules();
        rules.sharing.structure_gift_rate_limit.immediate_max = 1;
        rules.sharing.structure_gift_rate_limit.defer_ticks = 60;
        GiftMatch m(rules);
        const std::array<uint16_t, 2> two{m.structure(20), m.structure(50)};
        const auto outcome = m.give(two);
        CHECK(outcome.waiting == 2 && outcome.wait_seconds == 2);
        m.f.run(59);
        CHECK(m.kept(two[0]) && m.kept(two[1]));
        m.f.run(1);
        CHECK(!m.kept(two[0]) && !m.kept(two[1]));
    }
    {
        // window-ticks 30: a gift stops counting after 30 ticks; the waiting
        // batch, once given, counts in its turn.
        auto rules = gift_rules();
        rules.sharing.structure_gift_rate_limit.immediate_max = 2;
        rules.sharing.structure_gift_rate_limit.window_ticks = 30;
        rules.sharing.structure_gift_rate_limit.defer_ticks = 10;
        GiftMatch m(rules);
        const std::array<uint16_t, 2> two{m.structure(20), m.structure(50)};
        CHECK(m.give(two).given == 2);
        m.f.run(29);
        const std::array<uint16_t, 1> early{m.structure(80)};
        CHECK(m.give(early).waiting == 1);
        m.f.run(1);
        const std::array<uint16_t, 1> later{m.structure(110)};
        CHECK(m.give(later).given == 1);
        m.f.run(9);
        CHECK(!m.kept(early[0]));
        const std::array<uint16_t, 2> more{m.structure(140), m.structure(170)};
        CHECK(m.give(more).waiting == 2);
    }
    std::cout << "each parameter passed\n";
}

void saved_and_restored() {
    GiftMatch m(gift_rules());
    std::vector<uint16_t> units;
    for (uint32_t i = 0; i < 11; ++i)
        units.push_back(m.structure(20 + 18 * i));
    CHECK(m.give(units).waiting == 11);
    const auto* table =
        runtime::find_rule_state(m.f.match->rule_state(), runtime::structure_gifts_table_name);
    CHECK(table != nullptr);
    const auto bytes = table->bytes(table->context);
    const std::vector<uint8_t> saved(bytes.begin(), bytes.end());
    const uint64_t digest = m.f.match->fold_rule_state(1);
    // Another match restored from the bytes digests the same and gives the
    // batch when it is due.
    GiftMatch other(gift_rules());
    std::vector<uint16_t> same;
    for (uint32_t i = 0; i < 11; ++i)
        same.push_back(other.structure(20 + 18 * i));
    const auto* restored =
        runtime::find_rule_state(other.f.match->rule_state(), runtime::structure_gifts_table_name);
    CHECK(restored != nullptr && restored->restore(restored->context, saved));
    CHECK(other.f.match->fold_rule_state(1) == digest);
    other.f.run(900);
    for (const auto unit : same)
        CHECK(!other.kept(unit));
    CHECK(other.received() == 11);
    std::cout << "saved and restored passed\n";
}

} // namespace

int main() {
    try {
        window_and_batches();
        without_the_rule();
        structures_wait();
        each_parameter();
        saved_and_restored();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "structure gifts passed\n";
    return 0;
}
