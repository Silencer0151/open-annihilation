// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/mission_units.hpp"

#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace oa::sim::mission_units {
namespace {

inline constexpr size_t token_bytes = 256;
inline constexpr double fixed_one = 65536.0;
inline constexpr uint8_t schema_immunity_bit = data::campaign::mission_unit_flag::immunity;
inline constexpr uint32_t percent = 100;

// ASCII case-insensitive string equality.
bool same_name(const char* a, const char* b) noexcept {
    for (;; ++a, ++b) {
        const int ca = std::tolower(static_cast<unsigned char>(*a));
        const int cb = std::tolower(static_cast<unsigned char>(*b));
        if (ca != cb)
            return false;
        if (ca == 0)
            return true;
    }
}

// Truncation toward zero through 64 bits keeping the low 32 bits; NaN and
// out-of-range values give INT32_MIN.
int32_t truncate_to_i32(double value) noexcept {
    if (!(value > -9.2233720368547758e18 && value < 9.2233720368547758e18))
        return INT32_MIN;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<int64_t>(value)));
}

FixedVec3 map_point(float x, float z) noexcept {
    return FixedVec3{
        truncate_to_i32(static_cast<double>(x) * fixed_one),
        0,
        truncate_to_i32(static_cast<double>(z) * fixed_one)
    };
}

int32_t seconds_to_ticks(float seconds) noexcept {
    return truncate_to_i32(static_cast<double>(seconds) * static_cast<double>(ticks_per_second));
}

uint8_t order_for(
    const Hooks& hooks, OrderCategory category, Unit& unit, Unit* target, const FixedVec3* position
) {
    return hooks.order_for != nullptr
               ? hooks.order_for(hooks.context, category, unit, target, position)
               : 0;
}

void queue(
    const Hooks& hooks,
    uint8_t kind,
    Unit& unit,
    Unit* target,
    const FixedVec3* position,
    int32_t param_a,
    int32_t param_b
) {
    if (hooks.queue_order != nullptr)
        hooks.queue_order(
            hooks.context, kind, queue_append, unit, target, position, param_a, param_b
        );
}

void queue_named(
    const Hooks& hooks,
    const char* name,
    Unit& unit,
    Unit* target,
    const FixedVec3* position,
    int32_t param_a,
    int32_t param_b
) {
    const uint8_t kind = hooks.order_named != nullptr ? hooks.order_named(hooks.context, name) : 0;
    queue(hooks, kind, unit, target, position, param_a, param_b);
}

uint16_t type_id(const Hooks& hooks, const char* name) {
    return hooks.type_id != nullptr ? hooks.type_id(hooks.context, name) : 0;
}

bool has_movement_object(const Hooks& hooks, const Unit& unit) {
    return hooks.movement_object != nullptr ? hooks.movement_object(hooks.context, unit)
                                            : unit.movement != 0;
}

} // namespace

Unit* find_script_unit(const CreatedUnits& created, const char* name, const Unit* after) {
    int32_t index = 0;
    if (after != nullptr) {
        index = 1;
        if (created.count <= 0 || created.units[0] != after) {
            for (;;) {
                if (index >= created.count)
                    return nullptr;
                if (created.units[index++] == after)
                    break;
            }
        }
    }
    for (; index < created.count; ++index) {
        const data::campaign::MissionUnit& entry = created.schema[index];
        Unit* unit = created.units[index];
        if (entry.ident != nullptr && same_name(entry.ident, name) && unit != nullptr)
            return unit;
        if (entry.unit_name != nullptr && same_name(entry.unit_name, name) && unit != nullptr)
            return unit;
    }
    return nullptr;
}

