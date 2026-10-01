// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A surface's band (OA_SURFACE_FLAG_BANDED): a seeded run of filled
// polygons (convex, concave and twisted, some past the clip and the
// surface), clipped lines, raw, row-RLE and blended sprites, drawn on a
// whole surface and again band by band, each band drawing the whole run with
// its rows alone, leaves the same bytes, guards included, for every number
// of bands.

#include "synthetic_input.hpp"

#include "oa/present/blit.hpp"
#include "oa/present/display.hpp"
#include "oa/present/polygon.hpp"
#include "oa/present/raster.hpp"
#include "oa/present/rle.hpp"
#include "oa/present/surface.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

namespace {

using namespace oa::present;
using namespace oa::present::test;
using oa::Rect32;
using oa::Sprite;
using oa::Surface;

constexpr int32_t surface_width = 96;
constexpr int32_t surface_height = 80;
constexpr int32_t surface_pitch = 104;
constexpr std::size_t guard_bytes = 64;
constexpr uint8_t guard_fill = 0xA5;
constexpr int32_t rounds = 40;
constexpr int32_t draws_per_round = 60;
constexpr uint32_t seed = 0x3C6EF372u;
// Band counts the run is split into, the last more than some rounds have rows
// for their sprites.
constexpr std::array<int32_t, 5> band_counts{2, 3, 4, 7, 13};
// Draws reach this far past each edge of the surface.
constexpr int32_t reach = 24;
constexpr uint8_t sprite_key = 0;
constexpr int32_t alpha_table_bytes = 256 * 256;

enum class DrawKind : uint8_t { polygon, line, sprite, rle_sprite, blended_sprite, kinds };

/// One draw of the run, worked out once and drawn whole and by bands alike.
struct Draw {
    alignas(4) Rect32 clip{}; ///< packed, so aligned here
    DrawKind kind{};
    std::vector<PolygonVertex> vertices;
    int32_t x0{};
    int32_t y0{};
    int32_t x1{};
    int32_t y1{};
    uint8_t color{};
    std::size_t sprite{}; ///< index into the round's sprites
};

/// A sprite with its pixels, raw or row-RLE encoded.
struct OwnedSprite {
    Sprite sprite{};
    std::vector<uint8_t> raw;
    std::vector<uint8_t> stream;
};

/// Makes a raw sprite of random pixels, about a third of them the key, and its row-RLE encoding.
///
/// @param[in,out] random generator
/// @return the sprite, raw; `stream` holds its encoding
OwnedSprite make_sprite(Random& random) {
    OwnedSprite owned;
    const auto width = static_cast<uint16_t>(next_in(random, 1, 40));
    const auto height = static_cast<uint16_t>(next_in(random, 1, 40));
    owned.raw.resize(static_cast<std::size_t>(width) * height);
    for (uint8_t& pixel : owned.raw)
        pixel = next_in(random, 0, 2) == 0 ? sprite_key : next_byte(random);
    owned.sprite.width = width;
    owned.sprite.height = height;
    owned.sprite.origin_x = static_cast<int16_t>(next_in(random, -8, width + 8));
    owned.sprite.origin_y = static_cast<int16_t>(next_in(random, -8, height + 8));
    owned.sprite.key = sprite_key;
    owned.sprite.data = owned.raw.data();
    RleEncoder encoder;
    const int32_t size = encode_rle_sprite(encoder, nullptr, owned.sprite);
    owned.stream.resize(static_cast<std::size_t>(size));
    encode_rle_sprite(encoder, owned.stream.data(), owned.sprite);
    return owned;
}

/// Returns a random clip rectangle, now and then the whole surface.
///
/// @param[in,out] random generator
/// @return an inclusive clip inside the surface
Rect32 random_clip(Random& random) {
    if (next_in(random, 0, 3) == 0)
        return {0, 0, surface_width - 1, surface_height - 1};
    const int32_t x1 = next_in(random, 0, surface_width - 1);
    const int32_t y1 = next_in(random, 0, surface_height - 1);
    return {
        x1, y1, next_in(random, x1, surface_width - 1), next_in(random, y1, surface_height - 1)
    };
}

/// Works out a round's draws.
///
/// @param[in,out] random generator
/// @param sprites the round's sprites
/// @return the draws
std::vector<Draw> make_draws(Random& random, std::span<const OwnedSprite> sprites) {
    std::vector<Draw> draws(draws_per_round);
    for (Draw& draw : draws) {
        draw.kind =
            static_cast<DrawKind>(next_in(random, 0, static_cast<int32_t>(DrawKind::kinds) - 1));
        draw.clip = random_clip(random);
        draw.color = next_byte(random);
        draw.x0 = next_in(random, -reach, surface_width + reach);
        draw.y0 = next_in(random, -reach, surface_height + reach);
        draw.x1 = next_in(random, -reach, surface_width + reach);
        draw.y1 = next_in(random, -reach, surface_height + reach);
        draw.sprite =
            static_cast<std::size_t>(next_in(random, 0, static_cast<int32_t>(sprites.size()) - 1));
        if (draw.kind == DrawKind::polygon) {
            // Any order of corners: convex, concave and twisted alike.
            const int32_t corners = next_in(random, 3, 6);
            for (int32_t corner = 0; corner < corners; ++corner)
                draw.vertices.push_back(
                    {next_in(random, -reach, surface_width + reach),
                     next_in(random, -reach, surface_height + reach)}
                );
        }
    }
    return draws;
}

/// Draws a round's draws in order on a surface, within its band when it has one.
///
/// @param[in,out] target surface to draw on
/// @param draws the draws
/// @param sprites the round's sprites
void draw_all(Surface& target, std::span<const Draw> draws, std::span<const OwnedSprite> sprites) {
    for (const Draw& draw : draws) {
        set_surface_clip(target, draw.clip);
        const OwnedSprite& owned = sprites[draw.sprite];
        Sprite sprite = owned.sprite;
        switch (draw.kind) {
        case DrawKind::polygon:
            (void)fill_polygon(
                &target,
                draw.vertices.data(),
                static_cast<int32_t>(draw.vertices.size()),
                draw.color
            );
            break;
        case DrawKind::line:
            (void)draw_clipped_line(&target, draw.x0, draw.y0, draw.x1, draw.y1, draw.color);
            break;
        case DrawKind::sprite:
            draw_sprite(&target, &sprite, draw.x0, draw.y0);
            break;
        case DrawKind::rle_sprite:
            sprite.encoding = OA_SPRITE_ROW_RLE;
            sprite.data = const_cast<uint8_t*>(owned.stream.data());
            draw_sprite(&target, &sprite, draw.x0, draw.y0);
            break;
        case DrawKind::blended_sprite:
            draw_sprite_blended(&target, &sprite, draw.x0, draw.y0);
            break;
        case DrawKind::kinds:
            break;
        }
    }
}

/// Checks that every round draws the same bytes whole and band by band.
void test_bands_draw_the_whole() {
    Random random{seed};
    std::vector<uint8_t> alpha(alpha_table_bytes);
    fill_random(random, alpha);
    DisplayContext display{};
    display.flags = display_flag_alpha_table;
    display.alpha_table = alpha.data();
    bind_display(&display);
    for (int32_t round = 0; round < rounds; ++round) {
        std::vector<OwnedSprite> sprites;
        for (int32_t index = 0; index < 4; ++index)
            sprites.push_back(make_sprite(random));
        const std::vector<Draw> draws = make_draws(random, sprites);
        auto start = make_guarded_surface(
            surface_width, surface_height, surface_pitch, guard_bytes, guard_fill
        );
        fill_random(
            random,
            std::span<uint8_t>(
                start.surface.pixels,
                static_cast<std::size_t>(surface_pitch) * static_cast<std::size_t>(surface_height)
            )
        );
        GuardedSurface whole = start;
        whole.surface.pixels = whole.storage.data() + whole.guard;
        draw_all(whole.surface, draws, sprites);
        for (const int32_t count : band_counts) {
            GuardedSurface banded = start;
            banded.surface.pixels = banded.storage.data() + banded.guard;
            // Uneven bands: the first row of band k is k rows past an even split.
            for (int32_t band = 0; band < count; ++band) {
                const int32_t first = band == 0 ? 0 : band * surface_height / count + band % 3;
                const int32_t end = band + 1 == count
                                        ? surface_height
                                        : (band + 1) * surface_height / count + (band + 1) % 3;
                set_surface_band(banded.surface, first, end);
                draw_all(banded.surface, draws, sprites);
            }
            CHECK(banded.storage == whole.storage);
            if (banded.storage != whole.storage) {
                std::fprintf(stderr, "round %d, %d bands, differ\n", round, count);
                return;
            }
        }
        CHECK(guards_hold(whole, guard_fill));
    }
    bind_display(nullptr);
}

/// Checks the band accessors and that initialising a surface clears its band.
void test_band_accessors() {
    auto target = make_guarded_surface(8, 8, 8, 0, 0);
    CHECK(surface_band(target.surface).first < 0);
    CHECK(surface_band(target.surface).end > 8);
    set_surface_band(target.surface, 2, 5);
    CHECK_EQ(surface_band(target.surface).first, 2);
    CHECK_EQ(surface_band(target.surface).end, 5);
    // A sprite's rows outside the band go, with the matching source rows.
    Rect32 src{0, 0, 3, 7};
    Rect32 dst{1, 0, 4, 7};
    trim_to_surface(src, dst, target.surface);
    CHECK_EQ(dst.y1, 2);
    CHECK_EQ(dst.y2, 4);
    CHECK_EQ(src.y1, 2);
    CHECK_EQ(src.y2, 4);
    clear_surface_band(target.surface);
    CHECK(surface_band(target.surface).first < 0);
    set_surface_band(target.surface, 2, 5);
    init_surface(target.surface, 8, 8, 8, target.storage.data());
    CHECK((target.surface.flags & OA_SURFACE_FLAG_BANDED) == 0);
}

} // namespace

int main() {
    test_band_accessors();
    test_bands_draw_the_whole();
    return finish();
}
