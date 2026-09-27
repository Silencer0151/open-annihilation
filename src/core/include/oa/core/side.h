// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Side record (gamedata/SIDEDATA.TDF). */
#ifndef OA_CORE_SIDE_H
#define OA_CORE_SIDE_H

#include "oa/core/types.h"

#define OA_SIDE_COUNT 5

OA_CORE_BEGIN

#pragma pack(push, 1)

/* One SIDEDATA.TDF side: HUD layout and identity. */
typedef struct Side {
    char name[30];
    char name_prefix[4];
    char commander[32];
    Rect32 rect_logo;
    Rect32 rect_energy_bar;
    Rect32 rect_energy_num;
    Rect32 rect_metal_bar;
    Rect32 rect_metal_num;
    Rect32 rect_total_units;
    Rect32 rect_total_time;
    Rect32 rect_energy_max;
    Rect32 rect_metal_max;
    Rect32 rect_energy0;
    Rect32 rect_metal0;
    Rect32 rect_energy_produced;
    Rect32 rect_energy_consumed;
    Rect32 rect_metal_produced;
    Rect32 rect_metal_consumed;
    Rect32 rect_logo2;
    Rect32 rect_unit_name;
    Rect32 rect_damage_bar;
    Rect32 rect_unit_energy_make;
    Rect32 rect_unit_energy_use;
    Rect32 rect_unit_metal_make;
    Rect32 rect_unit_metal_use;
    Rect32 rect_mission_text;
    Rect32 rect_unit_name2;
    Rect32 rect_damage_bar2;
    Rect32 rect_name;
    Rect32 rect_description;
    Rect32 rect_reload1;
    Rect32 rect_reload2;
    Rect32 rect_reload3;
    uint32_t energy_color;
    uint32_t metal_color;
    uint32_t side_index; /* own index in Game.sides */
    oa_ref32 font;
} Side;

#pragma pack(pop)

OA_ASSERT_SIZE(Side, 0x232);
OA_ASSERT_OFFSET(Side, name, 0x0);
OA_ASSERT_OFFSET(Side, name_prefix, 0x1e);
OA_ASSERT_OFFSET(Side, commander, 0x22);
OA_ASSERT_OFFSET(Side, rect_logo, 0x42);
OA_ASSERT_OFFSET(Side, rect_energy_bar, 0x52);
OA_ASSERT_OFFSET(Side, rect_energy_num, 0x62);
OA_ASSERT_OFFSET(Side, rect_metal_bar, 0x72);
OA_ASSERT_OFFSET(Side, rect_metal_num, 0x82);
OA_ASSERT_OFFSET(Side, rect_total_units, 0x92);
OA_ASSERT_OFFSET(Side, rect_total_time, 0xa2);
OA_ASSERT_OFFSET(Side, rect_energy_max, 0xb2);
OA_ASSERT_OFFSET(Side, rect_metal_max, 0xc2);
OA_ASSERT_OFFSET(Side, rect_energy0, 0xd2);
OA_ASSERT_OFFSET(Side, rect_metal0, 0xe2);
OA_ASSERT_OFFSET(Side, rect_energy_produced, 0xf2);
OA_ASSERT_OFFSET(Side, rect_energy_consumed, 0x102);
OA_ASSERT_OFFSET(Side, rect_metal_produced, 0x112);
OA_ASSERT_OFFSET(Side, rect_metal_consumed, 0x122);
OA_ASSERT_OFFSET(Side, rect_logo2, 0x132);
OA_ASSERT_OFFSET(Side, rect_unit_name, 0x142);
OA_ASSERT_OFFSET(Side, rect_damage_bar, 0x152);
OA_ASSERT_OFFSET(Side, rect_unit_energy_make, 0x162);
OA_ASSERT_OFFSET(Side, rect_unit_energy_use, 0x172);
OA_ASSERT_OFFSET(Side, rect_unit_metal_make, 0x182);
OA_ASSERT_OFFSET(Side, rect_unit_metal_use, 0x192);
OA_ASSERT_OFFSET(Side, rect_mission_text, 0x1a2);
OA_ASSERT_OFFSET(Side, rect_unit_name2, 0x1b2);
OA_ASSERT_OFFSET(Side, rect_damage_bar2, 0x1c2);
OA_ASSERT_OFFSET(Side, rect_name, 0x1d2);
OA_ASSERT_OFFSET(Side, rect_description, 0x1e2);
OA_ASSERT_OFFSET(Side, rect_reload1, 0x1f2);
OA_ASSERT_OFFSET(Side, rect_reload2, 0x202);
OA_ASSERT_OFFSET(Side, rect_reload3, 0x212);
OA_ASSERT_OFFSET(Side, energy_color, 0x222);
OA_ASSERT_OFFSET(Side, metal_color, 0x226);
OA_ASSERT_OFFSET(Side, side_index, 0x22a);
OA_ASSERT_OFFSET(Side, font, 0x22e);

OA_CORE_END

#endif
