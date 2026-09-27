// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/detection.hpp"

#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

using namespace oa;
using namespace oa::sim::detection;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

constexpr uint8_t sea_level = 100;
constexpr uint8_t viewer = 0;
constexpr uint8_t enemy = 1;
constexpr uint8_t ally = 2;

// Unit types: 1 plain, 2 stealth; both 10 high.
enum : uint16_t { plain = 1, stealthy, type_count };

// Slot 1 is the viewer's, 2..6 the enemy's, 7 the ally's.
constexpr uint32_t viewer_slot = 1, enemy_first = 2, ally_slot = 7, slot_count = 8;

constexpr int32_t fixed(int32_t whole) {
    return static_cast<int32_t>(static_cast<uint32_t>(whole) << 16);
}

World* make_world() {
    World* w = world_create();
    WorldCapacity capacity{slot_count, type_count, 0};
    if (w == nullptr || !world_alloc_tables(w, &capacity))
        std::abort();
    w->game.sea_level = sea_level;
    w->game.viewpoint_player = viewer;
    for (uint16_t t = 1; t < type_count; ++t)
        w->unit_defs[t].model_height = fixed(10);
    w->unit_defs[stealthy].flags = OA_UNIT_DEF_FLAG_STEALTH;
    const auto own = [&](uint8_t player, uint32_t first, uint32_t last) {
        auto& p = w->game.players[player];
        p.in_use = 1;
        p.index = player;
        p.first_unit = oa_unit_ref_from_slot(first);
        p.last_unit = oa_unit_ref_from_slot(last);
        p.info = oa_ref_from_index(player);
        for (uint32_t slot = first; slot <= last; ++slot) {
            w->units[slot].owner = oa_ref_from_index(player);
            w->units[slot].owner_index = player;
        }
    };
    own(viewer, viewer_slot, viewer_slot);
    own(enemy, enemy_first, ally_slot - 1);
    own(ally, ally_slot, slot_count - 1);
    return w;
}

Unit& place(World& w, uint32_t slot, uint16_t type, int32_t x, int32_t y, int32_t z) {
    auto& unit = w.units[slot];
    unit.type_index = type;
    unit.def = oa_ref_from_index(type);
    unit.flags = OA_UNIT_FLAG_LIVE;
    unit.position = {fixed(x), fixed(y), fixed(z)};
    return unit;
}

uint32_t contacts(const Unit& unit) {
    return unit.flags & contact_bits;
}

// Radar reach 100 and sonar reach 50 from the origin.
ScanRecord scanner_at_origin() {
    ScanRecord scan;
    scan.radar_range_squared = 100 * 100;
    scan.sonar_range_squared = 50 * 50;
    scan.position = {0, fixed(sea_level), 0};
    return scan;
}

void test_distance() {
    const FixedVec3 a{fixed(3), fixed(9), fixed(4)};
    const FixedVec3 origin{};
    CHECK(squared_distance_high(a, origin) == 25);
    CHECK(squared_distance_high(origin, a) == 25);
    CHECK(squared_distance_high({fixed(1) / 2, 0, 0}, origin) == 0);
}

void test_submerged_hull_needs_sonar() {
    World* w = make_world();
    const auto scan = scanner_at_origin();
    // Top at 90: under the surface, so never a radar contact.
    auto& near = place(*w, enemy_first, plain, 30, sea_level - 20, 30);
    auto& far = place(*w, enemy_first + 1, plain, 0, sea_level - 20, 50);
    stamp_contact(scan, near, *w);
    stamp_contact(scan, far, *w);
    CHECK(contacts(near) == sonar_contact);
    CHECK(contacts(far) == 0);
    world_destroy(w);
}

void test_surface_and_land_contacts() {
    World* w = make_world();
    const auto scan = scanner_at_origin();
    auto& ship = place(*w, enemy_first, plain, 0, sea_level - 3, 30);
    auto& tank = place(*w, enemy_first + 1, plain, 30, sea_level + 5, 0);
    auto& radar_only = place(*w, enemy_first + 2, plain, 0, sea_level - 3, -70);
    // The top exactly at sea level still shows on radar.
    auto& awash = place(*w, enemy_first + 3, plain, 10, sea_level - 10, 0);
    auto& hidden = place(*w, enemy_first + 4, stealthy, 10, sea_level - 3, 0);
    for (auto* unit : {&ship, &tank, &radar_only, &awash, &hidden})
        stamp_contact(scan, *unit, *w);
    CHECK(contacts(ship) == (sonar_contact | radar_contact));
    CHECK(contacts(tank) == radar_contact);
    CHECK(contacts(radar_only) == radar_contact);
    CHECK(contacts(awash) == (sonar_contact | radar_contact));
    CHECK(contacts(hidden) == 0);
    world_destroy(w);
}

