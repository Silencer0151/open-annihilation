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

    TestWorld(std::size_t slots, std::size_t types) : units(slots), defs(types), orders(slots) {
        world->units = units.data();
        world->unit_slot_count = static_cast<uint32_t>(slots);
        world->unit_defs = defs.data();
        world->unit_def_count = static_cast<uint32_t>(types);
        for (std::size_t i = 0; i < slots; ++i)
            units[i].id = static_cast<uint16_t>(i);
    }

    World& operator*() noexcept { return *world; }

    Player& player(std::size_t index) noexcept { return world->game.players[index]; }

    // Inclusive unit range [first, first + count) for a player.
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

    uint32_t dispatch_mission(World&, Unit& u, Order& o, uint32_t e) override {
        calls.push_back("dispatch");
        events.push_back(e);
        return handler ? handler(u, o) : 3;
    }

    uint32_t random_bounded(uint32_t range) override {
        calls.push_back("random" + std::to_string(range));
        if (random_cursor < random_values.size())
            return random_values[random_cursor++];
        return random_value;
    }

    void queue_default_mission(World&, Unit&) override { calls.push_back("idle"); }

    void clear_weapon_target(Unit&, uint32_t n) override {
        calls.push_back("weapon" + std::to_string(n));
    }

    void destroy_order(Unit&, Order&) override { calls.push_back("destroy"); }

    void tick_script(Unit&, uint32_t) override { calls.push_back("script"); }

    // Each unit a scaled_damage call hit, with its amount and kind.
    struct Damage {
        uint16_t unit{};
        int32_t amount{};
        uint32_t kind{};
    };

    std::vector<Damage> damaged;

    void apply_scaled_damage(Unit& unit, int32_t amount, uint32_t kind) override {
        calls.push_back("damage");
        damaged.push_back({unit.id, amount, kind});
    }

    void regenerate_health(Unit&) override { calls.push_back("recover"); }

    void movement_tick(Unit&) override { calls.push_back("movement"); }

    int32_t terrain_height_under(Unit&) override {
        calls.push_back("terrain");
        return terrain;
    }

    void settle_on_ground(Unit&) override { calls.push_back("ground"); }

    void kill_unit(Unit&, uint8_t) override { calls.push_back("owner"); }

    void update_wind_generator(Unit&) override { calls.push_back("pre"); }

    void tick_weapon_aim(Unit&) override { calls.push_back("local"); }

    void local_player_ticked(Player&) override { calls.push_back("player"); }

    bool is_key_down(uint32_t) override {
        calls.push_back("key");
        return false;
    }

    void select_next_viewpoint_unit() override { calls.push_back("periodic1"); }

    void follow_next_selected(uint32_t) override { calls.push_back("periodic2"); }
};
