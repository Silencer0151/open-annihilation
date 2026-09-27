// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of the PNG row unfilter: each adaptive filter type on
// small rows with stated bytes, an unknown filter type, and a seeded sweep
// whose digest pins what the reader produces today.

#include "png_internal.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace {

using oa::formats::png::detail::Filter;
using oa::formats::png::detail::k_filter_count;
using oa::formats::png::detail::unfilter_row;

int failures = 0;

/// Records a failed check.
///
/// @param line source line of the check
/// @param what text of the failed condition
void report_failure(int line, const char* what) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, line, what);
    ++failures;
}

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            report_failure(__LINE__, #condition);                                                  \
        }                                                                                          \
    } while (false)

// 32-bit FNV-1a parameters.
constexpr uint32_t fnv_offset_basis = 0x811C9DC5u;
constexpr uint32_t fnv_prime = 0x01000193u;
// Marsaglia's xorshift32 shift triple.
constexpr uint32_t xorshift_left_first = 13;
constexpr uint32_t xorshift_right = 17;
constexpr uint32_t xorshift_left_second = 5;

constexpr uint32_t seed_rows = 0x7F4A7C15u;
constexpr int32_t sweep_rows = 500;
constexpr uint32_t sweep_digest = 0x96110AA5u;
constexpr std::size_t sweep_max_row_bytes = 64;
constexpr std::size_t sweep_max_stride = 8;
// The one filter byte past the defined types that the sweep also feeds.
constexpr uint8_t unknown_filter = k_filter_count;

/// Advances an xorshift32 generator.
///
/// @param[in,out] state generator state, never 0
/// @return the next value
uint32_t next(uint32_t& state) {
    state ^= state << xorshift_left_first;
    state ^= state >> xorshift_right;
    state ^= state << xorshift_left_second;
    return state;
}

/// Unfilters a copy of a row and returns it.
///
/// @param filter filter type byte
/// @param row filtered row
/// @param previous the row above, already unfiltered
/// @param stride bytes back to the left neighbour
/// @param[out] accepted whether unfilter_row accepted the filter type
/// @return the unfiltered copy
std::vector<uint8_t> unfiltered(
    uint8_t filter,
    std::vector<uint8_t> row,
    const std::vector<uint8_t>& previous,
    std::size_t stride,
    bool& accepted
) {
    accepted = unfilter_row(filter, row.data(), previous.data(), row.size(), stride);
    return row;
}

/// Checks each filter type on stated rows.
void test_filter_types() {
    const std::vector<uint8_t> previous = {10, 20, 30, 40, 250, 5};
    const std::vector<uint8_t> row = {1, 2, 3, 4, 10, 255};
    bool accepted = false;

    CHECK(unfiltered(static_cast<uint8_t>(Filter::none), row, previous, 2, accepted) == row);
    CHECK(accepted);
    // Sub adds the byte one stride back, wrapping at 256.
    CHECK(
        unfiltered(static_cast<uint8_t>(Filter::sub), row, previous, 2, accepted) ==
        (std::vector<uint8_t>{1, 2, 4, 6, 14, 5})
    );
    CHECK(accepted);
    // Up adds the byte above.
    CHECK(
        unfiltered(static_cast<uint8_t>(Filter::up), row, previous, 2, accepted) ==
        (std::vector<uint8_t>{11, 22, 33, 44, 4, 4})
    );
    // Average adds the truncated mean of the unfiltered left byte and the byte above.
    CHECK(
        unfiltered(static_cast<uint8_t>(Filter::average), row, previous, 2, accepted) ==
        (std::vector<uint8_t>{6, 12, 21, 30, 145, 16})
    );
    CHECK(accepted);

    // Paeth: the first byte predicts from above (15); the second from the
    // upper left, which is nearest to left + up - upper left.
    CHECK(
        unfiltered(static_cast<uint8_t>(Filter::paeth), {251, 1}, {15, 20}, 1, accepted) ==
        (std::vector<uint8_t>{10, 16})
    );
    CHECK(accepted);
    // With a zero row above, Paeth predicts from the left, as Sub does.
    const std::vector<uint8_t> zeros(row.size(), 0);
    CHECK(
        unfiltered(static_cast<uint8_t>(Filter::paeth), row, zeros, 2, accepted) ==
        unfiltered(static_cast<uint8_t>(Filter::sub), row, zeros, 2, accepted)
    );

    // An unknown type is refused and the row left alone.
    CHECK(unfiltered(unknown_filter, row, previous, 2, accepted) == row);
    CHECK(!accepted);

    // A stride past the row leaves Sub nothing to add.
    CHECK(unfiltered(static_cast<uint8_t>(Filter::sub), row, previous, 8, accepted) == row);
    // An empty row is accepted.
    CHECK(unfiltered(static_cast<uint8_t>(Filter::average), {}, {}, 1, accepted).empty());
    CHECK(accepted);
}

/// Unfilters seeded rows of every type and stride and pins their digest.
void test_sweep() {
    uint32_t state = seed_rows;
    uint32_t digest = fnv_offset_basis;
    std::vector<uint8_t> previous(sweep_max_row_bytes);
    for (int32_t i = 0; i < sweep_rows; ++i) {
        const std::size_t size = 1 + next(state) % sweep_max_row_bytes;
        const std::size_t stride = 1 + next(state) % sweep_max_stride;
        const auto filter = static_cast<uint8_t>(next(state) % (k_filter_count + 1));
        std::vector<uint8_t> row(size);
        for (uint8_t& value : row) {
            value = static_cast<uint8_t>(next(state));
        }
        const bool accepted = unfilter_row(filter, row.data(), previous.data(), size, stride);
        digest = (digest ^ (accepted ? 1u : 0u)) * fnv_prime;
        for (const uint8_t value : row) {
            digest = (digest ^ value) * fnv_prime;
        }
        // The unfiltered row becomes the row above the next one.
        std::copy(row.begin(), row.end(), previous.begin());
    }
    if (digest != sweep_digest) {
        std::fprintf(
            stderr,
            "%s:%d: sweep digest is 0x%08X, expected 0x%08X\n",
            __FILE__,
            __LINE__,
            digest,
            sweep_digest
        );
        ++failures;
    }
}

} // namespace

int main() {
    test_filter_types();
    test_sweep();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
