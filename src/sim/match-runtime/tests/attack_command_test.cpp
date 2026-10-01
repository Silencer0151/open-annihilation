// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The attack command a player or a computer player gives: it replaces the
// orders under way, carries no leash and no way back, and a unit holding its
// fire or its position takes it. Run with --data, the installed game's units
// commanded on the move brake where the command finds them and fire from
// there, and commanded from rest at a unit out of reach close until it is in
// range; gunships hover facing their target and, once it is destroyed, stay
// in the air circling the command's point until they find the next unit.
#include "combat_fixture.hpp"
#include "installed_units.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/ai.hpp"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <string_view>

using namespace combat_fixture;

namespace {

constexpr uint8_t move_ground_order = sim::ground_orders::move_ground_kind;
constexpr int32_t fixed_one = 1 << 16;

sim::ground_orders::Point point(int32_t x, int32_t z) {
    return {x * fixed_one, 0, z * fixed_one};
}

// Whole world units east of the map's edge.
int32_t east(const sim::unit_spawn::Slot& slot) {
    return slot.record.position.x / fixed_one;
}

int32_t speed(Fixture& f, const sim::unit_spawn::Slot& slot) {
    const auto* runtime = f.match->ground_runtime(slot.unit_index);
    CHECK(runtime != nullptr);
    return runtime->movement.speed;
}

void hold_fire(sim::unit_spawn::Slot& slot) {
    slot.unit->flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
}

// A move under way when the attack command comes no longer steers the unit:
// the command replaces the move, so the attacker brakes and fires from about
// where the command found it instead of carrying on to the move's end.
void attack_command_replaces_the_move_under_way() {
    Fixture f;
    auto& attacker = f.spawn(0, 24, 128);
    auto& target = f.spawn(1, 152, 128);
    hold_fire(target);
    (void)f.match->issue_ground_move(attacker.unit_index, point(232, 128), false);
    f.run(15);
    CHECK(speed(f, attacker) > 0);
    const auto commanded_at = east(attacker);
    CHECK(f.match->issue_attack_command(attacker.unit_index, target.unit_index, false, nullptr));
    CHECK(head_is(attacker, attack_chase_order) && attacker.unit->primary->next == nullptr);
    f.run(60);
    CHECK(speed(f, attacker) == 0);
    // The type brakes one unit a tick from two a tick.
    CHECK(east(attacker) - commanded_at <= 4);
    CHECK(f.shots_from(attacker) > 0);
}

// Queued, the attack command waits behind the orders under way.
void queued_attack_command_follows_the_orders() {
    Fixture f;
    auto& attacker = f.spawn(0, 24, 128);
    auto& target = f.spawn(1, 152, 128);
    hold_fire(target);
    (void)f.match->issue_ground_move(attacker.unit_index, point(88, 200), false);
    CHECK(f.match->issue_attack_command(attacker.unit_index, target.unit_index, true, nullptr));
    CHECK(head_is(attacker, move_ground_order));
    const auto* next = attacker.unit->primary->next;
    CHECK(next != nullptr && next->kind == attack_chase_order && next->next == nullptr);
}

// A unit on the manoeuvre order that goes after a unit on its own also queues
// a move back to where it stood; the command gives it no way back.
void attack_command_takes_no_way_back() {
    Fixture f;
    f.def.maneuver_leash_length = 32;
    auto& attacker = f.spawn(0, 24, 128);
    auto& own_choice = f.spawn(0, 24, 64);
    auto& target = f.spawn(1, 152, 128);
    hold_fire(target);
    CHECK(f.match->issue_attack(own_choice.unit_index, target.unit_index, false));
    CHECK(head_is(own_choice, attack_chase_order));
    const auto* back = own_choice.unit->primary->next;
    CHECK(back != nullptr && back->kind == move_ground_order);
    CHECK(f.match->issue_attack_command(attacker.unit_index, target.unit_index, false, nullptr));
    CHECK(head_is(attacker, attack_chase_order) && attacker.unit->primary->next == nullptr);
}

// Holding its fire and its position, a unit starts no attack of its own, but
// it takes the command and fires.
void attack_command_overrides_standing_orders() {
    Fixture f;
    auto& attacker = f.spawn(0, 24, 128);
    auto& target = f.spawn(1, 152, 128);
    hold_fire(target);
    attacker.unit->flags &= ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK);
    CHECK(!f.match->issue_attack(attacker.unit_index, target.unit_index, false));
    CHECK(f.match->issue_attack_command(attacker.unit_index, target.unit_index, false, nullptr));
    CHECK(head_is(attacker, attack_chase_order));
    f.run(30);
    CHECK(f.shots_from(attacker) > 0);
}

