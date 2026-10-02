// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Unit record and its embedded weapon/economy blocks. */
#ifndef OA_CORE_UNIT_H
#define OA_CORE_UNIT_H

#include "oa/core/types.h"

#ifndef __cplusplus
#include <stdbool.h>
#endif

#define OA_UNIT_WEAPON_COUNT 3
#define OA_UNIT_TARGET_IS_UNIT (-0x8000)

/* Bits of UnitWeapon.flags. */
#define OA_UNIT_WEAPON_AIMED 0x01u
#define OA_UNIT_WEAPON_ENABLED 0x02u
#define OA_UNIT_WEAPON_RETALIATE                                                                   \
    0x10u /* cleared together with the weapon target; retaliation needs it */

/* Bits of Unit.flags. */
#define OA_UNIT_FLAG_OCCUPANCY_MASK 0x00000003u
#define OA_UNIT_FLAG_MOVE_RATE_MASK 0x0000000cu
#define OA_UNIT_FLAG_SELECTED 0x00000010u
/* The owner may select and order the unit. Set at spawn; a mission script's
   orders clear it until MakeSelectable; required to stay selected. */
#define OA_UNIT_FLAG_SELECTABLE 0x00000020u
#define OA_UNIT_FLAG_CYCLE_VISITED 0x00000040u /* ? reached by select-next */
/* ? Select-next passes over a unit carrying it; marking a unit visited
   clears it. No game rule sets it; a saved game carries it and loading the
   save restores it. */
#define OA_UNIT_FLAG_CYCLE_SKIP 0x00000080u
#define OA_UNIT_FLAG_RADAR_CONTACT 0x00000100u /* on the viewer's radar or in its line of sight */
#define OA_UNIT_FLAG_VIEWPOINT_OWNED                                                               \
    0x00000200u /* viewer's, radar-sharing or sonar contact: shown under water */
#define OA_UNIT_FLAG_JAMMED 0x00000400u        /* ? inside an enemy radar or sonar jammer */
#define OA_UNIT_FLAG_CLOAK_RUNNING 0x00000800u /* Cloak_On ordered; upkeep keeps the unit cloaked */
#define OA_UNIT_FLAG_CLOAK_LOCKED                                                                  \
    0x00001000u /* enemy inside mincloakdistance at the last contact scan */
#define OA_UNIT_FLAG_CONSTRUCTION_DIRTY 0x00002000u
#define OA_UNIT_FLAG_DEATH_PENDING 0x00004000u
/* ? Set from a mission unit's immunity flag; MakeSelectable clears it. Enemy
   sightings and the computer players' nearest-enemy search pass over a unit
   carrying it. It does not stop selection: OA_UNIT_FLAG_SELECTABLE does. */
#define OA_UNIT_FLAG_NOT_SELECTABLE 0x00008000u
#define OA_UNIT_FLAG_POSITION_DIRTY 0x00010000u
#define OA_UNIT_FLAG_ATTACHED_WITHOUT_PIECE 0x00020000u /* carried with attach piece -1 */
#define OA_UNIT_FLAG_MOVE_ORDER_MASK 0x000c0000u        /* standing move order */
#define OA_UNIT_FLAG_MOVE_ORDER_SHIFT 18
#define OA_UNIT_FLAG_FIRE_ORDER_MASK 0x00300000u /* standing fire order */
#define OA_UNIT_FLAG_FIRE_ORDER_SHIFT 20
#define OA_UNIT_FLAG_BUILD_MENU 0x00400000u /* order panel shows the build pages */
#define OA_UNIT_FLAG_BUILD_PAGE_MASK 0x03800000u
#define OA_UNIT_FLAG_BUILD_PAGE_SHIFT 23
/* Set on a unit whose plot word another unit took; removing a unit carrying
   it rewrites the occupancy of every unit it overlaps. */
