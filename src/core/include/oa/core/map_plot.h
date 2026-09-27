// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Map plot record (one terrain cell). */
#ifndef OA_CORE_MAP_PLOT_H
#define OA_CORE_MAP_PLOT_H

#include "oa/core/types.h"

/* MapPlot.feature values at or above this carry no feature of their own. */
#define OA_PLOT_FEATURE_RESERVED 0xfffbu

/* Bits of MapPlot.flags. */
#define OA_PLOT_FLAG_ANIMATING_FEATURE 0x01u
#define OA_PLOT_FLAG_PLAYER_FEATURE_MASK 0x78u

/* Stride of the placed-feature records a plot's feature_record indexes. */
#define OA_PLACED_FEATURE_BYTES 0x30

OA_CORE_BEGIN

#pragma pack(push, 1)

/* The 13-byte cell record; Game.map_cells refers to map_width * map_height of them. */
typedef struct MapPlot {
    uint16_t ground_unit;
    uint16_t air_unit;
    uint8_t height;
    uint8_t high_height; /* highest map height at (x, z) to (x + 1, z + 1), within the map */
    uint8_t low_height;  /* lowest map height over the same cells */
    uint8_t metal;
    uint16_t feature;        /* FeatureDef index, or >= OA_PLOT_FEATURE_RESERVED */
    uint16_t feature_record; /* placed-feature record index */
    uint8_t flags;           /* PLOT_FLAG_* */
} MapPlot;

#pragma pack(pop)

OA_ASSERT_SIZE(MapPlot, 0xd);
OA_ASSERT_OFFSET(MapPlot, ground_unit, 0x0);
OA_ASSERT_OFFSET(MapPlot, air_unit, 0x2);
OA_ASSERT_OFFSET(MapPlot, height, 0x4);
OA_ASSERT_OFFSET(MapPlot, high_height, 0x5);
OA_ASSERT_OFFSET(MapPlot, low_height, 0x6);
OA_ASSERT_OFFSET(MapPlot, metal, 0x7);
OA_ASSERT_OFFSET(MapPlot, feature, 0x8);
OA_ASSERT_OFFSET(MapPlot, feature_record, 0xa);
OA_ASSERT_OFFSET(MapPlot, flags, 0xc);

OA_CORE_END

#endif
