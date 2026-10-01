// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/combat_state.hpp"
#include "oa/core/weapon_def.h"
#include <algorithm>
#include <cmath>
#include <bit>
#include <limits>
#include <cerrno>
#include <cstdlib>
#include <cstring>
#include <vector>

namespace oa::sim::combat_state {
namespace {
constexpr uint8_t preserved_flag_bits = 0xf0;
constexpr uint8_t initialized_flag = 0x10;
constexpr uint8_t nondefault_definition_flag = 0x02;
constexpr unsigned slot_index_shift = 2;

int32_t truncate_low32(double value) noexcept {
    constexpr double signed64_limit = 9223372036854775808.0;
    if (!std::isfinite(value) || value >= signed64_limit || value < -signed64_limit)
        return 0;
    const auto wide = static_cast<int64_t>(std::trunc(value));
    return static_cast<int32_t>(static_cast<uint32_t>(wide));
}

/// Arms one slot: its definition, a cleared stockpile, its flags and its muzzle offset.
void initialize_slot(
    WeaponSlot& slot, const WeaponDefinition* definition, size_t index, const SlotGeometry& geometry
) noexcept {
    slot.definition = definition;
    slot.stockpile = 0;
    slot.flags = static_cast<uint8_t>(
        (slot.flags & preserved_flag_bits) | initialized_flag |
        (static_cast<uint8_t>(index) << slot_index_shift) |
        (definition->registry_index != 0 ? nondefault_definition_flag : 0)
    );
    const auto difference = static_cast<int32_t>(
        static_cast<uint32_t>(geometry.muzzle_z) - static_cast<uint32_t>(geometry.aim_from_z)
    );
    slot.muzzle_offset = truncate_low32(static_cast<double>(difference) * weapon_range_scale);
}

/// Returns 1.25 times the wrapped Z difference of the two pieces, truncated toward
/// zero with the low 32 bits kept.
int32_t calculate_muzzle_offset(const SlotGeometry& geometry) noexcept {
    const auto difference = static_cast<int32_t>(
        static_cast<uint32_t>(geometry.muzzle_z) - static_cast<uint32_t>(geometry.aim_from_z)
    );
    return truncate_low32(static_cast<double>(difference) * weapon_range_scale);
}
} // namespace

InitializationResult initialize_weapon_slots(
    UnitWeapons& unit, const std::array<SlotGeometry, weapon_slot_count>& geometry
) noexcept {
    uint16_t maximum_reload_ticks = 0;
    for (size_t index = 0; index < weapon_slot_count; ++index) {
        auto& slot = unit.slots[index];
        const auto* definition = unit.definitions[index];
        initialize_slot(slot, definition, index, geometry[index]);
        maximum_reload_ticks = std::max(maximum_reload_ticks, definition->reload_time_ticks);
    }
    return {
        static_cast<int32_t>(maximum_reload_ticks) * milliseconds_per_second /
        simulation_ticks_per_second
    };
}

WeaponRegistry::WeaponRegistry() {
    for (size_t i = 0; i < weapon_registry_capacity; ++i)
        definitions_[i].registry_index = static_cast<uint8_t>(i);
}

void WeaponRegistry::install(const WeaponRecord& record) {
    if (record.name.empty() || record.name.size() >= 32 ||
        record.name.find('\0') != std::string::npos)
        throw std::invalid_argument("weapon internal name must be 1..31 bytes");
    if (!std::isfinite(record.reload_time_seconds))
        throw std::invalid_argument("weapon reloadtime must be finite");
    const double ticks =
        record.reload_time_seconds * static_cast<double>(simulation_ticks_per_second);
    constexpr double signed64_limit = 9223372036854775808.0;
    const auto wide = (ticks >= signed64_limit || ticks < -signed64_limit)
                          ? std::numeric_limits<int64_t>::min()
                          : static_cast<int64_t>(std::trunc(ticks));
    definitions_[record.index].reload_time_ticks =
        static_cast<uint16_t>(static_cast<uint64_t>(wide));
    names_[record.index] = record.name;
}

void WeaponRegistry::install_tdf_section(
    uint8_t index, std::string_view section_name, std::string_view reload_time_text
) {
    double value = 0.0;
    if (!reload_time_text.empty()) {
        std::string text(reload_time_text);
        char* end = nullptr;
        errno = 0;
        value = std::strtod(text.c_str(), &end);
        if (end == text.c_str() || errno == ERANGE || !std::isfinite(value))
            throw std::invalid_argument("invalid weapon reloadtime");
    }
    install({index, std::string(section_name), value});
}

void WeaponRegistry::install_tdf_section(
    std::string_view id_text, std::string_view section_name, std::string_view reload_time_text
) {
    std::string text(id_text);
    char* end = nullptr;
    errno = 0;
    const long value = std::strtol(text.c_str(), &end, 10);
    if (end == text.c_str() || errno == ERANGE || value < 0 ||
        value >= static_cast<long>(weapon_registry_capacity))
        throw std::invalid_argument("weapon TDF ID is outside 0..255");
    install_tdf_section(static_cast<uint8_t>(value), section_name, reload_time_text);
}

void WeaponRegistry::install_target_fields(
    uint8_t index,
    std::string_view range_text,
    std::string_view line_of_sight,
    std::string_view ballistic,
    std::string_view paralyzer,
    std::string_view water_weapon,
    std::string_view to_air_weapon,
    std::string_view default_damage,
    std::string_view projectile_velocity,
    std::string_view minimum_barrel_angle,
    std::string_view turret,
    std::string_view vlaunch,
    std::string_view energy_per_shot,
    std::string_view metal_per_shot,
    std::string_view weapontimer,
    std::string_view rendertype,
    std::string_view color,
    std::string_view color2,
    std::string_view areaofeffect,
    std::string_view noautorange,
    std::string_view commandfire,
    std::string_view unitsonly,
    std::string_view groundbounce,
    std::string_view interceptor,
    std::string_view accuracy,
    std::string_view tolerance,
    std::string_view pitch_tolerance,
    std::string_view start_velocity,
    std::string_view acceleration,
    std::string_view turn_rate,
    std::string_view selfprop,
    std::string_view guidance,
    std::string_view burnblow,
    std::string_view burst,
    std::string_view burstrate
) {
    const auto integer = [](std::string_view text, long fallback) {
        if (text.empty())
            return fallback;
        std::string copy(text);
        char* end = nullptr;
        errno = 0;
        const auto value = std::strtol(copy.c_str(), &end, 10);
        return end == copy.c_str() || errno == ERANGE ? fallback : value;
    };
    auto& definition = definitions_[index];
    definition.range_world_units = static_cast<int32_t>(integer(range_text, 0x7fff));
    definition.default_damage = static_cast<uint16_t>(integer(default_damage, 0));
    const auto floating = [](std::string_view text, double fallback) {
        if (text.empty())
            return fallback;
        std::string copy(text);
        char* end = nullptr;
        errno = 0;
        const auto value = std::strtod(copy.c_str(), &end);
        return end == copy.c_str() || errno == ERANGE ? fallback : value;
    };
    definition.projectile_velocity =
        truncate_low32(floating(projectile_velocity, 0.0) * weapon_velocity_tdf_to_fixed);
    constexpr double radians_per_degree = 0.017453292519943278;
    definition.minimum_barrel_angle_radians =
        static_cast<float>(floating(minimum_barrel_angle, -11.25) * radians_per_degree);
    const auto flag = [&](std::string_view text, uint32_t bit) {
        if ((static_cast<uint32_t>(integer(text, 0)) & 1U) != 0)
            definition.flags |= bit;
        else
            definition.flags &= ~bit;
    };
    flag(line_of_sight, weapon_line_of_sight_flag);
    flag(ballistic, weapon_ballistic_flag);
    flag(paralyzer, weapon_paralyzer_flag);
    flag(water_weapon, weapon_water_flag);
    flag(to_air_weapon, weapon_to_air_flag);
    flag(turret, weapon_turret_flag);
    flag(vlaunch, weapon_vlaunch_flag);
    flag(noautorange, weapon_noautorange_flag);
    flag(commandfire, weapon_commandfire_flag);
    flag(unitsonly, weapon_ground_skip_flag);
    flag(groundbounce, weapon_ground_bounce_flag);
    flag(interceptor, OA_WEAPON_FLAG_INTERCEPTOR);
    flag(selfprop, weapon_selfprop_flag);
    flag(guidance, weapon_guidance_flag);
    flag(burnblow, weapon_burnblow_flag);
    definition.energy_per_shot = static_cast<float>(floating(energy_per_shot, 0.0));
    definition.metal_per_shot = static_cast<float>(floating(metal_per_shot, 0.0));
    definition.weapontimer_ticks = static_cast<uint16_t>(
        truncate_low32(floating(weapontimer, 0.0) * simulation_ticks_per_second)
    );
    definition.rendertype = static_cast<uint8_t>(integer(rendertype, 0));
    definition.color = static_cast<uint8_t>(integer(color, 0));
    definition.color2 = static_cast<uint8_t>(integer(color2, 0));
    definition.areaofeffect = static_cast<uint16_t>(integer(areaofeffect, 0));
    definition.accuracy = static_cast<int16_t>(integer(accuracy, 0));
    definition.tolerance = static_cast<uint16_t>(integer(tolerance, 0));
    definition.pitch_tolerance = static_cast<uint16_t>(integer(pitch_tolerance, 0));
    definition.start_velocity =
        truncate_low32(floating(start_velocity, 0.0) * weapon_velocity_tdf_to_fixed);
    definition.acceleration =
        truncate_low32(floating(acceleration, 0.0) * weapon_acceleration_tdf_to_fixed);
    definition.turn_rate = static_cast<uint16_t>(
        truncate_low32(floating(turn_rate, 0.0) * weapon_turn_rate_tdf_to_tick)
    );
    // burst is read as an integer (WeaponDef.burst); burstrate seconds times
    // 30, truncated, is WeaponDef.burst_rate.
    definition.burst = static_cast<uint16_t>(integer(burst, 0));
    definition.burst_rate_ticks = static_cast<uint16_t>(
        truncate_low32(floating(burstrate, 0.0) * simulation_ticks_per_second)
    );
}

void WeaponRegistry::install_explosion_sprites(
    uint8_t index,
    std::string_view explosion_gaf,
    std::string_view explosion_art,
    std::string_view water_explosion_gaf,
    std::string_view water_explosion_art,
    std::string_view lava_explosion_gaf,
    std::string_view lava_explosion_art
) {
    auto& definition = definitions_[index];
    definition.explosion_gaf.assign(explosion_gaf);
    definition.explosion_art.assign(explosion_art);
    definition.water_explosion_gaf.assign(water_explosion_gaf);
    definition.water_explosion_art.assign(water_explosion_art);
    definition.lava_explosion_gaf.assign(lava_explosion_gaf);
    definition.lava_explosion_art.assign(lava_explosion_art);
}

void WeaponRegistry::install_sounds(
    uint8_t index,
    std::string_view soundstart,
    std::string_view soundhit,
    std::string_view soundwater
) {
    auto& definition = definitions_[index];
    definition.soundstart.assign(soundstart);
    definition.soundhit.assign(soundhit);
    definition.soundwater.assign(soundwater);
}

namespace {
double tdf_double(std::string_view text, double fallback) {
    if (text.empty())
        return fallback;
    std::string copy(text);
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtod(copy.c_str(), &end);
    return end == copy.c_str() || errno == ERANGE ? fallback : value;
}

long tdf_integer(std::string_view text, long fallback) {
    if (text.empty())
        return fallback;
    std::string copy(text);
    char* end = nullptr;
    errno = 0;
    const auto value = std::strtol(copy.c_str(), &end, 10);
    return end == copy.c_str() || errno == ERANGE ? fallback : value;
}

uint16_t tdf_ticks(std::string_view seconds) {
    return static_cast<uint16_t>(
        truncate_low32(tdf_double(seconds, 0.0) * simulation_ticks_per_second)
    );
}
} // namespace

void WeaponRegistry::install_flight_fields(uint8_t index, const WeaponFlightFields& fields) {
    auto& definition = definitions_[index];
    definition.edge_effectiveness = static_cast<float>(tdf_double(fields.edge_effectiveness, 0.0));
    definition.spray_angle = static_cast<uint16_t>(tdf_integer(fields.spray_angle, 0));
    definition.duration_ticks = tdf_ticks(fields.duration);
    definition.random_decay_ticks = tdf_ticks(fields.random_decay);
    definition.flight_time_ticks = tdf_ticks(fields.flight_time);
    definition.smoke_delay_ticks = tdf_ticks(fields.smoke_delay);
    definition.coverage = static_cast<int32_t>(tdf_integer(fields.coverage, 0));
    definition.shake_magnitude = static_cast<int32_t>(tdf_integer(fields.shake_magnitude, 0));
    // The whole 32-bit truncated result, unlike the word-sized tick fields.
    definition.shake_duration_ticks =
        truncate_low32(tdf_double(fields.shake_duration, 0.0) * simulation_ticks_per_second);
    const auto flag = [&](std::string_view text, uint32_t bit) {
        if ((static_cast<uint32_t>(tdf_integer(text, 0)) & 1U) != 0)
            definition.flags |= bit;
        else
            definition.flags &= ~bit;
    };
    flag(fields.beam_weapon, weapon_beam_flag);
    flag(fields.meteor, weapon_meteor_flag);
    flag(fields.dropped, weapon_dropped_flag);
    flag(fields.start_smoke, weapon_start_smoke_flag);
    flag(fields.end_smoke, weapon_end_smoke_flag);
    flag(fields.sound_trigger, weapon_sound_trigger_flag);
    flag(fields.tracks, weapon_tracks_flag);
    flag(fields.smoke_trail, weapon_smoke_trail_flag);
    flag(fields.propeller, weapon_propeller_flag);
    flag(fields.two_phase, weapon_two_phase_flag);
    flag(fields.cruise, weapon_cruise_flag);
    flag(fields.stockpile, weapon_stockpile_flag);
    flag(fields.targetable, weapon_targetable_flag);
    flag(fields.no_explode, weapon_no_explode_flag);
    flag(fields.shell_weapon, weapon_shell_flag);
    flag(fields.no_radar, weapon_no_radar_flag);
}

void WeaponRegistry::install_damage_override(
    uint8_t index, std::string_view unit_name, std::string_view amount_text
) {
    auto& overrides = definitions_[index].damage_overrides;
    std::string name(unit_name);
    for (auto& c : name)
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c + ('a' - 'A'));
    const auto amount = static_cast<int32_t>(tdf_integer(amount_text, 0));
    for (auto& entry : overrides)
        if (entry.unit_name == name) {
            entry.amount = amount;
            return;
        }
    overrides.push_back({std::move(name), amount});
}