// A unit is no target of its own command.
void attack_command_refuses_the_unit_itself() {
    Fixture f;
    auto& attacker = f.spawn(0, 24, 128);
    CHECK(!f.match->issue_attack_command(attacker.unit_index, attacker.unit_index, false, nullptr));
    CHECK(attacker.unit->primary == nullptr);
}

// The order a command leaves at the head of a unit's orders.
sim::match_runtime::Match::OrderRecordView
head_record(Fixture& f, const sim::unit_spawn::Slot& slot) {
    sim::match_runtime::Match::OrderRecordView record;
    CHECK(f.match->queue_records(slot.unit_index, false, &record, 1) == 1);
    return record;
}

// A computer player's squad attack is the attack command: the squad member
// drops the move under way, takes no move back, and takes the command with
// no point.
void computer_squad_attack_replaces_the_move_under_way() {
    Fixture f;
    auto& member = f.spawn(0, 24, 128);
    auto& target = f.spawn(1, 152, 128);
    hold_fire(target);
    (void)f.match->issue_ground_move(member.unit_index, point(232, 128), false);
    f.run(15);
    CHECK(speed(f, member) > 0);
    const auto host = sim::ai::match_computer_host(*f.match);
    CHECK(host.order_attack(host.context, member.unit_index, target.unit_index));
    CHECK(head_is(member, attack_chase_order) && member.unit->primary->next == nullptr);
    const auto record = head_record(f, member);
    CHECK(record.target == target.unit_index);
    CHECK(record.point == sim::ground_orders::Point{});
    f.run(60);
    CHECK(speed(f, member) == 0);
    CHECK(f.shots_from(member) > 0);
}

// The command keeps the point it was given at, the ground under the pointer,
// and a queued command given again there is found by that point.
void attack_command_keeps_its_point() {
    Fixture f;
    auto& attacker = f.spawn(0, 24, 128);
    auto& target = f.spawn(1, 152, 128);
    hold_fire(target);
    (void)f.match->issue_ground_move(attacker.unit_index, point(88, 200), false);
    const auto at = point(160, 120);
    CHECK(f.match->issue_attack_command(attacker.unit_index, target.unit_index, true, &at));
    const auto* queued = attacker.unit->primary->next;
    CHECK(queued != nullptr && queued->kind == attack_chase_order);
    sim::match_runtime::Match::OrderRecordView records[2];
    CHECK(f.match->queue_records(attacker.unit_index, false, records, 2) == 2);
    CHECK(records[1].target == target.unit_index && records[1].point == at);
    CHECK(f.match->cancel_queued_order(
        attacker.unit_index, attack_chase_order, target.unit_index, &at
    ));
    CHECK(head_is(attacker, move_ground_order) && attacker.unit->primary->next == nullptr);
}

// Commanded at a unit of its own side, a unit takes the Suppress order, which
// fires on the command's point and keeps no target.
void attack_command_on_its_own_side_keeps_no_target() {
    Fixture f;
    auto& attacker = f.spawn(0, 24, 128);
    auto& friendly = f.spawn(0, 152, 128);
    const auto at = point(150, 130);
    CHECK(f.match->issue_attack_command(attacker.unit_index, friendly.unit_index, false, &at));
    CHECK(head_is(attacker, sim::match_runtime::suppress_kind));
    const auto record = head_record(f, attacker);
    CHECK(record.target == 0 && record.point == at);
    bool drawn_at_the_point = false;
    f.match->visit_primary_queue(attacker.unit_index, [&](const auto& view) {
        drawn_at_the_point = view.destination == at;
    });
    CHECK(drawn_at_the_point);
}

// ---------------------------------------------------------------------------
// The installed game's units on open level ground, 16-unit cells.

constexpr int32_t land_columns = 128;
constexpr int32_t land_rows = 64;
constexpr uint8_t land_sea_level = 20;
constexpr uint8_t land_height = 60;
constexpr int32_t target_cell_x = 100;
constexpr int32_t land_row = 32;
// Each weapon of these types reaches a unit it is aimed at from any distance
// up to its range, so the command is given with the target at this fraction
// of the range.
constexpr double commanded_reach = 0.85;
// Ticks for a commanded unit to come to rest, and then to fire.
constexpr uint32_t rest_ticks = 300;
constexpr uint32_t fire_ticks = 240;
constexpr uint32_t approach_ticks = 900;
// Where a unit commanded from rest starts: beyond the reach of every weapon here.
constexpr int32_t distant_start = 700;

