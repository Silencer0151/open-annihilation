// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The intel hacks in a match: allied vision (intel.allied-los-sharing), which
// grants each player the sight, radar picture and unit visibility of every
// player who allies it, and allied jammers that do not jam
// (intel.allied-jammers-ignored), each against 3.1c's rules.
#include "combat_fixture.hpp"

#include "oa/sim/match_runtime/rule_state.hpp"

#include <array>
#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;
namespace runtime = oa::sim::match_runtime;

/// Contact bits the scan owns: radar, sonar and jammed.
constexpr uint32_t contact_bits =
    OA_UNIT_FLAG_RADAR_CONTACT | OA_UNIT_FLAG_VIEWPOINT_OWNED | OA_UNIT_FLAG_JAMMED;
/// Radar and sonar contact both, as the scan marks the units it is shown.
constexpr uint32_t shown = OA_UNIT_FLAG_RADAR_CONTACT | OA_UNIT_FLAG_VIEWPOINT_OWNED;
/// The table allied vision keeps its rule state in.
constexpr std::string_view allied_sight_table = "intel.allied-sight";

/// Returns the options of a match playing the intel hacks given.
///
/// @param allied_vision turns intel.allied-los-sharing on
/// @param allied_jammers turns intel.allied-jammers-ignored on
/// @param view_switch_branch its view-switch-branch parameter
/// @return the options
Options
intel_options(bool allied_vision, bool allied_jammers = false, bool view_switch_branch = false) {
    Options options;
    // Sight reaches the cells round a unit's own, so it covers the point the
    // scan tests, which height raises one row.
    options.sight_cells = 3;
    options.rules.intel.allied_los_sharing.enabled = allied_vision;
    options.rules.intel.allied_jammers_ignored.enabled = allied_jammers;
    options.rules.intel.allied_jammers_ignored.view_switch_branch = view_switch_branch;
    return options;
}

/// Seats a third player, allied only with itself.
///
/// @param f the fixture
void seat_third_player(Fixture& f) {
    f.match->simulation().players[2].present = true;
    f.match->simulation().players[2].status = OA_PLAYER_STATUS_LOCAL;
    std::array<uint8_t, 10> allies{};
    allies[2] = 1;
    f.match->configure_player_alliances(2, allies);
}

/// Makes one player ally another, one way.
///
/// @param f the fixture
/// @param from the player whose alliance row changes
/// @param to the player it allies
void ally(Fixture& f, uint8_t from, uint8_t to) {
    f.match->state().game.players[from].alliance[to] = 1;
}

/// Creates a unit that holds its fire and is switched off.
///
/// @param f the fixture
/// @param player owner
/// @param x whole world units
/// @param z whole world units
/// @return the unit's slot
sim::unit_spawn::Slot& place(Fixture& f, uint8_t player, uint32_t x, uint32_t z) {
    auto& slot = f.spawn(player, x, z);
    slot.record.flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
    slot.record.state_flags &= static_cast<uint8_t>(~OA_UNIT_STATE_ACTIVE);
    return slot;
}

/// Returns the sight cell a unit's stamp centred on.
///
/// @param f the fixture
/// @param slot the unit
/// @return the cell's index in the sight grid
std::size_t stamped_cell(const Fixture& f, const sim::unit_spawn::Slot& slot) {
    const auto x = static_cast<int16_t>(slot.record.sight_center_x);
    const auto z = static_cast<int16_t>(slot.record.sight_center_z);
    CHECK(x >= 0 && z >= 0 && x < f.match->sight().width && z < f.match->sight().height);
    return static_cast<std::size_t>(z) * static_cast<std::size_t>(f.match->sight().width) +
           static_cast<std::size_t>(x);
}

/// Returns a player's coverage count in a unit's stamped cell.
///
/// @param f the fixture
/// @param player the player whose coverage is read
/// @param slot the unit
/// @return the count
uint8_t coverage_at(const Fixture& f, uint8_t player, const sim::unit_spawn::Slot& slot) {
    return f.match->player_coverage(player)[stamped_cell(f, slot)];
}

/// Returns the scan's contact bits of a unit.
///
/// @param slot the unit
/// @return its radar, sonar and jammed bits
uint32_t contacts(const sim::unit_spawn::Slot& slot) {
    return slot.record.flags & contact_bits;
}

/// Gives the fixture's unit type a radar and a radar jammer, both reaching
/// the whole map.
///
/// @param f the fixture
void give_radar_and_jammer(Fixture& f) {
    auto& def = f.match->state().unit_defs[1];
    def.radar_distance = 400;
    def.radar_distance_jam = 400;
}

