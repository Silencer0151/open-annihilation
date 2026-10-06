// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/world_scaling.hpp"

#include "oa/present/world_renderer/scene_filter.hpp"

#include <algorithm>
#include <cmath>

namespace oa::app {

namespace {

/// Returns a scene extent: the map pixels a battlefield extent shows at the
/// scene's scale, with the margin past its right or bottom edge, rounded up
/// to an even count.
///
/// @param battlefield the battlefield's columns or rows
/// @param scale scene pixels per map pixel
/// @param zoom screen pixels per map pixel, above 0
/// @return the scene's columns or rows
[[nodiscard]] int32_t scene_extent(int32_t battlefield, float scale, float zoom) noexcept {
    const auto pixels = static_cast<int32_t>(std::ceil(
                            static_cast<double>(battlefield) * static_cast<double>(scale) /
                            static_cast<double>(zoom)
                        )) +
                        WorldScaling::margin;
    return pixels + (pixels % 2);
}

} // namespace

WorldScaling world_scaling(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    std::optional<float> draw_scale
) noexcept {
    if (!draw_scale || !(*draw_scale > 0.0F))
        return {zoom, battlefield_width, battlefield_height, false, SceneMethod::none};
    const float scale = *draw_scale;
    if (scale == zoom || !(zoom > 0.0F))
        return {scale, battlefield_width, battlefield_height, true, SceneMethod::nearest};
    return {
        scale,
        scene_extent(battlefield_width, scale, zoom),
        scene_extent(battlefield_height, scale, zoom),
        true,
        SceneMethod::nearest
    };
}

float accelerated_draw_scale(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    render_policy::SceneBudget budget
) noexcept {
    if (!(zoom > 0.0F) || zoom >= 1.0F)
        return zoom;
    double ratio = 1.0;
    uint64_t most_pixels = 0;
    switch (budget) {
    case render_policy::SceneBudget::none:
        return zoom;
    case render_policy::SceneBudget::reduced:
        ratio = reduced_budget_scene_ratio;
        most_pixels = reduced_budget_scene_pixels;
        break;
    case render_policy::SceneBudget::full:
        ratio = full_budget_scene_ratio;
        most_pixels = full_budget_scene_pixels;
        break;
    }
    const auto z = static_cast<double>(zoom);
    const double battlefield_pixels = static_cast<double>(std::max(battlefield_width, 1)) *
                                      static_cast<double>(std::max(battlefield_height, 1));
    double scale = std::min(1.0, z * std::sqrt(ratio));
    scale = std::min(scale, z * std::sqrt(static_cast<double>(most_pixels) / battlefield_pixels));
    scale = std::clamp(scale, z, std::min(1.0, 2.0 * z));
    return static_cast<float>(scale);
}

WorldScaling accelerated_world_scaling(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    render_policy::SceneBudget budget,
    bool magnify
) noexcept {
    const WorldScaling at_zoom{
        zoom, battlefield_width, battlefield_height, false, SceneMethod::none
    };
    if (!(zoom > 0.0F) || zoom == 1.0F)
        return at_zoom;
    if (zoom > 1.0F) {
        if (!magnify)
            return at_zoom;
        auto scaling = world_scaling(zoom, battlefield_width, battlefield_height, 1.0F);
        scaling.method = SceneMethod::magnify;
        return scaling;
    }
    const float scale = accelerated_draw_scale(zoom, battlefield_width, battlefield_height, budget);
    if (!(scale > zoom) || static_cast<double>(zoom) / static_cast<double>(scale) > area_cut_off)
        return at_zoom;
    auto scaling = world_scaling(zoom, battlefield_width, battlefield_height, scale);
    scaling.method = SceneMethod::area;
    return scaling;
}

double world_display_scale(const WorldScaling& scaling, float zoom, double density) noexcept {
    if (scaling.method != SceneMethod::magnify || !(scaling.draw_scale > 0.0F))
        return density;
    return static_cast<double>(zoom) * density / static_cast<double>(scaling.draw_scale);
}

SceneExtent
largest_magnified_scene(int32_t battlefield_width, int32_t battlefield_height) noexcept {
    const auto extent = [](int32_t battlefield) {
        const int32_t pixels = std::max(battlefield, 0) + WorldScaling::margin;
        return pixels + (pixels % 2);
    };
    return {extent(battlefield_width), extent(battlefield_height)};
}

uint32_t area_scale(float zoom, float draw_scale) noexcept {
    namespace wr = oa::present::world_renderer;
    if (!(zoom > 0.0F) || !(draw_scale > 0.0F))
        return wr::area_scale_max;
    const double scale = std::ceil(
        static_cast<double>(zoom) / static_cast<double>(draw_scale) *
        static_cast<double>(wr::area_fixed_one)
    );
    return static_cast<uint32_t>(std::clamp(
        scale, static_cast<double>(wr::area_scale_min), static_cast<double>(wr::area_scale_max)
    ));
}

uint32_t area_phase(double offset, float draw_scale, uint32_t scale) noexcept {
    if (!(offset > 0.0) || !(draw_scale > 0.0F))
        return 0;
    const double phase = std::round(offset * static_cast<double>(draw_scale) * scale);
    return static_cast<uint32_t>(std::min(phase, static_cast<double>(scale)));
}

MagnifiedSpan
magnified_span(uint32_t battlefield, float zoom, uint32_t scene, double offset) noexcept {
    MagnifiedSpan span;
    if (!(zoom > 0.0F) || battlefield == 0 || scene == 0)
        return span;
    const auto z = static_cast<double>(zoom);
    const uint32_t whole =
        std::min(static_cast<uint32_t>(std::ceil(static_cast<double>(battlefield) / z)), scene);
    const auto landed = static_cast<double>(std::lround(static_cast<double>(whole) * z));
    const double moved = std::clamp(offset, 0.0, 1.0);
    span.corner = moved > 0.0 ? std::min(whole + 1, scene) : whole;
    // The landed pixels over the whole corner's columns: the scale a view on
    // its camera's map pixel lands at, which a view between map pixels keeps,
    // moving by the offset at it, so that the corner still reaches past the
    // battlefield's far edge.
    span.start = moved > 0.0 ? -moved * landed / static_cast<double>(whole) : 0.0;
    span.extent = static_cast<double>(span.corner) * landed / static_cast<double>(whole);
    return span;
}

} // namespace oa::app
