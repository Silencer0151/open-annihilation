// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How a frame draws the battlefield (world_scaling.hpp), by table: without a
// draw scale, and at the zoom, the scene is the battlefield; at another
// scale it covers the battlefield's map pixels with the margin, rounded up
// to even sizes; and over the zoom range and the battlefields of every
// window, the scene holds every pixel the nearest resample reads from it.
// The accelerated tier's draw scale within each scene budget, its method at
// each zoom, the area pass's scale and the scene it reads, the largest
// magnified scene, and the scale the card draws the battlefield at on a
// window at native density. The view that tier draws between map pixels:
// the area pass's start for it, the magnified corner, which on the camera's
// map pixel is the one drawn before and between map pixels the same picture
// moved; how far the map lets the view lie past the camera; and the view a
// scroll moves, which toward the map's end follows the scroll's exact place
// and toward its start never jumps or turns back, while the camera steps
// whole map pixels, which on an axis joining a scroll under way catches up
// with the carry the camera steps on by that axis's first step, and which
// held against the map's edge stays still.
#include "oa/app/world_scaling.hpp"

#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <optional>

namespace {

using oa::app::SceneMethod;
using oa::app::world_scaling;
using oa::app::WorldScaling;
using oa::app::render_policy::SceneBudget;

// The battlefields the scene must cover: a 640x480 window's, and those of
// larger windows up to 3840x2160.
struct Battlefield {
    int32_t width{};  ///< battlefield columns in screen pixels
    int32_t height{}; ///< battlefield rows in screen pixels
};

constexpr std::array<Battlefield, 5> battlefields{
    {{640, 520}, {1161, 666}, {1664, 952}, {2304, 1312}, {3584, 2032}}
};

// The game's zoom range, walked in this many uneven steps, and the draw
// scales drawn apart at each zoom.
constexpr float lowest_zoom = 0.5F;
constexpr float highest_zoom = 4.0F;
constexpr int zoom_steps = 997;
constexpr std::array<float, 5> draw_scales{0.5F, 0.75F, 1.0F, 1.5F, 2.0F};

[[nodiscard]] bool same(const WorldScaling& scaling, const WorldScaling& expected) {
    return scaling.draw_scale == expected.draw_scale &&
           scaling.scene_width == expected.scene_width &&
           scaling.scene_height == expected.scene_height && scaling.apart == expected.apart &&
           scaling.method == expected.method;
}

// The methods of a frame drawn at the zoom and of a scene resampled nearest.
constexpr SceneMethod at_zoom = SceneMethod::none;
constexpr SceneMethod nearest = SceneMethod::nearest;

void without_a_draw_scale_the_scene_is_the_battlefield_at_the_zoom() {
    OA_CHECK(same(world_scaling(0.5F, 1664, 952, std::nullopt), {0.5F, 1664, 952, false, at_zoom}));
    OA_CHECK(same(world_scaling(2.0F, 1664, 952, std::nullopt), {2.0F, 1664, 952, false, at_zoom}));
    // A draw scale that is not above 0 counts as none.
    OA_CHECK(same(world_scaling(2.0F, 640, 520, 0.0F), {2.0F, 640, 520, false, at_zoom}));
    OA_CHECK(same(world_scaling(2.0F, 640, 520, -1.0F), {2.0F, 640, 520, false, at_zoom}));
    OA_CHECK(same(
        world_scaling(2.0F, 640, 520, std::numeric_limits<float>::quiet_NaN()),
        {2.0F, 640, 520, false, at_zoom}
    ));
}

void at_the_zoom_the_scene_apart_is_the_battlefield() {
    OA_CHECK(same(world_scaling(1.0F, 1664, 952, 1.0F), {1.0F, 1664, 952, true, nearest}));
    OA_CHECK(same(world_scaling(0.5F, 1664, 952, 0.5F), {0.5F, 1664, 952, true, nearest}));
    OA_CHECK(same(world_scaling(2.0F, 1161, 666, 2.0F), {2.0F, 1161, 666, true, nearest}));
    // A zoom that is not above 0 gives the battlefield's size.
    OA_CHECK(same(world_scaling(0.0F, 640, 520, 1.0F), {1.0F, 640, 520, true, nearest}));
}

void the_scene_covers_the_battlefield_with_the_margin() {
    // 1664 / 0.5 + 2 and 952 / 0.5 + 2.
    OA_CHECK(same(world_scaling(0.5F, 1664, 952, 1.0F), {1.0F, 3330, 1906, true, nearest}));
    // 1664 / 2 + 2 and 952 / 2 + 2.
    OA_CHECK(same(world_scaling(2.0F, 1664, 952, 1.0F), {1.0F, 834, 478, true, nearest}));
    // ceil(640 * 0.75 / 0.6) + 2 and ceil(520 * 0.75 / 0.6) + 2.
    OA_CHECK(same(world_scaling(0.6F, 640, 520, 0.75F), {0.75F, 802, 652, true, nearest}));
}

void odd_scene_sizes_round_up_to_even() {
    // ceil(1161 / 2) + 2 = 583 and 666 / 2 + 2 = 335.
    OA_CHECK(same(world_scaling(2.0F, 1161, 666, 1.0F), {1.0F, 584, 336, true, nearest}));
    // ceil(640 / 1.37) + 2 = 470 and ceil(521 / 1.37) + 2 = 383.
    OA_CHECK(same(world_scaling(1.37F, 640, 521, 1.0F), {1.0F, 470, 384, true, nearest}));
}

void the_scene_holds_what_the_resample_reads() {
    namespace wr = oa::present::world_renderer;
    for (const auto& battlefield : battlefields)
        for (int step = 0; step <= zoom_steps; ++step) {
            const float zoom = lowest_zoom + (highest_zoom - lowest_zoom) *
                                                 static_cast<float>(step) /
                                                 static_cast<float>(zoom_steps);
            for (const float draw_scale : draw_scales) {
                const auto scaling =
                    world_scaling(zoom, battlefield.width, battlefield.height, draw_scale);
                // The scale the frame resamples the scene into the world layer at.
                const auto scale =
                    static_cast<float>(static_cast<double>(zoom) / static_cast<double>(draw_scale));
                const auto width = static_cast<uint32_t>(battlefield.width);
                const auto height = static_cast<uint32_t>(battlefield.height);
                OA_CHECK(scaling.apart && scaling.draw_scale == draw_scale);
                OA_CHECK(scaling.method == nearest);
                OA_CHECK(
                    wr::resample_scene_extent(scale, width) <=
                    static_cast<uint64_t>(scaling.scene_width)
                );
                OA_CHECK(
                    wr::resample_scene_extent(scale, height) <=
                    static_cast<uint64_t>(scaling.scene_height)
                );
                if (draw_scale == zoom)
                    continue;
                OA_CHECK(scaling.scene_width % 2 == 0 && scaling.scene_height % 2 == 0);
                const auto covered = [&](int32_t battlefield_extent) {
                    return static_cast<int32_t>(std::ceil(
                               static_cast<double>(battlefield_extent) *
                               static_cast<double>(draw_scale) / static_cast<double>(zoom)
                           )) +
                           WorldScaling::margin;
                };
                OA_CHECK(scaling.scene_width >= covered(battlefield.width));
                OA_CHECK(scaling.scene_height >= covered(battlefield.height));
                OA_CHECK(scaling.scene_width <= covered(battlefield.width) + 1);
                OA_CHECK(scaling.scene_height <= covered(battlefield.height) + 1);
            }
        }
}

// The zooms the accelerated tier's cases walk, below and above 1.
[[nodiscard]] float zoom_at(int step) {
    return lowest_zoom +
           (highest_zoom - lowest_zoom) * static_cast<float>(step) / static_cast<float>(zoom_steps);
}

void the_draw_scale_keeps_within_the_budget() {
    using oa::app::accelerated_draw_scale;
    // A 1920x1080 window's battlefield at zoom 0.5: 1 within the full budget
    // (min(1, 0.5 x 2, 0.5 x sqrt(2^23 / 1584128))), 0.75 within the reduced
    // one (0.5 x 1.5), and the zoom with none.
    OA_CHECK(accelerated_draw_scale(0.5F, 1664, 952, SceneBudget::full) == 1.0F);
    OA_CHECK(accelerated_draw_scale(0.5F, 1664, 952, SceneBudget::reduced) == 0.75F);
    OA_CHECK(accelerated_draw_scale(0.5F, 1664, 952, SceneBudget::none) == 0.5F);
    // 2560x1440 and 3840x2160 windows: the scene's most pixels cap it, to
    // 0.5 x sqrt(2^23 / (2304 x 1312)) and 0.5 x sqrt(2^23 / (3584 x 2032)).
    OA_CHECK(
        std::abs(accelerated_draw_scale(0.5F, 2304, 1312, SceneBudget::full) - 0.8329F) < 1e-3F
    );
    OA_CHECK(
        std::abs(accelerated_draw_scale(0.5F, 3584, 2032, SceneBudget::full) - 0.5366F) < 1e-3F
    );
    // At zoom 1 and above, the zoom.
    OA_CHECK(accelerated_draw_scale(1.0F, 1664, 952, SceneBudget::full) == 1.0F);
    OA_CHECK(accelerated_draw_scale(2.0F, 1664, 952, SceneBudget::full) == 2.0F);
    for (const auto& battlefield : battlefields)
        for (const auto budget : {SceneBudget::none, SceneBudget::reduced, SceneBudget::full})
            for (int step = 0; step <= zoom_steps; ++step) {
                const float zoom = zoom_at(step);
                if (zoom >= 1.0F)
                    continue;
                const float scale =
                    accelerated_draw_scale(zoom, battlefield.width, battlefield.height, budget);
                OA_CHECK(scale >= zoom && scale <= std::min(1.0F, 2.0F * zoom));
                if (budget == SceneBudget::none)
                    OA_CHECK(scale == zoom);
                // The scene's pixels at that scale stay within the budget's most.
                const double pixels = static_cast<double>(battlefield.width) * battlefield.height *
                                      scale * scale / (zoom * zoom);
                if (budget == SceneBudget::full && scale > zoom)
                    OA_CHECK(pixels <= 1.0001 * oa::app::full_budget_scene_pixels);
                if (budget == SceneBudget::reduced && scale > zoom)
                    OA_CHECK(pixels <= 1.0001 * oa::app::reduced_budget_scene_pixels);
            }
}

void the_accelerated_method_follows_the_zoom() {
    using oa::app::accelerated_world_scaling;
    // Zoom 1: drawn as the standard tier draws it.
    OA_CHECK(same(
        accelerated_world_scaling(1.0F, 1664, 952, SceneBudget::full, true),
        {1.0F, 1664, 952, false, at_zoom}
    ));
    // Above zoom 1 the scene is drawn at 1 and magnified, unless magnify is off.
    OA_CHECK(same(
        accelerated_world_scaling(2.0F, 1664, 952, SceneBudget::full, true),
        {1.0F, 834, 478, true, SceneMethod::magnify}
    ));
    OA_CHECK(same(
        accelerated_world_scaling(2.0F, 1664, 952, SceneBudget::none, false),
        {2.0F, 1664, 952, false, at_zoom}
    ));
    // Below zoom 1 the area pass reduces the scene at the budget's scale.
    OA_CHECK(same(
        accelerated_world_scaling(0.5F, 1664, 952, SceneBudget::full, true),
        {1.0F, 3330, 1906, true, SceneMethod::area}
    ));
    // ceil(1664 x 0.75 / 0.5) + 2 and ceil(952 x 0.75 / 0.5) + 2.
    OA_CHECK(same(
        accelerated_world_scaling(0.5F, 1664, 952, SceneBudget::reduced, false),
        {0.75F, 2498, 1430, true, SceneMethod::area}
    ));
    // Under budget none, and above the cut-off, the zoom.
    OA_CHECK(same(
        accelerated_world_scaling(0.5F, 1664, 952, SceneBudget::none, true),
        {0.5F, 1664, 952, false, at_zoom}
    ));
    OA_CHECK(same(
        accelerated_world_scaling(0.95F, 1664, 952, SceneBudget::full, true),
        {0.95F, 1664, 952, false, at_zoom}
    ));
    namespace wr = oa::present::world_renderer;
    for (const auto& battlefield : battlefields)
        for (const auto budget : {SceneBudget::none, SceneBudget::reduced, SceneBudget::full})
            for (const bool magnify : {false, true})
                for (int step = 0; step <= zoom_steps; ++step) {
                    const float zoom = zoom_at(step);
                    const auto scaling = accelerated_world_scaling(
                        zoom, battlefield.width, battlefield.height, budget, magnify
                    );
                    OA_CHECK(scaling.apart == (scaling.method != at_zoom));
                    if (scaling.method == at_zoom) {
                        OA_CHECK(scaling.draw_scale == zoom);
                        OA_CHECK(scaling.scene_width == battlefield.width);
                        OA_CHECK(scaling.scene_height == battlefield.height);
                        continue;
                    }
                    OA_CHECK(same(
                        scaling,
                        {scaling.draw_scale,
                         world_scaling(
                             zoom, battlefield.width, battlefield.height, scaling.draw_scale
                         )
                             .scene_width,
                         world_scaling(
                             zoom, battlefield.width, battlefield.height, scaling.draw_scale
                         )
                             .scene_height,
                         true,
                         scaling.method}
                    ));
                    if (zoom > 1.0F) {
                        OA_CHECK(magnify && scaling.method == SceneMethod::magnify);
                        OA_CHECK(scaling.draw_scale == 1.0F);
                        const auto largest =
                            oa::app::largest_magnified_scene(battlefield.width, battlefield.height);
                        OA_CHECK(scaling.scene_width <= largest.width);
                        OA_CHECK(scaling.scene_height <= largest.height);
                        continue;
                    }
                    // Below zoom 1: the area pass, under a budget, below the cut-off.
                    OA_CHECK(scaling.method == SceneMethod::area && budget != SceneBudget::none);
                    OA_CHECK(scaling.draw_scale > zoom);
                    OA_CHECK(zoom / scaling.draw_scale <= oa::app::area_cut_off);
                    // The scene holds every pixel the area pass reads.
                    const auto scale = oa::app::area_scale(zoom, scaling.draw_scale);
                    OA_CHECK(scale >= wr::area_scale_min && scale <= wr::area_scale_max);
                    OA_CHECK(
                        wr::area_scene_extent(scale, static_cast<uint32_t>(battlefield.width)) <=
                        static_cast<uint32_t>(scaling.scene_width)
                    );
                    OA_CHECK(
                        wr::area_scene_extent(scale, static_cast<uint32_t>(battlefield.height)) <=
                        static_cast<uint32_t>(scaling.scene_height)
                    );
                    // And for a view a whole map pixel past the camera's.
                    const uint32_t phase = oa::app::area_phase(1.0, scaling.draw_scale, scale);
                    OA_CHECK(
                        wr::area_scene_extent(
                            scale, static_cast<uint32_t>(battlefield.width), phase
                        ) <= static_cast<uint32_t>(scaling.scene_width)
                    );
                    OA_CHECK(
                        wr::area_scene_extent(
                            scale, static_cast<uint32_t>(battlefield.height), phase
                        ) <= static_cast<uint32_t>(scaling.scene_height)
                    );
                }
}

void the_largest_magnified_scene_is_the_battlefield_with_its_margin() {
    const auto largest = oa::app::largest_magnified_scene(1664, 952);
    OA_CHECK(largest.width == 1666 && largest.height == 954);
    const auto odd = oa::app::largest_magnified_scene(1161, 667);
    OA_CHECK(odd.width == 1164 && odd.height == 670);
    // A zoom just above 1 gives it.
    const auto scaling = oa::app::accelerated_world_scaling(
        std::nextafter(1.0F, 2.0F), 1161, 667, SceneBudget::full, true
    );
    OA_CHECK(scaling.scene_width == odd.width && scaling.scene_height == odd.height);
}

void the_area_scale_is_the_zoom_over_the_draw_scale() {
    namespace wr = oa::present::world_renderer;
    OA_CHECK(oa::app::area_scale(0.5F, 1.0F) == wr::area_fixed_one / 2);
    OA_CHECK(oa::app::area_scale(0.75F, 1.0F) == wr::area_fixed_one * 3 / 4);
    OA_CHECK(oa::app::area_scale(1.0F, 1.0F) == wr::area_fixed_one);
    // 0.6 x 65536 = 39321.6, rounded up.
    OA_CHECK(oa::app::area_scale(0.6F, 1.0F) == 39322);
}

// On a window at native density the layout is in window points, so the
// draw scale, the scene and its budget are those of the layout at every
// density; the card draws the world layer at the density and a magnified
// scene at the zoom times the density, and NEAREST is kept where that
// product is a whole number. Below zoom 1 the method does not take the
// density yet: where the area pass runs, it runs at the layout's size at
// every density, and the card enlarges its result by the density, so the
// scene is not magnified there by the zoom times the density over the
// draw scale.
void the_display_scale_is_the_zoom_times_the_density() {
    using oa::app::accelerated_world_scaling;
    using oa::app::world_display_scale;
    namespace policy = oa::app::render_policy;
    constexpr std::array<double, 4> densities{1.0, 1.5, 2.0, 3.0};
    constexpr std::array<float, 7> zooms{0.5F, 0.75F, 1.0F, 1.37F, 1.5F, 2.0F, 4.0F};
    for (const auto& battlefield : battlefields)
        for (const float zoom : zooms)
            for (const SceneBudget budget :
                 {SceneBudget::none, SceneBudget::reduced, SceneBudget::full})
                for (const bool magnify : {false, true}) {
                    const WorldScaling scaling = accelerated_world_scaling(
                        zoom, battlefield.width, battlefield.height, budget, magnify
                    );
                    for (const double density : densities) {
                        const double scale = world_display_scale(scaling, zoom, density);
                        if (scaling.method == SceneMethod::magnify)
                            OA_CHECK(scale == static_cast<double>(zoom) * density);
                        else
                            OA_CHECK(scale == density);
                    }
                    // At density 1 the card draws at the scales it drew at
                    // before: the zoom for a magnified scene, 1 for the
                    // world layer.
                    OA_CHECK(
                        world_display_scale(scaling, zoom, 1.0) ==
                        (scaling.method == SceneMethod::magnify ? static_cast<double>(zoom) : 1.0)
                    );
                }
    // The whole-number test is made on the display's scale.
    policy::LadderState rung;
    rung.magnify = true;
    rung.filtered_chrome = true;
    rung.card = policy::CardFilter::pixelart;
    const auto filter = [&](float zoom, double density) {
        const WorldScaling scaling =
            accelerated_world_scaling(zoom, 1161, 666, SceneBudget::full, true);
        return policy::chrome_filter(rung, world_display_scale(scaling, zoom, density));
    };
    OA_CHECK(filter(1.5F, 2.0) == policy::ScaleFilter::nearest);
    OA_CHECK(filter(1.5F, 1.0) == policy::ScaleFilter::pixelart);
    OA_CHECK(filter(1.37F, 2.0) == policy::ScaleFilter::pixelart);
    OA_CHECK(filter(2.0F, 1.5) == policy::ScaleFilter::nearest);
    OA_CHECK(filter(4.0F, 1.25) == policy::ScaleFilter::nearest);
    OA_CHECK(filter(2.0F, 1.25) == policy::ScaleFilter::pixelart);
    // Zoom 1 is not split: the world layer at the density, NEAREST at a
    // whole-number density and filtered at any other.
    OA_CHECK(filter(1.0F, 2.0) == policy::ScaleFilter::nearest);
    OA_CHECK(filter(1.0F, 1.5) == policy::ScaleFilter::pixelart);
}

void the_area_phase_is_the_offset_in_scene_pixels() {
    namespace wr = oa::present::world_renderer;
    constexpr uint32_t half = wr::area_fixed_one / 2;
    OA_CHECK(oa::app::area_phase(0.0, 1.0F, half) == 0);
    OA_CHECK(oa::app::area_phase(0.5, 1.0F, half) == half / 2);
    OA_CHECK(oa::app::area_phase(1.0, 1.0F, half) == half);
    // A quarter of a map pixel at a draw scale of 0.8 is a fifth of a scene
    // pixel: 0.2 x 39322 = 7864.4, rounded.
    OA_CHECK(oa::app::area_phase(0.25, 0.8F, 39322) == 7864);
    // Never more than one scene pixel, nor below none.
    OA_CHECK(oa::app::area_phase(1.0, 1.0F, 39322) == 39322);
    OA_CHECK(oa::app::area_phase(-0.25, 1.0F, half) == 0);
    OA_CHECK(oa::app::area_phase(0.5, 0.0F, half) == 0);
}

void a_view_on_the_camera_pixel_draws_the_corner_it_always_has() {
    for (const auto& battlefield : battlefields) {
        const auto width = static_cast<uint32_t>(battlefield.width);
        const auto largest =
            oa::app::largest_magnified_scene(battlefield.width, battlefield.height);
        for (int step = 1; step <= zoom_steps; ++step) {
            const float zoom = 1.0F + (highest_zoom - 1.0F) * static_cast<float>(step) /
                                          static_cast<float>(zoom_steps);
            const auto scene = static_cast<uint32_t>(largest.width);
            const auto span = oa::app::magnified_span(width, zoom, scene, 0.0);
            const auto corner = std::min(
                static_cast<uint32_t>(std::ceil(static_cast<double>(width) / zoom)), scene
            );
            OA_CHECK(span.corner == corner);
            OA_CHECK(span.start == 0.0);
            OA_CHECK(
                span.extent == static_cast<double>(std::lround(static_cast<double>(corner) * zoom))
            );
        }
    }
}

void a_view_between_map_pixels_is_the_same_picture_moved() {
    for (const auto& battlefield : battlefields) {
        const auto width = static_cast<uint32_t>(battlefield.width);
        for (int step = 1; step <= zoom_steps; ++step) {
            const float zoom = 1.0F + (highest_zoom - 1.0F) * static_cast<float>(step) /
                                          static_cast<float>(zoom_steps);
            // The scene the magnified frame draws at the zoom.
            const auto scene = static_cast<uint32_t>(
                oa::app::accelerated_world_scaling(
                    zoom, battlefield.width, battlefield.height, SceneBudget::full, true
                )
                    .scene_width
            );
            const auto whole = oa::app::magnified_span(width, zoom, scene, 0.0);
            for (const double offset : {0.001, 0.25, 0.5, 0.999, 1.0}) {
                const auto span = oa::app::magnified_span(width, zoom, scene, offset);
                // One more scene column, within the scene, at the same
                // screen pixels per scene pixel, landing the offset before
                // the edge and still covering the battlefield.
                const double scale = whole.extent / whole.corner;
                OA_CHECK(span.corner == whole.corner + 1U && span.corner < scene);
                OA_CHECK(std::fabs(span.extent / span.corner - scale) < 1.0e-9);
                OA_CHECK(std::fabs(span.start + offset * scale) < 1.0e-9);
                // That scale is the zoom, to half a screen pixel over the corner.
                OA_CHECK(std::fabs(scale - zoom) * whole.corner <= 0.5 + 1.0e-9);
                OA_CHECK(span.start + span.extent >= static_cast<double>(width) - 1.0e-9);
            }
        }
    }
}

void the_map_bounds_how_far_the_view_lies_past_the_camera() {
    // Far from the camera's farthest place, a whole map pixel.
    OA_CHECK(oa::app::most_view_offset(0, 524) == 1.0);
    OA_CHECK(oa::app::most_view_offset(523, 524) == 1.0);
    // At it, none; past it, none.
    OA_CHECK(oa::app::most_view_offset(524, 524) == 0.0);
    OA_CHECK(oa::app::most_view_offset(600, 524) == 0.0);
}

void a_view_found_from_its_exact_place_lies_on_or_past_its_camera() {
    OA_CHECK(std::fabs(oa::app::view_offset_at(10.3, 10, 1.0) - 0.3) < 1.0e-9);
    OA_CHECK(oa::app::view_offset_at(9.7, 10, 1.0) == 0.0);
    OA_CHECK(oa::app::view_offset_at(10.9, 10, 0.4) == 0.4);
    OA_CHECK(oa::app::view_offset_at(12.0, 10, 1.0) == 1.0);
}

/// A scroll as the game moves it, each frame: the camera steps the whole
/// map pixels its carry holds, held on the map, and the view follows within
/// the camera's map pixel.
struct Scroll {
    int32_t map{};      ///< the map's pixels along the axis
    double visible{};   ///< map pixels the battlefield shows along it
    int32_t camera{};   ///< the camera, on the map
    double offset{};    ///< map pixels the view lies past it
    double carry{};     ///< the scroll's fraction of a map pixel not yet stepped
    int32_t steps{};    ///< whole map pixels the camera stepped in all
    double travelled{}; ///< map pixels the scroll's exact travel adds up to