int32_t damage_against(const WeaponDefinition& definition, std::string_view unit_name) noexcept {
    for (const auto& entry : definition.damage_overrides) {
        if (entry.unit_name.size() != unit_name.size())
            continue;
        bool same = true;
        for (size_t i = 0; i < unit_name.size() && same; ++i) {
            auto c = unit_name[i];
            if (c >= 'A' && c <= 'Z')
                c = static_cast<char>(c + ('a' - 'A'));
            same = c == entry.unit_name[i];
        }
        if (same)
            return entry.amount;
    }
    return definition.default_damage;
}

namespace {
char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

bool equal_name(std::string_view left, std::string_view right) noexcept {
    if (left.size() != right.size())
        return false;
    for (size_t i = 0; i < left.size(); ++i)
        if (ascii_lower(left[i]) != ascii_lower(right[i]))
            return false;
    return true;
}
} // namespace

const WeaponDefinition* WeaponRegistry::find(std::string_view name) const noexcept {
    if (name.empty())
        return nullptr;
    for (size_t i = 0; i < weapon_registry_capacity; ++i)
        if (!names_[i].empty() && equal_name(names_[i], name))
            return &definitions_[i];
    return nullptr;
}

WeaponBinding bind_unit_weapons(
    const WeaponRegistry& registry, const std::array<std::string_view, weapon_slot_count>& names
) noexcept {
    WeaponBinding result;
    for (size_t i = 0; i < weapon_slot_count; ++i) {
        const auto* found = registry.find(names[i]);
        result.weapons.definitions[i] = found ? found : &registry.default_definition();
        result.resolved_nondefault_weapon |= found && found->registry_index != 0;
    }
    return result;
}

