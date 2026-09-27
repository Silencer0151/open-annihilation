// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Campaign mission start-up units: creating the schema's placed units and
// running each unit's InitialMission script into queued orders, plus the
// kill-by-type sweeps used by mission cheats.
#pragma once

#include "oa/core/world.h"
#include "oa/data/campaign/campaign_file.hpp"

#include <cstdint>

namespace oa::sim::mission_units {

// Categories the order chooser maps to a unit-specific order kind.
enum class OrderCategory : uint8_t {
    move = 2,
    attack = 3,
    unload = 5,
    guard = 7,
    patrol = 9,
};

// Order names the script parser queues directly.
inline constexpr const char* order_attack_type = "ATTACKUTYPE";
inline constexpr const char* order_self_destruct = "SELFDESTRUCTFG";
inline constexpr const char* order_build_weapon = "BUILDWEAPON";
inline constexpr const char* order_mobile_build = "MOBILEBUILD";
inline constexpr const char* order_building_build = "BUILDINGBUILD";
inline constexpr const char* order_make_selectable = "MAKESELECTABLE";
inline constexpr const char* order_wait_for_attack = "WAITFORATTACK";
inline constexpr const char* order_wait = "WAIT";

// Flag the parser passes with every queued order (append to the queue).
inline constexpr uint32_t queue_append = 1;
// Script-issued waits and patrol dwell times are given in seconds.
inline constexpr float ticks_per_second = 30.0f;
// Unit.flags bit set from the schema's immunity flag.
inline constexpr uint32_t unit_flag_mission_immune = 0x00008000u;
// Kill outcome used by the mission sweeps.
inline constexpr uint8_t kill_outcome_removed = 8;
inline constexpr uint8_t carry_piece_none = 0xff;

// Services owned by the spawn, order, definition and outcome systems.
struct Hooks {
    void* context{};
    // Unit definition by FBI name; null when unknown.
    const UnitDef* (*find_def)(void* context, const char* name){};
    // Catalogue type id; 0 when unknown.
    uint16_t (*type_id)(void* context, const char* name){};
    // Whether a player slot is active.
    bool (*player_active)(void* context, int32_t player){};
    // Error report; the engine continues after it.
    void (*fatal)(void* context, const char* message){};
    // Snaps a structure's position to the build grid.
    void (*snap_to_build_grid)(void* context, const UnitDef& def, FixedVec3* position){};
    // sim::unit_spawn::create.
    Unit* (*create_unit)(
        void* context,
        uint8_t player,
        uint16_t type_id,
        const FixedVec3& position,
        bool finished,
        uint32_t state,
        uint16_t requested_slot
    ){};
    // Whether the unit has a movement object (made only for bmcode 1 types).
    // Null reads Unit.movement.
    bool (*movement_object)(void* context, const Unit& unit){};
    // The unit-specific order kind for a category, target and position.
    uint8_t (*order_for)(
        void* context, OrderCategory category, Unit& unit, Unit* target, const FixedVec3* position
    ){};
    // Order kind by order-table name.
    uint8_t (*order_named)(void* context, const char* name){};
    // Queues an order on a unit.
    void (*queue_order)(
        void* context,
        uint8_t kind,
        uint32_t flags,
        Unit& unit,
        Unit* target,
        const FixedVec3* position,
        int32_t param_a,
        int32_t param_b
    ){};
    // Match::set_carry_link.
    void (*carry)(void* context, Unit& child, Unit& parent, uint8_t piece, uint8_t mode){};
    // Tells the mission state that the schema created no unit.
    void (*no_mission_units)(void* context){};
    // Kills a unit with an outcome.
    void (*kill)(void* context, Unit& unit, uint8_t outcome){};
};

// The units created for each schema entry (null where creation failed),
// indexed like the schema table.
struct CreatedUnits {
    const data::campaign::MissionUnit* schema{};
    Unit* const* units{};
    int32_t count{};
};

/// Finds the unit created for a schema entry by name.
///
/// @param created units created for each schema entry
/// @param name Ident or Unitname to match, case-insensitively
/// @param after search after the entry that created this unit; null searches all
/// @return the unit, or null
[[nodiscard]] Unit*
find_script_unit(const CreatedUnits& created, const char* name, const Unit* after);

// The map point the script commands share. It persists from one unit's
// script to the next, so a command whose numbers do not parse reuses the last
// point of an earlier unit's script.
struct ScriptPoint {
    float x{};
    float z{};
};

/// Runs one InitialMission script for a unit: comma-separated commands queued as orders.
///
/// A unit that received orders stays unselectable until its script makes it selectable
/// (an implicit MAKESELECTABLE is appended otherwise).
///
/// @param[in,out] unit unit the script belongs to
/// @param script InitialMission text
/// @param created units created for each schema entry, for named targets
/// @param[in,out] point map point the commands share across scripts
/// @param hooks order, definition and carry services
void run_unit_script(
    Unit& unit,
    const char* script,
    const CreatedUnits& created,
    ScriptPoint& point,
    const Hooks& hooks
);

/// Creates the schema's units, then runs their scripts.
///
/// @param[in,out] world world the units join
/// @param schema mission unit entries
/// @param count number of entries
/// @param hooks spawn, order, definition and outcome services
/// @return false only when the per-entry table cannot be allocated
bool create_mission_units(
    World& world, const data::campaign::MissionUnit* schema, int32_t count, const Hooks& hooks
);

/// Removes every unit of a type.
///
/// @param[in,out] world world the units live in
/// @param type_index unit type
/// @param hooks kill service
void kill_units_of_type(World& world, uint16_t type_index, const Hooks& hooks);

/// Removes every unit.
///
/// @param[in,out] world world the units live in
/// @param hooks kill service
void kill_all_units(World& world, const Hooks& hooks);

} // namespace oa::sim::mission_units