    /// Returns the camera's farthest place.
    ///
    /// @return the map's pixels less those the battlefield shows, rounded
    [[nodiscard]] int32_t farthest() const {
        return std::max(0, map - static_cast<int32_t>(std::lround(visible)));
    }

    /// Scrolls one frame.
    ///
    /// @param way 1 toward the map's end, -1 toward its start
    /// @param travel map pixels the frame scrolls
    void frame(int32_t way, double travel) {
        const int32_t before = camera;
        const double carried = carry;
        carry += travel;
        const auto move = static_cast<int32_t>(std::floor(carry));
        carry -= move;
        camera = std::clamp(camera + way * move, 0, farthest());
        steps += way * move;
        travelled += way * travel;
        offset = oa::app::scrolled_view_offset(
            offset,
            way * travel,
            carried,
            camera - before,
            oa::app::most_view_offset(camera, farthest())
        );
    }

    /// Returns where the view is drawn.
    ///
    /// @return the camera's map pixel and the offset
    [[nodiscard]] double view() const { return camera + offset; }
};

void a_scroll_toward_the_end_follows_its_exact_place() {
    for (const double travel : {0.05, 0.23, 0.4, 0.5, 0.77, 1.0, 1.6, 2.3}) {
        Scroll scroll{4096, 500.4, 1000};
        for (int frame = 0; frame < 400; ++frame) {
            scroll.frame(1, travel);
            // The camera steps whole map pixels at the scroll's rate, as
            // it always has; the view is at the scroll's exact place.
            OA_CHECK(scroll.camera == 1000 + scroll.steps);
            OA_CHECK(std::fabs(scroll.view() - (1000.0 + scroll.travelled)) < 1.0e-6);
        }
    }
}

void a_scroll_toward_the_start_never_jumps_or_turns_back() {
    for (const double travel : {0.05, 0.23, 0.31, 0.5, 0.77, 1.0, 1.6, 2.3}) {
        Scroll scroll{4096, 500.4, 3000};
        double view = scroll.view();
        for (int frame = 0; frame < 400; ++frame) {
            scroll.frame(-1, travel);
            const double moved = view - scroll.view();
            // It moves toward the start by at most the frame's travel, never
            // back, and lags the scroll's exact place by at most a map pixel.
            OA_CHECK(moved >= -1.0e-9 && moved <= travel + 1.0e-9);
            OA_CHECK(std::fabs(scroll.view() - (3000.0 + scroll.travelled)) <= 1.0 + 1.0e-9);
            OA_CHECK(scroll.camera == 3000 + scroll.steps);
            view = scroll.view();
        }
    }
}

/// Returns the frames a scroll takes to carry a whole map pixel, the frame
/// it does so among them.
///
/// @param carry the fraction of a map pixel carried before the first frame
/// @param travel map pixels each frame scrolls, above 0
/// @return frames, at least 1
[[nodiscard]] int frames_to_step(double carry, double travel) {
    int frames = 1;
    for (double carried = carry + travel; carried < 1.0; carried += travel)
        ++frames;
    return frames;
}

// The carries a second axis joins a scroll at, the other axis having moved
// the camera's carry that far, and the travels it joins at, none of which
// carries a whole map pixel in a whole number of frames from them.
constexpr std::array<double, 5> joining_carries{0.1, 0.46, 0.69, 0.9, 0.99};
constexpr std::array<double, 5> joining_travels{0.07, 0.23, 0.4, 0.77, 1.6};
// Frames each joining scroll is followed for.
constexpr int joining_frames = 200;

void an_axis_joining_toward_the_end_catches_up_before_its_camera_steps() {
    for (const double travel : joining_travels)
        for (const double carry : joining_carries) {
            // Its view lies on its camera's map pixel, the carry before it.
            Scroll scroll{4096, 500.4, 1000};
            scroll.carry = carry;
            const int frames = frames_to_step(carry, travel);
            double view = scroll.view();
            for (int frame = 0; frame < joining_frames; ++frame) {
                scroll.frame(1, travel);
                const double moved = scroll.view() - view;
                view = scroll.view();
                // The camera steps on the carry as it always has.
                OA_CHECK(scroll.camera == 1000 + scroll.steps);
                if (frame < frames) {
                    // Up to the camera's first step each frame moves by the
                    // travel and an even share of the carry it trailed.
                    OA_CHECK(std::fabs(moved - (travel + carry / frames)) < 1.0e-9);
                } else {
                    // Then by the travel alone.
                    OA_CHECK(std::fabs(moved - travel) < 1.0e-9);
                }
                // From that step on it lies the carry past the camera.
                if (frame >= frames - 1)
                    OA_CHECK(std::fabs(scroll.offset - scroll.carry) < 1.0e-9);
            }
        }
    // Frames of uneven travel share what is left over the frames left at
    // each one's travel, never move less than their travel, and reach the
    // carry by the step all the same.
    constexpr std::array<double, 3> uneven{0.11, 0.3, 0.19};
    Scroll scroll{4096, 500.4, 1000};
    scroll.carry = 0.69;
    double view = scroll.view();
    bool stepped = false;
    for (int frame = 0; frame < joining_frames; ++frame) {
        const double travel = uneven[static_cast<std::size_t>(frame) % uneven.size()];
        scroll.frame(1, travel);
        const double moved = scroll.view() - view;
        view = scroll.view();
        OA_CHECK(moved >= travel - 1.0e-9);
        if (stepped)
            OA_CHECK(std::fabs(moved - travel) < 1.0e-9);
        stepped = stepped || scroll.steps != 0;
        if (stepped)
            OA_CHECK(std::fabs(scroll.offset - scroll.carry) < 1.0e-9);
    }
}

void an_axis_joining_toward_the_start_catches_up_before_its_camera_steps() {
    for (const double travel : joining_travels)
        for (const double carry : joining_carries) {
            // Its view lies a whole map pixel past its camera, the carry
            // behind the scroll's place toward the start.
            Scroll scroll{4096, 500.4, 3000};
            scroll.carry = carry;
            scroll.offset = 1.0;
            const int frames = frames_to_step(carry, travel);
            double view = scroll.view();
            for (int frame = 0; frame < joining_frames; ++frame) {
                scroll.frame(-1, travel);
                const double moved = view - scroll.view();
                view = scroll.view();
                OA_CHECK(scroll.camera == 3000 + scroll.steps);
                if (frame < frames)
                    OA_CHECK(std::fabs(moved - (travel + carry / frames)) < 1.0e-9);
                else
                    OA_CHECK(std::fabs(moved - travel) < 1.0e-9);
                if (frame >= frames - 1)
                    OA_CHECK(std::fabs(scroll.offset - (1.0 - scroll.carry)) < 1.0e-9);
            }
        }
}

void a_scroll_held_at_the_edge_stays_still() {
    constexpr double travel = 0.23;
    for (const double visible : {500.4, 499.6}) {
        Scroll scroll{1024, visible, 0};
        scroll.camera = scroll.farthest() - 3;
        double view = scroll.view();
        for (int frame = 0; frame < 200; ++frame) {
            scroll.frame(1, travel);
            // Toward the edge it moves by at most the frame's travel and
            // never turns back, and it never lies past the camera's
            // farthest place, so it shows no more past the map's edge than
            // the camera alone does there.
            const double moved = scroll.view() - view;
            OA_CHECK(moved >= -1.0e-9 && moved <= travel + 1.0e-9);
            OA_CHECK(scroll.view() <= scroll.farthest() + 1.0e-9);
            view = scroll.view();
        }
        OA_CHECK(scroll.camera == scroll.farthest() && scroll.offset == 0.0);
        scroll.camera = 3;
        scroll.offset = 0.0;
        scroll.carry = 0.0;
        view = scroll.view();
        for (int frame = 0; frame < 200; ++frame) {
            scroll.frame(-1, travel);
            const double moved = view - scroll.view();
            OA_CHECK(moved >= -1.0e-9 && moved <= travel + 1.0e-9 && scroll.view() >= 0.0);
            view = scroll.view();
        }
        OA_CHECK(scroll.camera == 0 && scroll.offset == 0.0);
    }
}

} // namespace