SpawnCombatResult initialize_spawn_combat(
    UnitWeapons& weapons,
    const WeaponRegistry& registry,
    const std::array<std::string_view, weapon_slot_count>& names,
    SpawnGeometryHost& host
) {
    SpawnCombatResult result;
    const auto binding = bind_unit_weapons(registry, names);
    weapons.definitions = binding.weapons.definitions;
    result.resolved_nondefault_weapon = binding.resolved_nondefault_weapon;
    uint16_t maximum_reload_ticks = 0;
    for (size_t i = 0; i < weapon_slot_count; ++i) {
        const auto* definition = weapons.definitions[i];
        // The slot is armed before either geometry callback runs.
        weapons.slots[i].definition = definition;
        weapons.slots[i].stockpile = 0;
        weapons.slots[i].flags = static_cast<uint8_t>(
            (weapons.slots[i].flags & preserved_flag_bits) | initialized_flag |
            (static_cast<uint8_t>(i) << slot_index_shift) |
            (definition->registry_index != 0 ? nondefault_definition_flag : 0)
        );
        const auto slot = static_cast<uint8_t>(i);
        const auto muzzle = host.query_weapon_world(slot);
        const auto aim = host.aim_from_world(slot);
        weapons.slots[i].muzzle_offset = calculate_muzzle_offset({muzzle[2], aim[2]});
        const auto* current_definition = weapons.slots[i].definition;
        if (!current_definition)
            throw std::runtime_error("geometry callback cleared weapon definition");
        maximum_reload_ticks =
            std::max(maximum_reload_ticks, current_definition->reload_time_ticks);
    }
    result.initialization.maximum_reload_milliseconds = static_cast<int32_t>(maximum_reload_ticks) *
                                                        milliseconds_per_second /
                                                        simulation_ticks_per_second;
    host.set_max_reload_time(result.initialization.maximum_reload_milliseconds);
    return result;
}