/// Switches a unit on, so its radar and jammer run.
///
/// @param slot the unit
void switch_on(sim::unit_spawn::Slot& slot) {
    slot.record.state_flags |= OA_UNIT_STATE_ACTIVE;
}

// An alliance grants the allied player the granter's sight, one way: the
// granter's units count in the grantee's coverage, not the other way round.
void allied_vision_shares_sight() {
    for (const bool on : {false, true}) {
        Fixture f(intel_options(on));
        ally(f, 1, 0);
        const auto& granted = place(f, 1, 200, 200);
        const auto& own = place(f, 0, 64, 64);
        CHECK(coverage_at(f, 1, granted) == 1);
        CHECK(coverage_at(f, 0, granted) == (on ? 1 : 0));
        CHECK(coverage_at(f, 0, own) == 1 && coverage_at(f, 1, own) == 0);
    }
    std::cout << "allied vision shares sight passed\n";
}

// The unit visibility test passes every unit whose owner allies the player,
// cloaked or out of sight; the player's own alliance row does not.
void allied_vision_sees_allied_units() {
    for (const bool on : {false, true}) {
        Fixture f(intel_options(on));
        auto& far = place(f, 1, 200, 200);
        (void)place(f, 0, 32, 32);
        far.record.state_flags |= OA_UNIT_STATE_CLOAKED;
        ally(f, 0, 1);
        CHECK(!f.match->unit_visible(0, far.unit_index));
        ally(f, 1, 0);
        CHECK(f.match->unit_visible(0, far.unit_index) == on);
        // The owner itself sees its unit through its own row.
        CHECK(f.match->unit_visible(1, far.unit_index));
    }
    std::cout << "allied vision sees allied units passed\n";
}

// A changed alliance row rebuilds the sight stamps at the next player
// deadline; the first look only notes the rows. A changed viewpoint does the
// same. Without the hack nothing is rebuilt.
void allied_vision_follows_alliances() {
    for (const bool on : {false, true}) {
        Fixture f(intel_options(on));
        const auto& granted = place(f, 1, 200, 200);
        (void)place(f, 0, 64, 64);
        f.run(1);
        CHECK(coverage_at(f, 0, granted) == 0);
        ally(f, 1, 0);
        // The players' next deadline is tick 30; a standing unit restamps nothing.
        f.run(28);
        CHECK(coverage_at(f, 0, granted) == 0);
        f.run(1);
        CHECK(coverage_at(f, 0, granted) == (on ? 1 : 0));
    }
    // A player another machine simulates keeps coverage here once viewed.
    Fixture f(intel_options(true));
    f.match->state().game.players[1].status = OA_PLAYER_STATUS_MIRRORED;
    const auto& remote = place(f, 1, 200, 200);
    f.run(1);
    CHECK(coverage_at(f, 1, remote) == 0);
    f.match->state().game.viewpoint_player = 1;
    f.run(30);
    CHECK(coverage_at(f, 1, remote) == 1);
    std::cout << "allied vision follows alliances passed\n";
}

// Allied vision keeps the rows it followed as rule state, only while on.
void allied_vision_rule_state() {
    {
        Fixture f(intel_options(false));
        CHECK(runtime::find_rule_state(f.match->rule_state(), allied_sight_table) == nullptr);
    }
    Fixture f(intel_options(true));
    const auto* table = runtime::find_rule_state(f.match->rule_state(), allied_sight_table);
    CHECK(table != nullptr);
    auto bytes = table->bytes(table->context);
    CHECK(bytes.size() == 2 + OA_PLAYER_COUNT * OA_PLAYER_COUNT && bytes[0] == 0);
    ally(f, 1, 0);
    f.run(1);
    bytes = table->bytes(table->context);
    // Followed, viewpoint 0, and row 1 naming players 0 and 1.
    CHECK(bytes[0] == 1 && bytes[1] == 0);
    CHECK(bytes[2 + 1 * OA_PLAYER_COUNT + 0] == 1 && bytes[2 + 1 * OA_PLAYER_COUNT + 1] == 1);
    CHECK(bytes[2 + 0 * OA_PLAYER_COUNT + 1] == 0);
    const std::array<uint8_t, 3> short_state{};
    CHECK(!table->restore(table->context, short_state));
    std::array<uint8_t, 2 + OA_PLAYER_COUNT * OA_PLAYER_COUNT> saved{};
    CHECK(table->restore(table->context, saved));
    CHECK(table->bytes(table->context)[0] == 0);
    std::cout << "allied vision rule state passed\n";
}