#define OA_UNIT_FLAG_COLLISION_OTHER 0x04000000u
/* The unit's plot occupancy must be rewritten; the rewrite clears it. */
#define OA_UNIT_FLAG_COLLISION_SELF 0x08000000u
#define OA_UNIT_FLAG_LIVE 0x10000000u
#define OA_UNIT_FLAG_BUILDING 0x20000000u    /* bmcode 0 structure */
#define OA_UNIT_FLAG_AIR_BASE 0x40000000u    /* from UnitDef isairbase */
#define OA_UNIT_FLAG_HAS_WEAPONS 0x80000000u /* from UnitDef has-weapons */

/* Bits of Unit.state_flags. */
#define OA_UNIT_STATE_ACTIVE 0x01u /* Activate script state; radar, sonar and jammers run */
#define OA_UNIT_STATE_CLOAKED 0x04u

/* Bits of Unit.build_flags, which unit scripts read and write. */
#define OA_UNIT_BUILD_IN_BUILD_STANCE 0x01u
#define OA_UNIT_BUILD_BUSY 0x02u
#define OA_UNIT_BUILD_YARD_OPEN 0x04u
#define OA_UNIT_BUILD_BUGGER_OFF 0x08u
#define OA_UNIT_BUILD_SCRIPT_MASK 0x0fu /* the four bits above */

/* Bits of Unit.flags2. */
#define OA_UNIT_FLAG2_Z_BUFFER 0x00000001u /* from UnitDef zbuffer */

OA_CORE_BEGIN

#pragma pack(push, 1)

/* One of a unit's three weapon slots. */
typedef struct UnitWeapon {
    int16_t target_a;          /* unit index, or world x when target_b != -0x8000 */
    int16_t target_b;          /* -0x8000 marks a unit target, else world z */
    uint8_t aim_callback[0x4]; /* Aim script return callback; left zero and not saved */
    /* 1 once the Aim script returns nonzero; cleared by aim starts and shots.
       Saved and restored as a 32-bit word. */
    uint32_t aim_ready;
    oa_ref32 def; /* WeaponDef */
    uint8_t muzzle_offset[0x4];
    uint16_t reload; /* ticks remaining */
    int16_t aim_heading;
    int16_t aim_pitch;
    uint8_t stockpile;
    uint8_t flags; /* UNIT_WEAPON_*; bits 2..3 slot index */
} UnitWeapon;

/* Per-tick resource flow for one resource. */
typedef struct ResourceAccumulator {
    float produced;
    float requested;
    float accepted;
    float gate;          /* accepted only grows while <= 0 */
    float last_produced; /* produced as of the last economy settlement */
    float last_requested;
} ResourceAccumulator;

/* Energy and metal flow of one unit. */
typedef struct UnitEconomy {
    ResourceAccumulator energy;
    ResourceAccumulator metal;
    oa_ref32 player; /* owner Player */
} UnitEconomy;

/* The 0x118-byte unit record. The game state holds units_per_player * 10 + 1
 * of these; slot 0 is reserved. */