namespace {
bool category_bit(std::span<const uint32_t> words, uint16_t category) noexcept {
    const auto word = static_cast<size_t>(category >> 5U);
    return word < words.size() && (words[word] & (uint32_t{1} << (category & 31U))) != 0;
}

uint32_t randomized_distance_score(
    const TargetSource& source, const TargetUnit& candidate, TargetSearchHost& host
) {
    const auto dx = std::bit_cast<int32_t>(source.position[0] - candidate.position[0]);
    const auto dz = std::bit_cast<int32_t>(source.position[2] - candidate.position[2]);
    const auto high = [](int32_t value) {
        const auto square = static_cast<uint64_t>(static_cast<int64_t>(value) * value);
        return static_cast<uint32_t>(square >> 32U);
    };
    return host.random_bounded(high(dx) + high(dz));
}
} // namespace

UnitIdentity select_automatic_target(
    const TargetSource& source, const TargetSearchRequest& request, TargetSearchHost& host
) {
    if (request.weapon_slot >= weapon_slot_count)
        return 0;
    const bool owner_override = source.owner_present && source.owner_status == 2;
    const auto radius = host.search_radius(source, request);
    const auto nearby = host.gather_nearby(source.owner_spatial_index, source.position, radius);
    std::vector<const TargetUnit*> candidates;
    candidates.reserve(nearby.size());
    for (const auto& candidate : nearby)
        candidates.push_back(&candidate);
    UnitIdentity category_match = 0, ordinary = 0;
    int32_t category_match_score = std::numeric_limits<int32_t>::max();
    int32_t ordinary_score = std::numeric_limits<int32_t>::max();
    for (uint32_t sampled = 0; sampled < target_search_sample_limit && !candidates.empty();
         ++sampled) {
        const auto selected = host.random_bounded(static_cast<uint32_t>(candidates.size()));
        const auto index = selected < candidates.size() ? static_cast<size_t>(selected) : size_t{};
        const auto& candidate = *candidates[index];
        candidates[index] = candidates.back();
        candidates.pop_back();
        if ((candidate.unit_flags & unit_targetable_flag) == 0 ||
            (candidate.unit_flags & unit_excluded_from_auto_target_flag) != 0 ||
            ((candidate.type_auto_target_flags & candidate_type_auto_target_flag) == 0 &&
             !owner_override && !host.global_target_override()) ||
            ((source.type_flags & source_range_check_bypass_flag) == 0 &&
             !host.weapon_can_reach(source, candidate, request.weapon_slot)) ||
            (!request.explicit_radius &&
             category_bit(source.implicit_excluded_category_mask, candidate.category)) ||
            ((source.weapon_flags[request.weapon_slot] & weapon_paralyzer_low_byte_flag) != 0 &&
             (candidate.candidate_flags & candidate_paralyzed_flag) != 0))
            continue;
        const auto score =
            std::bit_cast<int32_t>(randomized_distance_score(source, candidate, host));
        if (category_bit(
                source.preferred_category_masks[request.weapon_slot], candidate.category
            )) {
            if (score < category_match_score) {
                category_match_score = score;
                category_match = candidate.identity;
            }
        } else if (score < ordinary_score) {
            ordinary_score = score;
            ordinary = candidate.identity;
        }
    }
    // The nearest candidate outside the per-weapon mask wins over any inside it.
    return ordinary != 0 ? ordinary : category_match;
}

