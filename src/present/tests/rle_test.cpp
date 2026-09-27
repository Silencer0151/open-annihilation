// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of the row run-length sprite codec and the keyed table
// copies: the row encoder, the four row-RLE rectangle decoders (plain,
// blended, remapped, shaded) and the raw keyed copies (blended, shaded,
// masked remap). Edge cases state their bytes; seeded sweeps check each
// decode against the raw sprite it encodes and pin a digest of the result.
//
// The sweeps decode encoder output and the edge cases hand-written rows whose
// commands cover the sprite width. test_malformed_streams feeds the decoders
// streams that end early, rows whose commands stop short and rectangles the
// stream does not hold: each decode stops there and reports it.

#include "synthetic_input.hpp"

#include "oa/present/rle.hpp"
#include "oa/present/surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace {

using namespace oa::present;
using namespace oa::present::test;
using oa::Rect32;
using oa::Sprite;
using oa::Surface;

constexpr uint8_t key = 0x00;
constexpr uint8_t background = 0xEE;
constexpr std::size_t guard_bytes = 256;
constexpr std::size_t table_row_bytes = 0x100;
constexpr std::size_t pair_table_bytes = table_row_bytes * 0x100;
// Rows of a shade table addressed by any pixel value: those below the ramp
// base address rows before the table pointer.
constexpr std::size_t shade_rows_before = static_cast<std::size_t>(shade_ramp_base);

// Sweep sizes and seeds.
constexpr int32_t sweep_rows = 300;
constexpr int32_t sweep_sprites = 60;
constexpr uint32_t seed_rows = 0x428A2F98u;
constexpr uint32_t seed_decode = 0x71374491u;
constexpr uint32_t seed_tables = 0xB5C0FBCFu;
constexpr uint32_t seed_keyed = 0xE9B5DBA5u;

// A raw sprite with its row-RLE encoding.
struct EncodedSprite {
    int32_t width{};
    int32_t height{};
    std::vector<uint8_t> pixels{};
    std::vector<uint8_t> stream{};
};

/// Encodes a row and returns its bytes, checking that measuring agrees with writing.
///
/// @param row row pixels
/// @return the encoded commands
std::vector<uint8_t> encode_row(const std::vector<uint8_t>& row) {
    RleEncoder encoder;
    const auto width = static_cast<int32_t>(row.size());
    const int32_t measured = encode_rle_row(encoder, nullptr, row.data(), width, key);
    std::vector<uint8_t> out(static_cast<std::size_t>(measured) + 1, background);
    const int32_t written = encode_rle_row(encoder, out.data(), row.data(), width, key);
    CHECK_EQ(written, measured);
    CHECK(out.back() == background);
    out.pop_back();
    return out;
}

/// Draws a row of runs: key runs, one-colour runs and literals of varied lengths.
///
/// @param[in,out] random generator state
/// @param width row width in pixels
/// @return the row
std::vector<uint8_t> make_runs(Random& random, int32_t width) {
    std::vector<uint8_t> row;
    row.reserve(static_cast<std::size_t>(width));
    while (static_cast<int32_t>(row.size()) < width) {
        const int32_t left = width - static_cast<int32_t>(row.size());
        const int32_t length = next_in(random, 1, left < 200 ? left : 200);
        switch (next(random) % 4) {
        case 0:
            row.insert(row.end(), static_cast<std::size_t>(length), key);
            break;
        case 1:
            row.insert(
                row.end(),
                static_cast<std::size_t>(length),
                static_cast<uint8_t>(next_in(random, 1, 255))
            );
            break;
        default:
            for (int32_t i = 0; i < length; ++i) {
                row.push_back(next(random) % 8 == 0 ? key : next_byte(random));
            }
            break;
        }
    }
    return row;
}

/// Builds a raw sprite of runs and its row-RLE stream.
///
/// @param[in,out] random generator state
/// @param width width in pixels
/// @param height height in rows
/// @return the sprite pixels and stream
EncodedSprite make_encoded_sprite(Random& random, int32_t width, int32_t height) {
    EncodedSprite result;
    result.width = width;
    result.height = height;
    for (int32_t y = 0; y < height; ++y) {
        const auto row = make_runs(random, width);
        result.pixels.insert(result.pixels.end(), row.begin(), row.end());
    }
    Sprite sprite{};
    sprite.width = static_cast<uint16_t>(width);
    sprite.height = static_cast<uint16_t>(height);
    sprite.key = key;
    sprite.data = result.pixels.data();
    RleEncoder encoder;
    result.stream.resize(static_cast<std::size_t>(encode_rle_sprite(encoder, nullptr, sprite)));
    encode_rle_sprite(encoder, result.stream.data(), sprite);
    return result;
}