struct LandServices : sim::match_runtime::OfflineServices {
    void command_sound(sim::unit_spawn::Slot&, uint32_t) override {}

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void explode_piece(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void attach_unit(sim::unit_spawn::Slot&, int32_t, int32_t, int32_t) override {}

    void drop_unit(sim::unit_spawn::Slot&, int32_t) override {}

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {}

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}
};

struct LandScenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

// Two players who each order their own units, on level land.
struct Land {
    test::InstalledUnits& units;
    test::Seascape ground;
    LandServices services;
    LandScenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    // Land of `columns` by `rows` 16-unit cells, with open sea, its floor at
    // height 0, west of column `shore`.
    explicit Land(
        test::InstalledUnits& loaded,
        int32_t columns = land_columns,
        int32_t rows = land_rows,
        int32_t shore = 0
    )
        : units(loaded), ground(columns, rows, land_sea_level, shore, shore, 0, 0, land_height) {
        sim::match_runtime::OfflineInputs input{
            ground.map,
            units.loaded,
            units.types,
            units.fields,
            units.weapons,
            ground.terrain_values,
            ground.masks,
            columns / 2,
            rows / 2,
            32,
            2,
            0,
            30,
            1,
            &scenario,
            [] { return 1000u; },
            ground.plots,
            {},
            0,
            0,
            0.0F,
            units.features.defs
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        match->configure_strategic_environment({0, 0.5F, 0});
        for (uint8_t player = 0; player < 2; ++player) {
            match->simulation().players[player].present = true;
            match->simulation().players[player].status = OA_PLAYER_STATUS_LOCAL;
            std::array<uint8_t, 10> allies{};
            allies[player] = 1;
            match->configure_player_alliances(player, allies);
        }
        std::array<uint8_t, 10> local_allies{};
        local_allies[0] = 1;
        match->configure_outcomes(0, local_allies, false);
    }

    // A finished unit at (x, z) in whole world units.
    sim::unit_spawn::Slot&
    spawn(uint8_t player, std::string_view name, int32_t x, int32_t z = land_row * 16 + 8) {
        auto* slot = match->create(
            {player,
             units.type(name),
             {static_cast<uint32_t>(x * fixed_one), 0, static_cast<uint32_t>(z * fixed_one)},
             true,
             1,
             0}
        );
        CHECK(slot && slot->unit);
        return *slot;
    }

    void run(uint32_t ticks, const std::function<bool()>& until = {}) {
        for (uint32_t i = 0; i < ticks; ++i) {
            ++match->state().game.tick;
            match->tick();
            if (until && until())
                return;
        }
    }

    // A target that neither fires nor moves: the standing orders a finished
    // unit takes up in its first ticks are cleared after them.
    sim::unit_spawn::Slot&
    target(int32_t x = target_cell_x * 16 + 8, int32_t z = land_row * 16 + 8) {
        auto& slot = spawn(1, "CORAK", x, z);
        run(2);
        slot.record.flags &= ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK);
        return slot;
    }
};

double distance(const sim::unit_spawn::Slot& a, const sim::unit_spawn::Slot& b) {
    const auto dx = static_cast<double>(a.record.position.x - b.record.position.x) / fixed_one;
    const auto dz = static_cast<double>(a.record.position.z - b.record.position.z) / fixed_one;
    return std::hypot(dx, dz);
}

int32_t weapon_range(Land& land, const sim::unit_spawn::Slot& slot) {
    const auto* weapon = oa::world_weapon_def(&land.match->state(), slot.record.weapons[0].def);
    CHECK(weapon != nullptr);
    return weapon->range;
}

// Whether the unit's first slot fired in the last tick: its reload restarted.
struct ShotWatch {
    const sim::unit_spawn::Slot& slot;
    uint16_t last{};

    bool fired() {
        const auto reload = slot.record.weapons[0].reload;
        const bool restarted = reload > last;
        last = reload;
        return restarted;
    }
};

