// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How many screen pixels a run of battlefield pixels spans at each zoom: a
// pixel particle's two-pixel square stays two pixels at the game's own scale,
// grows with the zoom and never vanishes when zoomed out.
#include "oa/present/world_renderer.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
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

constexpr int32_t particle_side = 2;

int32_t span_at(float scale, int32_t map_pixels) {
    oa::present::world_renderer::BattlefieldViewport viewport{};
    viewport.scale = scale;
    return oa::present::world_renderer::screen_span(viewport, map_pixels);
}

} // namespace

int main() {
    // The game's own scale draws one screen pixel per battlefield pixel.
    CHECK(span_at(1.0F, particle_side) == 2);
    CHECK(span_at(0.0F, particle_side) == 2);
    CHECK(span_at(1.0F, 1) == 1);
    CHECK(span_at(1.0F, 16) == 16);
    // Zoomed in, the run grows with the scale, rounded to the nearest pixel.
    CHECK(span_at(2.0F, particle_side) == 4);
    CHECK(span_at(3.0F, particle_side) == 6);
    CHECK(span_at(1.5F, particle_side) == 3);
    CHECK(span_at(1.2F, particle_side) == 2);
    CHECK(span_at(1.3F, particle_side) == 3);
    // Zoomed out, and for an empty run, at least one pixel remains.
    CHECK(span_at(0.5F, particle_side) == 1);
    CHECK(span_at(0.25F, particle_side) == 1);
    CHECK(span_at(1.0F, 0) == 1);
    CHECK(span_at(-1.0F, particle_side) == 1);
    // A run too long for the result is held at its largest value.
    CHECK(
        span_at(4.0F, std::numeric_limits<int32_t>::max()) == std::numeric_limits<int32_t>::max()
    );
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
