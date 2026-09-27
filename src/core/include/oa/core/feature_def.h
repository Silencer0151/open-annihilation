// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Feature definition record (feature TDF files). */
#ifndef OA_CORE_FEATURE_DEF_H
#define OA_CORE_FEATURE_DEF_H

#include "oa/core/types.h"

/* Bits of FeatureDef.flags, packed from feature TDF booleans. */
#define OA_FEATURE_FLAG_SPRITE 0x0001u /* GAF sprite; clear for a 3DO object feature */
#define OA_FEATURE_FLAG_ANIMATING 0x0002u
#define OA_FEATURE_FLAG_ANIM_TRANS 0x0004u
#define OA_FEATURE_FLAG_SHAD_TRANS 0x0008u
#define OA_FEATURE_FLAG_FLAMABLE 0x0010u
#define OA_FEATURE_FLAG_GEOTHERMAL 0x0020u
#define OA_FEATURE_FLAG_BLOCKING 0x0040u
#define OA_FEATURE_FLAG_RECLAIMABLE 0x0080u
#define OA_FEATURE_FLAG_AUTO_RECLAIMABLE 0x0100u
#define OA_FEATURE_FLAG_INDESTRUCTIBLE 0x0200u
#define OA_FEATURE_FLAG_NO_DISPLAY_INFO 0x0400u
#define OA_FEATURE_FLAG_NO_DRAW_UNDER_GRAY                                                         \
    0x0800u /* also forced for dragon's teeth and fortification walls */

OA_CORE_BEGIN

#pragma pack(push, 1)

/* Where an animating feature type is in one of its GAF sequences. Loading
 * starts it on the sequence's first frame and each tick steps it. */
typedef struct FeatureDefCursor {
    uint16_t frame;
    uint16_t remaining; /* ticks left on the frame */
    uint8_t repeat;     /* nonzero when the sequence loops */
    /* No known use: zero when the definition is created; loading and
       stepping leave them unchanged. */
    uint8_t reserved_after_repeat[0x3];
    /* seq_name or seq_name_shadow while the type animates; 0 otherwise, and
       once a one-shot sequence ends */
    oa_ref32 sequence;
} FeatureDefCursor;

/* One feature TDF block; the game state points at a growable array of these. */
typedef struct FeatureDef {
    char name[128];
    char description[20];
    int16_t footprint_x;
    int16_t footprint_z;
    char animation_file[16]; /* GAF name, "reuse", or a loaded 3DO ref for object features */
    oa_ref32 animation;      /* GAF archive */
    oa_ref32 seq_name;
    oa_ref32 seq_name_shadow;
    oa_ref32 seq_name_burn;
    oa_ref32 seq_name_burn_shadow;
    oa_ref32 seq_name_die;
    oa_ref32 seq_name_die_shadow;
    oa_ref32 seq_name_reclamate;
    oa_ref32 seq_name_reclamate_shadow;
    FeatureDefCursor animation_cursor; /* on seq_name while the type is animating */
    FeatureDefCursor shadow_cursor;    /* on seq_name_shadow while the type is animating */
    oa_ref32 burn_weapon;              /* WeaponDef */
    int16_t spark_time;                /* ticks: the TDF sparktime in seconds times 30 */
    int16_t damage;                    /* hit points */
    float energy;
    float metal;
    uint16_t dead_feature;      /* featuredead index, 0xffff none */
    uint16_t burnt_feature;     /* featureburnt index, 0xffff none */
    uint16_t reclamate_feature; /* featurereclamate index, 0xffff none */
    int8_t height;
    int8_t spread_chance;
    int8_t reproduce;
    int8_t reproduce_area;
    uint16_t flags; /* FEATURE_FLAG_* */
} FeatureDef;

#pragma pack(pop)

OA_ASSERT_SIZE(FeatureDefCursor, 0xc);
OA_ASSERT_OFFSET(FeatureDefCursor, frame, 0x0);
OA_ASSERT_OFFSET(FeatureDefCursor, remaining, 0x2);
OA_ASSERT_OFFSET(FeatureDefCursor, repeat, 0x4);
OA_ASSERT_OFFSET(FeatureDefCursor, reserved_after_repeat, 0x5);
OA_ASSERT_OFFSET(FeatureDefCursor, sequence, 0x8);

OA_ASSERT_SIZE(FeatureDef, 0x100);
OA_ASSERT_OFFSET(FeatureDef, name, 0x0);
OA_ASSERT_OFFSET(FeatureDef, description, 0x80);
OA_ASSERT_OFFSET(FeatureDef, footprint_x, 0x94);
OA_ASSERT_OFFSET(FeatureDef, footprint_z, 0x96);
OA_ASSERT_OFFSET(FeatureDef, animation_file, 0x98);
OA_ASSERT_OFFSET(FeatureDef, animation, 0xa8);
OA_ASSERT_OFFSET(FeatureDef, seq_name, 0xac);
OA_ASSERT_OFFSET(FeatureDef, seq_name_shadow, 0xb0);
OA_ASSERT_OFFSET(FeatureDef, seq_name_burn, 0xb4);
OA_ASSERT_OFFSET(FeatureDef, seq_name_burn_shadow, 0xb8);
OA_ASSERT_OFFSET(FeatureDef, seq_name_die, 0xbc);
OA_ASSERT_OFFSET(FeatureDef, seq_name_die_shadow, 0xc0);
OA_ASSERT_OFFSET(FeatureDef, seq_name_reclamate, 0xc4);
OA_ASSERT_OFFSET(FeatureDef, seq_name_reclamate_shadow, 0xc8);
OA_ASSERT_OFFSET(FeatureDef, animation_cursor, 0xcc);
OA_ASSERT_OFFSET(FeatureDef, shadow_cursor, 0xd8);
OA_ASSERT_OFFSET(FeatureDef, burn_weapon, 0xe4);
OA_ASSERT_OFFSET(FeatureDef, spark_time, 0xe8);
OA_ASSERT_OFFSET(FeatureDef, damage, 0xea);
OA_ASSERT_OFFSET(FeatureDef, energy, 0xec);
OA_ASSERT_OFFSET(FeatureDef, metal, 0xf0);
OA_ASSERT_OFFSET(FeatureDef, dead_feature, 0xf4);
OA_ASSERT_OFFSET(FeatureDef, burnt_feature, 0xf6);
OA_ASSERT_OFFSET(FeatureDef, reclamate_feature, 0xf8);
OA_ASSERT_OFFSET(FeatureDef, height, 0xfa);
OA_ASSERT_OFFSET(FeatureDef, spread_chance, 0xfb);
OA_ASSERT_OFFSET(FeatureDef, reproduce, 0xfc);
OA_ASSERT_OFFSET(FeatureDef, reproduce_area, 0xfd);
OA_ASSERT_OFFSET(FeatureDef, flags, 0xfe);

OA_CORE_END

#endif
