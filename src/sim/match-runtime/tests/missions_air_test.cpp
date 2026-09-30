// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "../src/tick_internal.hpp"
#include <array>
#include <bit>
#include <iostream>
#include <stdexcept>
#include <string>

using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);   \
    } while (false)

namespace {
struct Services : sim::match_runtime::OfflineServices {
    std::vector<uint32_t> sounds;

    void command_sound(sim::unit_spawn::Slot&, uint32_t category) override {
        sounds.push_back(category);
    }

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

struct Scenario : sim::scenario::DefinitionHost {
    int32_t gravity{112};

    int32_t integer(std::string_view key, int32_t fallback) override {
        return key == "gravity" ? gravity : fallback;
    }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

constexpr uint8_t fighter_type = 1;
constexpr uint8_t tank_type = 2;
constexpr uint8_t pad_type = 3;
constexpr size_t type_count = 4;

constexpr uint32_t wait_attack = 0x100e8;
constexpr uint32_t wait_goal_or_cancel = 0xe2;

struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values;
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::shared_ptr<formats::objects3d::Model> model;
    std::shared_ptr<formats::cob::CobProgram> script;
    std::array<sim::unit_spawn::LoadedType, type_count> loaded{};
    std::array<sim::unit_spawn::Type, type_count> types{};
    std::array<data::unit_definitions::UnitDefinition, type_count> defs{};
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::vector<sim::spatial_state::Plot> collision_plots;
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    std::array<uint8_t, 1> yard{4};
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;