typedef struct Unit {
    oa_ref32 movement; /* movement object (side table); only bmcode 1 units have one */
    UnitWeapon weapons[3];
    float extracted_metal;    /* ? added when extracts_metal > 0 */
    oa_ref32 primary_order;   /* order list head */
    oa_ref32 secondary_order; /* order list head */
    int16_t bank;
    oa_angle heading;
    int16_t pitch;
    FixedVec3 position;
    int16_t cell_x;
    int16_t cell_z;
    uint16_t sight_center_x; /* cell of the last sight stamp */
    uint16_t sight_center_z;
    int16_t footprint_x;
    int16_t footprint_z;
    oa_ref32 spatial_bucket;
    oa_ref32 attach_parent;      /* Unit */
    oa_ref32 attach_first_child; /* Unit */
    oa_ref32 attach_next;        /* Unit; sibling chain */
    oa_ref32 def;                /* UnitDef */
    oa_ref32 owner;              /* Player */
    oa_ref32 script;             /* CobMachine; nonzero while scripted (side table) */
    /* ? The unit's model instance, then the head of the list of orders and
       air goals that target the unit. The engine keeps both in side tables,
       leaves these bytes zero, and saves do not carry them. */
    uint8_t links_after_script[0x8];
    uint16_t type_index;         /* 0 = empty slot */
    uint16_t id;                 /* own index in the unit array */
    uint16_t bob_phase;          /* random hover/float phase */
    int32_t squad;               /* group; 0 none, 1..9 squads, -1 never spawned */
    uint32_t decloak_until_tick; /* ? firing or some orders hold the cloak off until this tick */
    uint8_t reserved_after_decloak_until_tick[0x4]; /* no known use: left zero, not saved */
    uint16_t veteran_level;
    uint16_t events; /* script wake bits */
    UnitEconomy economy;
    uint32_t last_attacker_id;   /* Unit.id of the last damage source */
    uint8_t last_attacker_owner; /* its Player.index; 10 none */
    uint8_t damage_kind;         /* last damage kind */
    uint8_t health_percent;
    uint8_t previous_health_percent;
    uint8_t sight_band;        /* sight mask band of the last stamp */
    uint8_t attach_piece;      /* piece of attach_parent carrying this unit; 0xff none */
    uint8_t damage_countdown;  /* ticks since last hit, from 0xf0 */
    uint32_t capture_cooldown; /* ticks after capture while not selectable */
    uint8_t owner_index;       /* Player.index */
    /* No known use: zeroed when a unit spawns unfinished, else left as it
       was; never read and not saved. */
    uint32_t cleared_on_unfinished_spawn;
    float build_remaining; /* 1.0 unfinished .. 0.0 finished */
    int16_t health;
    uint8_t last_occupy_code[0x4];
    uint8_t state_flags;
    uint8_t build_flags; /* OA_UNIT_BUILD_* */
    uint32_t flags;      /* UNIT_FLAG_* */
    uint32_t flags2;     /* UNIT_FLAG2_* */
} Unit;

#pragma pack(pop)

OA_ASSERT_SIZE(UnitWeapon, 0x1c);
OA_ASSERT_OFFSET(UnitWeapon, target_a, 0x0);
OA_ASSERT_OFFSET(UnitWeapon, target_b, 0x2);
OA_ASSERT_OFFSET(UnitWeapon, aim_callback, 0x4);
OA_ASSERT_OFFSET(UnitWeapon, aim_ready, 0x8);
OA_ASSERT_OFFSET(UnitWeapon, def, 0xc);
OA_ASSERT_OFFSET(UnitWeapon, muzzle_offset, 0x10);
OA_ASSERT_OFFSET(UnitWeapon, reload, 0x14);
OA_ASSERT_OFFSET(UnitWeapon, aim_heading, 0x16);
OA_ASSERT_OFFSET(UnitWeapon, aim_pitch, 0x18);
OA_ASSERT_OFFSET(UnitWeapon, stockpile, 0x1a);
OA_ASSERT_OFFSET(UnitWeapon, flags, 0x1b);

OA_ASSERT_SIZE(ResourceAccumulator, 0x18);
OA_ASSERT_OFFSET(ResourceAccumulator, produced, 0x0);
OA_ASSERT_OFFSET(ResourceAccumulator, requested, 0x4);
OA_ASSERT_OFFSET(ResourceAccumulator, accepted, 0x8);
OA_ASSERT_OFFSET(ResourceAccumulator, gate, 0xc);
OA_ASSERT_OFFSET(ResourceAccumulator, last_produced, 0x10);
OA_ASSERT_OFFSET(ResourceAccumulator, last_requested, 0x14);

OA_ASSERT_SIZE(UnitEconomy, 0x34);
OA_ASSERT_OFFSET(UnitEconomy, energy, 0x0);
OA_ASSERT_OFFSET(UnitEconomy, metal, 0x18);
OA_ASSERT_OFFSET(UnitEconomy, player, 0x30);

