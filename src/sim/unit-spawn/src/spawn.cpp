// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_spawn/spawn.hpp"
#include <bit>
#include <cstdint>
#include <stdexcept>

namespace oa::sim::unit_spawn {
namespace {
constexpr uint8_t periodic_enabled = 2;
constexpr uint8_t no_player = 10;

uint32_t arithmetic_shift20(uint32_t value) {
    const auto high = value >> 20;
    return (value & 0x80000000u) ? high | 0xfffff000u : high;
}

// Callbacks may replace the unit's type; the game reloads it from the unit.
const oa::UnitDef& current_def(oa::World& w, const oa::Unit& u) {
    const auto* def = oa::world_unit_def_of(&w, &u);
    if (!def)
        throw std::invalid_argument("spawn callback selected an unbound unit type");
    return *def;
}

Type& current_type(oa::World& w, Tables& tables, const oa::Unit& u) {
    const auto index = oa::world_unit_def_ref(&w, &current_def(w, u)) - 1u;
    if (index >= tables.types.size())
        throw std::invalid_argument("spawn callback selected an unbound unit type");
    return tables.types[index];
}

SlotAssets& assets(oa::World& w, Tables& tables, const oa::Unit& u) {
    const auto slot = oa::world_unit_slot(&w, &u);
    if (slot >= tables.assets.size())
        throw std::invalid_argument("spawn slot has no asset record");
    return tables.assets[slot];
}

void require_handle(AssetHandle value, const char* name) {
    if (!value)
        throw std::runtime_error(name);
}

oa::oa_fixed fixed_word(uint32_t word) noexcept {
    return static_cast<oa::oa_fixed>(word);
}
} // namespace

void load_unit_def(const Type& t, oa::UnitDef& d) noexcept {
    d.flags = t.simulation.flags;
    d.abilities = t.simulation.abilities;
    d.max_damage = t.simulation.maximum_health;
    d.heal_time = t.simulation.heal_time;
    d.water_line = std::bit_cast<int8_t>(t.simulation.waterline_offset);
    d.default_mission_type = std::bit_cast<int8_t>(t.simulation.default_mission_type);
    d.footprint_x = t.footprint_x;
    d.footprint_z = t.footprint_z;
    d.player_limit = t.player_limit;
    d.build_angle = std::bit_cast<int16_t>(t.build_angle);
    d.gui_page_count = t.gui_page_count;
    d.bm_code = std::bit_cast<int8_t>(t.bm_code);
}

void initialize_numeric(oa::World& w, oa::Unit& u, const Request& r, Host& h) {
    const auto* owner = oa::world_unit_owner(&w, &u);
    if (!owner)
        throw std::invalid_argument("spawn slot has no preassigned owner");
    if (r.type >= w.unit_def_count)
        throw std::out_of_range("spawn type index");
    const auto& t = w.unit_defs[r.type];
    u.flags |= OA_UNIT_FLAG_LIVE;
    u.def = oa::oa_ref_from_index(r.type);
    u.flags = (static_cast<uint32_t>(t.bm_code == 0) << 29) | (u.flags & 0xdfffbfffu);
    u.footprint_x = t.footprint_x;
    u.footprint_z = t.footprint_z;
    u.flags = ((t.flags >> 16) << 31) | (u.flags & 0x7fffffffu);
    u.flags2 = ((t.flags >> 7) & 1) | (u.flags2 & 0xfffffffeu);
    u.flags = ((t.flags & 0x200) << 21) | (u.flags & 0xbfffffffu);
    if (!r.finished) {
        u.cleared_on_unfinished_spawn = 0;
        u.health = 0;
        u.build_remaining = 1.0F;
    } else {
        u.build_remaining = 0.0F;
        u.health = std::bit_cast<int16_t>(static_cast<uint16_t>(t.max_damage));
    }
    u.health_percent = 0;
    u.previous_health_percent = 0;
    u.position.x = fixed_word(r.position[0]);
    u.flags = (u.flags & 0xfffdf3e1u) | 0x10021;
    u.position.y = fixed_word(r.position[1]);
    u.state_flags = 0;
    u.position.z = fixed_word(r.position[2]);
    u.build_flags &= 0xf0;
    u.decloak_until_tick = 0;
    u.pitch = 0;
    // Wrapping arithmetic, an arithmetic shift by 20, then each grid coordinate narrows.
    const auto grid_x = arithmetic_shift20(
        r.position[0] - static_cast<uint32_t>(static_cast<int32_t>(t.footprint_x)) * 0x80000u +
        0x80000u
    );
    const auto grid_z = arithmetic_shift20(
        r.position[2] - static_cast<uint32_t>(static_cast<int32_t>(t.footprint_z)) * 0x80000u +
        0x80000u
    );
    u.cell_x = std::bit_cast<int16_t>(static_cast<uint16_t>(grid_x));
    u.cell_z = std::bit_cast<int16_t>(static_cast<uint16_t>(grid_z));
    // Heading: a random value below the type's build angle, less 0x8000 and less
    // half the build angle, the half taken from the type the unit holds by then.
    const auto angle = h.random_bounded(static_cast<uint16_t>(t.build_angle));
    u.damage_countdown = 0;
    u.bank = 0;
    u.sight_center_x = 0;
    u.heading = static_cast<uint16_t>(
        angle + 0xffff8000u - (static_cast<uint16_t>(current_def(w, u).build_angle) >> 1)
    );
    u.sight_center_z = 0;
    u.capture_cooldown = 0;
    u.flags = (static_cast<uint32_t>(owner->index == w.game.viewpoint_player) << 9) |
              (u.flags & 0xfffffcffu);
    for (uint32_t i = 0; i < 3; ++i) {
        h.init_weapon_target(u, i);
        h.reset_weapon_targets(u, static_cast<uint8_t>(i));
    }
    // The type is reloaded after the callbacks.
    if (!u.def)
        throw std::invalid_argument("weapon reset removed unit type");
    const auto definition_flags = current_def(w, u).flags;
    const auto flags_before_reset = u.flags;
    u.events = 0;
    u.veteran_level = 0;
    u.last_attacker_id = 0;
    u.last_attacker_owner = no_player;
    const auto standing_move = (definition_flags & 3) << 18;
    u.flags = standing_move | (flags_before_reset & 0xfff3ffffu);
    const auto standing_fire = (definition_flags & 0xc) << 18;
    u.flags = standing_fire | standing_move | (flags_before_reset & 0xffc3ffffu);
    const auto cloak = (definition_flags & 0x10) << 7;
    u.flags = cloak | standing_fire | standing_move | (flags_before_reset & 0xf3c3f7ffu);
    if (current_def(w, u).gui_page_count < 2)
        u.flags = cloak | standing_fire | standing_move | (flags_before_reset & 0xf003f7ffu);
    else
        u.flags =
            cloak | standing_fire | standing_move | (flags_before_reset & 0xf0c3f7ffu) | 0xc00000;
    u.sight_band = 0;
    h.init_unit_economy(u, u.owner_index);
    u.attach_piece = 0xff;
    u.bob_phase = static_cast<uint16_t>(h.random_bounded(0x10000));
    h.assign_squad(u, 0);
}

void attach_model_script(oa::World& w, Tables& tables, oa::Unit& u, Host& h) {
    if (u.type_index >= tables.types.size())
        throw std::out_of_range("spawn type index");
    const auto& t = tables.types[u.type_index];
    auto& a = assets(w, tables, u);
    if (!t.cob) {
        a.script_instance = 0;
        u.script = 0;
        a.model_instance = h.create_model_instance(t.model);
        require_handle(a.model_instance, "model allocation failed");
        h.model_owner(u);
    } else {
        a.script_instance = h.allocate_script();
        require_handle(a.script_instance, "script allocation failed");
        u.script = 1;
        h.load_script_state(a.script_instance, current_type(w, tables, u).cob);
        a.model_instance = h.create_scripted_model(t.model, current_type(w, tables, u).cob, u);
        require_handle(a.model_instance, "scripted model allocation failed");
        h.bind_script_model(a.script_instance, a.model_instance);
        h.call_script_create(a.script_instance);
    }
    h.model_reset(u);
}

std::size_t offline_pool_size(uint16_t limit) {
    const auto total = static_cast<std::size_t>(limit) * 10 + 1;
    if (limit == 0 || total > 65535)
        throw std::invalid_argument("unit pool limit would wrap the 16-bit unit count");
    return total;
}

void init_unit_pool(oa::World& w, uint16_t limit) {
    const auto total = offline_pool_size(limit);
    if (!w.unit_defs || w.unit_def_count == 0)
        throw std::invalid_argument("unit pool needs reserved type zero");
    if (!w.units || w.unit_slot_count != total)
        throw std::invalid_argument("unit pool buffers do not match the slot count");
    w.game.cycle_unit_id = 0;
    w.game.periodic_flags &= static_cast<uint8_t>(~periodic_enabled);
    w.game.units_per_player = limit;
    w.game.unit_slot_count = static_cast<uint16_t>(total);
    for (std::size_t i = 0; i < total; ++i) {
        w.units[i] = {};
        w.units[i].id = static_cast<uint16_t>(i);
        w.units[i].def = oa::oa_ref_from_index(0);
    }
    w.units[0].owner_index = 0xff;
    for (uint32_t player = 0; player < OA_PLAYER_COUNT; ++player) {
        const auto first = player * limit + 1u;
        auto& owner = w.game.players[player];
        owner.first_unit = oa::oa_ref_from_index(first);
        owner.last_unit = oa::oa_ref_from_index(first + limit - 1u);
        owner.base_unit_id = w.units[first].id;
        owner.last_unit_id = w.units[first + limit - 1u].id;
        for (uint32_t i = first; i < first + limit; ++i) {
            w.units[i].owner = oa::oa_ref_from_index(player);
            w.units[i].squad = -1;
            w.units[i].owner_index = owner.index;
        }
    }
}

oa::Unit* create(oa::World& w, Tables& tables, const Request& r, Host& h) {
    if (r.player >= OA_PLAYER_COUNT)
        throw std::out_of_range("spawn player index");
    if (!r.type)
        return nullptr;
    if (r.type >= w.unit_def_count || r.type >= tables.types.size())
        throw std::out_of_range("spawn type index");
    if (tables.assets.size() != w.unit_slot_count)
        throw std::invalid_argument("spawn asset table does not match unit pool");
    const auto& t = w.unit_defs[r.type];
    if (!(t.flags & OA_UNIT_DEF_FLAG_AVAILABLE))
        return nullptr;
    auto& p = w.game.players[r.player];
    uint32_t count = 0;
    auto* first = oa::world_player_units(&w, &p, &count);
    if (count == 0 && (p.first_unit || p.last_unit))
        throw std::out_of_range("spawn player slot range");
    if (t.player_limit != -1) {
        int32_t owned = 0;
        for (uint32_t i = 0; i < count; ++i)
            if (first[i].type_index == r.type)
                ++owned;
        if (t.player_limit <= owned)
            return nullptr;
    }
    oa::Unit* chosen = nullptr;
    const auto first_slot = count ? oa::world_unit_slot(&w, first) : 0u;
    if (r.requested_slot) {
        const auto index = static_cast<uint32_t>(r.requested_slot);
        if (index < first_slot || index - first_slot >= count)
            return nullptr;
        if (w.units[index].type_index)
            return nullptr;
        chosen = &w.units[index];
    } else {
        for (uint32_t i = 0; i < count; ++i)
            if (!first[i].type_index) {
                chosen = &first[i];
                break;
            }
    }
    if (!chosen)
        return nullptr;
    auto& u = *chosen;
    auto& a = assets(w, tables, u);
    a.weapon_slots_initialized.fill(true);
    u.type_index = r.type;
    initialize_numeric(w, u, r, h);
    attach_model_script(w, tables, u, h);
    h.initialize_weapons(u);
    h.initialize_extraction_rate(u);
    if (t.bm_code == 1) {
        a.movement_object = h.create_movement(u);
        // A null movement object is allowed and leaves Unit.movement clear.
        u.movement = a.movement_object != 0;
        // UnitDef.build_angle is written over Unit.heading after the movement object exists.
        u.heading = static_cast<uint16_t>(current_def(w, u).build_angle);
    }
    u.flags = ((u.flags ^ r.state) & OA_UNIT_FLAG_OCCUPANCY_MASK) ^ u.flags;
    h.fit_spawn_height(u);
    h.register_occupancy(u);
    h.notify_created(u);
    if (r.finished) {
        if (!t.bm_code)
            h.notify_finished(u);
        if (!u.def)
            throw std::invalid_argument("spawn callback removed unit type");
        const auto flags = current_def(w, u).flags;
        if (flags & OA_UNIT_DEF_FLAG_ACTIVATE_WHEN_BUILT)
            h.set_activation(u, true, true);
        // A finished unit whose type is a feature is marked to die at once,
        // dismissed (damage kind 7): it leaves its corpse and counts no kill.
        if (flags & OA_UNIT_DEF_FLAG_IS_FEATURE) {
            u.flags |= OA_UNIT_FLAG_DEATH_PENDING;
            u.damage_kind = 7;
        }
    }
    h.update_sight(u);
    ++p.unit_count;
    ++p.units_created;
    h.notify_scenario_created(u);
    return chosen;
}

int32_t count_start_positions(std::span<const StartMarker> markers) noexcept {
    int32_t count = 0;
    for (const auto& marker : markers)
        if (marker.kind == 1)
            ++count;
    return count;
}

bool start_position(
    std::span<const StartMarker> markers, int32_t index, std::array<uint32_t, 3>& output
) {
    for (const auto& marker : markers)
        if (marker.kind == 1 && marker.index == index) {
            output = {
                static_cast<uint32_t>(static_cast<int32_t>(marker.x)) << 16,
                0,
                static_cast<uint32_t>(static_cast<int32_t>(marker.z)) << 16
            };
            return true;
        }
    return false;
}

void grant_start_storage(oa::Player& player, int32_t metal, int32_t energy) noexcept {
    constexpr int32_t storage_floor = 200;
    player.resource_flags |= 1;
    player.shared_energy_storage =
        static_cast<float>(energy < storage_floor ? storage_floor : energy);
    player.shared_metal_storage = static_cast<float>(metal < storage_floor ? storage_floor : metal);
}

StartResult spawn_player_commander(
    oa::World& w,
    Tables& tables,
    uint8_t player,
    const PlayerSetup& setup,
    std::span<const StartMarker> markers,
    int32_t index,
    uint8_t local_player,
    int32_t viewport_width,
    int32_t viewport_height,
    Host& h,
    StartHost& start
) {
    if (player >= OA_PLAYER_COUNT || player >= tables.setups.size())
        throw std::out_of_range("start player index");
    auto& p = w.game.players[player];
    auto& stored = tables.setups[player];
    stored.side = setup.side;
    stored.color = setup.color;
    grant_start_storage(p, setup.metal, setup.energy);
    Request request;
    request.player = player;
    request.finished = true;
    request.state = ground_occupancy_state;
    if (!start_position(markers, index, request.position)) {
        start.report_missing_start_position(index);
        // No commander without a start position; 3.1c can carry on after the report.
        return {};
    }
    request.type = start.commander_type_for_side(stored.side);
    auto* result = create(w, tables, request, h);
    if (player == local_player) {
        const auto x = std::bit_cast<int16_t>(static_cast<uint16_t>(request.position[0] >> 16));
        const auto z = std::bit_cast<int16_t>(static_cast<uint16_t>(request.position[2] >> 16));
        const auto camera_x = static_cast<uint32_t>(static_cast<int32_t>(x)) -
                              static_cast<uint32_t>(viewport_width / 2);
        const auto camera_z = static_cast<uint32_t>(static_cast<int32_t>(z)) -
                              static_cast<uint32_t>(viewport_height / 2);
        start.set_camera_position(
            std::bit_cast<int32_t>(camera_x), std::bit_cast<int32_t>(camera_z), 0
        );
    }
    return {true, result};
}

int32_t attached_child_count(const oa::World& w, const oa::Unit& unit) {
    const auto self = oa::world_unit_ref(&w, &unit);
    int32_t count = 0;
    uint32_t seen = 0;
    for (const auto* child = oa::world_unit(&w, unit.attach_first_child); child != nullptr;
         child = oa::world_unit(&w, child->attach_next)) {
        // The slot count is a 16-bit word, so a longer chain must cycle.
        if (++seen > 65535)
            throw std::invalid_argument(
                "attachment sibling chain exceeds the 16-bit unit index space"
            );
        if (child->attach_parent == self)
            ++count;
    }
    return count;
}

int32_t average_plot_height(
    std::span<const uint32_t, 3> position,
    std::span<const sim::spatial_state::Plot> plots,
    int32_t map_width,
    int32_t map_height
) {
    const auto high_word = [](uint32_t word) noexcept {
        return static_cast<int16_t>(static_cast<uint16_t>(word >> 16));
    };
    // Signed division by 16 truncates toward zero, including negatives.
    const auto cell_x = static_cast<int32_t>(high_word(position[0])) / 16;
    const auto cell_z = static_cast<int32_t>(high_word(position[2])) / 16;
    const auto index =
        sim::spatial_state::plot_index_at(cell_x, cell_z, plots, map_width, map_height);
    if (!index)
        return -1;
    const auto& plot = plots[*index];
    return (static_cast<int32_t>(plot.low_height) + static_cast<int32_t>(plot.high_height)) >> 1;
}

bool player_pay_energy(oa::Player& player, float& requested, float amount) noexcept {
    const bool accepted = amount <= player.energy;
    if (accepted) {
        player.energy -= amount;
        requested += amount;
    }
    return accepted;
}

bool player_pay_metal(oa::Player& player, float& requested, float amount) noexcept {
    const bool accepted = amount <= player.metal;
    if (accepted) {
        player.metal -= amount;
        requested += amount;
    }
    return accepted;
}

bool player_pay_resources(
    oa::Player& player, float& energy_requested, float& metal_requested, float energy, float metal
) noexcept {
    if (!(energy <= player.energy) || !(metal <= player.metal))
        return false;
    (void)player_pay_energy(player, energy_requested, energy);
    (void)player_pay_metal(player, metal_requested, metal);
    return true;
}
} // namespace oa::sim::unit_spawn
