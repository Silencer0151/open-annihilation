// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of the 8-bit line and rectangle primitives: segment
// clipping to an extent and to a clip rectangle, rectangle clamping, line and
// rectangle fills, table remaps of lines and row blocks, and the dithered
// clear. Edge cases state their pixels; seeded sweeps pin a digest of what
// the engine draws today.

#include "synthetic_input.hpp"

#include "oa/present/raster.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace {

using namespace oa::present;
using namespace oa::present::test;
using oa::Rect32;

constexpr uint8_t background = 0xEE;
constexpr uint8_t ink = 0x5A;
constexpr std::size_t guard_bytes = 256;

// Sweep sizes and seeds.
constexpr int32_t sweep_lines = 400;
constexpr uint32_t seed_extent_clip = 0x2545F491u;
constexpr uint32_t seed_rect_clip = 0x9E3779B9u;
constexpr uint32_t seed_fill_lines = 0x6A09E667u;
constexpr uint32_t seed_remap_lines = 0xBB67AE85u;
constexpr uint32_t seed_dither = 0x3C6EF372u;
constexpr uint32_t seed_remap_rows = 0xA54FF53Au;

/// Builds a remap table whose row r maps a value v to v + r + 1.
///
/// @param rows number of 256-byte rows
/// @return the table
std::vector<uint8_t> make_step_table(int32_t rows) {
    std::vector<uint8_t> table(static_cast<std::size_t>(rows) * 0x100);
    for (int32_t r = 0; r < rows; ++r) {
        for (int32_t v = 0; v < 0x100; ++v) {
            table[static_cast<std::size_t>(r) * 0x100 + static_cast<std::size_t>(v)] =
                static_cast<uint8_t>(v + r + 1);
        }
    }
    return table;
}

/// Reports whether remap_line keeps every write of a segment inside the surface.
///
/// A y-major segment drifts one column left on each step that does not take
/// the diagonal. Only one climbing from its left end can drift in front of the
/// first row; every other segment inside the surface stays inside.
///
/// @param pitch bytes per row
/// @param x0 start column
/// @param y0 start row
/// @param x1 end column
/// @param y1 end row
/// @return true when the lowest byte the segment can reach is a surface byte
bool remap_stays_inside(int32_t pitch, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
    if (x0 == x1) {
        return true;
    }
    if (x1 < x0) {
        std::swap(x0, x1);
        std::swap(y0, y1);
    }
    const int32_t rise = y0 - y1;
    if (rise <= x1 - x0) {
        return true;
    }
    return y1 * pitch + x0 - rise >= 0;
}

/// Checks segment clipping to an extent, including the bottom-edge result count.
void test_clip_line_to_extent() {
    constexpr int32_t width = 10;
    constexpr int32_t height = 8;
    {
        int32_t x0 = 1, y0 = 1, x1 = 5, y1 = 6;
        CHECK_EQ(clip_line_to_extent(width, height, x0, y0, x1, y1), 1);
        CHECK(x0 == 1 && y0 == 1 && x1 == 5 && y1 == 6);
    }
    {
        // Wholly left of the extent.
        int32_t x0 = -5, y0 = 2, x1 = -1, y1 = 6;
        CHECK_EQ(clip_line_to_extent(width, height, x0, y0, x1, y1), 0);
    }
    {
        // Crossing the left edge: y moves by 4 * 5 / 10.
        int32_t x0 = -4, y0 = 0, x1 = 6, y1 = 5;
        CHECK_EQ(clip_line_to_extent(width, height, x0, y0, x1, y1), 1);
        CHECK(x0 == 0 && y0 == 2 && x1 == 6 && y1 == 5);
    }
    {
        // Every clip of the end against the bottom edge adds one to the result.
        int32_t x0 = 2, y0 = 2, x1 = 2, y1 = 12;
        CHECK_EQ(clip_line_to_extent(width, height, x0, y0, x1, y1), 2);
        CHECK(x0 == 2 && y0 == 2 && x1 == 2 && y1 == 7);
    }
    Random random{seed_extent_clip};
    uint32_t digest = fnv_offset_basis;
    for (int32_t i = 0; i < sweep_lines; ++i) {
        int32_t x0 = next_in(random, -3 * width, 4 * width);
        int32_t y0 = next_in(random, -3 * height, 4 * height);
        int32_t x1 = next_in(random, -3 * width, 4 * width);
        int32_t y1 = next_in(random, -3 * height, 4 * height);
        digest = digest_value(digest, clip_line_to_extent(width, height, x0, y0, x1, y1));
        digest = digest_value(digest, x0);
        digest = digest_value(digest, y0);
        digest = digest_value(digest, x1);
        digest = digest_value(digest, y1);
    }
    CHECK_EQ(digest, 0x0C178672u);
}

