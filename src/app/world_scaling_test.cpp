// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How a frame draws the battlefield (world_scaling.hpp), by table: without a
// draw scale, and at the zoom, the scene is the battlefield; at another
// scale it covers the battlefield's map pixels with the margin, rounded up
// to even sizes; and over the zoom range and the battlefields of every
// window, the scene holds every pixel the nearest resample reads from it.
// The accelerated tier's draw scale within each scene budget, its method at
// each zoom, the area pass's scale and the scene it reads, and the largest
// magnified scene.
#include "oa/app/world_scaling.hpp"

#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
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
    return oa::test::check_exit_status();
}
