// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/game_math.hpp"
#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>

namespace oa::base::game_math {
namespace {
// The exact binary64 constant 3.1c uses; do not replace it with one derived
// from a different pi.
constexpr double radians_to_heading = 10430.37835047;

// Constants of the game's 32-bit wrapped generator. The seed XOR value is a
// scrambling mask with no known external meaning.
constexpr uint32_t random_multiplier = 16807;
constexpr uint32_t random_quotient_divisor = 127773;
constexpr uint32_t random_modulus = 0x7fffffff;
constexpr uint32_t seed_scrambling_mask = 0x66e29572;
constexpr uint32_t force_odd_seed_bit = 1;

// Separate stores preserve binary64 operations and prevent fused multiply-add.
double rounded(double value) noexcept {
    volatile double result = value;
    return result;
}
} // namespace

uint32_t random_bounded(uint32_t& state, uint32_t bound) noexcept {
    if (std::bit_cast<int32_t>(bound) < 2)
        return 0;
    state = state * random_multiplier - (state / random_quotient_divisor) * random_modulus;
    if (std::bit_cast<int32_t>(state) < 1)
        state += random_modulus;
    return state % bound;
}

void seed_random(uint32_t& state, uint32_t input) noexcept {
    state = (input ^ seed_scrambling_mask) | force_odd_seed_bit;
}

uint16_t direction(int32_t x, int32_t z) noexcept {
    const double angle =
        rounded(std::atan2(static_cast<double>(x), static_cast<double>(z)) * radians_to_heading);
    const double lower = std::floor(angle), fraction = angle - lower;
    const auto nearest =
        lower + ((fraction > 0.5 || (fraction == 0.5 && std::fmod(lower, 2.0) != 0)) ? 1.0 : 0.0);
    return static_cast<uint16_t>(static_cast<int32_t>(nearest));
}

double planar_length(int32_t x, int32_t z) noexcept {
    const double a = std::fabs(static_cast<double>(x)), b = std::fabs(static_cast<double>(z));
    const double largest = std::max(a, b);
    if (largest == 0)
        return 0;
    const double nx = rounded(a / largest), nz = rounded(b / largest);
    const double squared = rounded(rounded(nx * nx) + rounded(nz * nz));
    return rounded(std::sqrt(squared) * largest);
}

uint32_t distance(int32_t x, int32_t z) noexcept {
    return static_cast<uint32_t>(static_cast<uint64_t>(planar_length(x, z)));
}

void scale_vector_fixed(int32_t out[3], const int32_t in[3], int32_t scale) noexcept {
    for (int i = 0; i < 3; ++i) {
        const auto product = static_cast<int64_t>(in[i]) * static_cast<int64_t>(scale);
        out[i] = static_cast<int32_t>(product >> 16);
    }
}

int32_t truncated_length(int32_t x, int32_t y, int32_t z) noexcept {
    const auto square = [](int32_t v) {
        return rounded(static_cast<double>(v) * static_cast<double>(v));
    };
    const double sum = rounded(rounded(square(z) + square(y)) + square(x));
    const auto wide = static_cast<int64_t>(std::sqrt(sum));
    return static_cast<int32_t>(static_cast<uint32_t>(static_cast<uint64_t>(wide)));
}

int32_t squared_magnitude_high(int32_t x, int32_t y, int32_t z) noexcept {
    const auto high = [](int32_t value) {
        return static_cast<int32_t>(
            static_cast<uint64_t>(static_cast<int64_t>(value) * static_cast<int64_t>(value)) >> 32
        );
    };
    return high(x) + high(y) + high(z);
}

void float_heap_sift_up(
    FloatHeapPair* heap, int32_t hole, int32_t lower, uint32_t value, float key
) noexcept {
    while (lower < hole && heap[(hole - 1) / 2].key < key) {
        const auto parent = (hole - 1) / 2;
        heap[hole] = heap[parent];
        hole = parent;
    }
    heap[hole].value = value;
    heap[hole].key = key;
}

void float_heap_replace(
    FloatHeapPair* heap, int32_t start, int32_t size, uint32_t value, float key
) noexcept {
    auto hole = start;
    auto child = 0;
    while (true) {
        const auto doubled = hole * 2;
        child = doubled + 2;
        if (size <= child)
            break;
        if (heap[child].key < heap[child - 1].key)
            child = doubled + 1;
        heap[hole] = heap[child];
        hole = child;
    }
    if (child == size) {
        heap[hole] = heap[child - 1];
        hole = hole * 2 + 1;
    }
    float_heap_sift_up(heap, hole, start, value, key);
}

void float_heap_extract(
    FloatHeapPair* heap, int32_t size, FloatHeapPair* out, uint32_t value, float key
) noexcept {
    *out = *heap;
    float_heap_replace(heap, 0, size, value, key);
}

void float_heap_make(FloatHeapPair* heap, int32_t size) noexcept {
    for (auto hole = size / 2; hole > 0;) {
        --hole;
        const auto pair = heap[hole];
        float_heap_replace(heap, hole, size, pair.value, pair.key);
    }
}

void float_heap_pop(FloatHeapPair* heap, int32_t size) noexcept {
    const auto last = heap[size - 1];
    float_heap_extract(heap, size - 1, &heap[size - 1], last.value, last.key);
}
} // namespace oa::base::game_math
