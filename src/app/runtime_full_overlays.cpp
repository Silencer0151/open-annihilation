// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Full tier takes from the processor's frame for the card: the fog
// grid the frame built, which the card's fog passes draw from; the overlay
// canvas's key colour, which the world layer is cleared to before the
// painters paint it; and the quads the two painters that shade the world
// beneath them, the kill board and the +stats panel, ask the card to draw
// in place of reading the world.
#include "oa/app/runtime.hpp"

#include "full_fog.hpp"
#include "full_presentation.hpp"
#include "xrgb_conversion.hpp"

#include <algorithm>
#include <cstdint>

namespace oa::app {

namespace {

/// The opacities frontend_renderer::blend_rect takes, in 256ths of a pixel.
constexpr float blend_opacity_whole = 256.0F;

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
    full_->world_quads.clear();
}

bool Runtime::paint_world_level(int x, int y, int width, int height, int32_t level) {
    if (!full_presentation() || overlay_target_ != &match_world_cpu_ || width <= 0 || height <= 0)
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
    if (!full_presentation() || overlay_target_ != &match_world_cpu_ || width <= 0 || height <= 0)
        return false;
    if (opacity == 0)
        return true;
    card::Colour shown = full::flat_colour(colour, gamma_identity_ ? nullptr : &gamma_table_);
    shown.alpha = std::min(1.0F, static_cast<float>(opacity) / blend_opacity_whole);
    full_->world_quads.push_back({x, y, width, height, shown, card::Blend::alpha});
    return true;
}

} // namespace oa::app