/// Checks segment clipping to the surface clip rectangle.
void test_clip_line_to_rect() {
    auto target = make_guarded_surface(12, 8, 12, 0, background);
    target.surface.clip = Rect32{2, 1, 9, 6};
    {
        int32_t x0 = 3, y0 = 2, x1 = 8, y1 = 5;
        CHECK_EQ(clip_line_to_rect(target.surface, x0, y0, x1, y1), 1);
        CHECK(x0 == 3 && y0 == 2 && x1 == 8 && y1 == 5);
    }
    {
        // Left of the clip and moving further left.
        int32_t x0 = 0, y0 = 2, x1 = -3, y1 = 4;
        CHECK_EQ(clip_line_to_rect(target.surface, x0, y0, x1, y1), 0);
    }
    {
        // Entering through the left edge: y moves by 2 * 4 / 8.
        int32_t x0 = 0, y0 = 1, x1 = 8, y1 = 5;
        CHECK_EQ(clip_line_to_rect(target.surface, x0, y0, x1, y1), 1);
        CHECK(x0 == 2 && y0 == 2 && x1 == 8 && y1 == 5);
    }
    {
        // A vertical segment left of the clip cannot be moved onto it.
        int32_t x0 = 1, y0 = 2, x1 = 1, y1 = 5;
        CHECK_EQ(clip_line_to_rect(target.surface, x0, y0, x1, y1), 0);
    }
    Random random{seed_rect_clip};
    uint32_t digest = fnv_offset_basis;
    for (int32_t i = 0; i < sweep_lines; ++i) {
        int32_t x0 = next_in(random, -20, 30);
        int32_t y0 = next_in(random, -20, 30);
        int32_t x1 = next_in(random, -20, 30);
        int32_t y1 = next_in(random, -20, 30);
        digest = digest_value(digest, clip_line_to_rect(target.surface, x0, y0, x1, y1));
        digest = digest_value(digest, x0);
        digest = digest_value(digest, y0);
        digest = digest_value(digest, x1);
        digest = digest_value(digest, y1);
    }
    CHECK_EQ(digest, 0xC873CDCEu);
}

/// Checks rectangle clamping to the surface clip rectangle.
void test_clip_rect() {
    auto target = make_guarded_surface(12, 8, 12, 0, background);
    target.surface.clip = Rect32{2, 1, 9, 6};
    Rect32 overlapping{0, 0, 20, 3};
    CHECK(clip_rect(target.surface, overlapping));
    CHECK(overlapping.x1 == 2 && overlapping.y1 == 1 && overlapping.x2 == 9 && overlapping.y2 == 3);

    Rect32 outside{10, 0, 12, 3};
    CHECK(!clip_rect(target.surface, outside));
    CHECK(outside.x1 == 10 && outside.y1 == 0 && outside.x2 == 12 && outside.y2 == 3);

    // Inside the clip but inverted: left as it is and reported empty.
    Rect32 inverted{5, 5, 4, 6};
    CHECK(!clip_rect(target.surface, inverted));
    CHECK(inverted.x1 == 5 && inverted.y1 == 5 && inverted.x2 == 4 && inverted.y2 == 6);

    // A single pixel on the clip corner survives.
    Rect32 corner{9, 6, 9, 6};
    CHECK(clip_rect(target.surface, corner));
}

/// Checks line fills clipped to the surface extent.
void test_fill_line() {
    auto target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_line(target.surface, 1, 2, 5, 2, ink);
    for (int32_t x = 1; x <= 5; ++x) {
        CHECK(pixel(target, x, 2) == ink);
    }
    CHECK_EQ(count_changed(target, background), 5);

    // Vertical, drawn from the bottom end.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_line(target.surface, 3, 5, 3, 1, ink);
    for (int32_t y = 1; y <= 5; ++y) {
        CHECK(pixel(target, 3, y) == ink);
    }
    CHECK_EQ(count_changed(target, background), 5);

    // Diagonal, and a single point.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_line(target.surface, 0, 0, 4, 4, ink);
    fill_line(target.surface, 7, 0, 7, 0, ink);
    for (int32_t i = 0; i <= 4; ++i) {
        CHECK(pixel(target, i, i) == ink);
    }
    CHECK(pixel(target, 7, 0) == ink);
    CHECK_EQ(count_changed(target, background), 6);

    // Clipped to the extent, not to the clip rectangle.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    target.surface.clip = Rect32{4, 4, 5, 5};
    fill_line(target.surface, -3, -3, 2, 2, ink);
    for (int32_t i = 0; i <= 2; ++i) {
        CHECK(pixel(target, i, i) == ink);
    }
    CHECK_EQ(count_changed(target, background), 3);

    // Wholly outside draws nothing.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_line(target.surface, 9, 0, 20, 5, ink);
    CHECK_EQ(count_changed(target, background), 0);

    target = make_guarded_surface(64, 48, 72, guard_bytes, background);
    Random random{seed_fill_lines};
    for (int32_t i = 0; i < sweep_lines; ++i) {
        const int32_t x0 = next_in(random, -32, 96);
        const int32_t y0 = next_in(random, -24, 72);
        const int32_t x1 = next_in(random, -32, 96);
        const int32_t y1 = next_in(random, -24, 72);
        fill_line(target.surface, x0, y0, x1, y1, next_byte(random));
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0x74657608u);
}