void test_reach_is_exclusive() {
    World* w = make_world();
    const auto scan = scanner_at_origin();
    auto& sonar_edge = place(*w, enemy_first, plain, 50, sea_level - 3, 0);
    auto& radar_edge = place(*w, enemy_first + 1, plain, 0, sea_level + 5, 100);
    stamp_contact(scan, sonar_edge, *w);
    stamp_contact(scan, radar_edge, *w);
    CHECK(contacts(sonar_edge) == radar_contact);
    CHECK(contacts(radar_edge) == 0);
    world_destroy(w);
}

void test_own_and_empty_units_are_skipped() {
    World* w = make_world();
    const auto scan = scanner_at_origin();
    auto& own = place(*w, viewer_slot, plain, 0, sea_level, 10);
    auto& empty = place(*w, enemy_first, plain, 0, sea_level, 10);
    empty.type_index = 0;
    stamp_contact(scan, own, *w);
    stamp_contact(scan, empty, *w);
    CHECK(contacts(own) == 0);
    CHECK(contacts(empty) == 0);
    world_destroy(w);
}

void test_jammers() {
    World* w = make_world();
    auto& radar_jammed = place(*w, enemy_first, plain, 0, sea_level, 0);
    auto& sonar_jammed = place(*w, enemy_first + 1, plain, 0, sea_level, 0);
    radar_jammed.flags |= radar_contact | sonar_contact;
    sonar_jammed.flags |= radar_contact | sonar_contact;
    jam_radar(radar_jammed);
    jam_sonar(sonar_jammed);
    CHECK(contacts(radar_jammed) == (sonar_contact | jammed));
    CHECK(contacts(sonar_jammed) == (radar_contact | jammed));
    CHECK((radar_jammed.flags & OA_UNIT_FLAG_LIVE) != 0);
    world_destroy(w);
}

void test_shared_radar() {
    World* w = make_world();
    const auto& watcher = w->game.players[viewer];
    const auto& friendly = w->game.players[ally];
    CHECK(!shares_radar(*w, watcher, friendly));
    w->game.players[ally].alliance[viewer] = 1;
    CHECK(!shares_radar(*w, watcher, friendly));
    w->player_info[ally].role = share_radar_role;
    CHECK(shares_radar(*w, watcher, friendly));
    w->game.players[ally].alliance[viewer] = 0;
    CHECK(!shares_radar(*w, watcher, friendly));
    world_destroy(w);
}

// Storage for one player's sightings over the fixture's unit slots.
struct SightingLists {
    uint16_t seen[slot_count]{};
    uint16_t radar[slot_count]{};
    Sightings sightings{seen, radar, slot_count};
};

// Files slots 1..7 for the viewer; `visible` is the viewer's sight test.
// Returns how many of the viewer's own finished units the walk reported.
uint32_t file_all(SightingLists& lists, World& w, const bool (&visible)[slot_count]) {
    const auto& player = w.game.players[viewer];
    clear_sightings(lists.sightings);
    uint32_t own = 0;
    for (uint32_t slot = 1; slot < slot_count; ++slot) {
        const auto& unit = w.units[slot];
        const bool seen = sighting_candidate(w, player, unit) && visible[slot];
        if (file_sighting(lists.sightings, w, player, unit, seen))
            ++own;
    }
    return own;
}

void test_sightings_cadence() {
    Sightings sightings;
    CHECK(!sightings_due(sightings, 29));
    CHECK(sightings_due(sightings, 30));
    sightings.refreshed_tick = 30;
    CHECK(!sightings_due(sightings, 59));
    CHECK(sightings_due(sightings, 60));
}