void gather_sightings(
    std::span<const uint16_t> seen,
    std::span<const uint16_t> radar,
    bool radar_fallback,
    const std::array<uint32_t, 3>& center,
    int32_t radius,
    IntelligenceHost& host,
    std::vector<TargetUnit>& found
) {
    const auto radius_squared =
        std::bit_cast<int32_t>(static_cast<uint32_t>(radius) * static_cast<uint32_t>(radius));
    const auto append = [&](std::span<const uint16_t> slots) {
        for (const auto slot : slots) {
            const UnitIdentity identity = slot;
            if (!host.unit_active(identity))
                continue;
            const auto* unit = host.resolve(identity);
            if (!unit)
                continue;
            const auto dx = std::bit_cast<int32_t>(center[0] - unit->position[0]);
            const auto dz = std::bit_cast<int32_t>(center[2] - unit->position[2]);
            const auto high = [](int32_t value) {
                return static_cast<uint32_t>(
                    static_cast<uint64_t>(static_cast<int64_t>(value) * value) >> 32U
                );
            };
            if (std::bit_cast<int32_t>(high(dx) + high(dz)) <= radius_squared)
                found.push_back(*unit);
        }
    };
    append(seen);
    if (found.empty() && radar_fallback)
        append(radar);
}

namespace {
uint8_t low_byte(int32_t value) noexcept {
    return static_cast<uint8_t>(static_cast<uint32_t>(value));
}

constexpr float coefficient(uint32_t bits) noexcept {
    return std::bit_cast<float>(bits);
}
} // namespace