/// Views caller-owned pixels as a memory surface with pitch equal to width.
///
/// @param pixels pixels, width * height bytes
/// @param width width in pixels
/// @param height height in rows
/// @return the surface
Surface view_of(std::vector<uint8_t>& pixels, int32_t width, int32_t height) {
    Surface surface{};
    init_surface(surface, width, height, width, pixels.data());
    return surface;
}

/// Checks the row encoder's command bytes for small rows.
void test_encode_rle_row() {
    RleEncoder encoder;
    const std::array<uint8_t, 3> blank = {key, key, key};
    std::array<uint8_t, 4> untouched = {background, background, background, background};
    CHECK_EQ(encode_rle_row(encoder, untouched.data(), blank.data(), 3, key), 0);
    CHECK(untouched[0] == background);

    // Three literals: one copy command.
    CHECK(encode_row({1, 2, 3}) == (std::vector<uint8_t>{0x08, 1, 2, 3}));
    // A key run becomes a skip, a repeat of four a fill.
    CHECK(encode_row({key, key, 5, 5, 5, 5}) == (std::vector<uint8_t>{0x05, 0x0E, 5}));
    // Literals around a key pixel.
    CHECK(encode_row({1, key, 3}) == (std::vector<uint8_t>{0x00, 1, 0x03, 0x00, 3}));
    // A pair that starts the row becomes a fill; after a literal a pair stays
    // literal and only a third equal pixel starts a fill.
    CHECK(encode_row({7, 7, 9}) == (std::vector<uint8_t>{0x06, 7, 0x00, 9}));
    CHECK(encode_row({1, 7, 7, 9}) == (std::vector<uint8_t>{0x0C, 1, 7, 7, 9}));
    CHECK(encode_row({7, 7, 7, 9}) == (std::vector<uint8_t>{0x0A, 7, 0x00, 9}));
    CHECK(encode_row({1, 7, 7, 7}) == (std::vector<uint8_t>{0x00, 1, 0x0A, 7}));

    // Seventy distinct literals split into copies of 64 and 6.
    std::vector<uint8_t> literals;
    for (int32_t i = 0; i < 70; ++i) {
        literals.push_back(static_cast<uint8_t>(i + 1));
    }
    const auto split = encode_row(literals);
    CHECK_EQ(split.size(), 72);
    CHECK(split[0] == 0xFC && split[65] == 0x14);

    Random random{seed_rows};
    uint32_t digest = fnv_offset_basis;
    for (int32_t i = 0; i < sweep_rows; ++i) {
        const auto row = make_runs(random, next_in(random, 1, 400));
        const auto encoded = encode_row(row);
        digest = digest_value(digest, static_cast<int32_t>(encoded.size()));
        digest = digest_bytes(digest, encoded);
    }
    CHECK_EQ(digest, 0x84E83254u);
}