    explicit Fixture(int32_t gravity = 112) {
        scenario.gravity = gravity;
        map.attribute_width = map.attribute_height = 64;
        map.attributes.resize(64 * 64);
        terrain_values.resize(64 * 64);
        collision_plots.resize(64 * 64);
        masks[0].width = masks[0].height = 1;
        masks[0].pixels = {1};
        model = std::make_shared<formats::objects3d::Model>();
        model->objects.resize(1);
        model->objects[0].name = "root";
        script = std::make_shared<formats::cob::CobProgram>();
        script->code = {sim::script_vm::opcode::return_};
        script->scripts = {{"Create", 0}};
        script->entry_points = {0};
        script->piece_names = {"root"};
        weapons.install_tdf_section(1, "AIRGUN", "0.1");
        weapons.install_target_fields(
            1,
            "300",
            "1",
            "0",
            "0",
            "0",
            "0",
            "10",
            "100",
            "",
            "1",
            "0",
            "0",
            "0",
            {},
            {},
            {},
            {},
            {},
            {},
            {},
            "1",
            "1",
            "1"
        );
        for (size_t i = 1; i < type_count; ++i) {
            auto& type = types[i];
            type.simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            type.simulation.maximum_health = 100;
            type.footprint_x = type.footprint_z = 1;
            type.bm_code = 1;
            type.model = reinterpret_cast<uintptr_t>(model.get());
            type.cob = reinterpret_cast<uintptr_t>(script.get());
            loaded[i].model = model;
            loaded[i].script = script;
            auto& def = defs[i];
            def.sight_distance = 400;
            def.acceleration_fixed = 0x10000;
            def.brake_rate_fixed = 0x10000;
            def.max_velocity_fixed = 4 * 0x10000;
            def.turn_rate = 1024;
            def.weapon1 = "AIRGUN";
            fields[i].definition = &def;
            fields[i].yard_mask = yard;
            fields[i].runtime_metadata = &metadata;
            fields[i].target_masks = &target_masks;
            fields[i].movement_class = 0;
        }
        types[fighter_type].simulation.flags |= OA_UNIT_DEF_FLAG_CAN_FLY;
        defs[fighter_type].cruise_altitude = 100;
        defs[fighter_type].attack_run_length = 50;
        types[pad_type].simulation.flags |= OA_UNIT_DEF_FLAG_BUILDER | OA_UNIT_DEF_FLAG_IS_AIRBASE;
        for (size_t i = 1; i < type_count; ++i)
            loaded[i].type = types[i];
        sim::match_runtime::OfflineInputs input{
            map, loaded, types, fields,    weapons, terrain_values,  masks, 8, 8, 4,    2,
            0,   30,     1,     &scenario, {},      collision_plots, {},    0, 0, 0.0F, {}
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        match->state().unit_defs[fighter_type].abilities = OA_UNIT_DEF_ABILITY_CAN_GUARD;
        match->configure_strategic_environment({0, 0.5f, 0});
        for (uint8_t player = 0; player < 2; ++player) {
            match->simulation().players[player].present = true;
            match->simulation().players[player].status = player == 0 ? 1 : 2;
            std::array<uint8_t, 10> allies{};
            allies[player] = 1;
            match->configure_player_alliances(player, allies);
        }
    }

    sim::unit_spawn::Slot& spawn(uint8_t player, uint8_t type, int32_t x, int32_t z) {
        auto* slot = match->create(
            {player,
             type,
             {static_cast<uint32_t>(x) << 16, 0, static_cast<uint32_t>(z) << 16},
             true,
             1,
             0}
        );
        CHECK(slot && slot->unit->object_present);
        slot->unit->flags |= OA_UNIT_FLAG_LIVE;
        return *slot;
    }

    uint32_t
    dispatch(sim::unit_spawn::Slot& s, sim::simulation_state::Order& order, uint32_t events) {
        sim::match_runtime::TickHost host(*match);
        return host.dispatch_mission(match->state(), s.record, order, events);
    }

    // An attack order from `from` at `to` rewritten to `kind`.
    sim::simulation_state::Order&
    attack(sim::unit_spawn::Slot& from, sim::unit_spawn::Slot& to, uint8_t kind) {
        const std::array<uint32_t, 3> at = to.unit->position;
        const sim::ground_orders::Point point{
            std::bit_cast<int32_t>(at[0]),
            std::bit_cast<int32_t>(at[1]),
            std::bit_cast<int32_t>(at[2])
        };
        (void)match->issue_order(
            from.unit_index,
            sim::match_runtime::attack_chase_kind,
            true,
            to.unit_index,
            &point,
            0,
            0
        );
        auto* order = from.unit->primary;
        while (order && order->next)
            order = order->next;
        CHECK(order);
        order->kind = kind;
        return *order;
    }
};

sim::simulation_state::Order& step(sim::simulation_state::Order& order, uint32_t result) {
    if (result == 1)
        ++order.phase;
    return order;
}

void air_strike() {
    Fixture f;
    auto& bomber = f.spawn(0, fighter_type, 100, 100);
    auto& tank = f.spawn(1, tank_type, 300, 100);
    auto& order = f.attack(bomber, tank, sim::match_runtime::air_strike_kind);
    const uint32_t tick = f.match->simulation().tick;

    CHECK(f.dispatch(bomber, order, 0) == 1);
    step(order, 1);
    // 200 units out: inside the run-in distance, so a pass goal is set.
    order.wait_events = 0;
    CHECK(f.dispatch(bomber, order, 0) == 1);
    CHECK(order.wait_events == wait_goal_or_cancel);
    step(order, 1);
    order.wait_events = 0;
    CHECK(f.dispatch(bomber, order, 0x20) == 1);
    CHECK(order.wait_events == wait_attack);
    step(order, 1);
    order.wait_events = 0;
    CHECK(f.dispatch(bomber, order, 0) == 1);
    step(order, 1);
    CHECK(f.dispatch(bomber, order, 0x20) == 1);
    order.wait_events = 0;
    CHECK(f.dispatch(bomber, order, 0) == 2);
    CHECK(order.wait_events == (wait_attack | 1) && order.wake_tick == tick + 1);
    step(order, 1);
    order.phase = 5;
    order.wait_events = 0;
    CHECK(f.dispatch(bomber, order, 0) == 1);
    CHECK(order.wait_events == wait_goal_or_cancel);
    const auto& aim = bomber.record.weapons[0];
    CHECK(aim.target_a == 300 && aim.target_b == 100);
    order.phase = 6;
    CHECK(f.dispatch(bomber, order, 0) == 2);
    CHECK(order.phase == 3 && order.wait_events == wait_goal_or_cancel);

    // Damaged below three quarters with an active pad in reach: land there.
    auto& pad = f.spawn(0, pad_type, 150, 150);
    pad.record.state_flags |= 1;
    bomber.record.health = 70;
    order.phase = 6;
    CHECK(f.dispatch(bomber, order, 0) == 0);
    CHECK(
        bomber.unit->primary && bomber.unit->primary->kind == sim::ground_orders::vtol_landing_kind
    );
    CHECK(bomber.unit->primary->next == &order && order.wait_events == 0);

    // Target lost while allowed to fire: hand over to VTOL_SeekAttack.
    bomber.unit->flags |= 0x100000u;
    CHECK(!order.next);
    CHECK(f.dispatch(bomber, order, 0x8) == 5);
    CHECK(order.next && order.next->kind == sim::match_runtime::vtol_seek_attack_kind);
}

void air_strike_without_gravity() {
    Fixture f(0);
    auto& bomber = f.spawn(0, fighter_type, 100, 100);
    auto& tank = f.spawn(1, tank_type, 300, 100);
    auto& order = f.attack(bomber, tank, sim::match_runtime::air_strike_kind);
    order.phase = 4;
    CHECK(f.dispatch(bomber, order, 0) == 7);
}

void air_to_air() {
    Fixture f;
    auto& fighter = f.spawn(0, fighter_type, 100, 100);
    auto& enemy = f.spawn(1, fighter_type, 400, 400);
    auto& order = f.attack(fighter, enemy, sim::match_runtime::air_to_air_kind);
    const uint32_t tick = f.match->simulation().tick;
    CHECK(f.dispatch(fighter, order, 0) == 1);
    // The fighter starts landed, so the take-off climb's goal events (0xe0)
    // join the one-tick timer.
    CHECK(order.wait_events == 0xe1 && order.wake_tick == tick + 1);
    step(order, 1);
    order.wait_events = 0;
    const bool aligned = sim::unit_movement::facing_toward(
        enemy.record.position.x,
        enemy.record.position.z,
        fighter.record.position.x,
        fighter.record.position.z,
        fighter.record.heading
    );
    CHECK(f.dispatch(fighter, order, 0) == 2);
    CHECK(order.wait_events == (wait_attack | 1) && order.wake_tick == tick + 0x2d);
    order.wait_events = 0;
    const auto result = f.dispatch(fighter, order, 0x20);
    if (aligned) {
        CHECK(result == 2 && order.wait_events == 1);
    } else {
        CHECK(result == 0 && order.wait_events == 0);
        CHECK(fighter.unit->primary && fighter.unit->primary->kind == 48);
        CHECK(fighter.unit->primary->next == &order);
    }
    CHECK(f.dispatch(fighter, order, 0x10000) == 5);
}

void air_to_ground_hover() {
    Fixture f;
    auto& gunship = f.spawn(0, fighter_type, 100, 100);
    auto& tank = f.spawn(1, tank_type, 200, 100);
    auto& order = f.attack(gunship, tank, sim::match_runtime::air_to_ground_hover_kind);
    CHECK(f.dispatch(gunship, order, 0) == 1);
    step(order, 1);
    CHECK(f.dispatch(gunship, order, 0) == 1);
    CHECK(order.wait_events == wait_attack);
    step(order, 1);
    order.wait_events = 0;
    CHECK(f.dispatch(gunship, order, 0) == 1);
    CHECK(order.wait_events == wait_attack);
    const auto& aim = gunship.record.weapons[0];
    CHECK(aim.target_a == tank.unit_index && aim.target_b == OA_UNIT_TARGET_IS_UNIT);
    step(order, 1);
    order.wait_events = 0;
    const auto result = f.dispatch(gunship, order, 0);
    CHECK(result == 2);
    CHECK(order.wait_events == wait_attack || order.wait_events == (wait_attack | 0x1000));
    CHECK(order.phase == 3);
    order.phase = 4;
    CHECK(f.dispatch(gunship, order, 0) == 7);
}

void seek_attack() {
    Fixture f;
    auto& fighter = f.spawn(0, fighter_type, 100, 100);
    auto& order = f.match->insert_ground_order(fighter.unit_index, 62);
    CHECK(f.dispatch(fighter, order, 0) == 1);
    step(order, 1);
    order.wait_events = 0;
    const uint32_t tick = f.match->simulation().tick;
    CHECK(f.dispatch(fighter, order, 0) == 2);
    CHECK((order.wait_events & 0xe1) == 0xe1);
    CHECK(order.wake_tick >= tick + 0x1e && order.wake_tick < tick + 0x3c);
    CHECK(f.dispatch(fighter, order, 0x40) == 5);
}

void seek_guard() {
    Fixture f;
    auto& fighter = f.spawn(0, fighter_type, 100, 100);
    auto& order = f.match->insert_ground_order(fighter.unit_index, 63);
    CHECK(f.dispatch(fighter, order, 0) == 1);
    step(order, 1);
    order.wait_events = 0;
    CHECK(f.dispatch(fighter, order, 0) == 2);
    CHECK((order.wait_events & 0xf9) == 0xf9);
    auto& ally = f.spawn(0, tank_type, 150, 120);
    order.wait_events = 0;
    CHECK(f.dispatch(fighter, order, 0) == 3);
    auto* guard = fighter.unit->primary;
    CHECK(guard && guard->kind == 49 && guard->next == &order);
    (void)ally;
}

void evade() {
    Fixture f;
    auto& fighter = f.spawn(0, fighter_type, 100, 100);
    auto& enemy = f.spawn(1, fighter_type, 400, 400);
    auto& lone = f.match->insert_ground_order(fighter.unit_index, 48);
    CHECK(f.dispatch(fighter, lone, 0) == 5);
    auto& order = f.attack(fighter, enemy, 48);
    CHECK(f.dispatch(fighter, order, 0) == 1 && order.wait_events == wait_attack);
    step(order, 1);
    order.wait_events = 0;
    CHECK(f.dispatch(fighter, order, 0) == 1 && order.wait_events == wait_attack);
    step(order, 1);
    CHECK(f.dispatch(fighter, order, 0) == 5);
}
} // namespace

int main() {
    air_strike();
    air_strike_without_gravity();
    air_to_air();
    air_to_ground_hover();
    seek_attack();
    seek_guard();
    evade();
    std::cout << "air attack missions passed\n";
}