int32_t classify_type(const StrategicType& type, const StrategicRefreshContext& context) noexcept {
    const auto resource = energy_rate(
        type.energy_use,
        type.wind_generator,
        type.tidal_generator,
        context.wind_factor,
        context.tidal_strength
    );
    WeaponScoreInput weapon_inputs[3]{};
    for (int slot = 0; slot < 3; ++slot)
        weapon_inputs[slot] = {
            type.weapons[static_cast<size_t>(slot)].registry_index != 0,
            type.weapons[static_cast<size_t>(slot)].range_world_units,
            type.weapons[static_cast<size_t>(slot)].damage_default
        };
    auto classification_base = (type.extracts_metal != 0.0F ? 11 : 1) +
                               (type.makes_metal != 0 ? 10 : 0) + (resource < 0.0F ? 10 : 0);
    classification_base = truncate_low32(
        static_cast<double>(type.build_cost_metal) * coefficient(0x3c23d70aU) + classification_base
    );
    const auto weapon_score =
        threat_score((type.abilities & ability_can_attack) != 0, weapon_inputs);
    return std::clamp(
        truncate_low32(
            static_cast<double>(classification_base) +
            static_cast<double>(type.build_cost_energy) * coefficient(0x3b03126fU)
        ) + static_cast<int8_t>(low_byte(weapon_score)),
        -100,
        100
    );
}

