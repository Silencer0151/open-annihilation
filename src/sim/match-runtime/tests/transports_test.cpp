// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/air/flight.hpp"
#include "oa/base/game_math.hpp"
#include "oa/sim/match_runtime.hpp"
#include "oa/sim/gameplay_input/order_cursor.hpp"
#include "oa/sim/unit_script.hpp"
#include "oa/test/game_assets.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <vector>
using namespace oa;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string("line ") + std::to_string(__LINE__) + ": " #x);   \
    } while (false)

// Transports: an air transport picks a unit up, carries it on its link piece
// and sets it down at an unload point; a hover transport does the same through
// its TransportPickup and TransportDrop scripts. Run with --data, the same
// runs repeat with the installed game's ARMATLAS, ARMTHOVR and ARMPW models
// and scripts.
namespace {
namespace op = sim::script_vm::opcode;
constexpr uint8_t vtol_pickup = 57, vtol_unload = 65, be_carried = 11, ground_pickup = 20,
                  ground_unload = 21;
constexpr uint16_t air_transport_type = 1, cargo_type = 2, heavy_type = 3, hover_transport_type = 4,
                   gun_type = 5, flak_type = 6;
constexpr size_t type_count = 7;
constexpr int32_t map_cells = 64;
constexpr uint16_t sight_cells = 5; // 32-unit sight cells mapped around a unit
constexpr uint32_t speech_failed = 7;
constexpr uint32_t speech_loading = 0x0c;
constexpr uint32_t speech_unloaded = 0x0d;
constexpr uint8_t order_retry = 0x80; // order flags, set when a mission returns 9
constexpr int32_t one = 1 << 16;      // one world unit, 16.16

struct Services : sim::match_runtime::OfflineServices {
    std::vector<std::pair<uint16_t, uint32_t>> speech;
    sim::match_runtime::Match* match{};

    void command_sound(sim::unit_spawn::Slot& slot, uint32_t category) override {
        speech.emplace_back(slot.unit_index, category);
    }

    size_t spoken(uint16_t unit, uint32_t category) const {
        size_t count = 0;
        for (const auto& [who, what] : speech)
            count += who == unit && what == category;
        return count;
    }

    bool spoke(uint16_t unit, uint32_t category) const { return spoken(unit, category) != 0; }

    void activation_sound(sim::unit_spawn::Slot&, sim::unit_activation::Sound) override {}

    void attachment_notification(sim::unit_spawn::Slot&, uint32_t) override {}

    void refresh_selected_unit(sim::unit_spawn::Slot&) override {}

    void emit_sfx(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void explode_piece(sim::unit_spawn::Slot&, uint32_t, int32_t) override {}

    void
    attach_unit(sim::unit_spawn::Slot& slot, int32_t target, int32_t piece, int32_t mode) override {
        match->script_attach_unit(slot.unit_index, target, piece, mode);
    }

    void drop_unit(sim::unit_spawn::Slot& slot, int32_t target) override {
        match->script_drop_unit(slot.unit_index, target);
    }

    void refresh_plot_height_range(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}

    void notify_object_footprint_removed(sim::spatial_state::Unit&, uint32_t) override {}

    void notify_footprint_changed(std::array<int16_t, 2>, std::array<int16_t, 2>) override {}
};

struct Scenario : sim::scenario::DefinitionHost {
    int32_t integer(std::string_view, int32_t fallback) override { return fallback; }