// Every unit whose owner allies the viewer is a contact, with no shared
// radar role; the allies' line of sight and radar add contacts.
void allied_vision_merges_radar() {
    for (const bool on : {false, true}) {
        Fixture f(intel_options(on));
        seat_third_player(f);
        give_radar_and_jammer(f);
        ally(f, 1, 0);
        (void)place(f, 0, 32, 32);
        auto& granted = place(f, 1, 200, 200);
        auto& in_ally_sight = place(f, 2, 200, 200);
        auto& in_ally_radar = place(f, 2, 40, 200);
        f.match->scan_contacts();
        CHECK(contacts(granted) == (on ? shown : 0));
        CHECK(contacts(in_ally_sight) == (on ? OA_UNIT_FLAG_RADAR_CONTACT : 0));
        CHECK(contacts(in_ally_radar) == 0);
        switch_on(granted);
        f.match->scan_contacts();
        // Switched on, the ally's unit jams in the viewer's own pass, and
        // then its radar finds the unit again in its owner's.
        CHECK(
            contacts(in_ally_radar) ==
            (on ? OA_UNIT_FLAG_RADAR_CONTACT | OA_UNIT_FLAG_JAMMED : OA_UNIT_FLAG_JAMMED)
        );
    }
    std::cout << "allied vision merges radar passed\n";
}

// A viewer without a unit range of its own runs the passes once, for itself:
// the ally's radar adds nothing, while its jammer still jams.
void allied_vision_needs_a_viewer_with_units() {
    Fixture f(intel_options(true));
    seat_third_player(f);
    give_radar_and_jammer(f);
    ally(f, 1, 0);
    auto& granted = place(f, 1, 200, 200);
    switch_on(granted);
    auto& in_ally_radar = place(f, 2, 40, 200);
    auto& viewer = f.match->state().game.players[0];
    viewer.first_unit = viewer.last_unit = 0;
    f.match->scan_contacts();
    CHECK((contacts(granted) & OA_UNIT_FLAG_VIEWPOINT_OWNED) != 0);
    CHECK(contacts(in_ally_radar) == OA_UNIT_FLAG_JAMMED);
    std::cout << "allied vision needs a viewer with units passed\n";
}

// The passes run per ally in player order, so a jammer that only jams in a
// later ally's pass clears what an earlier pass found: the viewer's own
// jammer, skipped in the viewer's pass, jams in its ally's pass, unless
// allied jammers are ignored.
void allied_vision_jams_in_each_pass() {
    for (const int mode : {0, 1, 2}) {
        const bool allied_vision = mode != 0;
        Fixture f(intel_options(allied_vision, mode == 2));
        seat_third_player(f);
        give_radar_and_jammer(f);
        ally(f, 1, 0);
        auto& radar = place(f, 0, 32, 32);
        switch_on(radar);
        auto& enemy = place(f, 2, 200, 40);
        (void)place(f, 1, 200, 200);
        f.match->scan_contacts();
        const uint32_t expected = mode == 1 ? OA_UNIT_FLAG_JAMMED : OA_UNIT_FLAG_RADAR_CONTACT;
        CHECK(contacts(enemy) == expected);
    }
    std::cout << "allied vision jams in each pass passed\n";
}

// Jammers of players the viewer allies do not jam its radar picture; a
// jammer whose owner allies the viewer, one way, still does.
void allied_jammers_do_not_jam() {
    for (const bool on : {false, true}) {
        for (const bool viewer_allies : {false, true}) {
            Fixture f(intel_options(false, on));
            seat_third_player(f);
            give_radar_and_jammer(f);
            auto& radar = place(f, 0, 32, 32);
            switch_on(radar);
            auto& enemy = place(f, 2, 200, 40);
            auto& jammer = place(f, 1, 200, 200);
            switch_on(jammer);
            ally(f, 1, 0);
            if (viewer_allies)
                ally(f, 0, 1);
            f.match->scan_contacts();
            const bool jammed = !(on && viewer_allies);
            CHECK(contacts(enemy) == (jammed ? OA_UNIT_FLAG_JAMMED : OA_UNIT_FLAG_RADAR_CONTACT));
        }
    }
    std::cout << "allied jammers do not jam passed\n";
}