void test_sightings_lists() {
    World* w = make_world();
    auto& me = w->game.players[viewer];
    me.alliance[viewer] = 1;
    me.alliance[ally] = 1;
    w->unit_defs[plain].flags = OA_UNIT_DEF_FLAG_TARGETING_UPGRADE;
    // The viewer's finished, switched-on targeting facility.
    auto& facility = place(*w, viewer_slot, plain, 0, sea_level, 0);
    facility.state_flags = OA_UNIT_STATE_ACTIVE;
    auto& spotted = place(*w, enemy_first, plain, 10, sea_level, 0);
    auto& radar_blip = place(*w, enemy_first + 1, plain, 20, sea_level, 0);
    radar_blip.flags |= radar_contact;
    auto& unsightable = place(*w, enemy_first + 2, plain, 30, sea_level, 0);
    unsightable.flags |= unsighted | radar_contact;
    auto& dying = place(*w, enemy_first + 3, plain, 40, sea_level, 0);
    dying.flags |= OA_UNIT_FLAG_DEATH_PENDING | radar_contact;
    // Slot 6 stays empty.
    auto& friendly = place(*w, ally_slot, plain, 50, sea_level, 0);
    friendly.flags |= radar_contact;
    friendly.state_flags = OA_UNIT_STATE_ACTIVE;

    CHECK(!sighting_candidate(*w, me, facility));
    CHECK(sighting_candidate(*w, me, spotted));
    CHECK(!sighting_candidate(*w, me, dying));
    CHECK(!sighting_candidate(*w, me, friendly));

    // Every live unit passes the viewer's sight test except the radar blip.
    const bool visible[slot_count]{false, true, true, false, true, true, true, true};
    SightingLists lists;
    // Only the facility is the viewer's own; the ally's unit is not counted.
    CHECK(file_all(lists, *w, visible) == 1);
    CHECK(lists.sightings.seen_count == 1 && lists.seen[0] == enemy_first);
    CHECK(lists.sightings.radar_count == 2);
    CHECK(lists.radar[0] == enemy_first + 1 && lists.radar[1] == enemy_first + 2);
    CHECK(lists.sightings.radar_fallback == 1);

    // An unfinished facility gives no fallback; a NaN fraction counts as
    // finished, as an unordered compare counts as equal.
    facility.build_remaining = 0.5F;
    CHECK(file_all(lists, *w, visible) == 0);
    CHECK(lists.sightings.radar_fallback == 0);
    CHECK(lists.sightings.seen_count == 1 && lists.sightings.radar_count == 2);
    facility.build_remaining = std::numeric_limits<float>::quiet_NaN();
    CHECK(file_all(lists, *w, visible) == 1);
    CHECK(lists.sightings.radar_fallback == 1);
    // Switched off it gives none, and the ally's switched-on facility never did.
    facility.build_remaining = 0.0F;
    facility.state_flags = 0;
    file_all(lists, *w, visible);
    CHECK(lists.sightings.radar_fallback == 0);
    world_destroy(w);
}

void test_sighted_within() {
    World* w = make_world();
    auto& far = place(*w, enemy_first, plain, 100, sea_level, 0);
    // 50 units away on the ground plane, whatever its height.
    auto& near = place(*w, enemy_first + 1, plain, 30, sea_level + 400, 40);
    SightingLists lists;
    const FixedVec3 origin{0, fixed(sea_level), 0};
    CHECK(!sighted_within(lists.sightings, *w, origin, 1000));
    lists.seen[0] = enemy_first;
    lists.seen[1] = enemy_first + 1;
    lists.sightings.seen_count = 2;
    CHECK(sighted_within(lists.sightings, *w, origin, 50));
    CHECK(!sighted_within(lists.sightings, *w, origin, 49));
    // A listed unit that has died since the rebuild no longer counts.
    near.flags |= OA_UNIT_FLAG_DEATH_PENDING;
    CHECK(!sighted_within(lists.sightings, *w, origin, 50));
    CHECK(sighted_within(lists.sightings, *w, origin, 100));
    far.flags = 0;
    CHECK(!sighted_within(lists.sightings, *w, origin, 100));
    // Only the seen list counts.
    near.flags = OA_UNIT_FLAG_LIVE;
    lists.radar[0] = enemy_first + 1;
    lists.sightings.radar_count = 1;
    lists.sightings.seen_count = 0;
    CHECK(!sighted_within(lists.sightings, *w, origin, 100));
    world_destroy(w);
}

/// Checks that a unit is active with OA_UNIT_FLAG_LIVE set and
/// OA_UNIT_FLAG_DEATH_PENDING clear, and finished when build_remaining is
/// neither below nor above 0.0, so an unordered fraction reads as zero.
void test_finished_unit() {
    Unit unit{};
    unit.flags = OA_UNIT_FLAG_LIVE;
    CHECK(finished_unit(unit));
    unit.flags = OA_UNIT_FLAG_LIVE | OA_UNIT_FLAG_DEATH_PENDING;
    CHECK(!finished_unit(unit));
    unit.flags = 0x00000000u;
    CHECK(!finished_unit(unit));
    unit.flags = OA_UNIT_FLAG_LIVE;
    unit.build_remaining = 0.5F;
    CHECK(!finished_unit(unit));
    unit.build_remaining = -0.0F;
    CHECK(finished_unit(unit));
    unit.build_remaining = std::numeric_limits<float>::quiet_NaN();
    CHECK(finished_unit(unit));
}

void test_expose_cloaker() {
    Unit unit{};
    unit.flags = OA_UNIT_FLAG_LIVE | cloak_ordered;
    expose_cloaker(unit, 1000);
    CHECK(unit.decloak_until_tick == 1090);
    CHECK(unit.flags == (OA_UNIT_FLAG_LIVE | cloak_ordered | cloak_locked));
}

} // namespace

int main() {
    test_distance();
    test_submerged_hull_needs_sonar();
    test_surface_and_land_contacts();
    test_reach_is_exclusive();
    test_own_and_empty_units_are_skipped();
    test_jammers();
    test_shared_radar();
    test_sightings_cadence();
    test_sightings_lists();
    test_sighted_within();
    test_expose_cloaker();
    test_finished_unit();
    if (failures)
        std::fprintf(stderr, "%d detection checks failed\n", failures);
    return failures ? EXIT_FAILURE : EXIT_SUCCESS;
}
