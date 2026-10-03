// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Full tier's world target plan (full_supersampling.hpp): at a factor
// of 1 no target; from zoom 1 up the stages draw at the zoom into a target
// of the battlefield rounded up to the grain, whose whole texture resolves
// into the size; below zoom 1 they draw at one texel a map pixel over a
// part rounded up to even pixels that the two-level blend reduces, the
// destination never short of the battlefield and the scale never outside
// one half to 1, which card::check_frame accepts, for battlefields of
// every parity at zooms across the range.
#include "full_supersampling.hpp"

#include "oa/test/check.hpp"

#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>

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
constexpr std::array<uint32_t, 9> zoomed_out_thousandths{
    500, 501, 550, 600, 707, 750, 900, 990, 999
};

/// Builds a frame with one reduction batch of a plan, for check_frame.
///
/// @param plan the plan
/// @return the frame
card::CardFrame frame_of(const fs::WorldTargetPlan& plan) {
    card::CardFrame frame;
    card::Batch batch;
    batch.operation = plan.two_level ? card::Operation::blend_reduce : card::Operation::resolve;
    batch.source = card::TargetHandle{1};
    batch.source_part = plan.source_part;
    batch.destination = plan.destination;
    frame.batches.push_back(batch);
    return frame;
}

/// A factor of 1 makes no target: the frame draws straight at the zoom.
void test_no_target_at_factor_1() {
    for (const float zoom : {0.5F, 0.75F, 1.0F, 1.37F, 2.0F, 4.0F}) {
        const auto plan = fs::plan_world_target(zoom, 1, 1792, 864);
        OA_CHECK(plan.factor == 1);
        OA_CHECK(plan.size_width == 1792 && plan.size_height == 864);
        OA_CHECK(plan.draw_scale == zoom);
        OA_CHECK(!plan.two_level);
        OA_CHECK(plan.source_part.width == 1792 && plan.destination.width == 1792);
    }
    OA_CHECK(fs::plan_world_target(1.0F, 2, 0, 864).factor == 1);
    OA_CHECK(fs::plan_world_target(0.0F, 2, 1792, 864).factor == 1);
}

/// From zoom 1 up the stages draw at the zoom; the whole texture, the size
/// times the factor, resolves into the size, which the grain rounds up.
void test_zoomed_in_resolves_the_whole_texture() {
    const auto plan = fs::plan_world_target(2.0F, 2, 1792, 864);
    OA_CHECK(plan.factor == 2 && !plan.two_level);
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
    OA_CHECK(between.draw_scale == 1.37F && !between.two_level);
}

/// Below zoom 1 the stages draw at one texel a map pixel over the part the
/// battlefield shows, and the two-level blend reduces it by the zoom: at
/// zoom 0.5 the part is twice the battlefield and the scale exactly one
/// half; at 0.75 the part is rounded up to even pixels and the destination
/// with it.
void test_zoomed_out_reduces_a_part_by_the_two_level_blend() {
    const auto half = fs::plan_world_target(0.5F, 2, 1792, 864);
    OA_CHECK(half.factor == 2 && half.two_level);
    OA_CHECK(half.draw_scale == 0.5F);
    OA_CHECK(half.source_part.width == 3584 && half.source_part.height == 1728);
    OA_CHECK(half.destination.width == 1792 && half.destination.height == 864);
    OA_CHECK(card::check_frame(frame_of(half)).empty());
    const auto odd = fs::plan_world_target(0.5F, 2, 1793, 865);
    OA_CHECK(odd.size_width == 1800 && odd.size_height == 876);
    OA_CHECK(odd.source_part.width == 3588 && odd.source_part.height == 1732);
    OA_CHECK(odd.destination.width == 1794 && odd.destination.height == 866);
    OA_CHECK(card::check_frame(frame_of(odd)).empty());
    const auto three_quarters = fs::plan_world_target(0.75F, 4, 1792, 864);
    OA_CHECK(three_quarters.draw_scale == 0.25F);
    OA_CHECK(three_quarters.source_part.width == 2392 && three_quarters.source_part.height == 1152);
    OA_CHECK(three_quarters.destination.width == 1794 && three_quarters.destination.height == 864);
    OA_CHECK(card::check_frame(frame_of(three_quarters)).empty());
}

/// Over battlefields of either parity, both factors and zooms across the
/// range: the part is even and within the texture, the destination covers
/// the battlefield, the scale lies within one half to 1, and the frame's
/// reduction batch is well formed.
void test_every_zoom_keeps_the_invariants() {
    for (const auto& battlefield : battlefields)
        for (const uint32_t factor : {2U, 4U}) {
            for (const uint32_t thousandths : zoomed_out_thousandths) {
                const float zoom = static_cast<float>(thousandths) / 1000.0F;
                const auto plan =
                    fs::plan_world_target(zoom, factor, battlefield[0], battlefield[1]);
                OA_CHECK(plan.two_level);
                OA_CHECK(plan.size_width >= battlefield[0] && plan.size_height >= battlefield[1]);
                OA_CHECK(
                    plan.size_width % fs::target_grain == 0 &&
                    plan.size_height % fs::target_grain == 0
                );
                const auto part_w = static_cast<uint32_t>(plan.source_part.width);
                const auto part_h = static_cast<uint32_t>(plan.source_part.height);
                OA_CHECK(part_w % fs::part_grain == 0 && part_h % fs::part_grain == 0);
                OA_CHECK(part_w <= plan.size_width * factor && part_h <= plan.size_height * factor);
                OA_CHECK(static_cast<double>(part_w) * zoom >= battlefield[0] - 1.0e-3);
                OA_CHECK(static_cast<uint32_t>(plan.destination.width) >= battlefield[0]);
                OA_CHECK(static_cast<uint32_t>(plan.destination.height) >= battlefield[1]);
                const double across = static_cast<double>(plan.destination.width) / part_w;
                const double down = static_cast<double>(plan.destination.height) / part_h;
                OA_CHECK(across >= 0.5 && across <= 1.0 && down >= 0.5 && down <= 1.0);
                // The stages draw at one texel a map pixel: the part spans
                // part_w / factor size pixels at the draw scale.
                OA_CHECK(std::fabs(plan.draw_scale * static_cast<float>(factor) - 1.0F) < 1.0e-6F);
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
            for (const float zoom : {1.0F, 1.37F, 2.0F, 2.5F, 4.0F}) {
                const auto plan =
                    fs::plan_world_target(zoom, factor, battlefield[0], battlefield[1]);
                OA_CHECK(!plan.two_level && plan.draw_scale == zoom);
                OA_CHECK(static_cast<uint32_t>(plan.destination.width) == plan.size_width);
                OA_CHECK(static_cast<uint32_t>(plan.source_part.width) == plan.size_width * factor);
                OA_CHECK(card::check_frame(frame_of(plan)).empty());
            }
        }
}

} // namespace

int main() {
    test_no_target_at_factor_1();
    test_zoomed_in_resolves_the_whole_texture();
    test_zoomed_out_reduces_a_part_by_the_two_level_blend();
    test_every_zoom_keeps_the_invariants();
    return oa::test::check_exit_status();
}
