// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Services the air and transport code needs from the simulation that owns
// it. Records that are not canonical yet (movement objects, orders, scripts,
// speech) and spatial queries are reached through these function pointers.
// A null service behaves as documented on each member.

#include "oa/core/world.h"

#include <cstdint>

namespace oa::sim::air {

struct AirDriver;

struct AirHost {
    void* context{};
    World* world{};

    // ---- world queries (goals and the driver) ----

    // Terrain height byte of the unit's spatial bucket. Null: 0.
    uint8_t (*bucket_height)(void* context, const Unit* unit){};
    // Whether the unit sits in the bucket of units outside the map. Null:
    // false.
    bool (*outside_map)(void* context, const Unit* unit){};
    // World position of one of the unit's query points; -1 is its origin.
    // Null: the unit position.
    FixedVec3 (*query_point)(void* context, const Unit* unit, int16_t index){};
    // Interpolated terrain height at X/Z, -1 off the map interior. Null: -1.
    int32_t (*terrain_height)(void* context, oa_fixed x, oa_fixed z){};

    // ---- unit movement objects ----

    // The unit's air driver, or null when it has no movement object or a
    // ground driver.
    AirDriver* (*driver)(void* context, Unit* unit){};
    // The occupancy bits of the unit's movement record flags (1 ground, 2
    // air); 0 without one.
    uint8_t (*movement_layer)(void* context, const Unit* unit){};
    // Switch the movement layer. Required.
    void (*set_movement_layer)(void* context, Unit* unit, uint8_t layer){};

    // ---- orders ----

    // Current simulation tick. Null: Game.tick.
    uint32_t (*tick)(void* context){};
    // Shared game random value below limit. Required.
    uint32_t (*random)(void* context, uint32_t limit){};
    // Owner speech: category index and optional text overriding the
    // category's default. Null: silent.
    void (*speech)(void* context, Unit* unit, uint32_t category, const char* text){};
    // Stand a weapon slot down (3 = all) and drop its target.
    void (*reset_weapons)(void* context, Unit* unit, uint8_t slot){};
    // Re-enable a stood-down weapon slot (3 = all).
    void (*enable_weapons)(void* context, Unit* unit, uint8_t slot){};
    // Attach the unit to a carrier piece, or detach it with a null carrier,
    // in the given link mode.
    void (*set_carry_link)(void* context, Unit* unit, Unit* carrier, int8_t piece, uint8_t mode){};
    // Set or clear Unit.state_flags bits with their script and speech side
    // effects. Null: only the bits change.
    void (*set_state_flags)(void* context, Unit* unit, uint8_t mask, bool on){};
    // Start the named script function as a new thread holding `value`; when
    // run_now is set the unit's threads run at once. Null: none.
    void (*script_start)(
        void* context, Unit* unit, const char* name, int32_t value, bool run_now
    ){};
    // Queue a new order of the named mission at the head of the unit's queue,
    // or at its end when `append` is set.
    void (*push_order)(
        void* context,
        Unit* unit,
        bool append,
        const char* mission,
        Unit* target,
        const FixedVec3* point,
        int32_t parameter,
        int32_t parameter_2,
        int32_t parameter_3
    ){};
    // Nearest unit the unit would attack on its own; null none.
    Unit* (*automatic_target)(void* context, Unit* unit){};
    // Issue an attack on the target the way automatic targeting does; false
    // when no order resulted.
    bool (*issue_attack)(void* context, Unit* unit, Unit* target){};
    // Whether the unit may set down at the point.
    bool (*can_land_at)(void* context, Unit* unit, const FixedVec3* point){};
    // Active repair pads (builder airbase defs with state flag 1) of a
    // player within `radius` world units of a point, in the player's unit
    // list order; returns the count written. Null: the World
    // unit range of the player is scanned instead.
    uint32_t (*repair_pads_near)(
        void* context,
        uint8_t player,
        const FixedVec3* point,
        int32_t radius,
        Unit** out,
        uint32_t capacity
    ){};

    // ---- landing pads and transport (landing.hpp, transport) ----

    // ---- attack orders (attack.hpp) ----

    // Aim a weapon slot at a unit.
    void (*aim_at_unit)(void* context, Unit* unit, Unit* target, uint8_t slot){};
    // Aim a weapon slot at a ground point.
    void (*aim_at_point)(void* context, Unit* unit, const FixedVec3* point, uint8_t slot){};
    // Definition of a weapon slot (Unit.weapons[slot].def). Null: resolved
    // through the World's weapon table.
    const WeaponDef* (*weapon_def)(void* context, const Unit* unit, uint8_t slot){};
    // Velocity of the unit's movement record (Movement.velocity); zero without
    // one.
    FixedVec3 (*movement_velocity)(void* context, const Unit* unit){};

    // ---- patrol, guard and build orders (patrol.hpp) ----
};

} // namespace oa::sim::air