void strategic_refresh(
    StrategicRefreshState& state,
    std::span<const StrategicType> types,
    const StrategicRefreshContext& context
) {
    state.classifications.assign(types.size(), 0);
    state.strengths.assign(types.size(), {});
    state.owned_counts.resize(types.size());
    for (size_t index = 1; index < types.size(); ++index) {
        const auto& type = types[index];
        const auto resource = static_cast<double>(energy_rate(
            type.energy_use,
            type.wind_generator,
            type.tidal_generator,
            context.wind_factor,
            context.tidal_strength
        ));
        const auto classification = classify_type(type, context);
        state.classifications[index] = low_byte(classification);
        auto priority = 1;
        if ((type.abilities & ability_can_attack) != 0)
            priority = 21;
        if ((type.flags & def_flag_builder) != 0 && state.owned_counts[index] < 3)
            priority += 30;
        if (resource < 0.0F)
            priority += 50;
        if (type.extracts_metal != 0.0F)
            priority += 50;
        if (type.makes_metal != 0)
            priority += 25;
        if ((type.flags & def_flag_can_fly) != 0)
            priority += 40;
        if (type.sonar_distance != 0)
            priority += 15;
        if (type.radar_distance != 0)
            priority += 5;
        priority = truncate_low32(
            static_cast<double>(priority) +
            std::clamp(static_cast<double>(type.energy_make), 0.0, 30.0)
        );
        if (state.owned_counts[index] == 0)
            priority = std::bit_cast<int32_t>(static_cast<uint32_t>(priority) << 2U);
        if (state.owned_counts[index] == 1)
            priority = std::bit_cast<int32_t>(static_cast<uint32_t>(priority) << 1U);
        if (type.min_water_depth >= 0)
            priority = std::bit_cast<int32_t>(static_cast<uint32_t>(priority) * 3U);
        if ((context.pool_units_per_player >> 1U) < context.owner_unit_count)
            priority += classification / 2;
        if ((type.abilities & ability_can_load) != 0 || (type.flags & def_flag_is_feature) != 0 ||
            (type.wind_generator != 0.0F && context.wind_max < context.wind_strength_divisor / 2))
            priority = 0;
        if (priority > 99)
            priority = 100;
        state.strengths[index][0] = low_byte(priority);
        const auto rounded_cost = static_cast<float>(
            static_cast<double>(type.build_cost_energy) * coefficient(0xbb23d70aU)
        );
        const auto energy_value =
            std::clamp(static_cast<double>(rounded_cost) - resource * 5.0, 0.0, 100.0);
        state.strengths[index][2] = low_byte(truncate_low32(energy_value));
        const auto metal_value = std::clamp(
            static_cast<double>(type.build_cost_metal) * coefficient(0xbca3d70aU) +
                (type.extracts_metal != 0.0F ? 100.0 : 0.0) + (type.makes_metal != 0 ? 25.0 : 0.0),
            0.0,
            100.0
        );
        state.strengths[index][1] = low_byte(truncate_low32(metal_value));
    }
}

bool issue_attack_order(
    const AttackSource& source, const TargetUnit& target, bool forced, AttackHost& host
) {
    if (source.identity == target.identity ||
        (((source.flags & standing_move_order_mask) == 0) && !forced) ||
        (((source.flags & standing_fire_order_mask) == 0) && !forced))
        return false;
    const auto attack_kind = host.resolve_order(attack_order_kind, source, &target, nullptr);
    if (attack_kind == 0)
        return false;
    std::array<AttackOrderRequest, 2> orders{};
    size_t count = 1;
    if ((source.flags & standing_move_order_mask) == standing_move_order_manoeuvre && !forced) {
        const auto move_kind =
            host.resolve_order(move_order_kind, source, nullptr, &source.position);
        orders[0] = {move_kind, 0, source.position, true, 0, 0, 0};
        orders[1] = {
            attack_kind,
            target.identity,
            {},
            false,
            source.maneuver_leash_length,
            std::bit_cast<int16_t>(static_cast<uint16_t>(source.position[0] >> 16U)),
            std::bit_cast<int16_t>(static_cast<uint16_t>(source.position[2] >> 16U))
        };
        count = 2;
    } else
        orders[0] = {attack_kind, target.identity, {}, false, 0, 0, 0};
    return host.commit_orders(source, std::span<const AttackOrderRequest>(orders.data(), count));
}

int threat_score(bool can_attack, const WeaponScoreInput weapons[3]) noexcept {
    int score = can_attack ? 0xb : 1;
    for (int slot = 0; slot < 3; ++slot) {
        if (!weapons[slot].present)
            continue;
        score += weapons[slot].range_world_units / 100 + 5 +
                 static_cast<int>(weapons[slot].default_damage) / 0x28;
    }
    if (score > 100)
        return 100;
    if (score < -100)
        return -100;
    return score;
}

float energy_rate(
    float energy_use,
    float wind_generator,
    float tidal_generator,
    float wind_factor,
    float tidal_factor
) noexcept {
    if (energy_use != 0.0F)
        return energy_use;
    if (!(wind_generator <= 0.0F))
        return -wind_factor * wind_generator;
    if (!(tidal_generator <= 0.0F))
        return -tidal_factor * tidal_generator;
    return 0.0F;
}
} // namespace oa::sim::combat_state