void run_unit_script(
    Unit& unit,
    const char* script,
    const CreatedUnits& created,
    ScriptPoint& point,
    const Hooks& hooks
) {
    bool ordered = false;
    bool explicit_selectable = false;
    float& x = point.x;
    float& z = point.z;
    char token[token_bytes];
    char args[token_bytes];
    char name[token_bytes];
    const char* cursor = script;
    while (*cursor != '\0') {
        while (std::isspace(static_cast<unsigned char>(*cursor)))
            ++cursor;
        size_t length = std::strcspn(cursor, ",");
        const size_t copied = length < token_bytes - 1 ? length : token_bytes - 1;
        std::memcpy(token, cursor, copied);
        token[copied] = '\0';
        cursor += length;
        if (*cursor == ',')
            ++cursor;
        std::strcpy(args, copied > 0 ? token + 1 : token);
        switch (token[0]) {
        case 'a':
        case 'A': {
            if (std::sscanf(args, " %f %f", &x, &z) == 2) {
                const FixedVec3 target = map_point(x, z);
                queue(
                    hooks,
                    order_for(hooks, OrderCategory::attack, unit, nullptr, &target),
                    unit,
                    nullptr,
                    &target,
                    0,
                    0
                );
                ordered = true;
                explicit_selectable = true;
                break;
            }
            name[0] = '\0';
            (void)std::sscanf(args, " %255[a-zA-Z0-9_.]", name);
            const uint16_t type = type_id(hooks, name);
            if (type == 0)
                break;
            queue_named(hooks, order_attack_type, unit, nullptr, nullptr, type, 0);
            ordered = true;
            break;
        }
        case 'm':
        case 'M': {
            (void)std::sscanf(args, " %f %f", &x, &z);
            const FixedVec3 target = map_point(x, z);
            queue(
                hooks,
                order_for(hooks, OrderCategory::move, unit, nullptr, &target),
                unit,
                nullptr,
                &target,
                0,
                0
            );
            ordered = true;
            break;
        }
        case 'i':
        case 'I': {
            name[0] = '\0';
            (void)std::sscanf(args, " %255[a-zA-Z0-9_.]", name);
            Unit* carrier = find_script_unit(created, name, nullptr);
            if (carrier != nullptr && hooks.carry != nullptr)
                hooks.carry(hooks.context, unit, *carrier, carry_piece_none, 0);
            break;
        }
        case 'g':
        case 'G': {
            name[0] = '\0';
            (void)std::sscanf(args, " %255[a-zA-Z0-9_.]", name);
            Unit* guarded = find_script_unit(created, name, nullptr);
            if (guarded == nullptr)
                break;
            queue(
                hooks,
                order_for(hooks, OrderCategory::guard, unit, guarded, nullptr),
                unit,
                guarded,
                nullptr,
                0,
                0
            );
            ordered = true;
            break;
        }
        case 'd':
        case 'D':
            queue_named(hooks, order_self_destruct, unit, nullptr, nullptr, 1, 0);
            ordered = true;
            explicit_selectable = true;
            break;
        case 'b':
        case 'B': {
            int32_t count = 1;
            if (args[0] == 'w' || args[0] == 'W') {
                (void)std::sscanf(args + 1, " %d", &count);
                queue_named(hooks, order_build_weapon, unit, nullptr, nullptr, 0, count);
                break;
            }
            name[0] = '\0';
            (void)std::sscanf(args, " %255[a-zA-Z0-9_.] %d %f %f", name, &count, &x, &z);
            const FixedVec3 site = map_point(x, z);
            const uint16_t type = type_id(hooks, name);
            if (type == 0)
                break;
            if (has_movement_object(hooks, unit))
                queue_named(hooks, order_mobile_build, unit, nullptr, &site, type, count);
            else
                queue_named(hooks, order_building_build, unit, nullptr, nullptr, type, count);
            ordered = true;
            break;
        }
        case 'o':
        case 'O': {
            int32_t move = static_cast<int32_t>(
                (unit.flags & OA_UNIT_FLAG_MOVE_ORDER_MASK) >> OA_UNIT_FLAG_MOVE_ORDER_SHIFT
            );
            int32_t fire = static_cast<int32_t>(
                (unit.flags & OA_UNIT_FLAG_FIRE_ORDER_MASK) >> OA_UNIT_FLAG_FIRE_ORDER_SHIFT
            );
            (void)std::sscanf(args, " %d %d", &move, &fire);
            const uint32_t standing =
                ((static_cast<uint32_t>(fire) & 3u) << 2 | (static_cast<uint32_t>(move) & 3u))
                << OA_UNIT_FLAG_MOVE_ORDER_SHIFT;
            unit.flags =
                (unit.flags & ~(OA_UNIT_FLAG_MOVE_ORDER_MASK | OA_UNIT_FLAG_FIRE_ORDER_MASK)) |
                standing;
            break;
        }
        case 'p':
        case 'P': {
            float dwell = 0.0f;
            (void)std::sscanf(args, " %f %f %f", &x, &z, &dwell);
            const FixedVec3 target = map_point(x, z);
            const uint8_t kind = order_for(hooks, OrderCategory::patrol, unit, nullptr, &target);
            queue(hooks, kind, unit, nullptr, &target, seconds_to_ticks(dwell), 0);
            ordered = true;
            explicit_selectable = true;
            break;
        }
        case 's':
        case 'S':
            queue_named(hooks, order_make_selectable, unit, nullptr, nullptr, 0, 0);
            ordered = true;
            explicit_selectable = true;
            break;
        case 'u':
        case 'U': {
            (void)std::sscanf(args, " %f %f", &x, &z);
            const FixedVec3 target = map_point(x, z);
            queue(
                hooks,
                order_for(hooks, OrderCategory::unload, unit, nullptr, &target),
                unit,
                nullptr,
                &target,
                0,
                0
            );
            ordered = true;
            break;
        }
        case 'w':
        case 'W': {
            if (args[0] == 'a' || args[0] == 'A') {
                name[0] = '\0';
                Unit* watched = nullptr;
                if (std::sscanf(args + 1, " %255[a-zA-Z0-9.]", name) == 1)
                    watched = find_script_unit(created, name, nullptr);
                if (watched == nullptr)
                    watched = &unit;
                queue_named(hooks, order_wait_for_attack, unit, watched, nullptr, 0, 0);
            } else {
                float seconds = 0.0f;
                int32_t value = 0;
                (void)std::sscanf(args, " %f %d", &seconds, &value);
                queue_named(
                    hooks, order_wait, unit, nullptr, nullptr, seconds_to_ticks(seconds), value
                );
            }
            ordered = true;
            break;
        }
        default:
            break;
        }
    }
    if (!ordered)
        return;
    unit.flags &= ~OA_UNIT_FLAG_SELECTABLE;
    if (!explicit_selectable)
        queue_named(hooks, order_make_selectable, unit, nullptr, nullptr, 0, 0);
}