int main() {
    without_a_draw_scale_the_scene_is_the_battlefield_at_the_zoom();
    at_the_zoom_the_scene_apart_is_the_battlefield();
    the_scene_covers_the_battlefield_with_the_margin();
    odd_scene_sizes_round_up_to_even();
    the_scene_holds_what_the_resample_reads();
    the_draw_scale_keeps_within_the_budget();
    the_accelerated_method_follows_the_zoom();
    the_largest_magnified_scene_is_the_battlefield_with_its_margin();
    the_area_scale_is_the_zoom_over_the_draw_scale();
    the_display_scale_is_the_zoom_times_the_density();
    the_area_phase_is_the_offset_in_scene_pixels();
    a_view_on_the_camera_pixel_draws_the_corner_it_always_has();
    a_view_between_map_pixels_is_the_same_picture_moved();
    the_map_bounds_how_far_the_view_lies_past_the_camera();
    a_view_found_from_its_exact_place_lies_on_or_past_its_camera();
    a_scroll_toward_the_end_follows_its_exact_place();
    a_scroll_toward_the_start_never_jumps_or_turns_back();
    an_axis_joining_toward_the_end_catches_up_before_its_camera_steps();
    an_axis_joining_toward_the_start_catches_up_before_its_camera_steps();
    a_scroll_held_at_the_edge_stays_still();
    return oa::test::check_exit_status();
}