// Commanded while it walks or drives toward a unit already within its reach,
// an installed unit brakes where the command finds it and fires from where it
// comes to rest, rather than carrying on along its move to the far side.
void installed_attack_command_stops_a_move(test::InstalledUnits& units, std::string_view name) {
    Land land(units);
    auto& target = land.target();
    auto& attacker = land.spawn(0, name, (target_cell_x - 26) * 16 + 8);
    land.run(2);
    const auto range = weapon_range(land, attacker);
    const auto& definition = land.units.definitions[attacker.record.type_index];
    (void)land.match->issue_ground_move(
        attacker.unit_index, point(target_cell_x * 16 + 208, land_row * 16 + 8), false
    );
    land.run(approach_ticks, [&] { return distance(attacker, target) <= range * commanded_reach; });
    const auto commanded_at = distance(attacker, target);
    const auto* runtime = land.match->ground_runtime(attacker.unit_index);
    CHECK(runtime != nullptr && runtime->movement.speed > 0);
    // Braking from the speed it has: v * v / 2b, and the tick it moves at v.
    const double moving = static_cast<double>(runtime->movement.speed) / fixed_one;
    const double brake = static_cast<double>(definition.brake_rate_fixed) / fixed_one;
    const double braking = moving * moving / (2.0 * brake) + moving;
    CHECK(land.match->issue_attack_command(attacker.unit_index, target.unit_index, false, nullptr));
    land.run(rest_ticks, [&] { return runtime->movement.speed == 0; });
    CHECK(runtime->movement.speed == 0);
    const auto rest = distance(attacker, target);
    if (!(rest >= commanded_at - braking - 1.0 && rest <= commanded_at))
        throw std::runtime_error(
            std::string(name) + " commanded at " + std::to_string(commanded_at) +
            " came to rest at " + std::to_string(rest) + ", braking " + std::to_string(braking)
        );
    ShotWatch shots{attacker, attacker.record.weapons[0].reload};
    land.run(fire_ticks, [&] { return shots.fired(); });
    CHECK(attacker.record.weapons[0].reload > 0);
    CHECK(std::abs(distance(attacker, target) - rest) < 1.0);
}

// Commanded from rest at a unit out of its reach, an installed unit closes
// until the unit is within its weapon's range, comes to rest there, and fires
// its first shot within range.
void installed_attack_command_closes_to_range(test::InstalledUnits& units, std::string_view name) {
    Land land(units);
    auto& target = land.target();
    auto& attacker = land.spawn(0, name, target_cell_x * 16 + 8 - distant_start);
    const auto range = weapon_range(land, attacker);
    CHECK(range < distant_start);
    land.run(2);
    const auto* runtime = land.match->ground_runtime(attacker.unit_index);
    CHECK(runtime != nullptr);
    CHECK(land.match->issue_attack_command(attacker.unit_index, target.unit_index, false, nullptr));
    ShotWatch shots{attacker, attacker.record.weapons[0].reload};
    std::optional<double> first_shot;
    land.run(approach_ticks, [&] {
        if (!first_shot && shots.fired())
            first_shot = distance(attacker, target);
        return first_shot && runtime->movement.speed == 0;
    });
    CHECK(first_shot && *first_shot <= range);
    const auto rest = distance(attacker, target);
    if (!(runtime->movement.speed == 0 && rest <= range && rest > range / 2.0))
        throw std::runtime_error(
            std::string(name) + " with range " + std::to_string(range) + " came to rest at " +
            std::to_string(rest)
        );
}

// Ticks a gunship has, once it first has its target within range, to destroy
// it.
constexpr uint32_t hover_watch_ticks = 600;
// Angle units either side of the bearing to the target a hovering gunship
// counts as facing it: a sixteenth of a turn.
constexpr int32_t hover_facing_tolerance = 0x1000;
// Share of the watched ticks a gunship faces its target at the least.
constexpr double hover_facing_share = 0.9;

