// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/geometry.hpp"

#include <cmath>
#include <cstdint>

namespace oa::base::geometry {
namespace {

// 512 samples per turn plus a quarter turn so the cosine view can index 128
// entries in without wrapping.
constexpr int16_t trig_table[trig_samples_per_turn + trig_quarter_turn_samples] = {
#include "trig_table.inc"
};

// Signed product shifted down to 1.13, keeping the low 32 bits.
int32_t fx(int32_t a, int32_t b) noexcept {
    const int64_t product = static_cast<int64_t>(a) * b;
    return static_cast<int32_t>(
        static_cast<uint32_t>(static_cast<uint64_t>(product >> trig_fraction_bits))
    );
}

int32_t wrap_add(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) + static_cast<uint32_t>(b));
}

int32_t wrap_sub(int32_t a, int32_t b) noexcept {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

int32_t wrap_neg(int32_t a) noexcept {
    return static_cast<int32_t>(0u - static_cast<uint32_t>(a));
}

} // namespace

bool rect_contains_point(const Rect32* rect, int32_t x, int32_t y) noexcept {
    return !(x < rect->x1 || rect->x2 < x || y < rect->y1 || rect->y2 < y);
}

bool rect_contains_rect(const Rect32* inner, const Rect32* outer) noexcept {
    return outer->x1 <= inner->x1 && inner->x1 <= outer->x2 && outer->x1 <= inner->x2 &&
           inner->x2 <= outer->x2 && outer->y1 <= inner->y1 && inner->y1 <= outer->y2 &&
           outer->y1 <= inner->y2 && inner->y2 <= outer->y2;
}

bool rects_overlap(const Rect32* a, const Rect32* b) noexcept {
    if (b->x2 < a->x1 || a->x2 < b->x1 || b->y2 < a->y1) {
        return false;
    }
    return b->y1 <= a->y2;
}

int32_t trig_sin(uint16_t angle) noexcept {
    return trig_table[angle >> trig_angle_shift];
}

int32_t trig_cos(uint16_t angle) noexcept {
    return trig_table[trig_quarter_turn_samples + (angle >> trig_angle_shift)];
}

int32_t mul_high32(int32_t a, int32_t b) noexcept {
    const int64_t product = static_cast<int64_t>(a) * b;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(product) >> 32));
}

int32_t mul_div(int32_t a, int32_t b, int32_t divisor) noexcept {
    if (divisor == 0) {
        return 0;
    }
    const int64_t quotient = static_cast<int64_t>(a) * b / divisor;
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(quotient)));
}

void rotation_matrix_build(
    RotationMatrix* matrix, uint16_t first_angle, uint16_t second_angle, uint16_t third_angle
) noexcept {
    const int32_t cos_b = trig_cos(second_angle);
    const int32_t cos_c = trig_cos(third_angle);
    const int32_t cos_a = trig_cos(first_angle);
    const int32_t sin_c = trig_sin(third_angle);
    const int32_t sin_a = trig_sin(first_angle);
    const int32_t sin_b = trig_sin(second_angle);
    auto& m = matrix->m;
    m[0][0] = fx(cos_c, cos_b);
    m[0][1] = wrap_add(fx(fx(sin_b, sin_a), cos_c), fx(sin_c, cos_a));
    m[0][2] = wrap_sub(fx(sin_a, sin_c), fx(fx(cos_a, sin_b), cos_c));
    m[1][0] = wrap_neg(fx(cos_b, sin_c));
    m[1][1] = wrap_sub(fx(cos_a, cos_c), fx(fx(sin_a, sin_b), sin_c));
    m[1][2] = wrap_add(fx(fx(cos_a, sin_b), sin_c), fx(sin_a, cos_c));
    m[2][0] = sin_b;
    m[2][1] = wrap_neg(fx(sin_a, cos_b));
    m[2][2] = fx(cos_a, cos_b);
}

void rotation_matrix_apply(
    const RotationMatrix* matrix, const int32_t in[3], int32_t out[3]
) noexcept {
    const int32_t x = in[0];
    const int32_t y = in[1];
    const int32_t z = in[2];
    const auto& m = matrix->m;
    for (int column = 0; column < 3; ++column) {
        out[column] =
            wrap_add(wrap_add(fx(z, m[2][column]), fx(x, m[0][column])), fx(y, m[1][column]));
    }
}

Vec3f vec3f_sub(Vec3f from, Vec3f to) noexcept {
    return {to.x - from.x, to.y - from.y, to.z - from.z};
}

Vec3f vec3f_int_delta(
    int32_t from_x, int32_t from_y, int32_t from_z, int32_t to_x, int32_t to_y, int32_t to_z
) noexcept {
    const auto delta = [](int32_t from, int32_t to) {
        return static_cast<float>(
            static_cast<int32_t>(static_cast<uint32_t>(to) - static_cast<uint32_t>(from))
        );
    };
    return {delta(from_x, to_x), delta(from_y, to_y), delta(from_z, to_z)};
}

double vec3f_length(Vec3f v) noexcept {
    double sum = static_cast<double>(v.z) * v.z + static_cast<double>(v.y) * v.y;
    sum += static_cast<double>(v.x) * v.x;
    return std::sqrt(sum);
}

Vec3f vec3f_normalize(Vec3f v) noexcept {
    const double length = vec3f_length(v);
    return {
        static_cast<float>(v.x / length),
        static_cast<float>(v.y / length),
        static_cast<float>(v.z / length)
    };
}

Vec3f vec3f_cross(Vec3f a, Vec3f b) noexcept {
    return {
        static_cast<float>(static_cast<double>(a.y) * b.z - static_cast<double>(a.z) * b.y),
        static_cast<float>(static_cast<double>(a.z) * b.x - static_cast<double>(a.x) * b.z),
        static_cast<float>(static_cast<double>(a.x) * b.y - static_cast<double>(a.y) * b.x)
    };
}

} // namespace oa::base::geometry