    std::optional<std::string> text(std::string_view) override { return std::nullopt; }
};

struct Function {
    const char* name;
    std::vector<uint32_t> code;
};

// Function bodies may jump to `end_label`, the offset of their own final word
// pair; assemble resolves it once the function's entry is known.
constexpr uint32_t end_label = 0xffffffffu;

std::shared_ptr<formats::cob::CobProgram> assemble(const std::vector<Function>& functions) {
    auto cob = std::make_shared<formats::cob::CobProgram>();
    for (const auto& function : functions) {
        const auto entry = static_cast<uint32_t>(cob->code.size());
        cob->scripts.push_back({function.name, entry});
        cob->entry_points.push_back(entry);
        for (const auto word : function.code)
            cob->code.push_back(
                word == end_label ? entry + static_cast<uint32_t>(function.code.size()) - 3 : word
            );
    }
    cob->header.script_count = static_cast<uint32_t>(functions.size());
    cob->header.static_variable_count = 2;
    cob->piece_names = {"base", "link"};
    return cob;
}

const std::vector<uint32_t> finish{op::push_constant, 0, op::return_};

std::vector<uint32_t> join(std::initializer_list<std::vector<uint32_t>> parts) {
    std::vector<uint32_t> code;
    for (const auto& part : parts)
        code.insert(code.end(), part.begin(), part.end());
    return code;
}

using Value = sim::unit_script::UnitValue;

uint32_t value(Value selector) {
    return static_cast<uint32_t>(selector);
}

// get <selector>(<operand pushed by `argument`>, 0, 0, 0)
std::vector<uint32_t> get(Value selector, std::vector<uint32_t> argument) {
    return join(
        {{op::push_constant, value(selector)},
         argument,
         {op::push_constant, 0, op::push_constant, 0, op::push_constant, 0, op::get}}
    );
}

std::vector<uint32_t> set_busy(uint32_t on) {
    return {op::push_constant, value(Value::busy), op::push_constant, on, op::set_unit_value};
}

std::vector<uint32_t> sleep_ms(uint32_t milliseconds) {
    return {op::push_constant, milliseconds, op::sleep};
}

std::vector<uint32_t> count_in_static(uint32_t index) {
    return {op::push_static, index, op::push_constant, 1, op::add, op::pop_static, index};
}

// attach-unit of the unit id argument to the link piece.
const std::vector<uint32_t> attach_argument{
    op::push_local, 0, op::push_constant, 1, op::push_constant, 0, op::attach_unit
};

// A query answering statics 0 and 1 in locals 0 and 1.
const std::vector<uint32_t> answer_statics{
    op::push_static,
    0,
    op::pop_local,
    0,
    op::push_static,
    1,
    op::pop_local,
    1,
    op::push_constant,
    0,
    op::return_
};

// Skips to the end unless the XZ distance between the two packed points is
// below `reach` world units.
std::vector<uint32_t>
unless_within(std::vector<uint32_t> first, std::vector<uint32_t> second, uint32_t reach) {
    return join(
        {{op::push_constant, value(Value::xz_hypot)},
         first,
         second,
         {op::subtract, op::push_constant, 0, op::push_constant, 0, op::push_constant, 0, op::get},
         {op::push_constant, reach << 16, op::less, op::jump_if_false, end_label}}
    );
}

std::array<uint32_t, 3> at(int32_t x, int32_t z) {
    return {static_cast<uint32_t>(x) << 16, 0, static_cast<uint32_t>(z) << 16};
}

sim::ground_orders::Point point(int32_t x, int32_t z) {
    return {x << 16, 0, z << 16};
}

int32_t world_x(const oa::Unit& unit) {
    return unit.position.x >> 16;
}

int32_t world_z(const oa::Unit& unit) {
    return unit.position.z >> 16;
}

// XZ distance in world units.
double planar(const oa::Unit& unit, int32_t x, int32_t z) {
    return std::hypot(
        (unit.position.x - (x << 16)) / 65536.0, (unit.position.z - (z << 16)) / 65536.0
    );
}

// Statics 0 and 1 of a unit's script, through its answer_statics query.
std::array<int32_t, 4> statics_of(sim::match_runtime::Match& match, uint16_t unit) {
    std::array<int32_t, 4> values{};
    CHECK(match.instance(unit)->script()->query("Statics", values));
    return values;
}

uint8_t kind_of(const sim::unit_spawn::Slot& slot) {
    return slot.unit->primary ? slot.unit->primary->kind : 0;
}

// Whether the unit is on a spatial bucket chain, where area searches find it.
bool in_buckets(sim::match_runtime::Match& match, uint16_t index) {
    match.prepare_spatial_state();
    const auto& world = match.spatial();
    const auto chained = [&](const sim::spatial_state::Bucket& bucket) {
        size_t steps = 0;
        for (auto id = bucket.head;
             id != sim::spatial_state::no_unit && steps++ < world.units.size();
             id = world.units[id].next_in_bucket)
            if (id == index)
                return true;
        return false;
    };
    for (const auto& bucket : world.buckets)
        if (chained(bucket))
            return true;
    return chained(world.outside_bucket);
}

// The map cell a footprint centred on the unit covers first.
int32_t cell_of(int32_t position, int16_t footprint) {
    return (position - footprint * 0x80000 + 0x80000) >> 20;
}

// Whether a hover transport with a two-cell footprint stands where its
// Ground_Unload close-in goal ends: 1.5 times its Z extent of 32 is 48, so its
// footprint origin cell is within three cells of the point's.
bool within_reach_cells(const oa::Unit& unit, int32_t x, int32_t z) {
    const auto dx = cell_of(unit.position.x, 2) - cell_of(x << 16, 2);
    const auto dz = cell_of(unit.position.z, 2) - cell_of(z << 16, 2);
    return dx * dx + dz * dz <= 3 * 3;
}

/// Loads a 3DO model of the installed game.
///
/// Throws through CHECK when nothing provides the file.
///
/// @param assets the installed game's store
/// @param path '/'-separated path of the model
/// @return the model
std::shared_ptr<formats::objects3d::Model>
installed_model(const AssetStore& assets, const std::string& path) {
    const auto bytes = test::read_game_file(assets, path);
    CHECK(!bytes.empty());
    return std::make_shared<formats::objects3d::Model>(
        formats::objects3d::load_3do(std::as_bytes(std::span(bytes)))
    );
}

/// Loads a COB unit script of the installed game.
///
/// Throws through CHECK when the file is missing or does not parse.
///
/// @param assets the installed game's store
/// @param path '/'-separated path of the script
/// @return the parsed program
std::shared_ptr<formats::cob::CobProgram>
installed_script(const AssetStore& assets, const std::string& path) {
    auto parsed = formats::cob::parse_cob(test::read_game_file(assets, path));
    CHECK(parsed);
    return std::make_shared<formats::cob::CobProgram>(std::move(*parsed.program));
}

struct Fixture {
    formats::tnt::Map map;
    std::vector<sim::visibility_state::TerrainCell> terrain_values;
    std::array<sim::visibility_state::SightMask, 1> masks{};
    std::shared_ptr<formats::objects3d::Model> model =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::objects3d::Model> crane =
        std::make_shared<formats::objects3d::Model>();
    std::shared_ptr<formats::cob::CobProgram> plain, carrier, loader;
    std::array<sim::unit_spawn::LoadedType, type_count> loaded;
    std::array<sim::unit_spawn::Type, type_count> types;
    std::array<data::unit_definitions::UnitDefinition, type_count> defs;
    std::array<sim::match_runtime::RuntimeTypeFields, type_count> fields{};
    data::unit_definitions::UnitTargetCategoryMasks target_masks;
    data::unit_definitions::RuntimeDefinitionMetadata metadata;
    std::array<uint8_t, 1> yard{4};
    std::vector<sim::spatial_state::Plot> collision_plots;
    sim::combat_state::WeaponRegistry weapons;
    Services services;
    Scenario scenario;
    std::unique_ptr<sim::match_runtime::Match> match;
    uint32_t clock = 1;