// Commanded at a ground unit, an installed gunship flies to weapon range and
// hovers there facing the unit, strafing from side to side and firing until
// the unit is destroyed, rather than flying from point to point facing where it
// goes.
void installed_gunship_hovers_facing_its_target(
    test::InstalledUnits& units, std::string_view name
) {
    Land land(units);
    auto& target = land.target();
    auto& gunship = land.spawn(0, name, target_cell_x * 16 + 8 - distant_start);
    const auto range = weapon_range(land, gunship);
    land.run(2);
    CHECK(land.match->issue_attack_command(gunship.unit_index, target.unit_index, false, nullptr));
    land.run(approach_ticks, [&] { return distance(gunship, target) <= range; });
    CHECK(distance(gunship, target) <= range);
    uint32_t watched = 0;
    uint32_t facing = 0;
    land.run(hover_watch_ticks, [&] {
        if (target.record.health == 0)
            return true;
        const auto bearing = base::game_math::direction(
            gunship.record.position.x - target.record.position.x,
            gunship.record.position.z - target.record.position.z
        );
        const auto error =
            static_cast<int16_t>(static_cast<uint16_t>(bearing - gunship.record.heading));
        ++watched;
        if (std::abs(static_cast<int32_t>(error)) <= hover_facing_tolerance)
            ++facing;
        return false;
    });
    const auto share = static_cast<double>(facing) / watched;
    if (target.record.health != 0 || share < hover_facing_share)
        throw std::runtime_error(
            std::string(name) + " faced its target " + std::to_string(facing) + " of " +
            std::to_string(watched) + " ticks and left it with " +
            std::to_string(target.record.health) + " health"
        );
}

// Ticks a vertical-launch missile has, from launch, to come down.
constexpr uint32_t missile_flight_ticks = 900;
// How far the unit drives off once the missile is away, in world units, and
// how near its aim point a missile counts as at it.
constexpr int32_t missile_target_drive = 160;
constexpr double missile_aim_reach = 32.0;
// Ticks a missile may spend at its aim point before it comes down: a dive
// passes through in two or three.
constexpr uint32_t missile_ticks_at_aim = 4;

// The live shot of a weapon launched from `origin`, if it still flies.
const oa::Projectile* shot_launched(const Land& land, oa_ref32 def, const oa::FixedVec3& origin) {
    for (const auto& shot : land.match->projectiles())
        if (shot.def == def && shot.origin.x == origin.x && shot.origin.z == origin.z)
            return &shot;
    return nullptr;
}

// A vertical-launch missile that does not track homes on where its target
// stood at launch. With the target driven off, it turns at its weapon's
// turnrate, angle units per second, and comes down at that point rather than
// circling over it.
void installed_vertical_missile_comes_down_where_aimed(
    test::InstalledUnits& units, std::string_view name
) {
    Land land(units);
    auto& target = land.target();
    auto& launcher = land.spawn(0, name, target_cell_x * 16 + 8 - 500);
    land.run(2);
    CHECK(land.match->issue_attack_command(launcher.unit_index, target.unit_index, false, nullptr));
    land.run(approach_ticks, [&] { return !land.match->projectiles().empty(); });
    CHECK(!land.match->projectiles().empty());
    const auto launched = land.match->projectiles().front();
    // The launcher holds its fire, so that the missile watched is the only one
    // launched from where it stands.
    (void)land.match->issue_stop(launcher.unit_index);
    launcher.record.flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
    (void)land.match->issue_ground_move(
        target.unit_index,
        point(target_cell_x * 16 + 8, land_row * 16 + 8 + missile_target_drive),
        false
    );
    uint32_t at_aim = 0;
    bool flying = true;
    land.run(missile_flight_ticks, [&] {
        const auto* shot = shot_launched(land, launched.def, launched.origin);
        if (shot == nullptr) {
            flying = false;
            return true;
        }
        const auto dx = static_cast<double>(shot->position.x - shot->target.x) / fixed_one;
        const auto dy = static_cast<double>(shot->position.y - shot->target.y) / fixed_one;
        const auto dz = static_cast<double>(shot->position.z - shot->target.z) / fixed_one;
        if (std::hypot(dx, dy, dz) < missile_aim_reach)
            ++at_aim;
        return false;
    });
    if (flying || at_aim > missile_ticks_at_aim)
        throw std::runtime_error(
            std::string(name) + " missile spent " + std::to_string(at_aim) +
            " ticks at its aim point and " + (flying ? "was still flying" : "came down")
        );
}