// A watching local player sees no jamming at all; the view-switch branch
// applies the alliance test alone while the viewpoint is another player in
// a game with mapping or line of sight.
void watchers_see_no_jamming() {
    for (const bool on : {false, true}) {
        Fixture f(intel_options(false, on));
        seat_third_player(f);
        give_radar_and_jammer(f);
        auto& radar = place(f, 0, 32, 32);
        switch_on(radar);
        auto& enemy = place(f, 1, 200, 40);
        auto& jammer = place(f, 2, 200, 200);
        switch_on(jammer);
        f.match->state().player_info[0].options = OA_SETUP_OPTION_WATCHER;
        f.match->scan_contacts();
        CHECK(
            contacts(enemy) ==
            (on ? shown : (shown & ~OA_UNIT_FLAG_RADAR_CONTACT) | OA_UNIT_FLAG_JAMMED)
        );
    }
    // The local player watches while viewing player 0.
    for (const int branch : {0, 1, 2}) {
        Fixture f(intel_options(false, true, branch != 0));
        seat_third_player(f);
        give_radar_and_jammer(f);
        auto& game = f.match->state().game;
        game.local_player_index = 1;
        f.match->state().player_info[1].options = OA_SETUP_OPTION_WATCHER;
        if (branch == 2)
            game.visibility_flags = 0;
        auto& radar = place(f, 0, 32, 32);
        switch_on(radar);
        auto& enemy = place(f, 1, 200, 40);
        auto& jammer = place(f, 2, 200, 200);
        switch_on(jammer);
        f.match->scan_contacts();
        const bool jammed = branch == 1;
        CHECK((contacts(enemy) & OA_UNIT_FLAG_JAMMED) == (jammed ? OA_UNIT_FLAG_JAMMED : 0u));
    }
    std::cout << "watchers see no jamming passed\n";
}

// A replay viewer without a slot of its own (recorder.ten-player-replay)
// looks through player 0's slot. Watching, it sees every unit on radar in its
// own view and player 0's radar once it switched to player 0's view; as every
// player's ally it sees every unit in every view. Neither sees jamming under
// intel.allied-jammers-ignored, as a seated watcher sees none.
void slotless_viewers_see_every_unit() {
    using runtime::SlotlessViewer;

    struct Case {
        SlotlessViewer viewer;
        bool switched;
        bool every_unit;
    };

    constexpr std::array<Case, 5> cases{{
        {SlotlessViewer::none, false, false},
        {SlotlessViewer::watcher, false, true},
        {SlotlessViewer::watcher, true, false},
        {SlotlessViewer::ally_of_every_player, false, true},
        {SlotlessViewer::ally_of_every_player, true, true},
    }};
    for (const auto& c : cases) {
        Fixture f(intel_options(false));
        place(f, 0, 32, 32);
        auto& enemy = place(f, 1, 200, 200);
        f.match->set_slotless_viewer(c.viewer);
        f.match->set_slotless_view_switched(c.switched);
        CHECK(f.match->slotless_viewer() == c.viewer);
        CHECK(f.match->slotless_full_radar() == c.every_unit);
        f.match->scan_contacts();
        CHECK(contacts(enemy) == (c.every_unit ? shown : 0u));
    }
    for (const auto viewer : {SlotlessViewer::watcher, SlotlessViewer::ally_of_every_player}) {
        Fixture f(intel_options(false, true));
        seat_third_player(f);
        give_radar_and_jammer(f);
        auto& radar = place(f, 0, 32, 32);
        switch_on(radar);
        auto& enemy = place(f, 1, 200, 40);
        auto& jammer = place(f, 2, 200, 200);
        switch_on(jammer);
        f.match->set_slotless_viewer(viewer);
        f.match->set_slotless_view_switched(true);
        f.match->scan_contacts();
        CHECK((contacts(enemy) & OA_UNIT_FLAG_JAMMED) == 0u);
        CHECK((contacts(enemy) & OA_UNIT_FLAG_RADAR_CONTACT) != 0u);
    }
    std::cout << "slotless viewers see every unit passed\n";
}

} // namespace

int main() {
    try {
        allied_vision_shares_sight();
        allied_vision_sees_allied_units();
        allied_vision_follows_alliances();
        allied_vision_rule_state();
        allied_vision_merges_radar();
        allied_vision_needs_a_viewer_with_units();
        allied_vision_jams_in_each_pass();
        allied_jammers_do_not_jam();
        watchers_see_no_jamming();
        slotless_viewers_see_every_unit();
    } catch (const std::exception& error) {
        std::cerr << "intel test failed: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
