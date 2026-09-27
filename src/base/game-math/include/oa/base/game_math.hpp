// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>

namespace oa::base::game_math {
/// Draws a bounded number from the game's shared random generator.
///
/// A Park-Miller style step: state * 16807 - (state / 127773) * (2^31 - 1),
/// with every intermediate wrapping at 32 bits and a non-positive result
/// raised by 2^31 - 1.
///
/// @param[in,out] state shared generator state, owned by the caller
/// @param bound number of possible results
/// @return state % bound, or 0 without advancing the state when bound, read
///         as signed, is below 2
uint32_t random_bounded(uint32_t& state, uint32_t bound) noexcept;
/// Seeds the game's shared random generator; no host randomness is introduced.
///
/// @param[out] state generator state
/// @param input seed value; it is XORed with a fixed mask and forced odd
void seed_random(uint32_t& state, uint32_t input) noexcept;
/// Returns the heading of the planar vector (x, z).
///
/// @param x east-west component
/// @param z north-south component
/// @return atan2(x, z) scaled to 65536 per turn and rounded to nearest even,
///         low 16 bits (COB GET selectors 12 and 14 use it)
uint16_t direction(int32_t x, int32_t z) noexcept;
/// Returns the length of the planar vector (x, z), truncated.
///
/// @param x east-west component
/// @param z north-south component
/// @return planar_length truncated toward zero to 64 bits, low 32 bits kept
///         (COB GET selectors 13 and 15 use it)
uint32_t distance(int32_t x, int32_t z) noexcept;
/// Returns the length of the planar vector (x, z) as a double.
///
/// The absolute components are normalized by their maximum, squared and
/// summed, square-rooted and rescaled, each step rounded to binary64.
///
/// @param x east-west component
/// @param z north-south component
/// @return the length before truncation
[[nodiscard]] double planar_length(int32_t x, int32_t z) noexcept;
/// Scales a 16.16 vector by a 16.16 factor.
///
/// @param[out] out scaled vector; may alias in
/// @param in vector to scale, 16.16 fixed point
/// @param scale factor, 16.16 fixed point
/// @quirk Each component uses a 64-bit multiply and an arithmetic shift right by
///        16, so negative results round toward negative infinity.
void scale_vector_fixed(int32_t out[3], const int32_t in[3], int32_t scale) noexcept;
/// Returns the length of (x, y, z) truncated toward zero.
///
/// @param x first component
/// @param y second component
/// @param z third component
/// @return the length truncated to 64 bits, low 32 bits kept
/// @quirk The squares are summed as z^2 + y^2 before x^2, which can land on a
///        different double than x^2 first.
/// @quirk A length past 2^31 wraps negative instead of saturating.
[[nodiscard]] int32_t truncated_length(int32_t x, int32_t y, int32_t z) noexcept;

/// Returns the sum of the high 32 bits of the three signed squares.
///
/// Projectile collision compares this with a range squared.
///
/// @param x first component
/// @param y second component
/// @param z third component
/// @return (x*x >> 32) + (y*y >> 32) + (z*z >> 32)
[[nodiscard]] int32_t squared_magnitude_high(int32_t x, int32_t y, int32_t z) noexcept;

// 8-byte max-heap node. The float is the key; the 32-bit word is the payload.
struct FloatHeapPair {
    uint32_t value{};
    float key{};
};

/// Moves a hole up a max-heap while the parent key is smaller, then writes a pair there.
///
/// @param[in,out] heap heap array
/// @param hole index of the empty slot
/// @param lower subtree root the hole must not climb past
/// @param value payload of the new pair
/// @param key key of the new pair
void float_heap_sift_up(
    FloatHeapPair* heap, int32_t hole, int32_t lower, uint32_t value, float key
) noexcept;

/// Fills a hole in a max-heap with a new pair.
///
/// Pulls the larger child into the hole down to the bottom, then places the
/// pair with float_heap_sift_up.
///
/// @param[in,out] heap heap array
/// @param hole index of the empty slot; also the subtree root the pair may not climb past
/// @param size one past the last occupied index
/// @param value payload of the new pair
/// @param key key of the new pair
/// @quirk On equal child keys the right child is taken.
void float_heap_replace(
    FloatHeapPair* heap, int32_t hole, int32_t size, uint32_t value, float key
) noexcept;

/// Copies the root pair out, then re-fills the root with a new pair.
///
/// @param[in,out] heap heap array
/// @param size element count passed to float_heap_replace
/// @param[out] out receives the root pair; written before the heap changes
/// @param value payload of the new pair
/// @param key key of the new pair
void float_heap_extract(
    FloatHeapPair* heap, int32_t size, FloatHeapPair* out, uint32_t value, float key
) noexcept;

/// Orders pairs into a max-heap.
///
/// Re-places each parent from the last one (size / 2 - 1) down to the root.
///
/// @param[in,out] heap heap array
/// @param size number of pairs
void float_heap_make(FloatHeapPair* heap, int32_t size) noexcept;

/// Moves the root pair to the last slot and re-heaps the others.
///
/// @param[in,out] heap heap array
/// @param size number of pairs, at least 1
void float_heap_pop(FloatHeapPair* heap, int32_t size) noexcept;
} // namespace oa::base::game_math
