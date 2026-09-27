// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Unit type record (FBI files). */
#ifndef OA_CORE_UNIT_DEF_H
#define OA_CORE_UNIT_DEF_H

#include "oa/core/types.h"

/* Bits of UnitDef.flags, packed from FBI booleans. */
#define OA_UNIT_DEF_FLAG_MOVE_ORDER_MASK 0x00000003u /* standingmoveorder */
#define OA_UNIT_DEF_FLAG_FIRE_ORDER_MASK 0x0000000cu /* standingfireorder */
#define OA_UNIT_DEF_FLAG_FIRE_ORDER_SHIFT 2
#define OA_UNIT_DEF_FLAG_INIT_CLOAKED 0x00000010u
#define OA_UNIT_DEF_FLAG_DOWNLOADABLE 0x00000020u
#define OA_UNIT_DEF_FLAG_BUILDER 0x00000040u
#define OA_UNIT_DEF_FLAG_Z_BUFFER 0x00000080u
#define OA_UNIT_DEF_FLAG_STEALTH 0x00000100u
#define OA_UNIT_DEF_FLAG_IS_AIRBASE 0x00000200u
#define OA_UNIT_DEF_FLAG_TARGETING_UPGRADE 0x00000400u
#define OA_UNIT_DEF_FLAG_CAN_FLY 0x00000800u
#define OA_UNIT_DEF_FLAG_CAN_HOVER 0x00001000u
#define OA_UNIT_DEF_FLAG_TELEPORTER 0x00002000u
#define OA_UNIT_DEF_FLAG_HIDE_DAMAGE 0x00004000u
#define OA_UNIT_DEF_FLAG_SHOOT_ME 0x00008000u
#define OA_UNIT_DEF_FLAG_HAS_WEAPONS 0x00010000u /* any of weapon1..3 set */
#define OA_UNIT_DEF_FLAG_ARMORED_STATE 0x00020000u
#define OA_UNIT_DEF_FLAG_ACTIVATE_WHEN_BUILT 0x00040000u
#define OA_UNIT_DEF_FLAG_FLOATER 0x00080000u
#define OA_UNIT_DEF_FLAG_UPRIGHT 0x00100000u
#define OA_UNIT_DEF_FLAG_AMPHIBIOUS 0x00200000u
/* ? Aircraft measure cruise_alt from sea level rather than from the ground
   under them. No FBI key sets it. */
#define OA_UNIT_DEF_FLAG_CRUISE_FROM_SEA_LEVEL 0x00400000u
#define OA_UNIT_DEF_FLAG_AVAILABLE 0x00800000u /* kept in the catalog */
#define OA_UNIT_DEF_FLAG_IS_FEATURE 0x01000000u
#define OA_UNIT_DEF_FLAG_NO_SHADOW 0x02000000u
#define OA_UNIT_DEF_FLAG_IMMUNE_TO_PARALYZER 0x04000000u
#define OA_UNIT_DEF_FLAG_HOVER_ATTACK 0x08000000u
#define OA_UNIT_DEF_FLAG_KAMIKAZE 0x10000000u
#define OA_UNIT_DEF_FLAG_ANTI_WEAPONS 0x20000000u
#define OA_UNIT_DEF_FLAG_DIGGER 0x40000000u
#define OA_UNIT_DEF_FLAG_BUILD_MENU_DEFAULT 0x80000000u /* ? order panel opens on the build pages */

