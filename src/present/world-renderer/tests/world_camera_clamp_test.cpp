// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Characterisation of the camera clamps, the radar view rectangle and
// centring on a world position: stated positions for a synthetic map whose
// radar picture the radar builder sizes, for a map smaller than the view and
// for an unset map, and a seeded sweep whose digest pins what the camera
// does today.

#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/core/map_plot.h"
#include "oa/present/world_renderer/world_radar.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <memory>

namespace {

namespace wr = oa::present::world_renderer;

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

// A synthetic 80x112-cell map (1280x1792 map pixels) under a 480x352 view.
constexpr int32_t map_width = 1280;
constexpr int32_t map_height = 1792;
constexpr int32_t view_width = 480;
constexpr int32_t view_height = 352;
// What the radar builder makes of that map: the taller axis fills the
// 126-pixel square and the other is scaled and centred.
constexpr int16_t radar_width = 90; // 1280 * 126 / 1792
constexpr int16_t radar_height = 126;
constexpr int16_t radar_x = 18; // (126 - 90) / 2
constexpr int16_t radar_y = 0;
constexpr int32_t fixed_shift = 16; // world positions are 16.16
// Every visibility bit set, so the test sees which one centring clears.
constexpr uint8_t all_visibility_flags = 0xFF;

// 32-bit FNV-1a parameters.
constexpr uint32_t fnv_offset_basis = 0x811C9DC5u;
constexpr uint32_t fnv_prime = 0x01000193u;
// Marsaglia's xorshift32 shift triple.
constexpr uint32_t xorshift_left_first = 13;
constexpr uint32_t xorshift_right = 17;
constexpr uint32_t xorshift_left_second = 5;
constexpr uint32_t seed_cameras = 0x13198A2Eu;
constexpr int32_t sweep_cameras = 400;
constexpr uint32_t sweep_digest = 0x96A96733u;

/// Declines every surface, so the radar builder only sizes the picture.
///
/// @param user unused
/// @param name unused
/// @param width unused
/// @param height unused
/// @return null
oa::Surface* decline_surface(void* user, const char* name, int32_t width, int32_t height) {
    (void)user;
    (void)name;
    (void)width;
    (void)height;
    return nullptr;
}

/// Sizes the radar picture for the Game block's map as the radar builder does.
///
/// @param[in,out] game block receiving the radar size and offsets
void size_radar(oa::Game& game) {
    wr::RadarSurfaces surfaces{};
    wr::RadarSurfaceHost host{};
    host.create_surface = decline_surface;
    wr::radar_build_picture(game, surfaces, wr::RadarPictureSource{}, host);
}

/// Sets a Game block's map and view sizes, with the view cells they cover.
///
/// @param[in,out] game block to set up
/// @param width map width in map pixels
/// @param height map height in map pixels
/// @param viewport_width view width in map pixels
/// @param viewport_height view height in map pixels
void set_map_and_view(
    oa::Game& game, int32_t width, int32_t height, int32_t viewport_width, int32_t viewport_height
) {
    game.map_pixel_width = width;
    game.map_pixel_height = height;
    game.viewport_width = viewport_width;
    game.viewport_height = viewport_height;
    game.view_cells_width = viewport_width / OA_MAP_CELL_PIXELS;
    game.view_cells_height = viewport_height / OA_MAP_CELL_PIXELS;
}

/// Allocates a zeroed Game block set up with the synthetic map, view and its radar.
///
/// @return the block
std::unique_ptr<oa::Game> make_game() {
    auto game = std::make_unique<oa::Game>();
    std::memset(game.get(), 0, sizeof(oa::Game));
    set_map_and_view(*game, map_width, map_height, view_width, view_height);
    size_radar(*game);
    return game;
}

/// Places the camera without clamping.
///
/// @param[in,out] game block holding the camera
/// @param x camera map-pixel X
/// @param y camera map-pixel Y
void place_camera(oa::Game& game, int32_t x, int32_t y) {
    game.camera_x = static_cast<uint32_t>(x);
    game.camera_y = static_cast<uint32_t>(y);
}

/// Reports whether the camera is at a position.
///
/// @param game block holding the camera
/// @param x expected map-pixel X
/// @param y expected map-pixel Y
/// @return true when both coordinates match
bool camera_at(const oa::Game& game, int32_t x, int32_t y) {
    return static_cast<int32_t>(game.camera_x) == x && static_cast<int32_t>(game.camera_y) == y;
}

/// Reports whether the glide target is at a position.
///
/// @param game block holding the target
/// @param x expected map-pixel X
/// @param y expected map-pixel Y
/// @return true when both coordinates match
bool target_at(const oa::Game& game, int32_t x, int32_t y) {
    return game.camera_target_x == x && game.camera_target_y == y;
}

/// Reports whether a rectangle has the given edges.
///
/// @param rect rectangle to test
/// @param x1 left edge
/// @param y1 top edge
/// @param x2 right edge
/// @param y2 bottom edge
/// @return true when every edge matches
bool rect_is(const oa::Rect32& rect, int32_t x1, int32_t y1, int32_t x2, int32_t y2) {
    return rect.x1 == x1 && rect.y1 == y1 && rect.x2 == x2 && rect.y2 == y2;
}

/// Returns the radar view rectangle the Game block holds.
///
/// @param game block holding the rectangle
/// @return a copy of Game.radar_view_rect
oa::Rect32 stored_radar_view(const oa::Game& game) {
    return game.radar_view_rect;
}

/// Checks the radar sizing and the radar view rectangle for the synthetic map and an unset map.
void test_radar_view_rect() {
    auto game = make_game();
    CHECK(game->radar_width == radar_width && game->radar_height == radar_height);
    CHECK(game->radar_offset_x == radar_x && game->radar_offset_y == radar_y);

    place_camera(*game, 640, 896);
    oa::Rect32 rect{};
    CHECK(wr::radar_view_rect(*game, rect));
    // x1 = 90 * 640 / 1280 + 18, x2 = 90 * 30 * 16 / 1280 - 1 + x1;
    // y1 = 126 * 896 / 1792, y2 = 126 * 22 * 16 / 1792 - 1 + y1.
    CHECK(rect_is(rect, 63, 63, 95, 86));

    place_camera(*game, 0, map_height - view_height);
    CHECK(wr::radar_view_rect(*game, rect));
    CHECK(rect_is(rect, 18, 101, 50, 124));

    // Without a map size the rectangle is left alone.
    game->map_pixel_height = 0;
    rect = oa::Rect32{-1, -2, -3, -4};
    CHECK(!wr::radar_view_rect(*game, rect));
    CHECK(rect_is(rect, -1, -2, -3, -4));
}

/// Checks the camera and glide-target clamps.
void test_clamps() {
    auto game = make_game();
    place_camera(*game, -5, 9999);
    game->camera_target_x = 5000;
    game->camera_target_y = -1;
    wr::camera_clamp_position(*game);
    // [0, map - view] on each axis, and the radar rectangle follows.
    CHECK(camera_at(*game, 0, map_height - view_height));
    CHECK(rect_is(stored_radar_view(*game), 18, 101, 50, 124));
    CHECK(target_at(*game, 5000, -1));
    wr::camera_clamp_target(*game);
    CHECK(target_at(*game, map_width - view_width, 0));
    CHECK(camera_at(*game, 0, map_height - view_height));

    // A map smaller than the view: a positive position clamps to the
    // negative limit, a negative one to 0.
    game = make_game();
    game->map_pixel_width = 256;
    game->map_pixel_height = 128;
    place_camera(*game, 100, -3);
    wr::camera_clamp_position(*game);
    CHECK(camera_at(*game, 256 - view_width, 0));
    game->camera_target_x = -9;
    game->camera_target_y = 1;
    wr::camera_clamp_target(*game);
    CHECK(target_at(*game, 0, 128 - view_height));

    // An unset map width clamps X to minus the view and keeps the radar
    // rectangle.
    game = make_game();
    game->map_pixel_width = 0;
    game->radar_view_rect = oa::Rect32{7, 7, 7, 7};
    place_camera(*game, 10, 10);
    wr::camera_clamp_position(*game);
    CHECK(camera_at(*game, -view_width, 10));
    CHECK(rect_is(stored_radar_view(*game), 7, 7, 7, 7));
}

/// Checks centring on a 16.16 world position, immediate and gliding.
void test_center_on_position() {
    auto game = make_game();
    game->visibility_flags = all_visibility_flags;
    // Screen X is the high word of X; screen Y that of Z - Y / 2 (700 - 32).
    const oa::FixedVec3 position{1000 << fixed_shift, 64 << fixed_shift, 700 << fixed_shift};
    wr::camera_center_on_position(*game, position, 0);
    CHECK(camera_at(*game, 1000 - view_width / 2, 668 - view_height / 2));
    CHECK(target_at(*game, 760, 492));
    CHECK((game->radar_blink_flags & wr::radar_flag_redraw) != 0);
    CHECK((game->visibility_flags & wr::visibility_flag_fog_mask_current) == 0);

    // Gliding sets only the (clamped) target.
    game = make_game();
    wr::camera_center_on_position(
        *game, oa::FixedVec3{4000 << fixed_shift, 0, 100 << fixed_shift}, 1
    );
    CHECK(camera_at(*game, 0, 0));
    CHECK(target_at(*game, map_width - view_width, 0));
    CHECK(game->radar_blink_flags == 0);

    // Half a pixel left of the origin has high word -1.
    game = make_game();
    place_camera(*game, 300, 300);
    wr::camera_center_on_position(*game, oa::FixedVec3{-(1 << (fixed_shift - 1)), 0, 0}, 0);
    CHECK(camera_at(*game, 0, 0));
}

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

/// Extends an FNV-1a digest over a 32-bit value, low byte first.
///
/// @param digest running digest
/// @param value value to add
/// @return the extended digest
uint32_t digest_value(uint32_t digest, int32_t value) {
    const auto bits = static_cast<uint32_t>(value);
    for (int32_t shift = 0; shift < 32; shift += 8) {
        digest = (digest ^ ((bits >> shift) & 0xFFu)) * fnv_prime;
    }
    return digest;
}

/// Centres and clamps seeded cameras over seeded maps and views and pins their digest.
///
/// Each seeded map gets the radar picture the radar builder sizes for it.
void test_sweep() {
    uint32_t state = seed_cameras;
    uint32_t digest = fnv_offset_basis;
    for (int32_t i = 0; i < sweep_cameras; ++i) {
        auto game = make_game();
        const auto width = static_cast<int32_t>(next(state) % 4096);
        const auto height = static_cast<int32_t>(next(state) % 4096);
        const auto viewport_width = static_cast<int32_t>(next(state) % 1024);
        const auto viewport_height = static_cast<int32_t>(next(state) % 800);
        set_map_and_view(*game, width, height, viewport_width, viewport_height);
        size_radar(*game);
        const oa::FixedVec3 position{
            static_cast<int32_t>(next(state) % 0x10000000u) - 0x08000000,
            static_cast<int32_t>(next(state) % 0x02000000u),
            static_cast<int32_t>(next(state) % 0x10000000u) - 0x08000000
        };
        wr::camera_center_on_position(*game, position, static_cast<int32_t>(next(state) % 2));
        const oa::Rect32 rect = stored_radar_view(*game);
        const int32_t target_x = game->camera_target_x;
        const int32_t target_y = game->camera_target_y;
        for (const int32_t value :
             {static_cast<int32_t>(game->camera_x),
              static_cast<int32_t>(game->camera_y),
              target_x,
              target_y,
              rect.x1,
              rect.y1,
              rect.x2,
              rect.y2}) {
            digest = digest_value(digest, value);
        }
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
    test_radar_view_rect();
    test_clamps();
    test_center_on_position();
    test_sweep();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