OA_ASSERT_SIZE(Unit, 0x118);
OA_ASSERT_OFFSET(Unit, movement, 0x0);
OA_ASSERT_OFFSET(Unit, weapons, 0x4);
OA_ASSERT_OFFSET(Unit, extracted_metal, 0x58);
OA_ASSERT_OFFSET(Unit, primary_order, 0x5c);
OA_ASSERT_OFFSET(Unit, secondary_order, 0x60);
OA_ASSERT_OFFSET(Unit, bank, 0x64);
OA_ASSERT_OFFSET(Unit, heading, 0x66);
OA_ASSERT_OFFSET(Unit, pitch, 0x68);
OA_ASSERT_OFFSET(Unit, position, 0x6a);
OA_ASSERT_OFFSET(Unit, cell_x, 0x76);
OA_ASSERT_OFFSET(Unit, cell_z, 0x78);
OA_ASSERT_OFFSET(Unit, sight_center_x, 0x7a);
OA_ASSERT_OFFSET(Unit, sight_center_z, 0x7c);
OA_ASSERT_OFFSET(Unit, footprint_x, 0x7e);
OA_ASSERT_OFFSET(Unit, footprint_z, 0x80);
OA_ASSERT_OFFSET(Unit, spatial_bucket, 0x82);
OA_ASSERT_OFFSET(Unit, attach_parent, 0x86);
OA_ASSERT_OFFSET(Unit, attach_first_child, 0x8a);
OA_ASSERT_OFFSET(Unit, attach_next, 0x8e);
OA_ASSERT_OFFSET(Unit, def, 0x92);
OA_ASSERT_OFFSET(Unit, owner, 0x96);
OA_ASSERT_OFFSET(Unit, script, 0x9a);
OA_ASSERT_OFFSET(Unit, links_after_script, 0x9e);
OA_ASSERT_OFFSET(Unit, type_index, 0xa6);
OA_ASSERT_OFFSET(Unit, id, 0xa8);
OA_ASSERT_OFFSET(Unit, bob_phase, 0xaa);
OA_ASSERT_OFFSET(Unit, squad, 0xac);
OA_ASSERT_OFFSET(Unit, decloak_until_tick, 0xb0);
OA_ASSERT_OFFSET(Unit, reserved_after_decloak_until_tick, 0xb4);
OA_ASSERT_OFFSET(Unit, veteran_level, 0xb8);
OA_ASSERT_OFFSET(Unit, events, 0xba);
OA_ASSERT_OFFSET(Unit, economy, 0xbc);
OA_ASSERT_OFFSET(Unit, last_attacker_id, 0xf0);
OA_ASSERT_OFFSET(Unit, last_attacker_owner, 0xf4);
OA_ASSERT_OFFSET(Unit, damage_kind, 0xf5);
OA_ASSERT_OFFSET(Unit, health_percent, 0xf6);
OA_ASSERT_OFFSET(Unit, previous_health_percent, 0xf7);
OA_ASSERT_OFFSET(Unit, sight_band, 0xf8);
OA_ASSERT_OFFSET(Unit, attach_piece, 0xf9);
OA_ASSERT_OFFSET(Unit, damage_countdown, 0xfa);
OA_ASSERT_OFFSET(Unit, capture_cooldown, 0xfb);
OA_ASSERT_OFFSET(Unit, owner_index, 0xff);
OA_ASSERT_OFFSET(Unit, cleared_on_unfinished_spawn, 0x100);
OA_ASSERT_OFFSET(Unit, build_remaining, 0x104);
OA_ASSERT_OFFSET(Unit, health, 0x108);
OA_ASSERT_OFFSET(Unit, last_occupy_code, 0x10a);
OA_ASSERT_OFFSET(Unit, state_flags, 0x10e);
OA_ASSERT_OFFSET(Unit, build_flags, 0x10f);
OA_ASSERT_OFFSET(Unit, flags, 0x110);
OA_ASSERT_OFFSET(Unit, flags2, 0x114);

/// Tests whether a unit may be targeted, damaged or counted as present: it
/// is live and not waiting to die.
///
/// @param flags the unit's Unit.flags
/// @return true when OA_UNIT_FLAG_LIVE is set and OA_UNIT_FLAG_DEATH_PENDING clear
static inline bool unit_is_live_target(uint32_t flags) {
    return (flags & OA_UNIT_FLAG_LIVE) != 0 && (flags & OA_UNIT_FLAG_DEATH_PENDING) == 0;
}

OA_CORE_END

#endif
