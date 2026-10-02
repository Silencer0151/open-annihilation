// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/sim/unit_spawn/spawn.hpp"
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>
#include <iostream>
#include <cstdint>
using namespace oa::sim::unit_spawn;

// A World with caller-sized unit and type tables and the native spawn tables.
struct SpawnWorld {
    std::unique_ptr<oa::World> world = std::make_unique<oa::World>();
    std::vector<oa::Unit> units;
    std::vector<oa::UnitDef> defs;
    std::vector<Type> types;
    std::vector<SlotAssets> assets;
    std::array<PlayerSetupState, 10> setups{};
    Tables tables;

    /// Builds a world over unit and type tables of the given sizes and the spawn
    /// tables over them, each unit holding its slot as its id.
    ///
    /// @param slots unit slots, slot 0 included
    /// @param type_count unit types, type 0 included
    SpawnWorld(std::size_t slots, std::size_t type_count)
        : units(slots), defs(type_count), types(type_count), assets(slots) {
        world->units = units.data();
        world->unit_slot_count = static_cast<uint32_t>(slots);
        world->unit_defs = defs.data();
        world->unit_def_count = static_cast<uint32_t>(type_count);
        tables = {types, assets, setups};
        for (std::size_t i = 0; i < slots; ++i)
            units[i].id = static_cast<uint16_t>(i);
    }

    /// Returns the world.
    ///
    /// @return the world
    oa::World& operator*() noexcept { return *world; }

    /// Returns a player record.
    ///
    /// @param index player index 0..9
    /// @return the record
    oa::Player& player(std::size_t index) noexcept { return world->game.players[index]; }

    /// Reloads the canonical type records from the runtime types after edits.
    void load_types() {
        for (std::size_t i = 0; i < types.size(); ++i)
            load_unit_def(types[i], defs[i]);
    }

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

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)

struct Fixture : Host {
    std::vector<std::string> calls;

    /// Records a draw and returns 7.
    ///
    /// @param n exclusive upper limit, recorded
    /// @return 7, or 0 for a zero limit
    uint32_t random_bounded(uint32_t n) override {
        calls.push_back("rng" + std::to_string(n));
        return n ? 7 : 0;
    }

    /// Records a weapon target reset.
    ///
    /// @param n weapon slot
    void init_weapon_target(oa::Unit&, uint32_t n) override {
        calls.push_back("resetA" + std::to_string(n));
    }

    /// Records a weapon stand-down.
    ///
    /// @param n weapon slot
    void reset_weapon_targets(oa::Unit&, uint8_t n) override {
        calls.push_back("resetB" + std::to_string(n));
    }

    /// Records the economy setup.
    void init_unit_economy(oa::Unit&, uint8_t) override { calls.push_back("economy"); }

    /// Records the squad assignment.
    void assign_squad(oa::Unit&, uint32_t) override { calls.push_back("spatial"); }

    /// Records a plain model instance.
    ///
    /// @return handle 100
    AssetHandle create_model_instance(AssetHandle) override {
        calls.push_back("plain");
        return 100;
    }

    /// Records the plain model's owner check.
    void model_owner(oa::Unit&) override { calls.push_back("owner"); }

    /// Records a script allocation.
    ///
    /// @return handle 200
    AssetHandle allocate_script() override {
        calls.push_back("allocate");
        return 200;
    }

    /// Records the COB load.
    void load_script_state(AssetHandle, AssetHandle) override { calls.push_back("load"); }

    /// Records a scripted model instance.
    ///
    /// @return handle 300
    AssetHandle create_scripted_model(AssetHandle, AssetHandle, oa::Unit&) override {
        calls.push_back("scriptmodel");
        return 300;
    }

    /// Records the script and model binding.
    void bind_script_model(AssetHandle, AssetHandle) override { calls.push_back("bind"); }

    /// Records the Create call.
    void call_script_create(AssetHandle) override { calls.push_back("Create"); }

    /// Records the model reset.
    void model_reset(oa::Unit&) override { calls.push_back("modelreset"); }

    /// Records the weapon setup.
    void initialize_weapons(oa::Unit&) override { calls.push_back("weapons"); }

    /// Records the extraction rate setup.
    void initialize_extraction_rate(oa::Unit&) override { calls.push_back("visibility"); }

    /// Records a movement object.
    ///
    /// @return handle 400
    AssetHandle create_movement(oa::Unit&) override {
        calls.push_back("movement");
        return 400;
    }

    /// Records the height fit.
    void fit_spawn_height(oa::Unit&) override { calls.push_back("height"); }

    /// Records the map registration.
    void register_occupancy(oa::Unit&) override { calls.push_back("register"); }

    /// Records the creation notice.
    void notify_created(oa::Unit&) override { calls.push_back("notify"); }

    /// Records the finished-building notice.
    void notify_finished(oa::Unit&) override { calls.push_back("finished"); }

    /// Records an activation, which unit creation always makes with both bits set.
    ///
    /// @param a activation bit
    /// @param b set rather than cleared
    void set_activation(oa::Unit&, bool a, bool b) override {
        CHECK(a && b);
        calls.push_back("activate");
    }

    /// Records the sight stamp.
    void update_sight(oa::Unit&) override { calls.push_back("finalize"); }

    /// Records the scenario notice.
    void notify_scenario_created(oa::Unit&) override { calls.push_back("scenario"); }
};