bool create_mission_units(
    World& world, const data::campaign::MissionUnit* schema, int32_t count, const Hooks& hooks
) {
    Unit** units = nullptr;
    if (count > 0) {
        units = static_cast<Unit**>(std::calloc(static_cast<size_t>(count), sizeof(Unit*)));
        if (units == nullptr)
            return false;
    }
    for (int32_t i = 0; i < count; ++i) {
        const data::campaign::MissionUnit& entry = schema[i];
        const UnitDef* def = hooks.find_def != nullptr && entry.unit_name != nullptr
                                 ? hooks.find_def(hooks.context, entry.unit_name)
                                 : nullptr;
        if (def == nullptr) {
            units[i] = nullptr;
            continue;
        }
        const auto player = static_cast<uint8_t>(entry.player - 1u);
        if (hooks.player_active == nullptr || !hooks.player_active(hooks.context, player)) {
            char message[128];
            std::snprintf(
                message,
                sizeof message,
                "Player number %d invalid for unit %s",
                entry.player,
                entry.unit_name
            );
            if (hooks.fatal != nullptr)
                hooks.fatal(hooks.context, message);
        }
        FixedVec3 position{entry.x, entry.y, entry.z};
        if (hooks.snap_to_build_grid != nullptr)
            hooks.snap_to_build_grid(hooks.context, *def, &position);
        Unit* unit =
            hooks.create_unit != nullptr
                ? hooks.create_unit(hooks.context, player, def->type_id, position, true, 1, 0)
                : nullptr;
        if (unit == nullptr)
            continue;
        unit->flags = (unit->flags & ~unit_flag_mission_immune) |
                      (static_cast<uint32_t>(entry.flags & schema_immunity_bit) << 8);
        const UnitDef* made = world_unit_def_of(&world, unit);
        const uint32_t max_damage = made != nullptr ? made->max_damage : def->max_damage;
        unit->health = static_cast<int16_t>(static_cast<uint16_t>(
            max_damage * static_cast<uint32_t>(static_cast<int32_t>(entry.health_percent)) / percent
        ));
        unit->heading = static_cast<oa_angle>(entry.angle);
        units[i] = unit;
    }
    const CreatedUnits created{schema, units, count};
    ScriptPoint point;
    for (int32_t i = 0; i < count; ++i)
        if (schema[i].initial_mission != nullptr && units[i] != nullptr)
            run_unit_script(*units[i], schema[i].initial_mission, created, point, hooks);
    if (count < 1 && hooks.no_mission_units != nullptr)
        hooks.no_mission_units(hooks.context);
    std::free(units);
    return true;
}

void kill_units_of_type(World& world, uint16_t type_index, const Hooks& hooks) {
    if (type_index == 0 || world.units == nullptr || hooks.kill == nullptr)
        return;
    for (uint32_t slot = 0; slot < world.unit_slot_count; ++slot)
        if (world.units[slot].type_index == type_index)
            hooks.kill(hooks.context, world.units[slot], kill_outcome_removed);
}

void kill_all_units(World& world, const Hooks& hooks) {
    if (world.units == nullptr || hooks.kill == nullptr)
        return;
    for (uint32_t slot = 0; slot < world.unit_slot_count; ++slot)
        if (world.units[slot].type_index != 0)
            hooks.kill(hooks.context, world.units[slot], kill_outcome_removed);
}

} // namespace oa::sim::mission_units
