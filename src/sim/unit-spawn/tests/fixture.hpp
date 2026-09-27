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

    oa::World& operator*() noexcept { return *world; }

    oa::Player& player(std::size_t index) noexcept { return world->game.players[index]; }

    // Canonical type records are reloaded from the runtime types after edits.
    void load_types() {
        for (std::size_t i = 0; i < types.size(); ++i)
            load_unit_def(types[i], defs[i]);
    }

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

    uint32_t random_bounded(uint32_t n) override {
        calls.push_back("rng" + std::to_string(n));
        return n ? 7 : 0;
    }

    void init_weapon_target(oa::Unit&, uint32_t n) override {
        calls.push_back("resetA" + std::to_string(n));
    }

    void reset_weapon_targets(oa::Unit&, uint8_t n) override {
        calls.push_back("resetB" + std::to_string(n));
    }

    void init_unit_economy(oa::Unit&, uint8_t) override { calls.push_back("economy"); }

    void assign_squad(oa::Unit&, uint32_t) override { calls.push_back("spatial"); }

    AssetHandle create_model_instance(AssetHandle) override {
        calls.push_back("plain");
        return 100;
    }

    void model_owner(oa::Unit&) override { calls.push_back("owner"); }

    AssetHandle allocate_script() override {
        calls.push_back("allocate");
        return 200;
    }

    void load_script_state(AssetHandle, AssetHandle) override { calls.push_back("load"); }

    AssetHandle create_scripted_model(AssetHandle, AssetHandle, oa::Unit&) override {
        calls.push_back("scriptmodel");
        return 300;
    }

    void bind_script_model(AssetHandle, AssetHandle) override { calls.push_back("bind"); }

    void call_script_create(AssetHandle) override { calls.push_back("Create"); }

    void model_reset(oa::Unit&) override { calls.push_back("modelreset"); }

    void initialize_weapons(oa::Unit&) override { calls.push_back("weapons"); }

    void initialize_extraction_rate(oa::Unit&) override { calls.push_back("visibility"); }

    AssetHandle create_movement(oa::Unit&) override {
        calls.push_back("movement");
        return 400;
    }

    void fit_spawn_height(oa::Unit&) override { calls.push_back("height"); }

    void register_occupancy(oa::Unit&) override { calls.push_back("register"); }

    void notify_created(oa::Unit&) override { calls.push_back("notify"); }

    void notify_finished(oa::Unit&) override { calls.push_back("finished"); }

    void set_activation(oa::Unit&, bool a, bool b) override {
        CHECK(a && b);
        calls.push_back("activate");
    }

    void update_sight(oa::Unit&) override { calls.push_back("finalize"); }

    void notify_scenario_created(oa::Unit&) override { calls.push_back("scenario"); }
};