    explicit Fixture(const AssetStore* installed = nullptr) {
        map.attribute_width = map.attribute_height = map_cells;
        map.attributes.resize(map_cells * map_cells);
        terrain_values.resize(map_cells * map_cells);
        // Every unit maps the 32-unit sight cells within 160 of it for its
        // owner. The path search takes cells its player has not mapped as
        // open, so a one-cell sight would let routes run through
        // standing units.
        masks[0].width = masks[0].height = 2 * sight_cells + 1;
        masks[0].offset_x = masks[0].offset_z = sight_cells;
        masks[0].pixels.assign(masks[0].width * masks[0].height, 1);
        // A base 10 units high; the link piece hangs 8 units under it.
        model->objects.resize(2);
        model->objects[0].name = "base";
        model->objects[0].vertices = {{0, 0, 0}, {0, 10 << 16, 0}};
        model->objects[0].first_child = 1;
        model->objects[1].name = "link";
        model->objects[1].parent = 0;
        model->objects[1].offset_from_parent = {0, -(8 << 16), 0};
        // The hover transport's link hangs 40 units ahead of it.
        *crane = *model;
        crane->objects[1].offset_from_parent = {0, 0, 40 << 16};
        plain = assemble({{"Create", finish}});
        // Lift a unit within 100 units onto the link; drop it when the
        // transport stands within 80 units of the point.
        const std::vector<uint32_t> unit_xz = get(Value::unit_xz, {op::push_local, 0});
        const std::vector<uint32_t> base_xz = get(Value::piece_xz, {op::push_constant, 0});
        // Both stay BUSY for a second after moving the unit.
        loader = assemble({
            {"Create", finish},
            {"TransportPickup",
             join(
                 {unless_within(unit_xz, base_xz, 100),
                  set_busy(1),
                  attach_argument,
                  sleep_ms(1000),
                  set_busy(0),
                  finish}
             )},
            // Only the unit id is counted as an argument, so the first push
            // lands on the point's local: keep the point in a static first.
            {"TransportDrop",
             join(
                 {{op::push_local, 1, op::pop_static, 0},
                  unless_within(base_xz, {op::push_static, 0}, 80),
                  set_busy(1),
                  {op::push_local, 0, op::drop_unit},
                  sleep_ms(1000),
                  set_busy(0),
                  finish}
             )},
        });
        // QueryTransport answers the link piece; BeginTransport keeps its
        // argument in static 0 and EndTransport counts its calls in static 1.
        carrier = assemble({
            {"Create", finish},
            {"QueryTransport",
             {op::push_constant, 1, op::pop_local, 0, op::push_constant, 0, op::return_}},
            {"BeginTransport", join({{op::push_local, 0, op::pop_static, 0}, finish})},
            {"EndTransport", join({count_in_static(1), finish})},
            {"Statics", answer_statics},
        });
        for (size_t i = 1; i < type_count; ++i) {
            types[i].simulation.flags = OA_UNIT_DEF_FLAG_AVAILABLE;
            types[i].simulation.maximum_health = 100;
            types[i].footprint_x = types[i].footprint_z = 2;
            types[i].bm_code = 1;
            types[i].model = reinterpret_cast<uintptr_t>(model.get());
            loaded[i].model = model;
            loaded[i].script = plain;
            defs[i].sight_distance = 160;
            defs[i].acceleration_fixed = 65536;
            defs[i].brake_rate_fixed = 65536;
            defs[i].max_velocity_fixed = 2 * 65536;
            defs[i].turn_rate = 1024;
            defs[i].footprint_x = defs[i].footprint_z = 2;
            defs[i].max_damage = 100;
            defs[i].energy_storage = defs[i].metal_storage = 1000.0F;
            fields[i].definition = &defs[i];
            fields[i].yard_mask = yard;
            fields[i].runtime_metadata = &metadata;
            fields[i].target_masks = &target_masks;
            fields[i].movement_class = 0;
        }
        loaded[air_transport_type].script = carrier;
        types[air_transport_type].simulation.flags |= OA_UNIT_DEF_FLAG_CAN_FLY;
        types[air_transport_type].simulation.abilities |= OA_UNIT_DEF_ABILITY_CAN_LOAD;
        defs[air_transport_type].can_fly = true;
        defs[air_transport_type].can_load = true;
        defs[air_transport_type].cant_be_transported = true;
        defs[air_transport_type].cruise_altitude = 60;
        defs[air_transport_type].max_velocity_fixed = 4 * 65536;
        defs[air_transport_type].transport_size = 3;
        defs[air_transport_type].transport_capacity = 1;
        types[heavy_type].footprint_x = types[heavy_type].footprint_z = 4;
        defs[heavy_type].footprint_x = defs[heavy_type].footprint_z = 4;
        loaded[hover_transport_type].model = crane;
        loaded[hover_transport_type].script = loader;
        types[hover_transport_type].model = reinterpret_cast<uintptr_t>(crane.get());
        types[hover_transport_type].simulation.flags |= OA_UNIT_DEF_FLAG_CAN_HOVER;
        types[hover_transport_type].simulation.abilities |= OA_UNIT_DEF_ABILITY_CAN_LOAD;
        defs[hover_transport_type].can_hover = true;
        defs[hover_transport_type].can_load = true;
        defs[hover_transport_type].cant_be_transported = true;
        defs[hover_transport_type].transport_size = 3;
        defs[hover_transport_type].transport_capacity = 1;
        fields[hover_transport_type].movement_class = 1;
        // The hovercraft's movement class record: its persistent map takes
        // the class footprint, which the class's types share.
        metadata.footprint_x = metadata.footprint_z = 2;
        // A ground gun and an anti-air gun, both reaching 300.
        weapons.install_tdf_section(1, "TESTGUN", "1");
        weapons.install_target_fields(1, "300", "1", "0", "0", "0", "0", "10", "100", "");
        weapons.install_tdf_section(2, "TESTFLAK", "1");
        weapons.install_target_fields(2, "300", "1", "0", "0", "0", "1", "10", "100", "");
        defs[gun_type].weapon1 = "TESTGUN";
        defs[flak_type].weapon1 = "TESTFLAK";
        for (const auto armed : {gun_type, flak_type}) {
            types[armed].simulation.flags |= OA_UNIT_DEF_FLAG_HAS_WEAPONS;
            defs[armed].can_attack = true;
        }
        if (installed != nullptr) {
            const std::array<std::pair<uint16_t, const char*>, 4> units{
                {{air_transport_type, "armatlas"},
                 {cargo_type, "armpw"},
                 {heavy_type, "armpw"},
                 {hover_transport_type, "armthovr"}}
            };
            for (const auto& [type, name] : units) {
                loaded[type].model =
                    installed_model(*installed, "objects3d/" + std::string(name) + ".3do");
                types[type].model = reinterpret_cast<uintptr_t>(loaded[type].model.get());
                if (type != cargo_type && type != heavy_type)
                    loaded[type].script =
                        installed_script(*installed, "scripts/" + std::string(name) + ".cob");
            }
        }
        for (size_t i = 1; i < type_count; ++i) {
            types[i].cob = reinterpret_cast<uintptr_t>(loaded[i].script.get());
            loaded[i].type = types[i];
        }
        collision_plots.resize(map_cells * map_cells);
        sim::match_runtime::OfflineInputs input{
            map,  loaded, types, fields, weapons,   terrain_values,    masks,           32, 32, 16,
            2,    0,      30,    1,      &scenario, [] { return 0u; }, collision_plots, {}, 0,  0,
            0.0F, {}
        };
        match = std::make_unique<sim::match_runtime::Match>(input, services);
        services.match = match.get();
        match->configure_strategic_environment({0, 0.5F, 0});
        match->simulation().players[0].present = true;
        match->simulation().players[0].status = 1;
        std::array<uint8_t, 10> allies{};
        allies[0] = 1;
        match->configure_player_alliances(0, allies);
        match->configure_outcomes(0, allies, false);
    }

