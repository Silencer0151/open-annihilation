// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The scene grid (scene_grid.hpp): a scale's 16.16 step, rounded to the
// nearest and at least 1, with 1 for a scale of 0 or less or no number;
// at a scale of 1 each map pixel is the scene pixel of its place; zoomed
// out, zoomed in and at a whole zoom each scene pixel, before the scene's
// first too, shows the map pixel whose pixels hold it, and the values of
// one of each by hand.
#include "oa/present/scene_grid.hpp"

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

} // namespace

int main() {
    test_steps();
    test_whole_scale();
    test_pixels_lie_in_their_map_pixel();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("scene grid tests passed");
    return EXIT_SUCCESS;
}
