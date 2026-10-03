// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Full tier takes from the processor's frame for the card: the fog
// grid the frame built, which the card's fog passes draw from; the overlay
// canvas's key colour, which the world layer is cleared to before the
// painters paint it; and the quads the painters that shade the world beneath
// them, the kill board, the +stats panel and game text in the modern fonts,
// ask the card to draw in place of reading the world.
#include "oa/app/runtime.hpp"

#include "full_fog.hpp"
#include "full_presentation.hpp"
#include "xrgb_conversion.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::app {

namespace {

/// The opacities frontend_renderer::blend_rect takes, in 256ths of a pixel.
constexpr float blend_opacity_whole = 256.0F;
/// The largest level of a channel.
constexpr double channel_whole = 255.0;
/// The most quads a pixel's count of them holds.
constexpr int most_counted_quads = 255;

} // namespace

void Runtime::note_full_fog_grid(
    const oa::present::world_renderer::FogGrid& grid,
    int32_t camera_x,
    int32_t camera_z,
    bool dithered
) {
    if (!full_)
        return;
    auto& fog = full_->fog;
    fog.grid = grid;
    fog.camera_x = camera_x;
    fog.camera_z = camera_z;
    fog.dithered = dithered;
    const auto* gamma = gamma_identity_ ? nullptr : &gamma_table_;
    fog.unmapped = full::flat_colour(fog_shading_.unmapped_rgb, gamma);
    fog.dither = full::flat_colour(fog_shading_.dither_rgb, gamma);
}

std::array<uint8_t, 3> Runtime::full_overlay_key() {
    if (!full_)
        return overlay_key_colour(match_palette_);
    auto& full = *full_;
    if (!full.overlay_key_found || full.overlay_key_palette != match_palette_) {
        full.overlay_key = overlay_key_colour(match_palette_);
        full.overlay_key_palette = match_palette_;
        full.overlay_key_found = true;
    }
    return full.overlay_key;
}

bool Runtime::full_frame_drawn() const noexcept {
    return full_ && full_->canvas_drawn;
}

void Runtime::note_full_canvas(bool canvas, uint32_t camera_x, uint32_t camera_y, float zoom) {
    if (!full_)
        return;
    full_->canvas_drawn = canvas;
    if (!canvas)
        return;
    full_->frame_camera_x = camera_x;
    full_->frame_camera_y = camera_y;
    full_->frame_zoom = zoom;
    full_->canvas_pixels = nullptr;
    full_->world_quads.clear();
}

void Runtime::note_full_canvas_pixels(const uint8_t* pixels) noexcept {
    if (full_ && full_->canvas_drawn)
        full_->canvas_pixels = pixels;
}

bool Runtime::paints_full_canvas() {
    if (!full_presentation() || !full_->canvas_drawn)
        return false;
    // The battlefield layer, or a surface that holds the canvas's pixels
    // while a painter after the fog paints on it.
    const auto& target = paint_target();
    return &target == &match_world_cpu_ ||
           (!target.rgb.empty() && target.rgb.data() == full_->canvas_pixels);
}

bool Runtime::paint_world_level(int x, int y, int width, int height, int32_t level) {
    if (!paints_full_canvas() || width <= 0 || height <= 0)
        return false;
    const full_fog::LevelQuad quad = full_fog::level_quad(level);
    if (quad.colour.alpha <= 0.0F)
        return true;
    full_->world_quads.push_back({x, y, width, height, quad.colour, quad.blend});
    return true;
}

bool Runtime::paint_world_blend(
    int x, int y, int width, int height, std::array<uint8_t, 3> colour, uint32_t opacity
) {
    if (!paints_full_canvas() || width <= 0 || height <= 0)
        return false;
    if (opacity == 0)
        return true;
    card::Colour shown = full::flat_colour(colour, gamma_identity_ ? nullptr : &gamma_table_);
    shown.alpha = std::min(1.0F, static_cast<float>(opacity) / blend_opacity_whole);
    full_->world_quads.push_back({x, y, width, height, shown, card::Blend::alpha});
    return true;
}

bool Runtime::paint_world_minimum(
    int x, int y, int width, int height, std::array<uint8_t, 3> colour
) {
    if (!paints_full_canvas() || width <= 0 || height <= 0)
        return false;
    const card::Colour shown = full::flat_colour(colour, gamma_identity_ ? nullptr : &gamma_table_);
    full_->world_quads.push_back({x, y, width, height, shown, card::Blend::minimum});
    return true;
}

std::vector<uint8_t> replay_world_quads(
    std::span<const FullWorldQuad> quads,
    bool minimum_composed,
    std::vector<uint8_t>& picture,
    uint32_t width,
    uint32_t height,
    int32_t origin_x,
    int32_t origin_y
) {
    std::vector<uint8_t> over(std::size_t{width} * height, 0);
    if (picture.size() < over.size() * 3U)
        return over;
    for (const FullWorldQuad& quad : quads) {
        const std::array<double, 3> colour{
            static_cast<double>(quad.colour.red) * channel_whole,
            static_cast<double>(quad.colour.green) * channel_whole,
            static_cast<double>(quad.colour.blue) * channel_whole
        };
        const auto alpha = static_cast<double>(quad.colour.alpha);
        const card::Blend blend = quad.blend == card::Blend::minimum && !minimum_composed
                                      ? card::Blend::alpha
                                      : quad.blend;
        const int64_t left = std::max<int64_t>(int64_t{origin_x} + quad.x, 0);
        const int64_t top = std::max<int64_t>(int64_t{origin_y} + quad.y, 0);
        const int64_t right =
            std::min<int64_t>(int64_t{origin_x} + quad.x + quad.width, int64_t{width});
        const int64_t bottom =
            std::min<int64_t>(int64_t{origin_y} + quad.y + quad.height, int64_t{height});
        for (int64_t y = top; y < bottom; ++y)
            for (int64_t x = left; x < right; ++x) {
                const auto at = static_cast<std::size_t>(y) * width + static_cast<std::size_t>(x);
                over[at] = static_cast<uint8_t>(std::min(over[at] + 1, most_counted_quads));
                for (std::size_t channel = 0; channel < 3; ++channel) {
                    uint8_t& level = picture[at * 3U + channel];
                    const auto under = static_cast<double>(level);
                    double shown = under;
                    switch (blend) {
                    case card::Blend::none:
                        shown = colour[channel];
                        break;
                    case card::Blend::alpha:
                        shown = colour[channel] * alpha + under * (1.0 - alpha);
                        break;
                    case card::Blend::alpha_premultiplied:
                        shown = colour[channel] + under * (1.0 - alpha);
                        break;
                    case card::Blend::additive:
                        shown = under + colour[channel] * alpha;
                        break;
                    case card::Blend::modulate:
                        shown = under * colour[channel] / channel_whole;
                        break;
                    case card::Blend::darken:
                        shown = under * (1.0 - alpha);
                        break;
                    case card::Blend::minimum:
                        shown = std::min(under, colour[channel]);
                        break;
                    }
                    level =
                        static_cast<uint8_t>(std::lround(std::clamp(shown, 0.0, channel_whole)));
                }
            }
    }
    return over;
}

} // namespace oa::app