// The VTOL_SeekAttack mission a finished aircraft attack hands over to.
constexpr uint8_t seek_attack_order = sim::match_runtime::vtol_seek_attack_kind;
// Unit.flags occupancy of a unit in the air.
constexpr uint32_t airborne = 2;
// Ticks a gunship that has destroyed its target is watched looking for the
// next one.
constexpr uint32_t seek_watch_ticks = 1500;
// Ticks a gunship has to find and destroy a second unit once it has
// destroyed the first.
constexpr uint32_t next_target_ticks = 3000;
// World units beyond its weapon's range at which a seeking aircraft circles
// its point.
constexpr int32_t seek_orbit_margin = 160;
// World units a gunship may pass beyond its circle: the radius within which
// it arrives at each point it flies to.
constexpr int32_t seek_arrival = 128;
// Where the second unit stands: south of the first, away from the way a
// gunship comes in, and inside the circle it flies around the first once it
// has destroyed it.
constexpr int32_t next_target_offset = 400;
// Cells along each side of the square map a gunship circles over: its circle
// stays clear of the map's edges, where an aircraft turns back toward the
// centre at the height of the off-map area.
constexpr int32_t coast_cells = 192;
// The first column of land: the sea lies west of it.
constexpr int32_t coast_shore = coast_cells / 2;
// Where the first target stands: on the land, 160 world units from the
// shore, halfway down the map. A gunship circling it passes over the sea.
constexpr int32_t coast_target_x = coast_shore * 16 + 160;
constexpr int32_t coast_target_z = coast_cells * 16 / 2;

// The ground under the pointer when a player commands an attack on a unit:
// the unit's own position.
sim::ground_orders::Point point_on(const sim::unit_spawn::Slot& slot) {
    const std::array<uint32_t, 3> position = slot.unit->position;
    return {
        std::bit_cast<int32_t>(position[0]),
        std::bit_cast<int32_t>(position[1]),
        std::bit_cast<int32_t>(position[2])
    };
}

double distance_to(const sim::unit_spawn::Slot& slot, const sim::ground_orders::Point& at) {
    const auto dx = static_cast<double>(slot.record.position.x - at[0]) / fixed_one;
    const auto dz = static_cast<double>(slot.record.position.z - at[2]) / fixed_one;
    return std::hypot(dx, dz);
}

// Whole world units above the surface under a unit: the land, or the sea
// where the ground lies below it.
int32_t height_above_surface(Land& land, const sim::unit_spawn::Slot& slot) {
    const auto ground = land.match->map_height(
        static_cast<uint32_t>(slot.record.position.x), static_cast<uint32_t>(slot.record.position.z)
    );
    return slot.record.position.y / fixed_one - std::max<int32_t>(ground, land_sea_level);
}

// Commands an installed gunship, from out of reach to the north, at a unit of
// the other player with the unit's position as the command's point, and runs
// until the gunship has destroyed it.
sim::unit_spawn::Slot&
gunship_destroys(Land& land, std::string_view name, sim::unit_spawn::Slot& target) {
    auto& gunship = land.spawn(
        0,
        name,
        target.record.position.x / fixed_one,
        target.record.position.z / fixed_one - distant_start
    );
    land.run(2);
    const auto at = point_on(target);
    CHECK(land.match->issue_attack_command(gunship.unit_index, target.unit_index, false, &at));
    land.run(approach_ticks + hover_watch_ticks, [&] { return target.record.health == 0; });
    if (target.record.health != 0)
        throw std::runtime_error(std::string(name) + " did not destroy its target");
    return gunship;
}

// A gunship that has destroyed the unit it was commanded at stays in the
// air: its attack hands over to VTOL_SeekAttack at the command's point, and
// it circles that point at its cruise height, beyond its weapon's range and
// over land and sea alike, looking for the next unit, rather than settling to
// the ground.
void installed_gunship_seeks_after_its_kill(test::InstalledUnits& units, std::string_view name) {
    Land land(units, coast_cells, coast_cells, coast_shore);
    auto& target = land.target(coast_target_x, coast_target_z);
    const auto at = point_on(target);
    auto& gunship = gunship_destroys(land, name, target);
    land.run(hover_watch_ticks, [&] { return head_is(gunship, seek_attack_order); });
    CHECK(head_is(gunship, seek_attack_order));
    const auto cruise = land.units.definitions[gunship.record.type_index].cruise_altitude;
    const auto orbit = weapon_range(land, gunship) + seek_orbit_margin;
    int32_t lowest = cruise;
    double farthest = 0.0;
    uint32_t watched = 0;
    land.run(seek_watch_ticks, [&] {
        ++watched;
        lowest = std::min(lowest, height_above_surface(land, gunship));
        farthest = std::max(farthest, distance_to(gunship, at));
        return !head_is(gunship, seek_attack_order) ||
               (gunship.record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) != airborne;
    });
    if (watched != seek_watch_ticks || lowest < cruise / 2 || farthest > orbit + seek_arrival)
        throw std::runtime_error(
            std::string(name) + " seeking for " + std::to_string(watched) + " ticks came down to " +
            std::to_string(lowest) + " above the surface at cruise height " +
            std::to_string(cruise) + " and strayed " + std::to_string(farthest) +
            " from its point, circling at " + std::to_string(orbit)
        );
}

