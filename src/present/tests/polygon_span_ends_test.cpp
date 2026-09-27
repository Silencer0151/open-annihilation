// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of plot_span_ends, the outline span writer: both end
// pixels of a span row, depth tested when the target sprite has a depth
// plane. Edge cases state their pixels; a seeded sweep pins a digest.

#include "synthetic_input.hpp"

#include "oa/present/polygon.hpp"

#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using namespace oa::present;
using namespace oa::present::test;
using oa::Sprite;

constexpr uint8_t ink = 0x5A;
constexpr uint8_t depth_fill = 0x40;
constexpr int32_t fixed_shift = 16; // span depths are 16.16
constexpr int32_t sweep_rows = 300;
constexpr uint32_t seed_spans = 0x243F6A88u;

// A sprite with owned pixel and optional depth planes.
struct SpriteTarget {
    Sprite sprite{};
    std::vector<uint8_t> pixels{};
    std::vector<uint8_t> depth{};
};

/// Allocates a sprite target with zero pixels and, when asked, a depth plane.
///
/// @param width width in pixels
/// @param height height in rows
/// @param with_depth true to give the sprite a depth plane filled with depth_fill
/// @return the target
SpriteTarget make_target(int32_t width, int32_t height, bool with_depth) {
    SpriteTarget target;
    target.pixels.assign(static_cast<std::size_t>(width * height), 0);
    target.sprite.width = static_cast<uint16_t>(width);
    target.sprite.height = static_cast<uint16_t>(height);
    target.sprite.data = target.pixels.data();
    if (with_depth) {
        target.depth.assign(target.pixels.size(), depth_fill);
        target.sprite.aux = target.depth.data();
    }
    return target;
}

/// Builds a span row with integer edges and integer depths.
///
/// @param left left edge column
/// @param right right edge column
/// @param left_depth depth at the left edge
/// @param right_depth depth at the right edge
/// @return the row, depths in 16.16
PolygonSpanRow span_row(int32_t left, int32_t right, int32_t left_depth, int32_t right_depth) {
    PolygonSpanRow row{};
    row.left = left;
    row.right = right;
    row.left_depth = left_depth << fixed_shift;
    row.right_depth = right_depth << fixed_shift;
    return row;
}

/// Counts the non-zero bytes of a plane.
///
/// @param plane bytes to scan
/// @return the count
int32_t count_set(const std::vector<uint8_t>& plane) {
    int32_t count = 0;
    for (const uint8_t value : plane) {
        count += value != 0 ? 1 : 0;
    }
    return count;
}

/// Checks the end pixels written without and with a depth plane.
void test_plot_span_ends() {
    // Without a depth plane both ends are written: the left edge and the
    // column the exclusive right edge names.
    auto target = make_target(8, 4, false);
    plot_span_ends(1, span_row(2, 5, 0, 0), target.sprite, ink);
    CHECK(target.pixels[1 * 8 + 2] == ink && target.pixels[1 * 8 + 5] == ink);
    CHECK_EQ(count_set(target.pixels), 2);

    // An empty or inverted span writes nothing.
    target = make_target(8, 4, false);
    plot_span_ends(1, span_row(4, 4, 0, 0), target.sprite, ink);
    plot_span_ends(1, span_row(5, 3, 0, 0), target.sprite, ink);
    CHECK_EQ(count_set(target.pixels), 0);

    // With a depth plane an end is written where the stored depth is at most
    // the end's integer depth, and the plane takes the new depth.
    target = make_target(8, 4, true);
    plot_span_ends(2, span_row(1, 6, depth_fill - 0x10, depth_fill + 0x10), target.sprite, ink);
    CHECK(target.pixels[2 * 8 + 1] == 0 && target.depth[2 * 8 + 1] == depth_fill);
    CHECK(target.pixels[2 * 8 + 6] == ink && target.depth[2 * 8 + 6] == depth_fill + 0x10);
    plot_span_ends(3, span_row(0, 7, depth_fill, depth_fill), target.sprite, ink);
    CHECK(target.pixels[3 * 8 + 0] == ink && target.pixels[3 * 8 + 7] == ink);
    CHECK_EQ(count_set(target.pixels), 3);

    Random random{seed_spans};
    target = make_target(40, 30, true);
    for (int32_t i = 0; i < sweep_rows; ++i) {
        const int32_t y = next_in(random, 0, 29);
        const int32_t left = next_in(random, 0, 39);
        const int32_t right = next_in(random, left - 3, 39);
        PolygonSpanRow row{};
        row.left = left;
        row.right = right;
        row.left_depth = static_cast<int32_t>(next(random) & 0x00FFFFFFu);
        row.right_depth = static_cast<int32_t>(next(random) & 0x00FFFFFFu);
        plot_span_ends(y, row, target.sprite, next_byte(random));
    }
    uint32_t digest = digest_bytes(fnv_offset_basis, target.pixels);
    digest = digest_bytes(digest, target.depth);
    CHECK_EQ(digest, 0x2742436Bu);
}

} // namespace

int main() {
    test_plot_span_ends();
    return finish();
}