/// Checks decode_rle_rows on a hand-written stream, whole and clipped.
void test_decode_rle_rows() {
    // 4x2: skip 1, copy 2 (7, 8), skip 1 / fill 4 with 9.
    const std::vector<uint8_t> stream = {5, 0, 0x03, 0x04, 7, 8, 0x03, 2, 0, 0x0E, 9};
    auto target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        decode_rle_rows(target.surface.pixels, 6, Rect32{1, 1, 0, 0}, stream, Rect32{0, 0, 3, 1})
    );
    CHECK(
        pixel(target, 1, 1) == background && pixel(target, 2, 1) == 7 && pixel(target, 3, 1) == 8
    );
    CHECK(pixel(target, 4, 1) == background);
    for (int32_t x = 1; x <= 4; ++x) {
        CHECK(pixel(target, x, 2) == 9);
    }
    CHECK_EQ(count_changed(target, background), 6);

    // Columns 2..3: the copy straddling the left edge resumes with its second
    // literal, and the fill with its inside part.
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, stream, Rect32{2, 0, 3, 1});
    CHECK(pixel(target, 0, 0) == 8 && pixel(target, 1, 0) == background);
    CHECK(pixel(target, 0, 1) == 9 && pixel(target, 1, 1) == 9);
    CHECK_EQ(count_changed(target, background), 3);

    // Row 1 alone: the first row is stepped over by its length.
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    decode_rle_rows(target.surface.pixels, 6, Rect32{0, 3, 0, 0}, stream, Rect32{1, 1, 2, 1});
    CHECK(pixel(target, 0, 3) == 9 && pixel(target, 1, 3) == 9);
    CHECK_EQ(count_changed(target, background), 2);

    // Inverted rows or columns decode nothing.
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, stream, Rect32{0, 1, 3, 0})
    );
    CHECK(
        decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, stream, Rect32{3, 0, 2, 1})
    );
    CHECK_EQ(count_changed(target, background), 0);

    // Random sprites decoded through random rectangles: every non-key pixel
    // lands, every key pixel leaves the destination alone.
    Random random{seed_decode};
    target = make_guarded_surface(96, 64, 100, guard_bytes, background);
    int32_t mismatched = 0;
    for (int32_t i = 0; i < sweep_sprites; ++i) {
        // Drawn one per statement: a call's arguments are evaluated in an unspecified order.
        const int32_t width = next_in(random, 1, 90);
        const int32_t height = next_in(random, 1, 40);
        const auto sprite = make_encoded_sprite(random, width, height);
        const int32_t x1 = next_in(random, 0, sprite.width - 1);
        const int32_t y1 = next_in(random, 0, sprite.height - 1);
        const Rect32 rect{
            x1, y1, next_in(random, x1, sprite.width - 1), next_in(random, y1, sprite.height - 1)
        };
        const int32_t dx = next_in(random, 0, 96 - (rect.x2 - rect.x1 + 1));
        const int32_t dy = next_in(random, 0, 64 - (rect.y2 - rect.y1 + 1));
        std::vector<uint8_t> before(target.surface.pixels, target.surface.pixels + 100 * 64);
        decode_rle_rows(target.surface.pixels, 100, Rect32{dx, dy, 0, 0}, sprite.stream, rect);
        for (int32_t y = rect.y1; y <= rect.y2; ++y) {
            for (int32_t x = rect.x1; x <= rect.x2; ++x) {
                const uint8_t source =
                    sprite.pixels[static_cast<std::size_t>(y * sprite.width + x)];
                const int32_t at = (dy + y - rect.y1) * 100 + dx + x - rect.x1;
                const uint8_t expected =
                    source == key ? before[static_cast<std::size_t>(at)] : source;
                mismatched += target.surface.pixels[at] != expected ? 1 : 0;
            }
        }
    }
    CHECK_EQ(mismatched, 0);
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0x8FF79D55u);
}

