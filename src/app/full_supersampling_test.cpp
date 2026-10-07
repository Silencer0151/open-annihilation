// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's world target plan (full_supersampling.hpp): at a factor
// of 1, and below zoom 1 at any factor, no target; from zoom 1 up the
// stages draw at the zoom into a target of the battlefield rounded up to
// the grain, whose whole texture resolves into the size, which
// card::check_frame accepts, for battlefields of every parity at zooms
// across the range.
#include "full_supersampling.hpp"

#include "oa/test/check.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <string>

namespace {

namespace fs = oa::app::full_supersampling;
namespace card = oa::app::card;

/// Battlefields about those of common windows, and odd sizes beside them.
constexpr std::array<std::array<uint32_t, 2>, 6> battlefields{{
    {1792, 864},
    {1793, 865},
    {1280, 720},
    {3584, 1728},
    {641, 479},
    {4864, 1152},
}};

/// The zooms below 1 the plan is checked at, in thousandths.
constexpr std::array<uint32_t, 15> zoomed_out_thousandths{
    167, 200, 249, 250, 300, 495, 500, 501, 550, 600, 707, 750, 900, 990, 999
};

/// Builds a frame with the reduction batch of a plan, for check_frame.
///
/// @param plan the plan
/// @return the frame
card::CardFrame frame_of(const fs::WorldTargetPlan& plan) {
    card::CardFrame frame;
    card::Batch batch;
    batch.operation = card::Operation::resolve;
    batch.source = card::TargetHandle{1};
    batch.destination = plan.destination;
    frame.batches.push_back(batch);
    return frame;
}

/// A factor of 1 makes no target, nor does a zoom below 1 at any factor:
/// the frame draws at the zoom as without anti-aliasing.
void test_no_target_at_factor_1_or_below_zoom_1() {
    for (const float zoom : {0.5F, 0.75F, 1.0F, 1.37F, 2.0F, 4.0F}) {
        const auto plan = fs::plan_world_target(zoom, 1, 1792, 864);
        OA_CHECK(plan.factor == 1);
        OA_CHECK(plan.size_width == 1792 && plan.size_height == 864);
        OA_CHECK(plan.draw_scale == zoom);
        OA_CHECK(plan.source_part.width == 1792 && plan.destination.width == 1792);
    }
    for (const uint32_t factor : {2U, 4U, 16U})
        for (const uint32_t thousandths : zoomed_out_thousandths) {
            const float zoom = static_cast<float>(thousandths) / 1000.0F;
            const auto plan = fs::plan_world_target(zoom, factor, 1793, 865);
            OA_CHECK(plan.factor == 1);
            OA_CHECK(plan.size_width == 1793 && plan.size_height == 865);
            OA_CHECK(plan.draw_scale == zoom);
            OA_CHECK(plan.destination.width == 1793 && plan.destination.height == 865);
        }
    OA_CHECK(fs::plan_world_target(1.0F, 2, 0, 864).factor == 1);
    OA_CHECK(fs::plan_world_target(0.0F, 2, 1792, 864).factor == 1);
}

/// From zoom 1 up the stages draw at the zoom; the whole texture, the size
/// times the factor, resolves into the size, which the grain rounds up.
void test_zoomed_in_resolves_the_whole_texture() {
    const auto plan = fs::plan_world_target(2.0F, 2, 1792, 864);
    OA_CHECK(plan.factor == 2);
    OA_CHECK(plan.size_width == 1800 && plan.size_height == 864);
    OA_CHECK(plan.draw_scale == 2.0F);
    OA_CHECK(
        plan.source_part.x == 0 && plan.source_part.y == 0 && plan.source_part.width == 3600 &&
        plan.source_part.height == 1728
    );
    OA_CHECK(plan.destination.width == 1800 && plan.destination.height == 864);
    OA_CHECK(card::check_frame(frame_of(plan)).empty());
    const auto four = fs::plan_world_target(1.0F, 4, 1280, 720);
    OA_CHECK(four.size_width == 1284 && four.size_height == 720);
    OA_CHECK(four.source_part.width == 5136 && four.source_part.height == 2880);
    OA_CHECK(four.destination.width == 1284 && four.destination.height == 720);
    OA_CHECK(four.draw_scale == 1.0F);
    const auto between = fs::plan_world_target(1.37F, 4, 1792, 864);
    OA_CHECK(between.factor == 4 && between.draw_scale == 1.37F);
}

/// Over battlefields of either parity, both factors and zooms from 1 up:
/// the size covers the battlefield on the grain, the whole texture resolves
/// into the size, and the frame's reduction batch is well formed.
void test_every_zoom_keeps_the_invariants() {
    for (const auto& battlefield : battlefields)
        for (const uint32_t factor : {2U, 4U})
            for (const float zoom : {1.0F, 1.37F, 2.0F, 2.5F, 4.0F}) {
                const auto plan =
                    fs::plan_world_target(zoom, factor, battlefield[0], battlefield[1]);
                OA_CHECK(plan.factor == factor && plan.draw_scale == zoom);
                OA_CHECK(plan.size_width >= battlefield[0] && plan.size_height >= battlefield[1]);
                OA_CHECK(
                    plan.size_width % fs::target_grain == 0 &&
                    plan.size_height % fs::target_grain == 0
                );
                OA_CHECK(static_cast<uint32_t>(plan.destination.width) == plan.size_width);
                OA_CHECK(static_cast<uint32_t>(plan.source_part.width) == plan.size_width * factor);
                const std::string fault = card::check_frame(frame_of(plan));
                OA_CHECK(fault.empty());
                if (!fault.empty())
                    std::fprintf(
                        stderr,
                        "%ux%u at zoom %.3f, factor %u: %s\n",
                        battlefield[0],
                        battlefield[1],
                        static_cast<double>(zoom),
                        factor,
                        fault.c_str()
                    );
            }
}

} // namespace

int main() {
    test_no_target_at_factor_1_or_below_zoom_1();
    test_zoomed_in_resolves_the_whole_texture();
    test_every_zoom_keeps_the_invariants();
    return oa::test::check_exit_status();
}
