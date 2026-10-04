// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The build cursor's site test over units, under the rule
// orders.build-site-kickout place-over-own-units: the placing player's own
// units that have a movement object leave the site open to the cursor, while
// other players' units, and the simulation's own test, still refuse it.
#include "combat_fixture.hpp"

#include <cstdint>
#include <iostream>

namespace {

using namespace combat_fixture;

// The fixture's type, mobile, has a one-cell yard that refuses units and
// nothing else.
constexpr uint16_t type = 1;
constexpr int32_t cell = 4;
constexpr uint8_t placer = 0;

// Puts a unit on the site's cell, as its occupancy would; the placer's unit
// stands there, so the placer sees the site.
void stand_on_site(Fixture& f, uint16_t unit) {
    f.match->spatial().plots[static_cast<std::size_t>(cell) * 16 + cell].ground = unit;
}

// Lets the type stand on the fixture's map, which lies under sea level, and
// maps the map as a launch does.
void prepare_sites(Fixture& f) {
    f.metadata.max_water_depth = 10000;
    f.metadata.min_water_depth = -10000;
    f.metadata.max_slope = 255;
    f.match->reset_sight_buffers(true);
}

bool cursor_accepts(Fixture& f, uint16_t skip_unit = 0) {
    return f.match->building_site(type, cell, cell, skip_unit, placer).has_value();
}

// The outline the build cursor draws around a site under ui.build-tools: red
// where the site is refused, yellow where it is let through over the placing
// player's own units, green otherwise.
enum class Outline : uint8_t { red, green, yellow };

// Tests the site as the build cursor does under place-over-own-units, naming
// unit slot 1 as the unit to skip.
Outline cursor_outline(Fixture& f, uint8_t placing_player) {
    bool over_own_units = false;
    sim::match_runtime::BuildSiteOptions report{};
    report.over_own_units = &over_own_units;
    if (!f.match->building_site(type, cell, cell, 1, placing_player, report))
        return Outline::red;
    return over_own_units ? Outline::yellow : Outline::green;
}

/// Without the rule, any unit in sight refuses the cursor's site.
void own_units_refuse_without_the_rule() {
    Fixture f;
    prepare_sites(f);
    auto& own = f.spawn(0, 72, 72);
    stand_on_site(f, 0);
    CHECK(cursor_accepts(f));
    stand_on_site(f, own.unit_index);
    CHECK(!cursor_accepts(f));
    CHECK(!f.match->building_site_clear(type, cell, cell, 0));
}

/// With the rule, the placer's own mobile units pass for the cursor alone.
void own_units_pass_with_the_rule() {
    Options options;
    options.rules.orders.build_site_kickout.enabled = true;
    options.rules.orders.build_site_kickout.place_over_own_units = true;
    Fixture f(options);
    prepare_sites(f);
    auto& own = f.spawn(0, 72, 72);
    auto& enemy = f.spawn(1, 200, 200);
    CHECK(f.match->ground_runtime(own.unit_index) != nullptr);
    stand_on_site(f, own.unit_index);
    CHECK(cursor_accepts(f));
    // The cursor's test says the site is let through over the placer's own
    // unit; an empty site leaves the report alone.
    bool over_own_units = false;
    sim::match_runtime::BuildSiteOptions report{};
    report.over_own_units = &over_own_units;
    CHECK(f.match->building_site(type, cell, cell, 0, placer, report).has_value());
    CHECK(over_own_units);
    stand_on_site(f, 0);
    over_own_units = false;
    CHECK(f.match->building_site(type, cell, cell, 0, placer, report).has_value());
    CHECK(!over_own_units);
    stand_on_site(f, own.unit_index);
    // The simulation's own test, which builders run, still sees the unit.
    CHECK(!f.match->building_site_clear(type, cell, cell, 0));
    CHECK(!f.match->building_site(type, cell, cell, 0));
    // Another player's unit refuses the cursor as before.
    stand_on_site(f, enemy.unit_index);
    CHECK(!cursor_accepts(f));
    // So does an empty slot of the placer's, which has no movement object.
    CHECK(f.match->ground_runtime(2) == nullptr && f.match->state().units[2].owner_index == 0);
    stand_on_site(f, 2);
    CHECK(!cursor_accepts(f));
}

/// The cursor's outline over each kind of unit on the site: the placer's own
/// units are yellow in any slot, slot 1 included, and in sight or not;
/// another player's unit is red, unless it holds slot 1, which the cursor
/// lets through as green.
void outline_over_each_unit() {
    Options options;
    options.rules.orders.build_site_kickout.enabled = true;
    options.rules.orders.build_site_kickout.place_over_own_units = true;
    Fixture f(options);
    prepare_sites(f);
    // The placer's first unit holds slot 1, as a skirmish's first Commander
    // does.
    auto& first_unit = f.spawn(0, 72, 72);
    auto& tank = f.spawn(0, 88, 72);
    auto& far_unit = f.spawn(1, 200, 200);
    CHECK(first_unit.unit_index == 1);
    stand_on_site(f, 0);
    CHECK(cursor_outline(f, placer) == Outline::green);
    stand_on_site(f, first_unit.unit_index);
    CHECK(cursor_outline(f, placer) == Outline::yellow);
    stand_on_site(f, tank.unit_index);
    CHECK(cursor_outline(f, placer) == Outline::yellow);
    stand_on_site(f, far_unit.unit_index);
    CHECK(cursor_outline(f, placer) == Outline::red);
    // Player 1 does not see the site: its own unit there still makes the
    // outline yellow, and player 0's tank does not refuse it.
    constexpr uint8_t other_placer = 1;
    CHECK(cursor_outline(f, other_placer) == Outline::yellow);
    stand_on_site(f, tank.unit_index);
    CHECK(cursor_outline(f, other_placer) == Outline::green);
    // Once player 1 sees the site the tank refuses it, while player 0's unit
    // in slot 1, which refuses the simulation's own test, does not.
    f.spawn(1, 72, 88);
    stand_on_site(f, tank.unit_index);
    CHECK(cursor_outline(f, other_placer) == Outline::red);
    stand_on_site(f, first_unit.unit_index);
    CHECK(!f.match->building_site(type, cell, cell, 0, other_placer));
    CHECK(cursor_outline(f, other_placer) == Outline::green);
}

/// The kickout and retry parameters leave the cursor's test alone.
void other_parameters_leave_the_cursor_alone() {
    Options options;
    options.rules.orders.build_site_kickout.enabled = true;
    options.rules.orders.build_site_kickout.kickout = true;
    options.rules.orders.build_site_kickout.retry_limit = 20;
    Fixture f(options);
    prepare_sites(f);
    auto& own = f.spawn(0, 72, 72);
    stand_on_site(f, own.unit_index);
    CHECK(!cursor_accepts(f));
}

} // namespace

int main() {
    try {
        own_units_refuse_without_the_rule();
        own_units_pass_with_the_rule();
        outline_over_each_unit();
        other_parameters_leave_the_cursor_alone();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
