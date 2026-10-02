// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/simulation_state.hpp"
#include <cstdint>
#include <functional>
#include <memory>
#include <string>
#include <vector>
using namespace oa::sim::simulation_state;
using oa::Player;
using oa::Unit;
using oa::World;

// A World over caller-sized unit and type tables; slot 0 and type 0 are reserved.
struct TestWorld {
    std::unique_ptr<World> world = std::make_unique<World>();
    std::vector<Unit> units;
    std::vector<oa::UnitDef> defs;
    std::vector<OrderQueue> orders;

    /// Builds a world over unit and type tables of the given sizes, each unit
    /// holding its slot as its id.
    ///
    /// @param slots unit slots, slot 0 included
    /// @param types unit types, type 0 included
    TestWorld(std::size_t slots, std::size_t types) : units(slots), defs(types), orders(slots) {
        world->units = units.data();
        world->unit_slot_count = static_cast<uint32_t>(slots);
        world->unit_defs = defs.data();
        world->unit_def_count = static_cast<uint32_t>(types);
        for (std::size_t i = 0; i < slots; ++i)
            units[i].id = static_cast<uint16_t>(i);
    }

    /// Returns the world.
    ///
    /// @return the world
    World& operator*() noexcept { return *world; }

    /// Returns a player record.
    ///
    /// @param index player index 0..9
    /// @return the record
    Player& player(std::size_t index) noexcept { return world->game.players[index]; }

    /// Gives a player the unit slots [first, first + count).
    ///
    /// @param index player index 0..9
    /// @param first first slot
    /// @param count number of slots, at least 1
    void range(std::size_t index, std::size_t first, std::size_t count) {
        player(index).first_unit = oa::oa_ref_from_index(static_cast<uint32_t>(first));
        player(index).last_unit = oa::oa_ref_from_index(static_cast<uint32_t>(first + count - 1));
    }
};

struct Fixture final : Host {
    std::vector<std::string> calls;
    std::vector<uint32_t> events;
    std::function<uint32_t(Unit&, Order&)> handler;
    uint32_t random_value{};
    std::vector<uint32_t> random_values;
    std::size_t random_cursor{};
    int32_t terrain{};

    /// Records a mission step and its events, and runs the test's handler.
    ///
    /// @param u unit the order belongs to
    /// @param o the order
    /// @param e events that woke it
    /// @return the handler's result, or 3 (wait) without one
    uint32_t dispatch_mission(World&, Unit& u, Order& o, uint32_t e) override {
        calls.push_back("dispatch");
        events.push_back(e);
        return handler ? handler(u, o) : 3;
    }

    /// Records a draw and returns the next queued value, or the fixed one once the
    /// queue runs out.
    ///
    /// @param range exclusive upper limit, recorded
    /// @return the value
    uint32_t random_bounded(uint32_t range) override {
        calls.push_back("random" + std::to_string(range));
        if (random_cursor < random_values.size())
            return random_values[random_cursor++];
        return random_value;
    }

    /// Records a default mission request.
    void queue_default_mission(World&, Unit&) override { calls.push_back("idle"); }

    /// Records a cleared weapon target.
    ///
    /// @param n weapon slot
    void clear_weapon_target(Unit&, uint32_t n) override {
        calls.push_back("weapon" + std::to_string(n));
    }

    /// Records a destroyed order.
    void destroy_order(Unit&, Order&) override { calls.push_back("destroy"); }

    /// Records a script step.
    void tick_script(Unit&, uint32_t) override { calls.push_back("script"); }

    // Each unit a scaled_damage call hit, with its amount and kind.
    struct Damage {
        uint16_t unit{};
        int32_t amount{};
        uint32_t kind{};
    };

    std::vector<Damage> damaged;

    /// Records a damage call with its unit, amount and kind.
    ///
    /// @param unit unit hit
    /// @param amount damage before scaling
    /// @param kind damage kind
    void apply_scaled_damage(Unit& unit, int32_t amount, uint32_t kind) override {
        calls.push_back("damage");
        damaged.push_back({unit.id, amount, kind});
    }

    /// Records a regeneration step.
    void regenerate_health(Unit&) override { calls.push_back("recover"); }

    /// Records a movement tick.
    void movement_tick(Unit&) override { calls.push_back("movement"); }

    /// Records a terrain query and returns the fixed height.
    ///
    /// @return the fixture's terrain height
    int32_t terrain_height_under(Unit&) override {
        calls.push_back("terrain");
        return terrain;
    }

    /// Records a ground fit.
    void settle_on_ground(Unit&) override { calls.push_back("ground"); }

    /// Records a kill.
    void kill_unit(Unit&, uint8_t) override { calls.push_back("owner"); }

    /// Records the wind generator update that precedes a unit's tick.
    void update_wind_generator(Unit&) override { calls.push_back("pre"); }

    /// Records the weapon tick of a unit simulated here.
    void tick_weapon_aim(Unit&) override { calls.push_back("local"); }

    /// Records a local player's tick.
    void local_player_ticked(Player&) override { calls.push_back("player"); }

    /// Records a key-state query.
    ///
    /// @return false: no key is held
    bool is_key_down(uint32_t) override {
        calls.push_back("key");
        return false;
    }

    /// Records the periodic viewpoint selection.
    void select_next_viewpoint_unit() override { calls.push_back("periodic1"); }

    /// Records the periodic follow.
    void follow_next_selected(uint32_t) override { calls.push_back("periodic2"); }
};