    void use_script(uint16_t type, std::shared_ptr<formats::cob::CobProgram> script) {
        loaded[type].script = std::move(script);
        types[type].cob = reinterpret_cast<uintptr_t>(loaded[type].script.get());
        loaded[type].type = types[type];
    }

    sim::unit_spawn::Slot& spawn(uint16_t type, int32_t x, int32_t z) {
        auto* slot = match->create({0, type, at(x, z), true, 1, 0});
        CHECK(slot && slot->movement_object);
        return *slot;
    }

    void run(uint32_t ticks) {
        for (uint32_t i = 0; i < ticks; ++i) {
            match->simulation().tick = clock++;
            match->tick();
        }
    }

    template <typename Done>
    bool run_until(uint32_t limit, Done done) {
        for (uint32_t i = 0; i < limit; ++i) {
            if (done())
                return true;
            run(1);
        }
        return done();
    }
};

void air_transport_carries_a_unit_to_the_unload_point(const AssetStore* installed) {
    Fixture f(installed);
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 360, 200);
    const auto transport = atlas.unit_index;
    const auto cargo = tank.unit_index;

    auto& pickup = f.match->issue_load(transport, cargo, false);
    CHECK(pickup.kind == vtol_pickup);
    CHECK(f.run_until(600, [&] { return tank.record.attach_parent != 0; }));
    CHECK(oa::oa_unit_slot_from_ref(tank.record.attach_parent) == transport);
    // The transport hovered with its link piece on the cargo, so attaching
    // barely moves it; the installed link hangs further below a hull that may
    // still be banked.
    const int32_t reach = installed == nullptr ? 4 : 24;
    CHECK(
        std::abs(world_x(tank.record) - 360) <= reach &&
        std::abs(world_z(tank.record) - 200) <= reach
    );
    CHECK(std::abs(tank.record.position.y >> 16) <= reach);
    // Carried: out of the buckets, unselectable, running BeCarried.
    CHECK(!in_buckets(*f.match, cargo));
    CHECK(!f.match->selectable(cargo));
    CHECK(tank.unit->primary && tank.unit->primary->kind == be_carried);
    CHECK(static_cast<int8_t>(tank.record.attach_piece) == 1);

