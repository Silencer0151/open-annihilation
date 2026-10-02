// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How a frame draws the battlefield (world_scaling.hpp), by table: without a
// draw scale, and at the zoom, the scene is the battlefield; at another
// scale it covers the battlefield's map pixels with the margin, rounded up
// to even sizes; and over the zoom range and the battlefields of every
// window, the scene holds every pixel the nearest resample reads from it.
#include "oa/app/world_scaling.hpp"

#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <limits>
#include <optional>

namespace {

using oa::app::world_scaling;
using oa::app::WorldScaling;

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
           scaling.scene_height == expected.scene_height && scaling.apart == expected.apart;
}

void without_a_draw_scale_the_scene_is_the_battlefield_at_the_zoom() {
    OA_CHECK(same(world_scaling(0.5F, 1664, 952, std::nullopt), {0.5F, 1664, 952, false}));
    OA_CHECK(same(world_scaling(2.0F, 1664, 952, std::nullopt), {2.0F, 1664, 952, false}));
    // A draw scale that is not above 0 counts as none.
    OA_CHECK(same(world_scaling(2.0F, 640, 520, 0.0F), {2.0F, 640, 520, false}));
    OA_CHECK(same(world_scaling(2.0F, 640, 520, -1.0F), {2.0F, 640, 520, false}));
    OA_CHECK(same(
        world_scaling(2.0F, 640, 520, std::numeric_limits<float>::quiet_NaN()),
        {2.0F, 640, 520, false}
    ));
}

void at_the_zoom_the_scene_apart_is_the_battlefield() {
    OA_CHECK(same(world_scaling(1.0F, 1664, 952, 1.0F), {1.0F, 1664, 952, true}));
    OA_CHECK(same(world_scaling(0.5F, 1664, 952, 0.5F), {0.5F, 1664, 952, true}));
    OA_CHECK(same(world_scaling(2.0F, 1161, 666, 2.0F), {2.0F, 1161, 666, true}));
    // A zoom that is not above 0 gives the battlefield's size.
    OA_CHECK(same(world_scaling(0.0F, 640, 520, 1.0F), {1.0F, 640, 520, true}));
}

void the_scene_covers_the_battlefield_with_the_margin() {
    // 1664 / 0.5 + 2 and 952 / 0.5 + 2.
    OA_CHECK(same(world_scaling(0.5F, 1664, 952, 1.0F), {1.0F, 3330, 1906, true}));
    // 1664 / 2 + 2 and 952 / 2 + 2.
    OA_CHECK(same(world_scaling(2.0F, 1664, 952, 1.0F), {1.0F, 834, 478, true}));
    // ceil(640 * 0.75 / 0.6) + 2 and ceil(520 * 0.75 / 0.6) + 2.
    OA_CHECK(same(world_scaling(0.6F, 640, 520, 0.75F), {0.75F, 802, 652, true}));
}

void odd_scene_sizes_round_up_to_even() {
    // ceil(1161 / 2) + 2 = 583 and 666 / 2 + 2 = 335.
    OA_CHECK(same(world_scaling(2.0F, 1161, 666, 1.0F), {1.0F, 584, 336, true}));
    // ceil(640 / 1.37) + 2 = 470 and ceil(521 / 1.37) + 2 = 383.
    OA_CHECK(same(world_scaling(1.37F, 640, 521, 1.0F), {1.0F, 470, 384, true}));
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

} // namespace

int main() {
    without_a_draw_scale_the_scene_is_the_battlefield_at_the_zoom();
    at_the_zoom_the_scene_apart_is_the_battlefield();
    the_scene_covers_the_battlefield_with_the_margin();
    odd_scene_sizes_round_up_to_even();
    the_scene_holds_what_the_resample_reads();
    return oa::test::check_exit_status();
}
