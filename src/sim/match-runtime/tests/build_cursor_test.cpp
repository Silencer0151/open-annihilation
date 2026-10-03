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
        other_parameters_leave_the_cursor_alone();
    } catch (const std::exception& error) {
        std::cerr << "FAILED: " << error.what() << '\n';
        return 1;
    }
    return 0;
}