/// Checks the blended, remapped and shaded row-RLE decoders against their tables.
void test_decode_rle_rows_through_tables() {
    Random random{seed_tables};
    std::vector<uint8_t> pairs(pair_table_bytes);
    fill_random(random, pairs);
    std::array<uint8_t, table_row_bytes> remap{};
    fill_random(random, remap);
    std::vector<uint8_t> shade_storage(pair_table_bytes);
    fill_random(random, shade_storage);
    const uint8_t* shade = shade_storage.data() + shade_rows_before * table_row_bytes;

    // Blended: table[source * 256 + destination].
    const std::vector<uint8_t> stream = {3, 0, 0x04, 0x10, 0x60, 0x03};
    auto target = make_guarded_surface(3, 1, 3, guard_bytes, background);
    decode_rle_rows_blended(
        target.surface.pixels, 3, Rect32{0, 0, 0, 0}, stream, Rect32{0, 0, 2, 0}, pairs.data()
    );
    CHECK(pixel(target, 0, 0) == pairs[0x10 * table_row_bytes + background]);
    CHECK(pixel(target, 1, 0) == pairs[0x60 * table_row_bytes + background]);
    CHECK(pixel(target, 2, 0) == background);

    // Remapped: table[source].
    target = make_guarded_surface(3, 1, 3, guard_bytes, background);
    decode_rle_rows_remapped(
        target.surface.pixels, 3, Rect32{0, 0, 0, 0}, stream, Rect32{0, 0, 2, 0}, remap.data()
    );
    CHECK(pixel(target, 0, 0) == remap[0x10] && pixel(target, 1, 0) == remap[0x60]);

    // Shaded: row (source - 0x4F), so 0x10 reads a row before the table.
    target = make_guarded_surface(3, 1, 3, guard_bytes, background);
    decode_rle_rows_shaded(
        target.surface.pixels, 3, Rect32{0, 0, 0, 0}, stream, Rect32{0, 0, 2, 0}, shade
    );
    CHECK(
        pixel(target, 0, 0) ==
        shade[(0x10 - shade_ramp_base) * static_cast<std::ptrdiff_t>(table_row_bytes) + background]
    );
    CHECK(
        pixel(target, 1, 0) ==
        shade[(0x60 - shade_ramp_base) * static_cast<std::ptrdiff_t>(table_row_bytes) + background]
    );

    target = make_guarded_surface(80, 48, 84, guard_bytes, background);
    fill_random(random, {target.surface.pixels, static_cast<std::size_t>(84 * 48)});
    int32_t mismatched = 0;
    for (int32_t i = 0; i < sweep_sprites; ++i) {
        // Drawn one per statement: a call's arguments are evaluated in an unspecified order.
        const int32_t width = next_in(random, 1, 70);
        const int32_t height = next_in(random, 1, 40);
        const auto sprite = make_encoded_sprite(random, width, height);
        const Rect32 rect{0, 0, sprite.width - 1, sprite.height - 1};
        const int32_t dx = next_in(random, 0, 80 - sprite.width);
        const int32_t dy = next_in(random, 0, 48 - sprite.height);
        std::vector<uint8_t> before(target.surface.pixels, target.surface.pixels + 84 * 48);
        const uint32_t mode = next(random) % 3;
        if (mode == 0) {
            decode_rle_rows_blended(
                target.surface.pixels, 84, Rect32{dx, dy, 0, 0}, sprite.stream, rect, pairs.data()
            );
        } else if (mode == 1) {
            decode_rle_rows_remapped(
                target.surface.pixels, 84, Rect32{dx, dy, 0, 0}, sprite.stream, rect, remap.data()
            );
        } else {
            decode_rle_rows_shaded(
                target.surface.pixels, 84, Rect32{dx, dy, 0, 0}, sprite.stream, rect, shade
            );
        }
        for (int32_t y = 0; y < sprite.height; ++y) {
            for (int32_t x = 0; x < sprite.width; ++x) {
                const uint8_t source =
                    sprite.pixels[static_cast<std::size_t>(y * sprite.width + x)];
                const auto at = static_cast<std::size_t>((dy + y) * 84 + dx + x);
                uint8_t expected = before[at];
                if (source != key) {
                    if (mode == 0) {
                        expected = pairs[source * table_row_bytes + before[at]];
                    } else if (mode == 1) {
                        expected = remap[source];
                    } else {
                        expected = shade
                            [(source - shade_ramp_base) *
                                 static_cast<std::ptrdiff_t>(table_row_bytes) +
                             before[at]];
                    }
                }
                mismatched += target.surface.pixels[at] != expected ? 1 : 0;
            }
        }
    }
    CHECK_EQ(mismatched, 0);
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0x9431CE69u);
}