    auto& unload = f.match->issue_unload(transport, point(200, 480), true);
    CHECK(unload.kind == vtol_unload);
    uint32_t followed = 0;
    int32_t hung = 0;
    CHECK(f.run_until(1500, [&] {
        if (tank.record.attach_parent) {
            hung = tank.record.position.y;
            const auto link = f.match->instance(transport)->piece_world(1);
            CHECK(tank.record.position.x == static_cast<int32_t>(link[0]));
            CHECK(tank.record.position.z == static_cast<int32_t>(link[2]));
            CHECK(tank.record.cell_x == cell_of(tank.record.position.x, tank.record.footprint_x));
            CHECK(tank.record.cell_z == cell_of(tank.record.position.z, tank.record.footprint_z));
            CHECK(!in_buckets(*f.match, cargo));
            // The carry link's mode 0 is the unit's movement layer while it
            // hangs (the carry link, then the occupancy removal of the movement
            // step).
            CHECK((tank.record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == 0);
            ++followed;
        }
        return tank.record.attach_parent == 0;
    }));
    CHECK(followed > 20);
    // The base hovers within 0.5 of the point with the hull level again; the
    // made-up link hangs straight under it, the installed one about a unit off.
    CHECK(planar(tank.record, 200, 480) <= (installed == nullptr ? 1 : 2));
    CHECK(in_buckets(*f.match, cargo));
    CHECK(f.match->selectable(cargo));
    // Released into the ground layer. The release snaps no height: the
    // cargo's movement step on the same tick finds its layer changed and
    // rewrites the position as moved, so the height fit puts it on the ground
    // through its model's ground plate.
    // That fit writes only the whole-unit word of the height, the flat
    // ground's 0, and keeps the fraction the unit hung at. The made-up model
    // has no ground plate, so it keeps the whole height it hung at.
    CHECK((tank.record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == 1);
    const auto released = tank.record.position.y;
    CHECK(released == (installed == nullptr ? hung : (hung & 0xffff)));
    CHECK(f.run_until(300, [&] {
        CHECK(tank.record.position.y == released);
        return atlas.unit->primary == nullptr || atlas.unit->primary->kind != vtol_unload;
    }));
}

// Hovering loaded over the pickup point, the transport's velocity no longer
// changes, so its bank eases back to level by the flight step's decay alone:
// each tick keeps 0xf333/0x10000 of the movement object's filtered velocity
// change, and the bank is the angle of its negated X in the heading frame
// (turned by the heading rotation) against gravity * 0x10000 / 0xccd. Whatever bank
// the arrival left at the attach fades within a few seconds.
void loaded_air_transport_levels_out_while_hovering(const AssetStore* installed) {
    Fixture f(installed);
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 360, 200);
    // The pickup hover matches the cargo's heading; off the axes the turn
    // into the heading frame rounds.
    tank.record.heading = 0x2345;
    const auto transport = atlas.unit_index;
    f.match->issue_load(transport, tank.unit_index, false);
    CHECK(f.run_until(600, [&] { return tank.record.attach_parent != 0; }));
    CHECK(atlas.record.heading == 0x2345);
    auto& movement = f.match->ground_runtime(transport)->movement;
    auto& filtered = f.match->ground_runtime(transport)->previous_vector;
    const std::array<int32_t, 3> still{};
    CHECK(f.run_until(60, [&] { return movement.velocity == still; }));
    const auto& def = *oa::world_unit_def_of(&f.match->state(), &atlas.record);
    const auto gravity = f.match->state().game.gravity != 0 ? f.match->state().game.gravity
                                                            : sim::ground_orders::default_gravity;
    const auto vertical = static_cast<int32_t>((static_cast<int64_t>(gravity) << 16) / 0xccd);
    const auto level = [&] {
        int32_t x = filtered[0], z = filtered[2];
        sim::air::rotate_xz(static_cast<int16_t>(atlas.record.heading), &x, &z);
        const auto roll =
            static_cast<int32_t>((static_cast<int64_t>(def.bank_scale) * -int64_t{x}) >> 16);
        return static_cast<int16_t>(base::game_math::direction(roll, vertical));
    };
    const auto first = static_cast<int16_t>(atlas.record.bank);
    CHECK(first == level());
    for (int tick = 0; tick < 90; ++tick) {
        const auto before = filtered;
        f.run(1);
        CHECK(movement.velocity == still);
        for (size_t axis = 0; axis < 3; ++axis)
            CHECK(
                filtered[axis] ==
                static_cast<int32_t>((static_cast<int64_t>(before[axis]) * 0xf333) >> 16)
            );
        CHECK(static_cast<int16_t>(atlas.record.bank) == level());
    }
    CHECK(std::abs(static_cast<int16_t>(atlas.record.bank)) * 10 <= std::abs(first) + 10);
}

// Nothing in automatic targeting looks at a carry link: the target pick draws
// its candidates from the seen enemies the knowledge walk lists, and neither
// that list nor the visibility test checks Unit.attach_parent. What a carry does
// change is the unit's movement layer, which takes the link's mode 0 (the
// Atlas's VTOL_Pickup and the Bear's attach-unit both pass 0). The range test
// wants an anti-air weapon's target in the air layer, so a
// carried unit is out of an anti-air gun's reach, but not out of a ground
// gun's.
void carried_units_stay_in_reach_of_ground_guns(const AssetStore* installed) {
    Fixture f(installed);
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& bear = f.spawn(hover_transport_type, 600, 200);
    auto& flown = f.spawn(cargo_type, 360, 200);
    auto& shipped = f.spawn(cargo_type, 600, 280);
    auto& gun = f.spawn(gun_type, 480, 360);
    auto& flak = f.spawn(flak_type, 480, 40);
    f.match->issue_load(atlas.unit_index, flown.unit_index, false);
    f.match->issue_load(bear.unit_index, shipped.unit_index, false);
    CHECK(f.run_until(900, [&] {
        return flown.record.attach_parent && shipped.record.attach_parent;
    }));
    f.run(2);
    for (const auto* cargo : {&flown, &shipped}) {
        CHECK((cargo->record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == 0);
        CHECK(f.match->weapon_can_reach(gun.unit_index, cargo->unit_index, 0));
        CHECK(!f.match->weapon_can_reach(flak.unit_index, cargo->unit_index, 0));
    }
    // The flying carrier stays in the anti-air gun's reach; the hovering one
    // is a ground target.
    CHECK((atlas.record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == 2);
    CHECK(f.match->weapon_can_reach(flak.unit_index, atlas.unit_index, 0));
    CHECK(!f.match->weapon_can_reach(flak.unit_index, bear.unit_index, 0));
    CHECK(f.match->weapon_can_reach(gun.unit_index, bear.unit_index, 0));
}

void air_transport_capacity_is_enforced(const AssetStore* installed) {
    Fixture f(installed);
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& heavy = f.spawn(heavy_type, 300, 200);
    f.match->issue_load(atlas.unit_index, heavy.unit_index, false);
    f.run(2);
    CHECK(heavy.record.attach_parent == 0);
    CHECK(f.services.spoke(atlas.unit_index, speech_failed));
    CHECK(atlas.unit->primary == nullptr || atlas.unit->primary->kind != vtol_pickup);

    auto& first = f.spawn(cargo_type, 360, 200);
    auto& second = f.spawn(cargo_type, 200, 360);
    f.match->issue_load(atlas.unit_index, first.unit_index, false);
    CHECK(f.run_until(600, [&] { return first.record.attach_parent != 0; }));
    CHECK(f.match->loaded_child_count(atlas.unit_index) == 1);
    f.match->issue_load(atlas.unit_index, second.unit_index, false);
    f.run(30);
    CHECK(second.record.attach_parent == 0);
    CHECK(f.match->loaded_child_count(atlas.unit_index) == 1);
}

// VTOL_Pickup and VTOL_Unload goal by goal with the made-up units: cruise
// altitude 60, the link 8 under the base, a cargo 10 high on flat ground.
void air_transport_flies_the_game_goals() {
    Fixture f;
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 360, 200);
    const auto transport = atlas.unit_index;
    const auto phase = [&] { return atlas.unit->primary ? atlas.unit->primary->phase : 0; };

    f.match->issue_load(transport, tank.unit_index, false);
    // Phase 1 follows the cargo; its 0x30 arrival radius wakes phase 2, and
    // phase 3 starts BeginTransport with the cargo height and hovers over it.
    CHECK(f.run_until(600, [&] { return kind_of(atlas) == vtol_pickup && phase() >= 4; }));
    CHECK(planar(atlas.record, 360, 200) < 0x30);
    CHECK(statics_of(*f.match, transport)[0] == 10 * one);
    // The hover goal arrives with the base over the cargo and 8 above it, so
    // the link meets the cargo, and phase 4 attaches it there.
    CHECK(f.run_until(600, [&] { return tank.record.attach_parent != 0; }));
    CHECK(planar(atlas.record, 360, 200) <= 0.5);
    CHECK(std::abs(atlas.record.position.y - 8 * one) < one);
    // Nothing replaces the hover goal: the loaded transport ends the order on
    // the goal's next event, still at the pickup height.
    CHECK(f.run_until(30, [&] { return kind_of(atlas) != vtol_pickup; }));
    CHECK(std::abs(atlas.record.position.y - 8 * one) < one);

    // Phase 0 flies at cruise altitude until within 0x140 of the point, where
    // phase 1 takes the goal down to the cargo height over the point.
    f.match->issue_unload(transport, point(200, 800), false);
    int32_t highest = 0;
    CHECK(f.run_until(900, [&] {
        highest = std::max(highest, atlas.record.position.y);
        return kind_of(atlas) == vtol_unload && phase() >= 2;
    }));
    CHECK(std::abs(highest - 60 * one) < one);
    CHECK(planar(atlas.record, 200, 800) < 0x140);
    // Phase 2 starts EndTransport and sets the cargo down where it hangs, the
    // link 8 under a base 10 above the point.
    CHECK(f.run_until(900, [&] { return tank.record.attach_parent == 0; }));
    CHECK(planar(tank.record, 200, 800) <= 1);
    CHECK(std::abs(tank.record.position.y - 2 * one) < one);
    // Phase 2's goal climbs back to cruise altitude where the unit was set
    // down. Its arrival wakes the order, which ends as the transport is empty
    // before phase 3 could speak.
    const auto release = atlas.record.position;
    CHECK(f.run_until(300, [&] { return kind_of(atlas) != vtol_unload; }));
    CHECK(std::abs(atlas.record.position.y - 60 * one) < one);
    CHECK(planar(atlas.record, release.x >> 16, release.z >> 16) <= 1);
    CHECK(!f.services.spoke(transport, speech_unloaded));
    CHECK(statics_of(*f.match, transport)[1] == 1);
}

// VTOL_Unload refuses a point the cargo cannot stand on: it speaks the failure
// and returns 9, which with nothing queued restarts it after at least 30
// ticks, so it never descends until the point is clear.
void air_transport_waits_for_a_clear_unload_point() {
    constexpr auto weapon = static_cast<uint8_t>(sim::match_runtime::DeathKind::weapon);
    Fixture f;
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 360, 200);
    auto& blocker = f.spawn(cargo_type, 200, 800);
    const auto transport = atlas.unit_index;
    f.match->issue_load(transport, tank.unit_index, false);
    CHECK(f.run_until(600, [&] { return tank.record.attach_parent != 0; }));

    f.match->issue_unload(transport, point(200, 800), false);
    // Only phase 1 would install the descent over the point; phase 0's goal
    // holds cruise altitude.
    int32_t lowest = 60 * one;
    size_t failures = 0;
    uint32_t last_failure = 0;
    for (uint32_t tick = 0; tick < 400; ++tick) {
        f.run(1);
        if (failures != 0)
            lowest = std::min(lowest, atlas.record.position.y);
        if (const auto spoken = f.services.spoken(transport, speech_failed); spoken != failures) {
            CHECK(spoken == failures + 1);
            CHECK(failures == 0 || tick - last_failure >= 30);
            failures = spoken;
            last_failure = tick;
        }
    }
    CHECK(failures >= 400 / 60);
    CHECK(lowest > 59 * one);
    CHECK(tank.record.attach_parent != 0);
    CHECK(kind_of(atlas) == vtol_unload && (atlas.unit->primary->flags & order_retry));

    f.match->apply_damage_event(blocker, nullptr, 30000, weapon, 0);
    CHECK(f.run_until(900, [&] { return tank.record.attach_parent == 0; }));
    CHECK(planar(tank.record, 200, 800) <= 1);
}

bool dead(const sim::unit_spawn::Slot& slot) {
    return slot.record.type_index == 0;
}

void carried_units_leave_a_dead_carrier(const AssetStore* installed) {
    constexpr auto weapon = static_cast<uint8_t>(sim::match_runtime::DeathKind::weapon);
    // A carried unit that dies leaves its carrier free to load again.
    Fixture f(installed);
    auto& atlas = f.spawn(air_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 360, 200);
    f.match->issue_load(atlas.unit_index, tank.unit_index, false);
    CHECK(f.run_until(600, [&] { return tank.record.attach_parent != 0; }));
    f.match->apply_damage_event(tank, nullptr, 30000, weapon, 0);
    CHECK(f.run_until(60, [&] { return dead(tank); }));
    CHECK(f.match->loaded_child_count(atlas.unit_index) == 0 && !atlas.record.attach_first_child);

    // A carrier that dies takes its cargo with it and sets it down first.
    auto& second = f.spawn(cargo_type, 200, 360);
    f.match->issue_load(atlas.unit_index, second.unit_index, false);
    CHECK(f.run_until(600, [&] { return second.record.attach_parent != 0; }));
    f.match->apply_damage_event(atlas, nullptr, 30000, weapon, 0);
    CHECK(f.run_until(60, [&] { return dead(atlas) && dead(second); }));
    CHECK(!second.record.attach_parent && !atlas.record.attach_first_child);
}

// Ticks a transport's `kind` order spent with its script BUSY, and ticks it
// outlived BUSY by, running until the order ends.
struct BusyWait {
    uint32_t busy{};
    uint32_t after{};
};

BusyWait wait_out_busy(Fixture& f, const sim::unit_spawn::Slot& transport, uint8_t kind) {
    constexpr uint8_t busy_flag = 0x02; // unit build_flags, COB BUSY
    BusyWait wait;
    CHECK(f.run_until(600, [&] {
        if (kind_of(transport) != kind)
            return true;
        if (transport.record.build_flags & busy_flag)
            ++wait.busy;
        else if (wait.busy)
            ++wait.after;
        return false;
    }));
    return wait;
}

void hover_transport_loads_and_unloads_through_its_scripts(const AssetStore* installed) {
    Fixture f(installed);
    auto& bear = f.spawn(hover_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 200, 280);
    const auto transport = bear.unit_index;
    const auto cargo = tank.unit_index;
    CHECK(sim::gameplay_input::can_load_unit(f.match->state(), bear.record, tank.record));

    auto& pickup = f.match->issue_load(transport, cargo, false);
    CHECK(pickup.kind == ground_pickup);
    CHECK(f.run_until(900, [&] { return tank.record.attach_parent != 0; }));
    CHECK(oa::oa_unit_slot_from_ref(tank.record.attach_parent) == transport);
    CHECK(tank.unit->primary && tank.unit->primary->kind == be_carried);
    CHECK(!in_buckets(*f.match, cargo) && !f.match->selectable(cargo));
    // The crane still swings the unit aboard before it clears BUSY; the order
    // waits for that, then finds the unit carried.
    const auto boarding = wait_out_busy(f, bear, ground_pickup);
    if (installed == nullptr)
        CHECK(boarding.busy >= 25 && boarding.after <= 1);
    // Full: capacity 1 refuses a second passenger.
    auto& other = f.spawn(cargo_type, 500, 200);
    CHECK(!sim::gameplay_input::can_load_unit(f.match->state(), bear.record, other.record));

    auto& unload = f.match->issue_unload(transport, point(560, 560), false);
    CHECK(unload.kind == ground_unload);
    CHECK(f.run_until(2000, [&] {
        if (tank.record.attach_parent) {
            const auto piece = static_cast<uint32_t>(static_cast<int8_t>(tank.record.attach_piece));
            const auto link = f.match->instance(transport)->piece_world(piece);
            CHECK(tank.record.position.x == static_cast<int32_t>(link[0]));
            CHECK(tank.record.position.z == static_cast<int32_t>(link[2]));
        }
        return tank.record.attach_parent == 0;
    }));
    // The first TransportDrop found the point out of reach; the second ran
    // where the close-in goal ended, and the unit stays where it hung.
    CHECK(within_reach_cells(bear.record, 560, 560));
    const double hung = std::hypot(
        (tank.record.position.x - bear.record.position.x) / 65536.0,
        (tank.record.position.z - bear.record.position.z) / 65536.0
    );
    if (installed == nullptr)
        CHECK(std::abs(hung - 40) < 1);
    else
        CHECK(planar(tank.record, 560, 560) <= 48);
    CHECK((tank.record.flags & OA_UNIT_FLAG_OCCUPANCY_MASK) == 1);
    CHECK(in_buckets(*f.match, cargo) && f.match->selectable(cargo));
    const auto setting_down = wait_out_busy(f, bear, ground_unload);
    if (installed == nullptr)
        CHECK(setting_down.busy >= 25 && setting_down.after <= 1);
    CHECK(f.services.spoke(transport, speech_unloaded));
    CHECK(sim::gameplay_input::can_load_unit(f.match->state(), bear.record, other.record));
}

// Out of the script's reach, Ground_Pickup closes in with a goal of radius 0
// on the cargo's own cell, which the transport can never stand on. The
// search routes it to the nearest open cell (it takes cells within the
// wall-follow cost as goal cells); the search from there fails, and that
// failure (event 0x40) wakes phase 5, which restarts the
// order for a second TransportPickup with the cargo in reach. The installed
// script reaches 125 from its turret piece: 37.5 plus 87.5 in BoomCalc.
void hover_transport_closes_in_on_a_unit_out_of_reach(const AssetStore* installed) {
    Fixture f(installed);
    auto& bear = f.spawn(hover_transport_type, 200, 200);
    auto& tank = f.spawn(cargo_type, 200, 420);
    const auto transport = bear.unit_index;
    const int32_t cargo_x = cell_of(tank.record.position.x, 2),
                  cargo_z = cell_of(tank.record.position.z, 2);
    const double reach = installed == nullptr ? 100 : 125;
    f.match->issue_load(transport, tank.unit_index, false);
    bool closed_in = false;
    std::vector<double> attempts;
    CHECK(f.run_until(900, [&] {
        const auto* goal = f.match->ground_runtime(transport)->navigation.goal;
        if (goal && kind_of(bear) == ground_pickup) {
            CHECK(goal->tolerance == 0 && goal->cell[0] == cargo_x && goal->cell[1] == cargo_z);
            closed_in = true;
        }
        // Both footprints are two cells wide: the transport never overlaps
        // the cargo, so it never stands on the goal cell.
        CHECK(
            std::abs(bear.record.cell_x - cargo_x) >= 2 ||
            std::abs(bear.record.cell_z - cargo_z) >= 2
        );
        if (f.services.spoken(transport, speech_loading) > attempts.size())
            attempts.push_back(planar(bear.record, 200, 420));
        return tank.record.attach_parent != 0;
    }));
    CHECK(closed_in);
    CHECK(attempts.size() == 2 && attempts[0] > reach && attempts[1] < reach);
    CHECK(!f.services.spoke(transport, speech_failed));
    CHECK(oa::oa_unit_slot_from_ref(tank.record.attach_parent) == transport);
}

void hover_transport_refuses_a_unit_too_large() {
    Fixture f;
    auto& bear = f.spawn(hover_transport_type, 200, 200);
    auto& heavy = f.spawn(heavy_type, 200, 280);
    auto& pickup = f.match->issue_load(bear.unit_index, heavy.unit_index, false);
    CHECK(pickup.kind == ground_pickup);
    f.run(2);
    CHECK(kind_of(bear) != ground_pickup);
    CHECK(f.services.spoke(bear.unit_index, speech_failed));
    CHECK(!f.services.spoke(bear.unit_index, speech_loading));
    CHECK(heavy.record.attach_parent == 0);
}

// Ground_Pickup and Ground_Unload give the script three attempts, closing in
// between them, then return 9: with nothing queued the order stays, marked
// for retry, and restarts after at least 30 ticks.
void hover_transport_gives_up_after_three_attempts() {
    {
        // TransportPickup stays BUSY for 300 ms and never attaches. Standing
        // on the cargo, each close-in goal arrives at once.
        Fixture f;
        f.use_script(
            hover_transport_type,
            assemble(
                {{"Create", finish},
                 {"TransportPickup", join({set_busy(1), sleep_ms(300), set_busy(0), finish})}}
            )
        );
        auto& bear = f.spawn(hover_transport_type, 200, 200);
        auto& tank = f.spawn(cargo_type, 200, 200);
        const auto transport = bear.unit_index;
        f.match->issue_load(transport, tank.unit_index, false);
        CHECK(f.run_until(600, [&] {
            return bear.unit->primary && (bear.unit->primary->flags & order_retry);
        }));
        CHECK(f.services.spoken(transport, speech_loading) == 3);
        CHECK(kind_of(bear) == ground_pickup && tank.record.attach_parent == 0);
        f.run(29);
        CHECK(f.services.spoken(transport, speech_loading) == 3);
    }
    {
        // TransportDrop counts its calls in static 1, stays BUSY for 300 ms
        // and never drops.
        Fixture f;
        f.use_script(
            hover_transport_type,
            assemble(
                {{"Create", finish},
                 {"TransportPickup", join({set_busy(1), attach_argument, set_busy(0), finish})},
                 {"TransportDrop",
                  join({count_in_static(1), set_busy(1), sleep_ms(300), set_busy(0), finish})},
                 {"Statics", answer_statics}}
            )
        );
        auto& bear = f.spawn(hover_transport_type, 200, 200);
        auto& tank = f.spawn(cargo_type, 200, 280);
        const auto transport = bear.unit_index;
        f.match->issue_load(transport, tank.unit_index, false);
        CHECK(f.run_until(600, [&] { return tank.record.attach_parent != 0; }));
        CHECK(f.run_until(600, [&] { return kind_of(bear) != ground_pickup; }));
        f.match->issue_unload(transport, point(560, 560), false);
        CHECK(f.run_until(2000, [&] {
            return bear.unit->primary && (bear.unit->primary->flags & order_retry);
        }));
        CHECK(statics_of(*f.match, transport)[1] == 3);
        CHECK(kind_of(bear) == ground_unload && tank.record.attach_parent != 0);
        CHECK(within_reach_cells(bear.record, 560, 560));
        f.run(29);
        CHECK(statics_of(*f.match, transport)[1] == 3);
    }
}
} // namespace

int main(int argc, char** argv) {
    const bool installed_data = test::game_data_requested(argc, argv);
    const auto assets =
        installed_data
            ? std::optional<AssetStore>(test::require_game_assets("the installed transports"))
            : std::nullopt;
    try {
        const AssetStore* installed = assets ? &*assets : nullptr;
        air_transport_carries_a_unit_to_the_unload_point(installed);
        if (installed == nullptr) {
            air_transport_flies_the_game_goals();
            air_transport_waits_for_a_clear_unload_point();
            hover_transport_refuses_a_unit_too_large();
            hover_transport_gives_up_after_three_attempts();
        }
        air_transport_capacity_is_enforced(installed);
        loaded_air_transport_levels_out_while_hovering(installed);
        carried_units_stay_in_reach_of_ground_guns(installed);
        carried_units_leave_a_dead_carrier(installed);
        hover_transport_loads_and_unloads_through_its_scripts(installed);
        hover_transport_closes_in_on_a_unit_out_of_reach(installed);
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << (installed_data ? "installed transports ok\n" : "transports ok\n");
    return 0;
}
