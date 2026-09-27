// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Movement class record (MOVEINFO.TDF). */
#ifndef OA_CORE_MOVE_CLASS_H
#define OA_CORE_MOVE_CLASS_H

#include "oa/core/types.h"

#define OA_MOVE_CLASS_COUNT 32

OA_CORE_BEGIN

#pragma pack(push, 1)

/* MOVEINFO.TDF CLASSn record; the table holds 32 classes, CLASS0 to CLASS31. */
typedef struct MoveClass {
    oa_ref32 name; /* the class name: its MoveClassTable.names slot + 1, 0 for an empty slot */
    int16_t footprint_x;
    int16_t footprint_z;
    int16_t max_water_depth;
    int16_t min_water_depth;
    uint8_t max_slope;
    uint8_t bad_slope;
    uint8_t max_water_slope;
    uint8_t bad_water_slope;
    int32_t grid_width; /* passability grid, allocated per class */
    int32_t grid_height;
    oa_ref32 grid;                    /* uint32[(grid_height + 15) / 16 * grid_width] */
    uint8_t reserved_after_grid[0x4]; /* zeroed with the class; the engine never reads it */
} MoveClass;

#pragma pack(pop)

OA_ASSERT_SIZE(MoveClass, 0x20);
OA_ASSERT_OFFSET(MoveClass, name, 0x0);
OA_ASSERT_OFFSET(MoveClass, footprint_x, 0x4);
OA_ASSERT_OFFSET(MoveClass, footprint_z, 0x6);
OA_ASSERT_OFFSET(MoveClass, max_water_depth, 0x8);
OA_ASSERT_OFFSET(MoveClass, min_water_depth, 0xa);
OA_ASSERT_OFFSET(MoveClass, max_slope, 0xc);
OA_ASSERT_OFFSET(MoveClass, bad_slope, 0xd);
OA_ASSERT_OFFSET(MoveClass, max_water_slope, 0xe);
OA_ASSERT_OFFSET(MoveClass, bad_water_slope, 0xf);
OA_ASSERT_OFFSET(MoveClass, grid_width, 0x10);
OA_ASSERT_OFFSET(MoveClass, grid_height, 0x14);
OA_ASSERT_OFFSET(MoveClass, grid, 0x18);
OA_ASSERT_OFFSET(MoveClass, reserved_after_grid, 0x1c);

OA_CORE_END

#endif