/* Bits of UnitDef.abilities, packed from FBI booleans. */
#define OA_UNIT_DEF_ABILITY_MOBILE_STAND_ORDERS 0x00000001u
#define OA_UNIT_DEF_ABILITY_FIRE_STAND_ORDERS 0x00000002u
#define OA_UNIT_DEF_ABILITY_ON_OFFABLE 0x00000004u
#define OA_UNIT_DEF_ABILITY_CAN_STOP 0x00000008u
#define OA_UNIT_DEF_ABILITY_CAN_ATTACK 0x00000010u
#define OA_UNIT_DEF_ABILITY_CAN_GUARD 0x00000020u
#define OA_UNIT_DEF_ABILITY_CAN_PATROL 0x00000040u
#define OA_UNIT_DEF_ABILITY_CAN_MOVE 0x00000080u
#define OA_UNIT_DEF_ABILITY_CAN_LOAD 0x00000100u
#define OA_UNIT_DEF_ABILITY_CAN_REPAIR 0x00000200u /* copied from canreclamate */
#define OA_UNIT_DEF_ABILITY_CAN_RECLAMATE 0x00000400u
#define OA_UNIT_DEF_ABILITY_CAN_RESURRECT 0x00000800u
#define OA_UNIT_DEF_ABILITY_CAN_CAPTURE 0x00001000u
#define OA_UNIT_DEF_ABILITY_CAN_CLOAK 0x00002000u /* cloakcost > 0 */
#define OA_UNIT_DEF_ABILITY_CAN_DGUN 0x00004000u
#define OA_UNIT_DEF_ABILITY_NO_RESTRICT 0x00008000u
#define OA_UNIT_DEF_ABILITY_WACKY 0x00010000u /* FBI wacky; its use is unresolved */
#define OA_UNIT_DEF_ABILITY_SHOW_PLAYER_NAME 0x00020000u
#define OA_UNIT_DEF_ABILITY_COMMANDER 0x00040000u
#define OA_UNIT_DEF_ABILITY_CANT_BE_TRANSPORTED 0x00080000u
#define OA_UNIT_DEF_ABILITY_SELF_DESTRUCT_MASK 0x00700000u /* selfdestructcountdown, default 5 */
#define OA_UNIT_DEF_ABILITY_SELF_DESTRUCT_SHIFT 20

OA_CORE_BEGIN

#pragma pack(push, 1)

/* One FBI unit type. The game state points at an array of these (stride 0x249). */
typedef struct UnitDef {
    char name[32];
    char unit_name[32];
    char description[64];
    char object_name[32];
    char side[30];
    char ai_weight[64];
    char ai_limit[64];
    uint32_t fbi_hash; /* FBI buffer hash; multiplayer sync id */
    /* Checksum of the unit's COB, GUI and download files, xor weapon_checksum;
       0 until worked out. */
    uint32_t content_checksum;
    uint32_t weapon_checksum; /* xor of the hashes of the unit's weapon sections */
    int16_t footprint_x;
    int16_t footprint_z;
    oa_ref32 yard_map;       /* footprint bytes */
    uint32_t build_id_count; /* limit 31 */
    oa_ref32 build_ids;      /* uint16 type ids */
    int32_t player_limit;    /* -1 unlimited */
    oa_fixed bounds_min_x;   /* model box relative to the unit origin */
    oa_fixed bounds_min_y;
    oa_fixed bounds_min_z;
    oa_fixed bounds_max_x;
    oa_fixed model_height; /* bounds max y; high word read as int16 */
    oa_fixed bounds_max_z;
    oa_fixed size_x; /* bounds max - min */
    oa_fixed size_y;
    oa_fixed size_z;
    oa_fixed size_radius; /* (size_x + size_z) / 3 */
    float build_cost_energy;
    float build_cost_metal;
    oa_ref32 script; /* CobScriptHeader */
    oa_fixed max_velocity;
    oa_fixed slope_speed_step; /* max_velocity / (max_slope + 1) */
    oa_fixed brake_rate;
    oa_fixed acceleration;
    oa_fixed bank_scale;
    oa_fixed pitch_scale;
    oa_fixed damage_modifier;
    int32_t move_rate1;
    int32_t move_rate2;
    oa_ref32 move_class; /* MoveClass */
    int16_t turn_rate;
    int16_t corpse;
    int16_t max_water_depth;
    int16_t min_water_depth;
    float energy_make;
    float energy_use;
    float metal_make;
    float extracts_metal;
    float wind_generator;
    float tidal_generator;
    float cloak_cost;
    float cloak_cost_moving;
    float energy_storage;
    float metal_storage;
    int32_t build_time;
    oa_ref32 weapon1; /* WeaponDef */
    oa_ref32 weapon2; /* WeaponDef */
    oa_ref32 weapon3; /* WeaponDef */
    uint32_t max_damage;
    int16_t worker_time;
    int16_t heal_time;
    int16_t sight_distance;
    int16_t radar_distance;
    int16_t sonar_distance;
    int16_t min_cloak_distance;
    int16_t radar_distance_jam;
    int16_t sonar_distance_jam;
    int16_t sound_category;
    int16_t build_angle;
    int16_t build_distance;
    int16_t maneuver_leash_length;
    int16_t attack_run_length;
    int16_t kamikaze_distance;
    int16_t sort_bias;
    int16_t cruise_alt;
    uint16_t type_id;          /* sorted catalog index */
    oa_ref32 explode_as;       /* WeaponDef */
    oa_ref32 self_destruct_as; /* WeaponDef */
    uint8_t max_slope;
    uint8_t max_water_slope;
    int8_t transport_size;
    int8_t transport_capacity;
    int8_t water_line;
    int8_t makes_metal;
    uint8_t gui_page_count;
    int8_t bm_code; /* 0 = building */
    int8_t default_mission_type;
    oa_ref32 primary_bad_target_category;   /* 64-byte type mask */
    oa_ref32 secondary_bad_target_category; /* 64-byte type mask */
    oa_ref32 special_bad_target_category;   /* 64-byte type mask */
    oa_ref32 no_chase_category;             /* 64-byte type mask */
    uint32_t flags;                         /* UNIT_DEF_FLAG_*; unaligned */
    uint32_t abilities;                     /* UNIT_DEF_ABILITY_*; unaligned */
} UnitDef;

