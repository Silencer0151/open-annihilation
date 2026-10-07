// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The scene grid (scene_grid.hpp): a scale's 16.16 step, rounded to the
// nearest and at least 1, with 1 for a scale of 0 or less or no number;
// at a scale of 1 each map pixel is the scene pixel of its place; zoomed
// out, zoomed in and at a whole zoom each scene pixel, before the scene's
// first too, shows the map pixel whose pixels hold it, and the values of
// one of each by hand; with a phase, the same, the scene's first pixel that
// far into its map pixel; a scene started on the scene pixels laid from the
// map's corner (scene_origin) shows each map pixel there, wherever it
// starts; and zoomed in, a point moving by less than a map pixel moves the
// scene pixel it lies in (scene_pixel_of), and with it a view centred on
// it, finer than a map pixel.
#include "oa/present/scene_grid.hpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <initializer_list>
#include <limits>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace oa::present;

/// The scene pixels each scale is walked over, from before the scene's first.
constexpr int64_t first_walked = -300;
constexpr int64_t last_walked = 3000;

/// Checks a scale's step, and the step of a scale the grid cannot take.
void test_steps() {
    CHECK(scene_step(1.0F) == scene_step_one);
    CHECK(scene_step(0.5F) == scene_step_one / 2);
    CHECK(scene_step(0.37F) == 24248);
    CHECK(scene_step(0.0F) == scene_step_one);
    CHECK(scene_step(-2.0F) == scene_step_one);
    CHECK(scene_step(std::numeric_limits<float>::quiet_NaN()) == scene_step_one);
    CHECK(scene_step(1.0e-9F) == 1);
}

/// Checks that at a scale of 1 each map pixel is the scene pixel of its place.
void test_whole_scale() {
    for (int64_t pixel = first_walked; pixel <= last_walked; ++pixel) {
        CHECK(map_pixel_shown(pixel, scene_step_one) == pixel);
        CHECK(first_scene_pixel(pixel, scene_step_one) == pixel);
    }
}

/// Checks that every scene pixel lies among the pixels of the map pixel it shows.
void test_pixels_lie_in_their_map_pixel() {
    for (const float scale : {0.37F, 1.37F, 2.0F}) {
        const uint32_t step = scene_step(scale);
        for (int64_t pixel = first_walked; pixel <= last_walked; ++pixel) {
            const int64_t shown = map_pixel_shown(pixel, step);
            CHECK(first_scene_pixel(shown, step) <= pixel);
            CHECK(pixel < first_scene_pixel(shown + 1, step));
        }
    }
    // By hand: at 2.5 map pixel 1 covers scene pixels 3 and 4, and map
    // pixel -1 the pixels -2 and -1; at 0.5 scene pixel 3 shows map pixel 6,
    // and map pixel 5 is left out, its neighbour 6 first at scene pixel 3.
    const uint32_t magnified = scene_step(2.5F);
    CHECK(first_scene_pixel(1, magnified) == 3);
    CHECK(map_pixel_shown(4, magnified) == 1);
    CHECK(map_pixel_shown(5, magnified) == 2);
    CHECK(first_scene_pixel(-1, magnified) == -2);
    CHECK(map_pixel_shown(-1, magnified) == -1);
    CHECK(map_pixel_shown(-3, magnified) == -2);
    const uint32_t halved = scene_step(0.5F);
    CHECK(map_pixel_shown(3, halved) == 6);
    CHECK(first_scene_pixel(5, halved) == 3);
    CHECK(first_scene_pixel(6, halved) == 3);
    CHECK(map_pixel_shown(-1, halved) == -2);
}

/// Checks that a scene started on the scene pixels laid from the map's
/// corner shows every map pixel on those pixels, less where it starts,
/// from places a fraction of a map pixel apart, zoomed out.
void test_origin_lays_the_map_from_its_corner() {
    const uint32_t step = scene_step(0.37F);
    for (double place = 40.0; place < 52.0; place += 0.29) {
        const SceneOrigin origin = scene_origin(place, step);
        CHECK(origin.phase < step);
        const auto start =
            static_cast<int64_t>(std::llround(place * step / static_cast<double>(scene_step_one)));
        for (int64_t pixel = first_walked; pixel <= last_walked; pixel += 7) {
            CHECK(
                origin.map_pixel + map_pixel_shown(pixel, step, origin.phase) ==
                map_pixel_shown(start + pixel, step)
            );
            CHECK(
                first_scene_pixel(pixel - origin.map_pixel, step, origin.phase) ==
                first_scene_pixel(pixel, step) - start
            );
        }
    }
    // At a scale of 1 the scene starts on the nearest map pixel, with no
    // phase, halves away from zero.
    CHECK(scene_origin(12.5, scene_step_one).map_pixel == 13);
    CHECK(scene_origin(-12.5, scene_step_one).map_pixel == -13);
    CHECK(scene_origin(12.4, scene_step_one).phase == 0);
}

/// Checks that zoomed in a point a fraction of a map pixel along moves the
/// scene pixel it lies in, and a view centred on it, finer than a map
/// pixel, and that at a scale of 1 that pixel is the point's map pixel.
void test_point_moves_by_scene_pixels() {
    const uint32_t doubled = scene_step(2.0F);
    // 100, 100.25, 100.5, 100.75 and 101 map pixels, in 16.16.
    constexpr int32_t at = 100 << 16;
    constexpr int32_t quarter = 1 << 14;
    CHECK(scene_pixel_of(at, doubled) == 200);
    CHECK(scene_pixel_of(at + quarter, doubled) == 200);
    CHECK(scene_pixel_of(at + 2 * quarter, doubled) == 201);
    CHECK(scene_pixel_of(at + 3 * quarter, doubled) == 201);
    CHECK(scene_pixel_of(at + 4 * quarter, doubled) == 202);
    CHECK(scene_pixel_of(-quarter, doubled) == -1);
    // The view centred there starts half a map pixel along: the camera's
    // map pixel and a phase of half the step.
    const double place = 201.0 * scene_step_one / doubled;
    const SceneOrigin origin = scene_origin(place, doubled);
    CHECK(origin.map_pixel == 100 && origin.phase == doubled / 2);
    CHECK(scene_pixel_of(at + 3 * quarter, scene_step_one) == 100);
    CHECK(scene_pixel_of(-quarter, scene_step_one) == -1);
}

} // namespace

int main() {
    test_steps();
    test_whole_scale();
    test_pixels_lie_in_their_map_pixel();
    test_origin_lays_the_map_from_its_corner();
    test_point_moves_by_scene_pixels();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("scene grid tests passed");
    return EXIT_SUCCESS;
}