/// Checks rectangle fills, including the inverted-row quirk.
void test_fill_rect() {
    auto target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_rect(target.surface, Rect32{1, 1, 3, 2}, ink);
    CHECK_EQ(count_changed(target, background), 6);
    CHECK(pixel(target, 1, 1) == ink && pixel(target, 3, 2) == ink);

    // No width: nothing.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_rect(target.surface, Rect32{3, 1, 2, 4}, ink);
    CHECK_EQ(count_changed(target, background), 0);

    // Rows below y1 are inverted: the first row is still filled.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    fill_rect(target.surface, Rect32{1, 3, 2, 1}, ink);
    CHECK_EQ(count_changed(target, background), 2);
    CHECK(pixel(target, 1, 3) == ink && pixel(target, 2, 3) == ink);
}

/// Checks the row-block remap.
void test_remap_rows() {
    const auto table = make_step_table(1);
    auto target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    remap_rows(&target.surface.pixels[1 * 10 + 2], 10, 3, 2, table.data());
    CHECK_EQ(count_changed(target, background), 6);
    CHECK(pixel(target, 2, 1) == background + 1 && pixel(target, 4, 2) == background + 1);
    CHECK(pixel(target, 5, 1) == background);

    // Non-positive sizes do nothing.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    remap_rows(target.surface.pixels, 10, 0, 3, table.data());
    remap_rows(target.surface.pixels, 10, 3, -1, table.data());
    CHECK_EQ(count_changed(target, background), 0);

    target = make_guarded_surface(40, 30, 48, guard_bytes, background);
    Random random{seed_remap_rows};
    fill_random(random, {target.surface.pixels, static_cast<std::size_t>(48 * 30)});
    std::array<uint8_t, 0x100> shuffle{};
    fill_random(random, shuffle);
    for (int32_t i = 0; i < 20; ++i) {
        const int32_t x = next_in(random, 0, 39);
        const int32_t y = next_in(random, 0, 29);
        const int32_t w = next_in(random, -2, 40 - x);
        const int32_t h = next_in(random, -2, 30 - y);
        remap_rows(&target.surface.pixels[y * 48 + x], 48, w, h, shuffle.data());
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0xF302FCF0u);
}

/// Checks line remaps, including the vertical and sloped quirks.
void test_remap_line() {
    constexpr int32_t rows = 4;
    const auto table = make_step_table(rows);
    // Vertical: the start row is addressed by width (6) rather than pitch (10),
    // so column 2 from row 1 lands on column 8 of row 0.
    auto target = make_guarded_surface(6, 5, 10, guard_bytes, background);
    remap_line(target.surface, 2, 1, 2, 3, 0, table.data());
    CHECK_EQ(count_changed(target, background), 3);
    CHECK(pixel(target, 8, 0) == background + 1);
    CHECK(pixel(target, 8, 1) == background + 1);
    CHECK(pixel(target, 8, 2) == background + 1);

    // Horizontal lines remap every pixel once through the chosen row.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    remap_line(target.surface, 5, 1, 1, 1, 2, table.data());
    CHECK_EQ(count_changed(target, background), 5);
    CHECK(pixel(target, 1, 1) == background + 3 && pixel(target, 5, 1) == background + 3);

    // An x-major slope never leaves its first column: each of its rows is
    // remapped twice.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    remap_line(target.surface, 0, 0, 5, 2, 0, table.data());
    CHECK_EQ(count_changed(target, background), 3);
    CHECK(pixel(target, 0, 0) == background + 2);
    CHECK(pixel(target, 0, 1) == background + 2);
    CHECK(pixel(target, 0, 2) == background + 2);

    // A y-major slope drifts left one column per step that does not take the
    // diagonal.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    remap_line(target.surface, 4, 0, 5, 4, 0, table.data());
    CHECK_EQ(count_changed(target, background), 5);
    CHECK(pixel(target, 4, 0) == background + 1);
    CHECK(pixel(target, 3, 1) == background + 1);
    CHECK(pixel(target, 3, 2) == background + 1);
    CHECK(pixel(target, 2, 3) == background + 1);
    CHECK(pixel(target, 1, 4) == background + 1);

    // Climbing from column 0, that drift runs in front of the first row: from
    // (0, 3) to (1, 0) the remap reaches offsets 30, 19 and 9 of the surface,
    // then the byte two before it. light_clipped_line passes such a segment
    // on whole, since both ends lie inside the clip rectangle.
    target = make_guarded_surface(8, 6, 10, guard_bytes, background);
    CHECK(!remap_stays_inside(target.surface.pitch, 0, 3, 1, 0));
    remap_line(target.surface, 0, 3, 1, 0, 0, table.data());
    CHECK_EQ(count_changed(target, background), 4);
    CHECK(pixel(target, 0, 3) == background + 1);
    CHECK(pixel(target, 9, 1) == background + 1);
    CHECK(pixel(target, 9, 0) == background + 1);
    CHECK(target.storage[guard_bytes - 2] == background + 1);
    CHECK(!guards_hold(target, background));

    // Seeded segments whose writes stay inside the surface.
    target = make_guarded_surface(48, 32, 56, guard_bytes, background);
    Random random{seed_remap_lines};
    int32_t drawn = 0;
    for (int32_t i = 0; i < sweep_lines; ++i) {
        const int32_t x0 = next_in(random, 0, 47);
        const int32_t y0 = next_in(random, 0, 31);
        const int32_t x1 = next_in(random, 0, 47);
        const int32_t y1 = next_in(random, 0, 31);
        const int32_t row = next_in(random, 0, rows - 1);
        if (!remap_stays_inside(target.surface.pitch, x0, y0, x1, y1)) {
            continue;
        }
        remap_line(target.surface, x0, y0, x1, y1, row, table.data());
        ++drawn;
    }
    CHECK_EQ(drawn, 397);
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0x2DA93072u);
}

