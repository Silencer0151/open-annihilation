// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Shared primitive types for the canonical game-state records.
 *
 * Every record in oa/core keeps the byte layout of the 3.1c game's record:
 * fixed-width fields, packed where 3.1c packs them, and a 32-bit reference
 * slot wherever a record refers to another. The headers compile as C11 and
 * C++20; in C++ the types live in namespace oa. */
#ifndef OA_CORE_TYPES_H
#define OA_CORE_TYPES_H

#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
#define OA_STATIC_ASSERT(cond, msg) static_assert(cond, msg)
#define OA_CORE_BEGIN namespace oa {
#define OA_CORE_END }
#else
#define OA_STATIC_ASSERT(cond, msg) _Static_assert(cond, msg)
#define OA_CORE_BEGIN
#define OA_CORE_END
#endif

#define OA_ASSERT_SIZE(type, size) OA_STATIC_ASSERT(sizeof(type) == (size), #type " size")
#define OA_ASSERT_OFFSET(type, field, offset)                                                      \
    OA_STATIC_ASSERT(offsetof(type, field) == (offset), #type "." #field " offset")

OA_CORE_BEGIN

/* Signed 16.16 world coordinate, speed or scale. */
typedef int32_t oa_fixed;

/* Binary angle: 65536 units per revolution. */
typedef uint16_t oa_angle;

/* A 32-bit slot by which a record refers to another. Engine code keeps a
 * handle or index here; the field comment names the target record. */
typedef uint32_t oa_ref32;

#pragma pack(push, 1)

typedef struct FixedVec3 {
    oa_fixed x;
    oa_fixed y;
    oa_fixed z;
} FixedVec3;

/* Inclusive screen rectangle as written from x1/y1/x2/y2 TDF keys. */
typedef struct Rect32 {
    int32_t x1;
    int32_t y1;
    int32_t x2;
    int32_t y2;
} Rect32;

#pragma pack(pop)

OA_ASSERT_SIZE(FixedVec3, 12);
OA_ASSERT_SIZE(Rect32, 16);

OA_CORE_END

#endif