/// Checks that every row-RLE decoder stops at a malformed row and reports it.
///
/// Each stream is well formed up to one fault: the decode keeps the pixels
/// drawn before it, draws nothing after it and returns false.
void test_malformed_streams() {
    // A 4x2 sprite whose first row copies one pixel (7) and stops; its second
    // row fills 4 with 9.
    const std::vector<uint8_t> short_row = {2, 0, 0x00, 7, 2, 0, 0x0E, 9};
    auto target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(!decode_rle_rows(
        target.surface.pixels, 6, Rect32{1, 1, 0, 0}, short_row, Rect32{0, 0, 3, 1}
    ));
    CHECK(pixel(target, 1, 1) == 7);
    CHECK_EQ(count_changed(target, background), 1);
    CHECK(guards_hold(target, background));
    // Clipped to its first column the row is whole, and so is the second row.
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, short_row, Rect32{0, 0, 0, 1})
    );
    CHECK(pixel(target, 0, 0) == 7 && pixel(target, 0, 1) == 9);
    CHECK_EQ(count_changed(target, background), 2);

    // Commands that end inside the left clip margin.
    const std::vector<uint8_t> short_lead = {1, 0, 0x03};
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(!decode_rle_rows(
        target.surface.pixels, 6, Rect32{0, 0, 0, 0}, short_lead, Rect32{2, 0, 3, 0}
    ));
    CHECK_EQ(count_changed(target, background), 0);

    // A copy whose literals, and a fill whose colour, lie past the row's end.
    const std::vector<uint8_t> cut_copy = {3, 0, 0x0C, 1, 2, 3, 4};
    const std::vector<uint8_t> cut_fill = {1, 0, 0x0E, 9};
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        !decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, cut_copy, Rect32{0, 0, 3, 0})
    );
    CHECK(
        !decode_rle_rows(target.surface.pixels, 6, Rect32{0, 1, 0, 0}, cut_fill, Rect32{0, 0, 3, 0})
    );
    CHECK_EQ(count_changed(target, background), 0);
    // Only the literals the rectangle reads need to lie inside the row.
    CHECK(
        decode_rle_rows(target.surface.pixels, 6, Rect32{0, 2, 0, 0}, cut_copy, Rect32{0, 0, 1, 0})
    );
    CHECK(pixel(target, 0, 2) == 1 && pixel(target, 1, 2) == 2);
    CHECK_EQ(count_changed(target, background), 2);

    // A rectangle taller than the stream: the rows it holds are drawn.
    const std::vector<uint8_t> one_row = {2, 0, 0x0E, 9};
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        !decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, one_row, Rect32{0, 0, 3, 2})
    );
    CHECK_EQ(count_changed(target, background), 4);
    // Rows below the stream, and rows above it.
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        !decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, one_row, Rect32{0, 1, 3, 1})
    );
    CHECK(
        !decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, one_row, Rect32{0, -1, 3, 0})
    );
    CHECK_EQ(count_changed(target, background), 0);

    // A length word that counts past the stream's end, or is cut by it.
    const std::vector<uint8_t> long_row = {9, 0, 0x0E, 9};
    const std::vector<uint8_t> cut_length = {2};
    target = make_guarded_surface(6, 4, 6, guard_bytes, background);
    CHECK(
        !decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, long_row, Rect32{0, 0, 3, 0})
    );
    CHECK(!decode_rle_rows(
        target.surface.pixels, 6, Rect32{0, 0, 0, 0}, cut_length, Rect32{0, 0, 3, 0}
    ));
    CHECK(!decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, {}, Rect32{0, 0, 3, 0}));
    CHECK_EQ(count_changed(target, background), 0);
    // An empty rectangle reads nothing, even from an empty stream.
    CHECK(decode_rle_rows(target.surface.pixels, 6, Rect32{0, 0, 0, 0}, {}, Rect32{0, 1, 3, 0}));

    // The table decoders share the walk: each draws the first row's pixel and
    // stops. Every table here maps a source pixel to itself.
    std::vector<uint8_t> by_source(pair_table_bytes);
    for (std::size_t i = 0; i < by_source.size(); ++i) {
        by_source[i] = static_cast<uint8_t>(i / table_row_bytes);
    }
    std::array<uint8_t, table_row_bytes> same{};
    for (std::size_t i = 0; i < same.size(); ++i) {
        same[i] = static_cast<uint8_t>(i);
    }
    const uint8_t* shade = by_source.data() + shade_rows_before * table_row_bytes;
    target = make_guarded_surface(4, 2, 4, guard_bytes, background);
    CHECK(!decode_rle_rows_blended(
        target.surface.pixels,
        4,
        Rect32{0, 0, 0, 0},
        short_row,
        Rect32{0, 0, 3, 1},
        by_source.data()
    ));
    CHECK(!decode_rle_rows_remapped(
        target.surface.pixels, 4, Rect32{1, 0, 0, 0}, short_row, Rect32{0, 0, 2, 1}, same.data()
    ));
    CHECK(!decode_rle_rows_shaded(
        target.surface.pixels, 4, Rect32{2, 0, 0, 0}, short_row, Rect32{0, 0, 1, 1}, shade
    ));
    CHECK(pixel(target, 0, 0) == 7 && pixel(target, 1, 0) == 7 && pixel(target, 2, 0) == 7);
    CHECK_EQ(count_changed(target, background), 3);
    CHECK(guards_hold(target, background));
}