/// Checks the dithered clear, including the aligned-middle overrun.
void test_clear_dithered_rect() {
    // Row 0 clears the even columns, row 1 the odd ones; the aligned middle
    // runs to x2 rounded up to four, one and two pixels past x2 = 5.
    auto target = make_guarded_surface(12, 4, 12, guard_bytes, background);
    CHECK_EQ(clear_dithered_rect(&target.surface, Rect32{1, 0, 5, 1}, 0), 1);
    const std::array<uint8_t, 12> row0 = {
        background,
        background,
        0,
        background,
        0,
        background,
        0,
        background,
        background,
        background,
        background,
        background
    };
    const std::array<uint8_t, 12> row1 = {
        background,
        0,
        background,
        0,
        background,
        0,
        background,
        0,
        background,
        background,
        background,
        background
    };
    for (int32_t x = 0; x < 12; ++x) {
        CHECK(pixel(target, x, 0) == row0[static_cast<std::size_t>(x)]);
        CHECK(pixel(target, x, 1) == row1[static_cast<std::size_t>(x)]);
    }
    CHECK_EQ(count_changed(target, background), 7);

    // An odd phase swaps the halves.
    target = make_guarded_surface(12, 4, 12, guard_bytes, background);
    CHECK_EQ(clear_dithered_rect(&target.surface, Rect32{1, 0, 5, 0}, 1), 1);
    CHECK(
        pixel(target, 1, 0) == 0 && pixel(target, 3, 0) == 0 && pixel(target, 2, 0) == background
    );

    // Clipped to the clip rectangle; wholly outside clears nothing.
    target = make_guarded_surface(12, 4, 12, guard_bytes, background);
    target.surface.clip = Rect32{0, 0, 3, 3};
    CHECK_EQ(clear_dithered_rect(&target.surface, Rect32{6, 0, 9, 3}, 0), 1);
    CHECK_EQ(count_changed(target, background), 0);

    target = make_guarded_surface(40, 24, 40, guard_bytes, background);
    Random random{seed_dither};
    for (int32_t i = 0; i < 40; ++i) {
        const int32_t x1 = next_in(random, -8, 44);
        const int32_t y1 = next_in(random, -4, 26);
        const Rect32 rect{x1, y1, x1 + next_in(random, -2, 20), y1 + next_in(random, -2, 10)};
        target.surface.clip = Rect32{
            next_in(random, 0, 4),
            next_in(random, 0, 4),
            next_in(random, 30, 39),
            next_in(random, 18, 23)
        };
        clear_dithered_rect(&target.surface, rect, next_in(random, 0, 3));
        // Refill so later rectangles clear fresh pixels.
        if (i % 8 == 7) {
            fill_random(random, {target.surface.pixels, static_cast<std::size_t>(40 * 24)});
        }
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0x785C66B4u);
}

} // namespace

int main() {
    test_clip_line_to_extent();
    test_clip_line_to_rect();
    test_clip_rect();
    test_fill_line();
    test_fill_rect();
    test_remap_rows();
    test_remap_line();
    test_clear_dithered_rect();
    return finish();
}
