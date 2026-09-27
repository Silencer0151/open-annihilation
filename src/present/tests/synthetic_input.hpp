// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Seeded synthetic inputs, digests and checks shared by the presentation
// characterisation tests. Their pinned values are what the engine produces
// for these inputs today: a changed digest means changed pixels.
#pragma once

#include "oa/present/surface.h"

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <span>
#include <vector>

namespace oa::present::test {

inline int failures = 0;

/// Records a failed check.
///
/// @param file source file of the check
/// @param line source line of the check
/// @param what text of the failed condition
inline void report_failure(const char* file, int line, const char* what) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, what);
    ++failures;
}

/// Compares an engine-produced value with the value a test pins, printing both on a mismatch.
///
/// @param file source file of the check
/// @param line source line of the check
/// @param what text of the checked expression
/// @param actual value the engine produced
/// @param expected value the test pins
inline void
check_value(const char* file, int line, const char* what, int64_t actual, int64_t expected) {
    if (actual == expected) {
        return;
    }
    std::fprintf(
        stderr,
        "%s:%d: %s is %lld (0x%llX), expected %lld (0x%llX)\n",
        file,
        line,
        what,
        static_cast<long long>(actual),
        static_cast<unsigned long long>(actual),
        static_cast<long long>(expected),
        static_cast<unsigned long long>(expected)
    );
    ++failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            ::oa::present::test::report_failure(__FILE__, __LINE__, #condition);                   \
        }                                                                                          \
    } while (false)

#define CHECK_EQ(actual, expected)                                                                 \
    ::oa::present::test::check_value(                                                              \
        __FILE__, __LINE__, #actual, static_cast<int64_t>(actual), static_cast<int64_t>(expected)  \
    )

/// Prints the failure count and returns the test's exit code.
///
/// @return 0 when every check passed, 1 otherwise
inline int finish() {
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}

// Marsaglia's xorshift32 shift triple.
inline constexpr uint32_t xorshift_left_first = 13;
inline constexpr uint32_t xorshift_right = 17;
inline constexpr uint32_t xorshift_left_second = 5;

// Reproducible pseudo-random source for synthetic inputs; the state must not be 0.
struct Random {
    uint32_t state{};
};

/// Advances the generator.
///
/// @param[in,out] random generator state
/// @return the next 32-bit value
inline uint32_t next(Random& random) {
    uint32_t x = random.state;
    x ^= x << xorshift_left_first;
    x ^= x >> xorshift_right;
    x ^= x << xorshift_left_second;
    random.state = x;
    return x;
}

/// Draws a value in an inclusive range.
///
/// @param[in,out] random generator state
/// @param low smallest value
/// @param high largest value, at least `low`
/// @return a value in [low, high]
inline int32_t next_in(Random& random, int32_t low, int32_t high) {
    const auto span = static_cast<uint32_t>(static_cast<int64_t>(high) - low + 1);
    return static_cast<int32_t>(low + static_cast<int64_t>(next(random) % span));
}

/// Draws a byte.
///
/// @param[in,out] random generator state
/// @return the low byte of the next value
inline uint8_t next_byte(Random& random) {
    return static_cast<uint8_t>(next(random));
}

/// Fills bytes from the generator.
///
/// @param[in,out] random generator state
/// @param[out] bytes bytes to fill
inline void fill_random(Random& random, std::span<uint8_t> bytes) {
    for (uint8_t& byte : bytes) {
        byte = next_byte(random);
    }
}

// 32-bit FNV-1a parameters.
inline constexpr uint32_t fnv_offset_basis = 0x811C9DC5u;
inline constexpr uint32_t fnv_prime = 0x01000193u;

/// Extends an FNV-1a digest over bytes.
///
/// @param digest running digest; start from fnv_offset_basis
/// @param bytes bytes to add
/// @return the extended digest
inline uint32_t digest_bytes(uint32_t digest, std::span<const uint8_t> bytes) {
    for (const uint8_t byte : bytes) {
        digest = (digest ^ byte) * fnv_prime;
    }
    return digest;
}

/// Extends an FNV-1a digest over a 32-bit value, low byte first.
///
/// @param digest running digest
/// @param value value to add
/// @return the extended digest
inline uint32_t digest_value(uint32_t digest, int32_t value) {
    const auto bits = static_cast<uint32_t>(value);
    const uint8_t bytes[] = {
        static_cast<uint8_t>(bits),
        static_cast<uint8_t>(bits >> 8),
        static_cast<uint8_t>(bits >> 16),
        static_cast<uint8_t>(bits >> 24)
    };
    return digest_bytes(digest, bytes);
}

/// Returns the FNV-1a digest of bytes.
///
/// @param bytes bytes to digest
/// @return the digest
inline uint32_t digest_of(std::span<const uint8_t> bytes) {
    return digest_bytes(fnv_offset_basis, bytes);
}

// Pixel storage with guard bytes before and after the surface, so routines
// whose quirks step outside a row stay inside owned memory and the guard
// bytes show whether they were touched.
struct GuardedSurface {
    Surface surface{};
    std::vector<uint8_t> storage{};
    std::size_t guard{};
};

/// Allocates a memory surface of `pitch` bytes per row with guard bytes on both sides.
///
/// Every byte, guards included, starts as `fill`; the clip is the whole surface.
///
/// @param width width in pixels
/// @param height height in rows
/// @param pitch bytes per row, at least `width`
/// @param guard bytes before the first row and after the last
/// @param fill initial value of every byte
/// @return the surface and its storage
inline GuardedSurface make_guarded_surface(
    int32_t width, int32_t height, int32_t pitch, std::size_t guard, uint8_t fill
) {
    GuardedSurface result;
    result.guard = guard;
    result.storage.assign(
        guard * 2 + static_cast<std::size_t>(pitch) * static_cast<std::size_t>(height), fill
    );
    result.surface.width = width;
    result.surface.height = height;
    result.surface.pitch = pitch;
    result.surface.pixels = result.storage.data() + guard;
    result.surface.clip = Rect32{0, 0, width - 1, height - 1};
    result.surface.flags = OA_SURFACE_FLAG_MEMORY;
    return result;
}

/// Returns the byte at a column and row of a guarded surface.
///
/// @param target surface to read
/// @param x column
/// @param y row
/// @return the pixel
inline uint8_t pixel(const GuardedSurface& target, int32_t x, int32_t y) {
    return target.surface.pixels[static_cast<std::ptrdiff_t>(y) * target.surface.pitch + x];
}

/// Counts the bytes of a guarded surface's storage, guards included, that differ from a value.
///
/// @param target surface to scan
/// @param value byte the storage was filled with
/// @return the number of other bytes
inline int32_t count_changed(const GuardedSurface& target, uint8_t value) {
    int32_t changed = 0;
    for (const uint8_t byte : target.storage) {
        changed += byte != value ? 1 : 0;
    }
    return changed;
}

/// Reports whether a guarded surface's guard bytes still hold a value.
///
/// @param target surface to scan
/// @param value byte the guards were filled with
/// @return true when no guard byte changed
inline bool guards_hold(const GuardedSurface& target, uint8_t value) {
    const std::size_t end = target.storage.size();
    for (std::size_t i = 0; i < target.guard; ++i) {
        if (target.storage[i] != value || target.storage[end - 1 - i] != value) {
            return false;
        }
    }
    return true;
}

} // namespace oa::present::test
