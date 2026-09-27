// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Projectile record. */
#ifndef OA_CORE_PROJECTILE_H
#define OA_CORE_PROJECTILE_H

#include "oa/core/types.h"

#define OA_PROJECTILE_CAPACITY 300

/* Bits of Projectile.flags. */
#define OA_PROJECTILE_FLAG_BEAM_TAIL 0x0001u /* a beam's tail follows its head */
#define OA_PROJECTILE_FLAG_RETIRED 0x0002u   /* compacted out at the end of the tick */
/* Two-bit phase counter of a two-phase weapon; nonzero steers in the second phase. */
#define OA_PROJECTILE_PHASE_MASK 0x0030u
#define OA_PROJECTILE_PHASE_STEP 0x0010u

OA_CORE_BEGIN

#pragma pack(push, 1)

/* One projectile in the game state's fixed pool. */
typedef struct Projectile {
    oa_ref32 def; /* WeaponDef */
    FixedVec3 position;
    FixedVec3 origin;
    FixedVec3 velocity;
    FixedVec3 target; /* aim point at launch; interceptors search this, not position */
    uint8_t reserved_after_target[0x2]; /* no known use: zeroed with the pool */
    oa_angle heading;
    oa_angle pitch;
    oa_fixed speed;
    int32_t distance; /* ? launch horizontal distance */
    uint32_t burst_tick;
    uint32_t lifetime_tick;
    uint32_t created_tick;
    oa_ref32 target_unit;      /* Unit; guidance follows it while live */
    oa_ref32 source;           /* Unit */
    oa_ref32 intercept_target; /* Projectile an interceptor homes on */
    int16_t feature_cell_x;
    int16_t feature_cell_z;
    /* ? Mean of the high and low heights of the plot under the shot. The
       engine leaves it zero and works out the height when it draws the shot's
       shadow. */
    int16_t plot_height;
    uint16_t burst_remaining;
    uint16_t query_piece;
    uint8_t reserved_after_query_piece[0x2]; /* no known use: zeroed with the pool */
    uint8_t owner_index;
    int16_t compact_index; /* pool index before the last compaction */
    uint16_t flags;
} Projectile;

#pragma pack(pop)

OA_ASSERT_SIZE(Projectile, 0x6b);
OA_ASSERT_OFFSET(Projectile, def, 0x0);
OA_ASSERT_OFFSET(Projectile, position, 0x4);
OA_ASSERT_OFFSET(Projectile, origin, 0x10);
OA_ASSERT_OFFSET(Projectile, velocity, 0x1c);
OA_ASSERT_OFFSET(Projectile, target, 0x28);
OA_ASSERT_OFFSET(Projectile, reserved_after_target, 0x34);
OA_ASSERT_OFFSET(Projectile, heading, 0x36);
OA_ASSERT_OFFSET(Projectile, pitch, 0x38);
OA_ASSERT_OFFSET(Projectile, speed, 0x3a);
OA_ASSERT_OFFSET(Projectile, distance, 0x3e);
OA_ASSERT_OFFSET(Projectile, burst_tick, 0x42);
OA_ASSERT_OFFSET(Projectile, lifetime_tick, 0x46);
OA_ASSERT_OFFSET(Projectile, created_tick, 0x4a);
OA_ASSERT_OFFSET(Projectile, target_unit, 0x4e);
OA_ASSERT_OFFSET(Projectile, source, 0x52);
OA_ASSERT_OFFSET(Projectile, intercept_target, 0x56);
OA_ASSERT_OFFSET(Projectile, feature_cell_x, 0x5a);
OA_ASSERT_OFFSET(Projectile, feature_cell_z, 0x5c);
OA_ASSERT_OFFSET(Projectile, plot_height, 0x5e);
OA_ASSERT_OFFSET(Projectile, burst_remaining, 0x60);
OA_ASSERT_OFFSET(Projectile, query_piece, 0x62);
OA_ASSERT_OFFSET(Projectile, reserved_after_query_piece, 0x64);
OA_ASSERT_OFFSET(Projectile, owner_index, 0x66);
OA_ASSERT_OFFSET(Projectile, compact_index, 0x67);
OA_ASSERT_OFFSET(Projectile, flags, 0x69);

OA_CORE_END

#endif