#pragma pack(pop)

OA_ASSERT_SIZE(UnitDef, 0x249);
OA_ASSERT_OFFSET(UnitDef, name, 0x0);
OA_ASSERT_OFFSET(UnitDef, unit_name, 0x20);
OA_ASSERT_OFFSET(UnitDef, description, 0x40);
OA_ASSERT_OFFSET(UnitDef, object_name, 0x80);
OA_ASSERT_OFFSET(UnitDef, side, 0xa0);
OA_ASSERT_OFFSET(UnitDef, ai_weight, 0xbe);
OA_ASSERT_OFFSET(UnitDef, ai_limit, 0xfe);
OA_ASSERT_OFFSET(UnitDef, fbi_hash, 0x13e);
OA_ASSERT_OFFSET(UnitDef, content_checksum, 0x142);
OA_ASSERT_OFFSET(UnitDef, weapon_checksum, 0x146);
OA_ASSERT_OFFSET(UnitDef, footprint_x, 0x14a);
OA_ASSERT_OFFSET(UnitDef, footprint_z, 0x14c);
OA_ASSERT_OFFSET(UnitDef, yard_map, 0x14e);
OA_ASSERT_OFFSET(UnitDef, build_id_count, 0x152);
OA_ASSERT_OFFSET(UnitDef, build_ids, 0x156);
OA_ASSERT_OFFSET(UnitDef, player_limit, 0x15a);
OA_ASSERT_OFFSET(UnitDef, bounds_min_x, 0x15e);
OA_ASSERT_OFFSET(UnitDef, bounds_min_y, 0x162);
OA_ASSERT_OFFSET(UnitDef, bounds_min_z, 0x166);
OA_ASSERT_OFFSET(UnitDef, bounds_max_x, 0x16a);
OA_ASSERT_OFFSET(UnitDef, model_height, 0x16e);
OA_ASSERT_OFFSET(UnitDef, bounds_max_z, 0x172);
OA_ASSERT_OFFSET(UnitDef, size_x, 0x176);
OA_ASSERT_OFFSET(UnitDef, size_y, 0x17a);
OA_ASSERT_OFFSET(UnitDef, size_z, 0x17e);
OA_ASSERT_OFFSET(UnitDef, size_radius, 0x182);
OA_ASSERT_OFFSET(UnitDef, build_cost_energy, 0x186);
OA_ASSERT_OFFSET(UnitDef, build_cost_metal, 0x18a);
OA_ASSERT_OFFSET(UnitDef, script, 0x18e);
OA_ASSERT_OFFSET(UnitDef, max_velocity, 0x192);
OA_ASSERT_OFFSET(UnitDef, slope_speed_step, 0x196);
OA_ASSERT_OFFSET(UnitDef, brake_rate, 0x19a);
OA_ASSERT_OFFSET(UnitDef, acceleration, 0x19e);
OA_ASSERT_OFFSET(UnitDef, bank_scale, 0x1a2);
OA_ASSERT_OFFSET(UnitDef, pitch_scale, 0x1a6);
OA_ASSERT_OFFSET(UnitDef, damage_modifier, 0x1aa);
OA_ASSERT_OFFSET(UnitDef, move_rate1, 0x1ae);
OA_ASSERT_OFFSET(UnitDef, move_rate2, 0x1b2);
OA_ASSERT_OFFSET(UnitDef, move_class, 0x1b6);
OA_ASSERT_OFFSET(UnitDef, turn_rate, 0x1ba);
OA_ASSERT_OFFSET(UnitDef, corpse, 0x1bc);
OA_ASSERT_OFFSET(UnitDef, max_water_depth, 0x1be);
OA_ASSERT_OFFSET(UnitDef, min_water_depth, 0x1c0);
OA_ASSERT_OFFSET(UnitDef, energy_make, 0x1c2);
OA_ASSERT_OFFSET(UnitDef, energy_use, 0x1c6);
OA_ASSERT_OFFSET(UnitDef, metal_make, 0x1ca);
OA_ASSERT_OFFSET(UnitDef, extracts_metal, 0x1ce);
OA_ASSERT_OFFSET(UnitDef, wind_generator, 0x1d2);
OA_ASSERT_OFFSET(UnitDef, tidal_generator, 0x1d6);
OA_ASSERT_OFFSET(UnitDef, cloak_cost, 0x1da);
OA_ASSERT_OFFSET(UnitDef, cloak_cost_moving, 0x1de);
OA_ASSERT_OFFSET(UnitDef, energy_storage, 0x1e2);
OA_ASSERT_OFFSET(UnitDef, metal_storage, 0x1e6);
OA_ASSERT_OFFSET(UnitDef, build_time, 0x1ea);
OA_ASSERT_OFFSET(UnitDef, weapon1, 0x1ee);
OA_ASSERT_OFFSET(UnitDef, weapon2, 0x1f2);
OA_ASSERT_OFFSET(UnitDef, weapon3, 0x1f6);
OA_ASSERT_OFFSET(UnitDef, max_damage, 0x1fa);
OA_ASSERT_OFFSET(UnitDef, worker_time, 0x1fe);
OA_ASSERT_OFFSET(UnitDef, heal_time, 0x200);
OA_ASSERT_OFFSET(UnitDef, sight_distance, 0x202);
OA_ASSERT_OFFSET(UnitDef, radar_distance, 0x204);
OA_ASSERT_OFFSET(UnitDef, sonar_distance, 0x206);
OA_ASSERT_OFFSET(UnitDef, min_cloak_distance, 0x208);
OA_ASSERT_OFFSET(UnitDef, radar_distance_jam, 0x20a);
OA_ASSERT_OFFSET(UnitDef, sonar_distance_jam, 0x20c);
OA_ASSERT_OFFSET(UnitDef, sound_category, 0x20e);
OA_ASSERT_OFFSET(UnitDef, build_angle, 0x210);
OA_ASSERT_OFFSET(UnitDef, build_distance, 0x212);
OA_ASSERT_OFFSET(UnitDef, maneuver_leash_length, 0x214);
OA_ASSERT_OFFSET(UnitDef, attack_run_length, 0x216);
OA_ASSERT_OFFSET(UnitDef, kamikaze_distance, 0x218);
OA_ASSERT_OFFSET(UnitDef, sort_bias, 0x21a);
OA_ASSERT_OFFSET(UnitDef, cruise_alt, 0x21c);
OA_ASSERT_OFFSET(UnitDef, type_id, 0x21e);
OA_ASSERT_OFFSET(UnitDef, explode_as, 0x220);
OA_ASSERT_OFFSET(UnitDef, self_destruct_as, 0x224);
OA_ASSERT_OFFSET(UnitDef, max_slope, 0x228);
OA_ASSERT_OFFSET(UnitDef, max_water_slope, 0x229);
OA_ASSERT_OFFSET(UnitDef, transport_size, 0x22a);
OA_ASSERT_OFFSET(UnitDef, transport_capacity, 0x22b);
OA_ASSERT_OFFSET(UnitDef, water_line, 0x22c);
OA_ASSERT_OFFSET(UnitDef, makes_metal, 0x22d);
OA_ASSERT_OFFSET(UnitDef, gui_page_count, 0x22e);
OA_ASSERT_OFFSET(UnitDef, bm_code, 0x22f);
OA_ASSERT_OFFSET(UnitDef, default_mission_type, 0x230);
OA_ASSERT_OFFSET(UnitDef, primary_bad_target_category, 0x231);
OA_ASSERT_OFFSET(UnitDef, secondary_bad_target_category, 0x235);
OA_ASSERT_OFFSET(UnitDef, special_bad_target_category, 0x239);
OA_ASSERT_OFFSET(UnitDef, no_chase_category, 0x23d);
OA_ASSERT_OFFSET(UnitDef, flags, 0x241);
OA_ASSERT_OFFSET(UnitDef, abilities, 0x245);

OA_CORE_END

#endif