/// Checks the raw keyed copies through tables: blended, shaded and the masked remap.
void test_keyed_table_copies() {
    Random random{seed_keyed};
    std::vector<uint8_t> pairs(pair_table_bytes);
    fill_random(random, pairs);
    std::array<uint8_t, table_row_bytes> remap{};
    fill_random(random, remap);
    std::vector<uint8_t> shade_storage(pair_table_bytes);
    fill_random(random, shade_storage);
    const uint8_t* shade = shade_storage.data() + shade_rows_before * table_row_bytes;

    std::vector<uint8_t> source_pixels = {0x10, key, 0x60, 0x90, 0x4F, key};
    const Surface source = view_of(source_pixels, 3, 2);
    auto target = make_guarded_surface(4, 3, 5, guard_bytes, background);
    copy_rect_blended(
        target.surface, source, Rect32{0, 0, 2, 1}, Rect32{1, 1, 0, 0}, key, pairs.data()
    );
    CHECK(pixel(target, 1, 1) == pairs[0x10 * table_row_bytes + background]);
    CHECK(pixel(target, 2, 1) == background);
    CHECK(
        pixel(target, 3, 2) == background &&
        pixel(target, 2, 2) == pairs[0x4F * table_row_bytes + background]
    );

    target = make_guarded_surface(4, 3, 5, guard_bytes, background);
    copy_rect_shaded(target.surface, source, Rect32{0, 0, 2, 1}, Rect32{1, 1, 0, 0}, key, shade);
    CHECK(
        pixel(target, 1, 1) ==
        shade[(0x10 - shade_ramp_base) * static_cast<std::ptrdiff_t>(table_row_bytes) + background]
    );
    CHECK(pixel(target, 2, 2) == shade[background]);

    // The mask's own values are ignored: only the destination is remapped.
    target = make_guarded_surface(4, 3, 5, guard_bytes, background);
    remap_under_mask(
        target.surface, source, Rect32{0, 0, 2, 1}, Rect32{1, 1, 0, 0}, key, remap.data()
    );
    CHECK(pixel(target, 1, 1) == remap[background] && pixel(target, 2, 1) == background);
    CHECK(pixel(target, 3, 1) == remap[background]);
    CHECK_EQ(count_changed(target, background), 4);

    // An empty source rectangle copies nothing.
    target = make_guarded_surface(4, 3, 5, guard_bytes, background);
    copy_rect_blended(
        target.surface, source, Rect32{2, 0, 1, 1}, Rect32{0, 0, 0, 0}, key, pairs.data()
    );
    copy_rect_shaded(target.surface, source, Rect32{0, 1, 2, 0}, Rect32{0, 0, 0, 0}, key, shade);
    remap_under_mask(
        target.surface, source, Rect32{2, 1, 1, 0}, Rect32{0, 0, 0, 0}, key, remap.data()
    );
    CHECK_EQ(count_changed(target, background), 0);

    target = make_guarded_surface(64, 40, 68, guard_bytes, background);
    fill_random(random, {target.surface.pixels, static_cast<std::size_t>(68 * 40)});
    std::vector<uint8_t> sheet_pixels(48 * 32);
    for (uint8_t& value : sheet_pixels) {
        value = next(random) % 4 == 0 ? key : next_byte(random);
    }
    const Surface sheet = view_of(sheet_pixels, 48, 32);
    for (int32_t i = 0; i < sweep_sprites; ++i) {
        const int32_t w = next_in(random, 1, 30);
        const int32_t h = next_in(random, 1, 30);
        const int32_t sx = next_in(random, 0, 48 - w);
        const int32_t sy = next_in(random, 0, 32 - h);
        const Rect32 from{sx, sy, sx + w - 1, sy + h - 1};
        const Rect32 to{next_in(random, 0, 64 - w), next_in(random, 0, 40 - h), 0, 0};
        switch (next(random) % 3) {
        case 0:
            copy_rect_blended(target.surface, sheet, from, to, key, pairs.data());
            break;
        case 1:
            copy_rect_shaded(target.surface, sheet, from, to, key, shade);
            break;
        default:
            remap_under_mask(target.surface, sheet, from, to, key, remap.data());
            break;
        }
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0xBFE7C658u);
}

} // namespace

int main() {
    test_encode_rle_row();
    test_decode_rle_rows();
    test_decode_rle_rows_through_tables();
    test_malformed_streams();
    test_keyed_table_copies();
    return finish();
}
