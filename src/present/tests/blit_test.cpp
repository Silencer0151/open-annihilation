// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of the rectangle copies and the opaque sprite draw:
// clip trimming, whole-surface copies clipped to the destination (plain and
// keyed), unclipped rectangle copies (plain and keyed) and draw_sprite_opaque
// for raw, row-RLE and composite sprites. Edge cases state their pixels;
// seeded sweeps pin a digest of what the engine draws today.

#include "synthetic_input.hpp"

#include "oa/present/blit.hpp"
#include "oa/present/rle.hpp"
#include "oa/present/surface.hpp"

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <vector>

namespace {

using namespace oa::present;
using namespace oa::present::test;
using oa::Rect32;
using oa::Sprite;

constexpr uint8_t background = 0xEE;
constexpr uint8_t key = 0x00;
constexpr std::size_t guard_bytes = 256;
// Sprite.child_draw_mode of a child drawn blended; any non-zero value is.
constexpr uint8_t child_draw_blended = 1;

// Sweep sizes and seeds.
constexpr int32_t sweep_copies = 200;
constexpr uint32_t seed_trim = 0x510E527Fu;
constexpr uint32_t seed_surface_copies = 0x9B05688Cu;
constexpr uint32_t seed_rect_copies = 0x1F83D9ABu;
constexpr uint32_t seed_sprites = 0x5BE0CD19u;

/// Builds a source surface whose pixel at (x, y) is 16 * y + x + 1, with a key-colour column.
///
/// @param width width in pixels, at most 15
/// @param height height in rows, at most 15
/// @param key_column column filled with the key colour; outside the surface for none
/// @return the surface
GuardedSurface make_numbered_source(int32_t width, int32_t height, int32_t key_column) {
    auto source = make_guarded_surface(width, height, width, 0, key);
    for (int32_t y = 0; y < height; ++y) {
        for (int32_t x = 0; x < width; ++x) {
            source.surface.pixels[y * width + x] =
                x == key_column ? key : static_cast<uint8_t>(16 * y + x + 1);
        }
    }
    return source;
}

/// Checks trim_to_clip on a destination partly left of and below the clip.
void test_trim_to_clip() {
    Rect32 src{0, 0, 9, 9};
    Rect32 dst{-3, 2, 6, 11};
    trim_to_clip(src, dst, Rect32{0, 0, 7, 7});
    CHECK(src.x1 == 3 && src.y1 == 0 && src.x2 == 9 && src.y2 == 5);
    CHECK(dst.x1 == 0 && dst.y1 == 2 && dst.x2 == 6 && dst.y2 == 7);

    // Wholly right of and below the clip: the result is inverted, not checked.
    src = Rect32{0, 0, 2, 2};
    dst = Rect32{10, 10, 12, 12};
    trim_to_clip(src, dst, Rect32{0, 0, 7, 7});
    CHECK(src.x1 == 0 && src.y1 == 0 && src.x2 == -3 && src.y2 == -3);
    CHECK(dst.x1 == 10 && dst.y1 == 10 && dst.x2 == 7 && dst.y2 == 7);

    Random random{seed_trim};
    uint32_t digest = fnv_offset_basis;
    for (int32_t i = 0; i < sweep_copies; ++i) {
        const int32_t w = next_in(random, 1, 20);
        const int32_t h = next_in(random, 1, 20);
        const int32_t x = next_in(random, -25, 40);
        const int32_t y = next_in(random, -25, 40);
        src = Rect32{0, 0, w - 1, h - 1};
        dst = Rect32{x, y, x + w - 1, y + h - 1};
        const int32_t cx = next_in(random, 0, 10);
        const int32_t cy = next_in(random, 0, 10);
        trim_to_clip(
            src, dst, Rect32{cx, cy, cx + next_in(random, 0, 20), cy + next_in(random, 0, 20)}
        );
        for (const int32_t value :
             {src.x1, src.y1, src.x2, src.y2, dst.x1, dst.y1, dst.x2, dst.y2}) {
            digest = digest_value(digest, value);
        }
    }
    CHECK_EQ(digest, 0x54D70CC0u);
}

/// Checks the whole-surface copies clipped to the destination extent.
void test_copy_surface() {
    const auto source = make_numbered_source(3, 2, 1);
    auto target = make_guarded_surface(4, 3, 6, guard_bytes, background);
    copy_surface_clipped(target.surface, source.surface, -1, 1);
    // Source columns 1 and 2 land on columns 0 and 1 of rows 1 and 2.
    CHECK(pixel(target, 0, 1) == key && pixel(target, 1, 1) == 3);
    CHECK(pixel(target, 0, 2) == key && pixel(target, 1, 2) == 19);
    CHECK(pixel(target, 2, 1) == background && pixel(target, 0, 0) == background);

    // The keyed copy leaves the destination under key pixels.
    target = make_guarded_surface(4, 3, 6, guard_bytes, background);
    copy_surface_keyed(target.surface, source.surface, 1, 0, key);
    CHECK(
        pixel(target, 1, 0) == 1 && pixel(target, 2, 0) == background && pixel(target, 3, 0) == 3
    );
    CHECK(
        pixel(target, 1, 1) == 17 && pixel(target, 2, 1) == background && pixel(target, 3, 1) == 19
    );

    // Wholly outside on either side copies nothing; the clip rectangle is ignored.
    target = make_guarded_surface(4, 3, 6, guard_bytes, background);
    target.surface.clip = Rect32{3, 2, 3, 2};
    copy_surface_clipped(target.surface, source.surface, 4, 0);
    copy_surface_clipped(target.surface, source.surface, -3, 0);
    copy_surface_clipped(target.surface, source.surface, 0, 3);
    copy_surface_clipped(target.surface, source.surface, 0, -2);
    copy_surface_clipped(target.surface, source.surface, 0, 0);
    CHECK(pixel(target, 0, 0) == 1 && pixel(target, 2, 1) == 19);

    Random random{seed_surface_copies};
    target = make_guarded_surface(40, 30, 44, guard_bytes, background);
    for (int32_t i = 0; i < sweep_copies; ++i) {
        const int32_t w = next_in(random, 1, 24);
        const int32_t h = next_in(random, 1, 24);
        auto piece = make_guarded_surface(w, h, w + next_in(random, 0, 3), 0, key);
        fill_random(random, piece.storage);
        const int32_t x = next_in(random, -30, 45);
        const int32_t y = next_in(random, -30, 35);
        if (next(random) % 2 == 0) {
            copy_surface_clipped(target.surface, piece.surface, x, y);
        } else {
            copy_surface_keyed(
                target.surface, piece.surface, x, y, static_cast<uint8_t>(next_byte(random) & 0x3)
            );
        }
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0xFA4ED64Cu);
}

/// Checks the unclipped rectangle copies.
void test_copy_rect() {
    const auto source = make_numbered_source(5, 4, 2);
    auto target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    copy_rect(target.surface, source.surface, Rect32{1, 1, 3, 2}, Rect32{2, 3, 99, 99});
    CHECK(pixel(target, 2, 3) == 18 && pixel(target, 3, 3) == key && pixel(target, 4, 3) == 20);
    CHECK(pixel(target, 2, 4) == 34 && pixel(target, 4, 4) == 36);
    CHECK(pixel(target, 1, 3) == background && pixel(target, 5, 3) == background);

    target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    copy_rect_keyed(target.surface, source.surface, Rect32{1, 1, 3, 2}, Rect32{2, 3, 99, 99}, key);
    CHECK(
        pixel(target, 2, 3) == 18 && pixel(target, 3, 3) == background && pixel(target, 4, 3) == 20
    );

    // An empty or inverted source rectangle copies nothing.
    target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    copy_rect(target.surface, source.surface, Rect32{2, 1, 1, 2}, Rect32{0, 0, 0, 0});
    copy_rect_keyed(target.surface, source.surface, Rect32{1, 2, 3, 1}, Rect32{0, 0, 0, 0}, key);
    CHECK(pixel(target, 0, 0) == background && pixel(target, 1, 0) == background);

    Random random{seed_rect_copies};
    target = make_guarded_surface(40, 30, 44, guard_bytes, background);
    auto sheet = make_guarded_surface(32, 32, 36, 0, key);
    fill_random(random, sheet.storage);
    for (int32_t i = 0; i < sweep_copies; ++i) {
        const int32_t w = next_in(random, 1, 16);
        const int32_t h = next_in(random, 1, 16);
        const int32_t sx = next_in(random, 0, 32 - w);
        const int32_t sy = next_in(random, 0, 32 - h);
        const Rect32 from{sx, sy, sx + w - 1, sy + h - 1};
        const Rect32 to{next_in(random, 0, 40 - w), next_in(random, 0, 30 - h), 0, 0};
        if (next(random) % 2 == 0) {
            copy_rect(target.surface, sheet.surface, from, to);
        } else {
            copy_rect_keyed(
                target.surface,
                sheet.surface,
                from,
                to,
                static_cast<uint8_t>(next_byte(random) & 0x3)
            );
        }
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0x236AD460u);
}

/// Checks draw_sprite_opaque for raw, row-RLE and composite sprites.
void test_draw_sprite_opaque() {
    // A raw sprite ignores its key colour and is clipped to the clip rectangle.
    auto raw = create_sprite(3, 2);
    raw.pixels = {1, key, 3, 4, 5, key};
    raw.sprite.data = raw.pixels.data();
    raw.sprite.origin_x = 1;
    raw.sprite.origin_y = 1;
    raw.sprite.key = key;
    auto target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    target.surface.clip = Rect32{0, 0, 3, 4};
    draw_sprite_opaque(&target.surface, &raw.sprite, 2, 2);
    CHECK(pixel(target, 1, 1) == 1 && pixel(target, 2, 1) == key && pixel(target, 3, 1) == 3);
    CHECK(pixel(target, 1, 2) == 4 && pixel(target, 2, 2) == 5 && pixel(target, 3, 2) == key);
    draw_sprite_opaque(&target.surface, &raw.sprite, 4, 3);
    CHECK(pixel(target, 3, 2) == 1 && pixel(target, 4, 2) == background);
    draw_sprite_opaque(&target.surface, nullptr, 0, 0);

    // The same sprite as a row-RLE stream leaves key pixels untouched.
    RleEncoder encoder;
    std::vector<uint8_t> stream(
        static_cast<std::size_t>(encode_rle_sprite(encoder, nullptr, raw.sprite))
    );
    encode_rle_sprite(encoder, stream.data(), raw.sprite);
    Sprite rle = raw.sprite;
    rle.encoding = OA_SPRITE_ROW_RLE;
    rle.data = stream.data();
    target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    draw_sprite_opaque(&target.surface, &rle, 1, 1);
    CHECK(
        pixel(target, 0, 0) == 1 && pixel(target, 1, 0) == background && pixel(target, 2, 0) == 3
    );
    CHECK(
        pixel(target, 0, 1) == 4 && pixel(target, 1, 1) == 5 && pixel(target, 2, 1) == background
    );

    // A row whose commands stop short of the sprite width ends the draw
    // there: the first row copies one pixel (7), and the second row, a fill
    // of 9, is not drawn.
    std::vector<uint8_t> short_row = {2, 0, 0x00, 7, 2, 0, 0x0E, 9};
    Sprite cut = rle;
    cut.width = 4;
    cut.data = short_row.data();
    target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    draw_sprite_opaque(&target.surface, &cut, 1, 1);
    CHECK(pixel(target, 0, 0) == 7);
    CHECK_EQ(count_changed(target, background), 1);
    CHECK(guards_hold(target, background));

    // A composite draws its children keyed; a child marked for blending
    // needs a display alpha table, so without a display it draws nothing.
    Sprite keyed_child = raw.sprite;
    Sprite blended_child = raw.sprite;
    blended_child.child_draw_mode = child_draw_blended;
    blended_child.origin_x = 0;
    Sprite* children[] = {&keyed_child, &blended_child};
    Sprite composite{};
    composite.child_count = 2;
    composite.data = children;
    target = make_guarded_surface(6, 5, 8, guard_bytes, background);
    draw_sprite_opaque(&target.surface, &composite, 1, 1);
    CHECK(
        pixel(target, 0, 0) == 1 && pixel(target, 1, 0) == background &&
        pixel(target, 3, 0) == background
    );

    Random random{seed_sprites};
    target = make_guarded_surface(48, 36, 52, guard_bytes, background);
    target.surface.clip = Rect32{3, 2, 44, 33};
    for (int32_t i = 0; i < sweep_copies; ++i) {
        // Drawn one per statement: a call's arguments are evaluated in an unspecified order.
        const auto width = static_cast<uint16_t>(next_in(random, 1, 20));
        const auto height = static_cast<uint16_t>(next_in(random, 1, 20));
        auto sprite = create_sprite(width, height);
        for (uint8_t& value : sprite.pixels) {
            value = next(random) % 3 == 0 ? key : next_byte(random);
        }
        sprite.sprite.key = key;
        sprite.sprite.origin_x = static_cast<int16_t>(next_in(random, -4, 12));
        sprite.sprite.origin_y = static_cast<int16_t>(next_in(random, -4, 12));
        const int32_t x = next_in(random, -20, 60);
        const int32_t y = next_in(random, -20, 50);
        if (next(random) % 2 == 0) {
            draw_sprite_opaque(&target.surface, &sprite.sprite, x, y);
            continue;
        }
        std::vector<uint8_t> encoded(
            static_cast<std::size_t>(encode_rle_sprite(encoder, nullptr, sprite.sprite))
        );
        encode_rle_sprite(encoder, encoded.data(), sprite.sprite);
        Sprite encoded_sprite = sprite.sprite;
        encoded_sprite.encoding = OA_SPRITE_ROW_RLE;
        encoded_sprite.data = encoded.data();
        draw_sprite_opaque(&target.surface, &encoded_sprite, x, y);
    }
    CHECK(guards_hold(target, background));
    CHECK_EQ(digest_of(target.storage), 0xCC6C7B4Eu);
}

} // namespace

int main() {
    test_trim_to_clip();
    test_copy_surface();
    test_copy_rect();
    test_draw_sprite_opaque();
    return finish();
}
