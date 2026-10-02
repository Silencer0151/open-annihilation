// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Integer rectangles, fixed-point trigonometry and rotation, and the small
// float vector helpers shared across the engine.

#include "oa/core/types.h"

#include <cstdint>

namespace oa::base::geometry {

// Rectangles are the canonical inclusive oa::Rect32 (x1, y1, x2, y2).

/// Tests whether a point lies inside a rectangle, edges included.
///
/// @param rect inclusive rectangle
/// @param x point column
/// @param y point row
/// @return true when x1 <= x <= x2 and y1 <= y <= y2
[[nodiscard]] bool rect_contains_point(const Rect32* rect, int32_t x, int32_t y) noexcept;
/// Tests whether one rectangle lies entirely inside another, edges included.
///
/// Both horizontal edges of inner are checked against outer's span, then both
/// vertical edges.
///
/// @param inner rectangle that must fit
/// @param outer rectangle that must contain it
/// @return true when all four edges of inner lie within outer
[[nodiscard]] bool rect_contains_rect(const Rect32* inner, const Rect32* outer) noexcept;
/// Tests whether two inclusive rectangles share at least one point.
///
/// @param a first rectangle
/// @param b second rectangle
/// @return true when the rectangles overlap or touch
[[nodiscard]] bool rects_overlap(const Rect32* a, const Rect32* b) noexcept;

// Engine angles are 16-bit binary angles (0x10000 per turn). The trig table
// holds 512 samples per turn of sin in 1.13 fixed point (8192 = 1.0).
inline constexpr unsigned trig_fraction_bits = 13;
inline constexpr unsigned trig_angle_shift = 7;
inline constexpr unsigned trig_samples_per_turn = 512;
inline constexpr unsigned trig_quarter_turn_samples = trig_samples_per_turn / 4;

/// Looks up the sine of a binary angle in the 512-sample table.
///
/// @param angle binary angle, 0x10000 per turn; the low 7 bits are ignored
/// @return sine in 1.13 fixed point (8192 = 1.0)
[[nodiscard]] int32_t trig_sin(uint16_t angle) noexcept;
/// Looks up the cosine of a binary angle, a quarter turn into the sine table.
///
/// @param angle binary angle, 0x10000 per turn; the low 7 bits are ignored
/// @return cosine in 1.13 fixed point (8192 = 1.0)
[[nodiscard]] int32_t trig_cos(uint16_t angle) noexcept;

/// Returns the high 32 bits of the signed 64-bit product.
///
/// @param a first factor
/// @param b second factor
/// @return (a * b) >> 32
[[nodiscard]] int32_t mul_high32(int32_t a, int32_t b) noexcept;
/// Computes (a * b) / divisor with a 64-bit intermediate, truncating toward zero.
///
/// @param a first factor
/// @param b second factor
/// @param divisor divisor; 0 yields 0
/// @return the quotient's low 32 bits, also when it does not fit 32 bits
[[nodiscard]] int32_t mul_div(int32_t a, int32_t b, int32_t divisor) noexcept;

// 1.13 fixed-point rotation matrix, row major (m[row][column]).
struct RotationMatrix {
    int32_t m[3][3]{};
};

/// Builds a rotation matrix from an object's three angle words.
///
/// Which axis each angle rotates about is not yet confirmed.
///
/// @param[out] matrix receives the 1.13 fixed-point rotation
/// @param first_angle first of the three angle words, a binary angle
/// @param second_angle second angle word, a binary angle
/// @param third_angle third angle word, a binary angle
/// @quirk Products are truncated to 1.13 after each multiply, in 3.1c's order.
void rotation_matrix_build(
    RotationMatrix* matrix, uint16_t first_angle, uint16_t second_angle, uint16_t third_angle
) noexcept;
/// Multiplies a row vector by a rotation matrix.
///
/// @param matrix 1.13 fixed-point rotation
/// @param in vector to rotate (x, y, z)
/// @param[out] out in * matrix; may alias in
/// @quirk Each product is truncated to 1.13 before summing.
void rotation_matrix_apply(
    const RotationMatrix* matrix, const int32_t in[3], int32_t out[3]
) noexcept;

struct Vec3f {
    float x{};
    float y{};
    float z{};
};

/// Subtracts one vector from another.
///
/// @param from start point
/// @param to end point
/// @return to - from
[[nodiscard]] Vec3f vec3f_sub(Vec3f from, Vec3f to) noexcept;
/// Returns the float difference of two integer points.
///
/// @param from_x start x
/// @param from_y start y
/// @param from_z start z
/// @param to_x end x
/// @param to_y end y
/// @param to_z end z
/// @return to - from, each difference wrapped at 32 bits before conversion to float
[[nodiscard]] Vec3f vec3f_int_delta(
    int32_t from_x, int32_t from_y, int32_t from_z, int32_t to_x, int32_t to_y, int32_t to_z
) noexcept;
/// Returns the Euclidean length of a vector.
///
/// @param v vector measured
/// @return sqrt(z*z + y*y + x*x), accumulated in double precision in that order
[[nodiscard]] double vec3f_length(Vec3f v) noexcept;
/// Scales a vector to unit length.
///
/// Each component is divided by the double-precision length, then rounded to float.
///
/// @param v vector to normalize
/// @return the unit vector
/// @quirk A zero vector yields NaN components, as in 3.1c.
[[nodiscard]] Vec3f vec3f_normalize(Vec3f v) noexcept;
/// Returns the cross product of two vectors.
///
/// @param a left operand
/// @param b right operand
/// @return a x b; each component's two products are exact in double, their
///         difference is rounded once there and then to float
[[nodiscard]] Vec3f vec3f_cross(Vec3f a, Vec3f b) noexcept;

} // namespace oa::base::geometry