// A gunship that has destroyed the unit it was commanded at goes after the
// next unit it finds while it circles, and destroys it too, without coming
// down to the ground in between.
void installed_gunship_moves_on_after_its_kill(test::InstalledUnits& units, std::string_view name) {
    Land land(units, coast_cells, coast_cells, coast_shore);
    auto& target = land.target(coast_target_x, coast_target_z);
    auto& next = land.spawn(1, "CORAK", coast_target_x, coast_target_z + next_target_offset);
    land.run(2);
    next.record.flags &= ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK);
    auto& gunship = gunship_destroys(land, name, target);
    const auto cruise = land.units.definitions[gunship.record.type_index].cruise_altitude;
    int32_t lowest = cruise;
    land.run(next_target_ticks, [&] {
        lowest = std::min(lowest, height_above_surface(land, gunship));
        return next.record.health == 0;
    });
    if (next.record.health != 0 || lowest < cruise / 2)
        throw std::runtime_error(
            std::string(name) + " left the next unit with " + std::to_string(next.record.health) +
            " health and came down to " + std::to_string(lowest) +
            " above the surface at cruise height " + std::to_string(cruise)
        );
}

void installed_attack_commands(const AssetStore& store) {
    test::InstalledUnits units(
        store,
        {"CORAK",
         "ARMPW",
         "ARMZEUS",
         "CORPYRO",
         "ARMHAM",
         "CORTHUD",
         "ARMROCK",
         "CORSTORM",
         "ARMFLASH",
         "CORGATOR",
         "ARMSTUMP",
         "CORRAID",
         "ARMBRAWL",
         "CORAPE",
         "ARMMH",
         "CORMH"}
    );
    // Kbots with lasers, lightning, flame, plasma cannon and rockets, and a
    // vehicle: all brake within the stretch the command finds them at.
    for (const auto* name :
         {"ARMPW", "CORAK", "ARMZEUS", "CORPYRO", "ARMHAM", "CORTHUD", "CORSTORM", "ARMFLASH"})
        installed_attack_command_stops_a_move(units, name);
    for (const auto* name :
         {"ARMPW",
          "CORAK",
          "ARMZEUS",
          "ARMHAM",
          "CORTHUD",
          "ARMROCK",
          "CORSTORM",
          "ARMFLASH",
          "CORGATOR",
          "ARMSTUMP",
          "CORRAID"})
        installed_attack_command_closes_to_range(units, name);
    // Gunships, which hover to attack and look for the next unit once their
    // target is destroyed.
    for (const auto* name : {"ARMBRAWL", "CORAPE"}) {
        installed_gunship_hovers_facing_its_target(units, name);
        installed_gunship_seeks_after_its_kill(units, name);
        installed_gunship_moves_on_after_its_kill(units, name);
    }
    // Missile hovercraft, whose missiles launch vertically and do not track.
    for (const auto* name : {"ARMMH", "CORMH"})
        installed_vertical_missile_comes_down_where_aimed(units, name);
}

} // namespace

int main(int argc, char** argv) {
    if (test::game_data_requested(argc, argv)) {
        const auto assets = test::require_game_assets("the installed attackers");
        try {
            installed_attack_commands(assets);
        } catch (const std::exception& error) {
            std::cerr << "installed attack commands: " << error.what() << '\n';
            return 1;
        }
        std::cout << "installed attack commands passed\n";
        return 0;
    }
    try {
        attack_command_replaces_the_move_under_way();
        queued_attack_command_follows_the_orders();
        attack_command_takes_no_way_back();
        attack_command_overrides_standing_orders();
        attack_command_refuses_the_unit_itself();
        computer_squad_attack_replaces_the_move_under_way();
        attack_command_keeps_its_point();
        attack_command_on_its_own_side_keeps_no_target();
    } catch (const std::exception& error) {
        std::cerr << "attack command: " << error.what() << '\n';
        return 1;
    }
    std::cout << "attack command passed\n";
    return 0;
}
