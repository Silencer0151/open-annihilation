// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Composition of the match battlefield frame.
#include "oa/app/runtime.hpp"
#include "oa/app/match_model_draws.hpp"
#include "oa/present/model/model_draw.hpp"
#include "oa/present/model/rgb_bridge.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/raster.hpp"
#include "oa/ui/console/game_fields.hpp"
#include "oa/ui/hud/health_bar.hpp"
#include "oa/ui/hud/sprite_placement.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include "oa/present/world_renderer/world_draw_order.hpp"
#include "oa/present/world_renderer/world_overlays.hpp"
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace oa::app {

// 3DO renderer state of one match: prepared models and textures, the draw
// state of every unit slot, and the 8-bit bridge onto the RGB frame. Each 3D
// feature keeps its own draw state in its MatchFeatureDraw.
struct MatchModels {
    const void* match{};
    oa::sim::match_runtime::Match* offline{};
    oa::present::model::ModelLibrary library;
    oa::present::model::ModelDisplay display;
    oa::present::model::ModelRenderer renderer;
    oa::present::model::RgbBridge bridge;
    std::vector<UnitDrawState> units; // by unit slot
    // 3D features draw through one zeroed unit record marked as a z-buffered
    // building owned by the viewpoint player, kept here in place of its part
    // of Game.search_context_block.
    oa::Unit feature_unit{};
    uint32_t animation_tick{};
    bool animation_started{};
    uint32_t debris_tick{}; // the tick whose first draw started the debris particles
    bool debris_drawn{};
    bool weapon_names_loaded{};
    std::unordered_map<uint8_t, std::string> weapon_model_names; // by registry index
    std::unordered_map<uint8_t, std::shared_ptr<const oa::formats::objects3d::Model>> weapon_models;
    std::vector<uint8_t> shadow_pixels;
    oa::Sprite projectile_shadow{}; // FX.GAF "shadow" frame 0
};

namespace {

namespace model_render = oa::present::model;

// Margin around the estimated screen extent of a model draw.
constexpr int32_t kModelRegionMargin = 16;
// Reach of a projectile model around its screen point.
constexpr int32_t kProjectileReach = 64;
// Weapon render types drawn as 3DO models, and the half turn added to the
// missile's heading and pitch words.
constexpr uint8_t kRenderMissile = 1;
constexpr uint8_t kRenderBomb = 3;
constexpr uint8_t kRenderModel = 6;
constexpr uint16_t kHalfTurn = 0x8000;
// Texture animation steps replayed at most per frame.
constexpr uint32_t kMaxAnimationSteps = 30;

int32_t high_word(int32_t value) {
    return static_cast<int16_t>(static_cast<uint32_t>(value) >> 16);
}

int32_t camera_fixed(int32_t pixels) {
    return static_cast<int32_t>(static_cast<uint32_t>(pixels) << 16);
}

int32_t wrapping_sub(int32_t a, int32_t b) {
    return static_cast<int32_t>(static_cast<uint32_t>(a) - static_cast<uint32_t>(b));
}

int32_t terrain_height(void* user, const oa::FixedVec3& position) {
    auto* models = static_cast<MatchModels*>(user);
    return models->offline->map_height(
        static_cast<uint32_t>(position.x), static_cast<uint32_t>(position.z)
    );
}

/// Returns what drawing needs of a unit's model: its instance, prepared model, draw state and records.
///
/// The draw state is the slot's for its current instance generation, so a
/// unit made where another died draws from fresh state.
///
/// @param[in,out] models the match's renderer state; its slot's draw state
///     begins afresh for a new instance
/// @param slot unit slot
/// @return the model, or a null instance for slot 0, a slot outside the pool
///     or one without an instance
model_render::ModelRef unit_model(MatchModels& models, uint16_t slot) {
    auto& world = models.offline->world();
    if (slot == 0 || slot >= world.record.unit_slot_count)
        return {};
    auto& runtime = models.offline->runtime_state(slot);
    if (!runtime.instance)
        return {};
    auto& instance = runtime.instance->model();
    auto& state = unit_draw_state(models.units, slot, runtime.instance_generation);
    const oa::Unit& record = world.record.units[slot];
    return {
        &instance,
        &model_render::prepare_model(models.library, instance.model_handle()),
        &state,
        &record,
        oa::world_unit_def_of(&world.record, &record)
    };
}

/// Returns the prepared model of a unit type's model known only by address.
///
/// Debris, shatter fragments and the shatter loader carry a unit's model by
/// address; the loaded unit type the unit was made from owns it. An entry the
/// library holds for the live model is returned; otherwise the model is
/// prepared through that type's handle.
///
/// @param[in,out] library the match's model library
/// @param types the match's loaded unit types
/// @param model a live model
/// @return the prepared model, or null when no loaded type owns the model
const model_render::PreparedModel* prepared_type_model(
    model_render::ModelLibrary& library,
    std::span<const oa::sim::unit_spawn::LoadedType> types,
    const oa::formats::objects3d::Model& model
) {
    if (const auto* prepared = model_render::find_prepared_model(library, model))
        return prepared;
    for (const auto& type : types)
        if (type.model.get() == &model)
            return &model_render::prepare_model(library, type.model);
    return nullptr;
}

model_render::ModelRef carried_model(void* user, const oa::Unit& unit) {
    auto* models = static_cast<MatchModels*>(user);
    const auto& world = models->offline->world().record;
    return unit_model(*models, static_cast<uint16_t>(oa::world_unit_slot(&world, &unit)));
}

void include(oa::Rect32& region, int32_t x, int32_t y, int32_t width, int32_t height) {
    const int32_t left = region.x1;
    const int32_t top = region.y1;
    const int32_t right = region.x2;
    const int32_t bottom = region.y2;
    region.x1 = std::min(left, x);
    region.y1 = std::min(top, y);
    region.x2 = std::max(right, x + width);
    region.y2 = std::max(bottom, y + height);
}

// Bridge region a unit draw can touch: its model, cached image and ground
// shadow, from the last transforms plus a margin.
oa::Rect32 unit_region(
    const model_render::ModelRenderer& renderer, const model_render::ModelRef& model, int32_t ground
) {
    const oa::Unit& unit = *model.unit;
    const int32_t x = high_word(wrapping_sub(unit.position.x, camera_fixed(renderer.camera_x))) +
                      renderer.origin_x;
    const int32_t z = high_word(wrapping_sub(unit.position.z, camera_fixed(renderer.camera_y))) +
                      renderer.origin_y;
    const int32_t unit_y = z - (high_word(unit.position.y) >> 1);
    const int32_t shadow_x = x + model_render::shadow_offset_x;
    const int32_t shadow_y = z - (ground >> 1);
    oa::Rect32 region{x, unit_y, x, unit_y};
    const auto frame = model_render::measure_model_bounds(*model.instance, nullptr);
    include(region, x - frame.origin_x, unit_y - frame.origin_y, frame.width, frame.height);
    include(
        region, shadow_x - frame.origin_x, shadow_y - frame.origin_y, frame.width, frame.height
    );
    const auto shear = model_render::measure_shadow_bounds(*model.instance);
    include(
        region, shadow_x - shear.origin_x, shadow_y - shear.origin_y, shear.width, shear.height
    );
    for (const oa::Sprite* sprite : {&model.state->image.sprite, &model.state->shadow.sprite}) {
        if (sprite->data == nullptr)
            continue;
        include(
            region, x - sprite->origin_x, unit_y - sprite->origin_y, sprite->width, sprite->height
        );
        include(
            region,
            shadow_x - sprite->origin_x,
            shadow_y - sprite->origin_y,
            sprite->width,
            sprite->height
        );
    }
    return {
        region.x1 - kModelRegionMargin,
        region.y1 - kModelRegionMargin,
        region.x2 + kModelRegionMargin,
        region.y2 + kModelRegionMargin
    };
}

// Selection boxes go to the bridge: the overlay projects about the
// game's battlefield corner, the bridge's 8-bit view starts at it, and
// each line captures the tiles it crosses before drawing.
void bridge_line(
    void* user, oa::Surface*, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color
) {
    auto& bridge = *static_cast<model_render::RgbBridge*>(user);
    x0 -= oa::present::world_renderer::battlefield_origin_x;
    x1 -= oa::present::world_renderer::battlefield_origin_x;
    y0 -= oa::present::world_renderer::battlefield_origin_y;
    y1 -= oa::present::world_renderer::battlefield_origin_y;
    model_render::bridge_open(
        bridge, {std::min(x0, x1), std::min(y0, y1), std::max(x0, x1), std::max(y0, y1)}
    );
    oa::present::draw_clipped_line(&bridge.surface, x0, y0, x1, y1, color);
}

} // namespace

const oa::formats::gaf::RenderedFrame*
Runtime::feature_sequence_image(oa_ref32 sequence, uint16_t frame) {
    if (sequence == 0 || sequence > feature_assets_.sequences.size())
        return nullptr;
    auto& rendered = feature_assets_.rendered[sequence - 1];
    if (rendered.empty())
        for (const auto& source : feature_assets_.sequences[sequence - 1]->frames) {
            auto image = oa::formats::gaf::render_normal(source);
            rendered.push_back(
                image.ok() ? std::move(*image.frame) : oa::formats::gaf::RenderedFrame{}
            );
        }
    return frame < rendered.size() && !rendered[frame].pixels.empty() ? &rendered[frame] : nullptr;
}

MatchModels& Runtime::match_models() {
    if (match_models_ && match_models_->match == match_.get())
        return *match_models_;
    match_models_ = std::make_shared<MatchModels>();
    auto& fresh = *match_models_;
    fresh.match = match_.get();
    fresh.offline = match_.get();
    // Feature draw states belong to the renderer that began them, as the
    // unit states in `fresh` do.
    for (auto& feature : match_features_)
        feature.state = {};
    fresh.library.textures = model_render::load_texture_library(assets_);
    model_render::build_model_display(
        fresh.display, oa::present::palette_from_bytes(match_palette_)
    );
    oa::present::copy_alpha_table(fresh.display.context, display_.context.alpha_table);
    model_render::init_composite_buffer(fresh.renderer);
    fresh.renderer.user = &fresh;
    fresh.renderer.ground_height = terrain_height;
    fresh.renderer.model_of = carried_model;
    fresh.feature_unit.flags = OA_UNIT_FLAG_VIEWPOINT_OWNED | OA_UNIT_FLAG_BUILDING;
    fresh.feature_unit.flags2 = OA_UNIT_FLAG2_Z_BUFFER;
    // The first record of the type table Game.unit_defs refers to. The
    // World keeps that table in unit_defs, so until the match binds
    // Game.unit_defs the table start is record 0.
    const auto& game = match_->world().record.game;
    fresh.feature_unit.def = game.unit_defs != 0 ? game.unit_defs : oa::oa_ref_from_index(0);
    if (model_light_)
        std::copy(model_light_->begin(), model_light_->end(), std::begin(fresh.renderer.light));
    if (const auto* shadow = gaf_sequence(match_fx_, "shadow");
        shadow != nullptr && !shadow->frames.empty() && shadow->frames.front().layers.empty() &&
        shadow->frames.front().pixels.size() ==
            static_cast<std::size_t>(shadow->frames.front().width) *
                shadow->frames.front().height) {
        const auto& frame = shadow->frames.front();
        fresh.shadow_pixels = frame.pixels;
        fresh.projectile_shadow.width = frame.width;
        fresh.projectile_shadow.height = frame.height;
        fresh.projectile_shadow.origin_x = frame.origin_x;
        fresh.projectile_shadow.origin_y = frame.origin_y;
        fresh.projectile_shadow.key = frame.transparency_index;
        fresh.projectile_shadow.data = fresh.shadow_pixels.data();
    }
    return fresh;
}

void Runtime::blit_gaf_blended_hotspot(
    oa::present::world_renderer::Surface& destination,
    const oa::formats::gaf::RenderedFrame& frame,
    const oa::present::world_renderer::ScreenPoint& screen,
    float scale,
    MatchModels& models
) {
    const auto& display = models.display.context;
    if ((display.flags & oa::present::display_flag_alpha_table) == 0)
        return;
    if (scale <= 0.0F)
        scale = 1.0F;
    const auto left =
        screen.x - static_cast<int>(std::lround(static_cast<double>(frame.origin_x) * scale));
    const auto top =
        screen.y - static_cast<int>(std::lround(static_cast<double>(frame.origin_y) * scale));
    const auto dest_w =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.width) * scale)));
    const auto dest_h =
        std::max(1, static_cast<int>(std::lround(static_cast<double>(frame.height) * scale)));
    const auto& palette = models.display.palette;
    for (int row = 0; row < dest_h; ++row) {
        const int y = top + row;
        if (y < world_pixel_clip_.y || y >= world_pixel_clip_.y + world_pixel_clip_.h || y < 0 ||
            y >= static_cast<int>(destination.height))
            continue;
        const auto source_row =
            static_cast<std::size_t>(row) * frame.height / static_cast<std::size_t>(dest_h);
        for (int column = 0; column < dest_w; ++column) {
            const int x = left + column;
            if (x < world_pixel_clip_.x || x >= world_pixel_clip_.x + world_pixel_clip_.w ||
                x < 0 || x >= static_cast<int>(destination.width))
                continue;
            const auto source_column =
                static_cast<std::size_t>(column) * frame.width / static_cast<std::size_t>(dest_w);
            const auto offset = source_row * frame.width + source_column;
            if (offset >= frame.coverage.size() || frame.coverage[offset] == 0)
                continue;
            auto* pixel =
                destination.rgb.data() +
                (static_cast<std::size_t>(y) * destination.width + static_cast<std::size_t>(x)) *
                    3U;
            const uint8_t under =
                model_render::bridge_index(models.bridge, pixel[0], pixel[1], pixel[2]);
            const auto& blended =
                palette
                    .entries[display.alpha_table
                                 [static_cast<std::size_t>(frame.pixels[offset]) * 0x100 + under]];
            pixel[0] = blended.r;
            pixel[1] = blended.g;
            pixel[2] = blended.b;
        }
    }
}

void Runtime::release_model_images() {
    if (!match_models_)
        return;
    for (auto& tracked : match_models_->units) {
        tracked.state.image = {};
        tracked.state.shadow = {};
    }
    for (auto& feature : match_features_) {
        feature.state.image = {};
        feature.state.shadow = {};
    }
}

std::size_t Runtime::cached_model_images() const {
    if (!match_models_)
        return 0;
    std::size_t count = 0;
    for (const auto& tracked : match_models_->units)
        count += tracked.state.image.sprite.data != nullptr ? 1 : 0;
    for (const auto& feature : match_features_)
        count += feature.state.image.sprite.data != nullptr ? 1 : 0;
    return count;
}

uint8_t Runtime::match_palette_index(uint8_t r, uint8_t g, uint8_t b) {
    return model_render::bridge_index(match_models().bridge, r, g, b);
}

const oa::present::DisplayContext& Runtime::match_display_context() {
    return match_models().display.context;
}

void Runtime::set_match_model_light(int32_t x, int32_t y, int32_t z) {
    auto& renderer = match_models().renderer;
    model_render::set_model_light(renderer, x, y, z);
    model_light_ = {renderer.light[0], renderer.light[1], renderer.light[2]};
}

int32_t Runtime::loaded_match_primitives(
    const oa::formats::objects3d::Model& model,
    uint32_t object,
    std::vector<oa::sim::effect_particles::PiecePrimitive>& out
) {
    out.clear();
    const auto* loaded =
        prepared_type_model(match_models().library, loaded_commander_types_, model);
    if (loaded == nullptr)
        return -1;
    const auto& prepared = loaded->objects.at(object);
    for (const auto& primitive : prepared.primitives) {
        const auto& source = model.objects[object].primitives[primitive.source_index];
        out.push_back(
            {source.color_index,
             static_cast<uint32_t>(source.vertex_indices.size()),
             source.vertex_indices.data(),
             primitive.flags}
        );
    }
    return prepared.skips_first ? 0 : -1;
}

std::optional<std::size_t>
Runtime::greyed_picture_frame(const oa::ui::gui_layout::Gadget& gadget) const {
    constexpr uint32_t last_frame_attribute = 0x100U;
    constexpr uint32_t first_frame_attributes = 0x1800U;
    const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
    if (button == nullptr || !button->grayed_out || button->stages != 0)
        return std::nullopt;
    const auto* sequence = gaf_sequence(match_hud_->sprites, gadget.common.name);
    if (sequence == nullptr)
        sequence = gaf_sequence(match_hud_->shared_sprites, gadget.common.name);
    if (sequence == nullptr || sequence->frames.empty())
        return std::nullopt;
    const std::size_t last = sequence->frames.size() - 1;
    const auto attributes = static_cast<uint32_t>(gadget.common.attributes);
    if ((attributes & last_frame_attribute) != 0)
        return last;
    if ((attributes & first_frame_attributes) != 0)
        return 0;
    return std::min(static_cast<std::size_t>(std::max<int16_t>(button->status, 0)) + 2U, last);
}

void Runtime::render_match_surface() {
    if (!match_ || !selected_tnt_)
        throw std::logic_error("match renderer requires an initialized offline match");
    mark_profile(OA_PROFILE_MISC);
    sync_match_wrecks();
    const auto map_width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
    const auto bf_w = match_layout_.battlefield_width();
    const auto bf_h = match_layout_.battlefield_height();
    const auto vis_w = visible_map_width();
    const auto vis_h = visible_map_height();
    const auto camera_x =
        static_cast<uint32_t>(std::clamp(match_camera_x_, 0, std::max(0, map_width - vis_w)));
    const auto camera_y =
        static_cast<uint32_t>(std::clamp(match_camera_z_, 0, std::max(0, map_height - vis_h)));
    match_camera_x_ = static_cast<int32_t>(camera_x);
    match_camera_z_ = static_cast<int32_t>(camera_y);
    ensure_radar_surfaces();
    bind_match_view();
    // The frame's on-screen list, after the ticks and before the drawing: the
    // pointer and the under-attack notice test the list the frame drawn last
    // built.
    rebuild_on_screen_units();
    track_match_drag();
    auto viewport = live_viewport(camera_x, camera_y);
    match_use_layers_ = sdl_.renderer != nullptr && !options_.headless_check;
    renderer::Surface hud;
    if (match_hud_) {
        std::vector<renderer::ButtonPresentation> presentation;
        for (std::size_t index = 0; index < match_hud_->layout.gadgets.size(); ++index) {
            if (const auto frame = greyed_picture_frame(match_hud_->layout.gadgets[index])) {
                presentation.push_back(
                    {match_hud_->layout.gadgets[index].common.name,
                     renderer::ButtonCondition::disabled,
                     frame,
                     std::nullopt,
                     std::nullopt}
                );
                continue;
            }
            // The order page's status buttons draw a grayed state instead of hiding.
            const auto status_frame = match_status_frame(index);
            auto condition = renderer::ButtonCondition::normal;
            const auto* grayed_button = std::get_if<oa::ui::gui_layout::ButtonFields>(
                &match_hud_->layout.gadgets[index].fields
            );
            if (!status_frame && grayed_button != nullptr && grayed_button->grayed_out)
                // A grayed-out menu button, such as SAVEGAME when the game
                // cannot save, shows grayed rather than hidden.
                condition = renderer::ButtonCondition::disabled;
            else if (
                // Build page navigation is on a unit's build pages, never on
                // a paused menu (PREFS.GUI's OK is PREV).
                !status_frame &&
                ((!pause_menu_shown() &&
                  is_build_page_nav(match_hud_->layout.gadgets[index].common.name) &&
                  builder_gui_page_count() <= 1) ||
                 (match_hud_action(match_hud_->layout.gadgets[index].common.name) == "MISSION" &&
                  !campaign_mission_) ||
                 !gadget_command_available(match_hud_->layout.gadgets[index]))
            )
                condition = renderer::ButtonCondition::hidden;
            else if (hovered_ == index || match_command_lit(index))
                condition = renderer::ButtonCondition::pressed;
            // A multi-stage button (RESTART.GUI's Difficulty) shows its stage.
            const auto& gadget = match_hud_->layout.gadgets[index];
            std::optional<std::size_t> stage;
            if (const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields);
                button != nullptr && button->stages > 1) {
                if (const auto found = widget_text_stages_.find(gadget.common.name);
                    found != widget_text_stages_.end())
                    stage = found->second;
            }
            presentation.push_back(
                {gadget.common.name,
                 condition,
                 status_frame ? status_frame : stage,
                 stage,
                 std::nullopt}
            );
        }
        // A team panel's logos, row icons and recipient list.
        std::vector<renderer::ListPresentation> lists;
        present_team_panel(presentation, lists);
        const auto hud_start = std::chrono::steady_clock::now();
        hud = renderer::render_screen(*match_hud_, presentation, lists);
        phase_times_.hud += std::chrono::duration_cast<std::chrono::nanoseconds>(
                                std::chrono::steady_clock::now() - hud_start
        )
                                .count();
    } else if (
        match_chrome_.width == static_cast<uint32_t>(kCanvasWidth) &&
        match_chrome_.height == static_cast<uint32_t>(kCanvasHeight) &&
        match_chrome_.rgb.size() == static_cast<std::size_t>(kCanvasWidth * kCanvasHeight * 3)
    )
        hud = {match_chrome_.width, match_chrome_.height, match_chrome_.rgb};
    else
        hud = {
            static_cast<uint32_t>(kCanvasWidth),
            static_cast<uint32_t>(kCanvasHeight),
            std::vector<uint8_t>(static_cast<std::size_t>(kCanvasWidth * kCanvasHeight * 3), 0)
        };
    const auto terrain_pixels =
        static_cast<std::size_t>(bf_w) * static_cast<std::size_t>(bf_h) * 3U;
    if (match_terrain_cache_.width != static_cast<uint32_t>(bf_w) ||
        match_terrain_cache_.height != static_cast<uint32_t>(bf_h) ||
        match_terrain_cache_.rgb.size() != terrain_pixels) {
        match_terrain_cache_.width = static_cast<uint32_t>(bf_w);
        match_terrain_cache_.height = static_cast<uint32_t>(bf_h);
        match_terrain_cache_.rgb.resize(terrain_pixels);
        terrain_cache_cam_x_ = ~0u;
    }
    if (terrain_cache_cam_x_ != camera_x || terrain_cache_cam_y_ != camera_y ||
        std::abs(terrain_cache_zoom_ - match_zoom()) > 1.0e-4F) {
        if (auto error = oa::present::world_renderer::fill_scaled_viewport(
                *selected_tnt_,
                match_palette_,
                camera_x,
                camera_y,
                static_cast<uint32_t>(bf_w),
                static_cast<uint32_t>(bf_h),
                match_zoom(),
                match_terrain_cache_.rgb.data(),
                static_cast<uint32_t>(bf_w)
            ))
            throw std::runtime_error("cannot render match terrain: " + error->message);
        terrain_cache_cam_x_ = camera_x;
        terrain_cache_cam_y_ = camera_y;
        terrain_cache_zoom_ = match_zoom();
    }
    // The world layer is the battlefield alone; the HUD stays in 640x480
    // source space until presentation (or compose_match_frame) scales it.
    match_hud_cpu_ = std::move(hud);
    match_world_cpu_.width = static_cast<uint32_t>(bf_w);
    match_world_cpu_.height = static_cast<uint32_t>(bf_h);
    match_world_cpu_.rgb.resize(terrain_pixels);
    std::memcpy(match_world_cpu_.rgb.data(), match_terrain_cache_.rgb.data(), terrain_pixels);
    viewport.destination_x = 0;
    viewport.destination_y = 0;
    viewport.surface_width = static_cast<uint32_t>(bf_w);
    viewport.surface_height = static_cast<uint32_t>(bf_h);
    world_pixel_clip_ = {0, 0, bf_w, bf_h};
    oa::present::world_renderer::Surface world_surface{
        match_world_cpu_.width, match_world_cpu_.height, std::move(match_world_cpu_.rgb)
    };
    const auto cull_margin =
        std::max(256, static_cast<int>(std::lround(128.0 * static_cast<double>(match_zoom()))));
    const auto on_battlefield = [&](int x, int y) {
        return x >= viewport.destination_x - cull_margin &&
               x < viewport.destination_x + bf_w + cull_margin &&
               y >= viewport.destination_y - cull_margin &&
               y < viewport.destination_y + bf_h + cull_margin;
    };
    sync_dead_feature_draws();
    // Model name of each weapon registry slot (TDF `model`), read once.
    const auto weapon_model = [&](
                                  MatchModels& models, uint8_t index
                              ) -> const std::shared_ptr<const oa::formats::objects3d::Model>& {
        if (!models.weapon_names_loaded) {
            models.weapon_names_loaded = true;
            for (const auto& path : assets_.list_effective("weapons", ".tdf")) {
                const auto bytes = read(path);
                if (!bytes)
                    continue;
                const std::string_view text(
                    reinterpret_cast<const char*>(bytes->data()), bytes->size()
                );
                const auto document = oa::data::unit_definitions::parse_tdf(text);
                if (!document)
                    continue;
                for (const auto& section : document.value.sections) {
                    const auto* id = section.find("id");
                    const auto* name = section.find("model");
                    if (id == nullptr || name == nullptr || name->empty())
                        continue;
                    const auto slot = std::strtoul(std::string(*id).c_str(), nullptr, 10);
                    if (slot <= 0xff)
                        models.weapon_model_names.emplace(static_cast<uint8_t>(slot), *name);
                }
            }
        }
        if (const auto found = models.weapon_models.find(index);
            found != models.weapon_models.end())
            return found->second;
        std::shared_ptr<const oa::formats::objects3d::Model> model;
        if (const auto name = models.weapon_model_names.find(index);
            name != models.weapon_model_names.end()) {
            try {
                const auto bytes = assets_.read("objects3d/" + name->second + ".3do").bytes;
                const auto* begin = reinterpret_cast<const std::byte*>(bytes.data());
                model = std::make_shared<const oa::formats::objects3d::Model>(
                    oa::formats::objects3d::load_3do({begin, bytes.size()})
                );
            } catch (const std::exception&) {
            }
        }
        return models.weapon_models.emplace(index, std::move(model)).first->second;
    };
    // Projectile render types 1, 3 and 6: the projectile's ground shadow,
    // then its model; a missile also draws its first child
    // (the flame or propeller) while it still has flight time.
    const auto draw_projectile_models = [&](MatchModels& models) {
        auto& renderer = models.renderer;
        for (const auto& shot : match_->projectiles()) {
            const auto* weapon = match_->projectile_weapon(shot);
            if (weapon == nullptr || shot.burst_remaining != 0)
                continue;
            const auto type = weapon->rendertype;
            if (type != kRenderMissile && type != kRenderBomb && type != kRenderModel)
                continue;
            const auto shot_position = oa::sim::match_runtime::fixed_words(shot.position);
            try {
                if (!match_->point_visible(
                        static_cast<uint8_t>(match_view_player()), shot_position
                    ))
                    continue;
            } catch (const std::exception&) {
                continue;
            }
            const auto& model = weapon_model(models, weapon->registry_index);
            if (model == nullptr || model->objects.empty())
                continue;
            const auto& prepared = model_render::prepare_model(models.library, model);
            const oa::formats::objects3d::FixedVector3 position{
                wrapping_sub(shot.position.x, camera_fixed(renderer.camera_x)),
                shot.position.y,
                wrapping_sub(shot.position.z, camera_fixed(renderer.camera_y))
            };
            const int32_t x = high_word(position.x) + renderer.origin_x;
            const int32_t z = high_word(position.z) + renderer.origin_y;
            const int32_t y = z - (high_word(position.y) >> 1);
            // As Projectile.plot_height: the terrain height under the shot.
            const int32_t shadow_y =
                z - (match_->map_height(shot_position[0], shot_position[2]) >> 1);
            oa::Rect32 region{
                x - kProjectileReach,
                y - kProjectileReach,
                x + kProjectileReach,
                y + kProjectileReach
            };
            include(
                region,
                x - kProjectileReach,
                shadow_y - kProjectileReach,
                2 * kProjectileReach,
                2 * kProjectileReach
            );
            model_render::bridge_open(models.bridge, region);
            if (models.projectile_shadow.data != nullptr)
                oa::present::draw_sprite_blended(
                    &models.bridge.surface, &models.projectile_shadow, x, shadow_y
                );
            // Type 3 draws unrotated.
            oa::sim::model_runtime::RotationWords rotation{};
            if (type == kRenderMissile)
                rotation = {
                    0,
                    static_cast<int16_t>(shot.heading + kHalfTurn),
                    static_cast<int16_t>(shot.pitch + kHalfTurn)
                };
            else if (type == kRenderModel)
                rotation = {
                    0, static_cast<int16_t>(shot.heading), static_cast<int16_t>(shot.pitch)
                };
            model_render::draw_projectile_model(
                renderer,
                &models.bridge.surface,
                position,
                model->objects[0],
                prepared.objects[0],
                rotation
            );
            const auto child = model->objects[0].first_child;
            if (type != kRenderMissile || child == oa::formats::objects3d::kNoObject ||
                child >= model->objects.size() ||
                static_cast<int32_t>(shot.lifetime_tick) <= static_cast<int32_t>(renderer.tick))
                continue;
            // This match keeps no spin angle for a propeller: it draws with no roll.
            if ((weapon->flags & OA_WEAPON_FLAG_PROPELLER) != 0)
                rotation.xy = 0;
            model_render::draw_projectile_model(
                renderer,
                &models.bridge.surface,
                position,
                model->objects[child],
                prepared.objects[child],
                rotation
            );
        }
    };
    auto& models = match_models();
    auto& world_record = match_->world().record;
    auto& renderer = models.renderer;
    renderer.world = &world_record;
    renderer.graphics_flags = world_record.game.graphics_flags;
    renderer.tick = match_->simulation().tick;
    renderer.camera_x = static_cast<int32_t>(viewport.source_x);
    renderer.camera_y = static_cast<int32_t>(viewport.source_y);
    renderer.origin_x = 0;
    renderer.origin_y = 0;
    // Each player's team colour: the owner's PlayerSetupInfo.color.
    for (std::size_t player = 0; player < std::size(renderer.team_colors); ++player) {
        const auto* owner = oa::world_player(&world_record, static_cast<uint32_t>(player));
        const auto* info = owner != nullptr ? oa::world_player_info(&world_record, owner) : nullptr;
        renderer.team_colors[player] = info != nullptr ? info->color : 0;
    }
    if (!models.animation_started || renderer.tick < models.animation_tick) {
        models.animation_started = true;
        models.animation_tick = renderer.tick;
    }
    // Entries of models freed since the last frame go, with their texture
    // animations, before the live ones step.
    model_render::release_expired_models(models.library);
    for (uint32_t step = 0; models.animation_tick < renderer.tick; ++models.animation_tick)
        if (step++ < kMaxAnimationSteps)
            model_render::step_texture_animations(models.library);
    const auto scale = viewport.scale == 0.0F ? 1.0F : viewport.scale;
    const model_render::RgbFrame rgb_frame{
        world_surface.rgb.data(),
        static_cast<int32_t>(world_surface.width),
        static_cast<int32_t>(world_surface.height),
        static_cast<int32_t>(world_surface.width) * 3
    };
    const oa::Rect32 bridge_area{
        viewport.destination_x,
        viewport.destination_y,
        viewport.destination_x + bf_w - 1,
        viewport.destination_y + bf_h - 1
    };
    oa::present::DisplayContext* bound_display = oa::present::display_context();
    oa::present::bind_display(&models.display.context);
    draw_match_debug_grid(models.bridge, models.display, rgb_frame, bridge_area, scale);
    mark_profile(OA_PROFILE_RENDER_STATIC);
    model_render::bridge_begin(
        models.bridge, rgb_frame, bridge_area, scale, models.display.palette
    );
    // Models draw into the bridge and sprites straight into the RGB frame,
    // so the bridge's pixels go back to the frame before a sprite draws over
    // them, and the next model captures the frame again.
    bool bridge_holds_draws = false;
    const auto commit_models = [&] {
        if (bridge_holds_draws)
            model_render::bridge_end(models.bridge);
        bridge_holds_draws = false;
    };
    const auto draw_object_feature = [&](MatchFeatureDraw& feature) {
        // The placed record's object, rotation words and position
        // go to the stand-in unit, which then draws like any unit.
        models.feature_unit.bank = feature.rotation.xy;
        models.feature_unit.heading = static_cast<uint16_t>(feature.rotation.xz);
        models.feature_unit.pitch = feature.rotation.yz;
        models.feature_unit.position = {feature.position.x, feature.position.y, feature.position.z};
        const model_render::ModelRef model{
            &feature.instance,
            &model_render::prepare_model(models.library, feature.instance.model_handle()),
            &feature.state,
            &models.feature_unit,
            oa::world_unit_def(&world_record, models.feature_unit.def)
        };
        model_render::note_piece_changes(model);
        model_render::bridge_open(
            models.bridge,
            unit_region(renderer, model, terrain_height(&models, models.feature_unit.position))
        );
        model_render::draw_linked_model(renderer, &models.bridge.surface, model, false);
        bridge_holds_draws = true;
    };
    advance_gaf_feature_anims(match_->simulation().tick);
    // plan_feature_draw picks each sprite feature's frames, its
    // shadow first while FeatureShadows is on, and which blend through the
    // alpha table; a record playing a burn, die or reclamate sequence shows
    // the frames it has reached.
    const auto anim_frame = [&](std::size_t index,
                                bool first) -> const oa::formats::gaf::RenderedFrame* {
        if (index >= match_gaf_anims_.size() || match_gaf_anims_[index].frames.empty())
            return nullptr;
        const auto& anim = match_gaf_anims_[index];
        return first ? &anim.frames.front()
                     : &anim.frames[std::min(
                           static_cast<std::size_t>(anim.frame), anim.frames.size() - 1
                       )];
    };
    const auto draw_sprite_feature = [&](const MatchGafFeatureDraw& feature,
                                         const oa::ui::hud::FeatureDraw& plan,
                                         const oa::present::world_renderer::ScreenPoint& screen) {
        commit_models();
        const auto* plot = oa::world_plot(&world_record, feature.cell_x, feature.cell_z);
        const auto* record =
            plot != nullptr && (plot->flags & OA_PLOT_FLAG_ANIMATING_FEATURE) != 0
                ? oa::sim::feature_runtime::feature_record(world_record, plot->feature_record)
                : nullptr;
        for (int32_t index = 0; index < plan.sprite_count; ++index) {
            const auto& sprite = plan.sprites[index];
            const oa::formats::gaf::RenderedFrame* image = nullptr;
            if (sprite.frame == oa::ui::hud::FeatureFrame::placed) {
                if (record != nullptr) {
                    const auto& cursor =
                        sprite.shadow ? record->sprite.shadow : record->sprite.animation;
                    image = feature_sequence_image(cursor.sequence, cursor.frame);
                }
                // A record whose sequence is not loaded keeps the definition's body.
                if (image == nullptr && !sprite.shadow)
                    image = anim_frame(feature.anim, false);
            } else {
                image = anim_frame(
                    sprite.shadow ? feature.shadow_anim : feature.anim,
                    sprite.frame == oa::ui::hud::FeatureFrame::first
                );
            }
            if (image == nullptr)
                continue;
            if (sprite.translucent)
                blit_gaf_blended_hotspot(world_surface, *image, screen, viewport.scale, models);
            else
                blit_gaf_hotspot(world_surface, *image, screen, match_palette_, viewport.scale);
        }
    };
    oa::present::world_renderer::OverlayRaster selection_raster{};
    selection_raster.user = &models.bridge;
    selection_raster.line = bridge_line;
    const auto draw_unit = [&](uint16_t unit_index) {
        const auto model = unit_model(models, unit_index);
        if (model.instance == nullptr)
            return;
        // The movement state's speed: an idle mobile shows the first frame
        // of its animated textures.
        bool idle = false;
        try {
            if (const auto* movement = match_->ground_runtime(unit_index))
                idle = movement->movement.speed == 0;
        } catch (const std::exception&) {
        }
        // A selected unit's box is outlined before the unit draws.
        if ((model.unit->flags & OA_UNIT_FLAG_SELECTED) != 0 &&
            !model.instance->model().objects.empty())
            oa::present::world_renderer::overlay_selection_box(
                world_record.game,
                selection_raster,
                &models.bridge.surface,
                *model.unit,
                model.instance->model().objects.front()
            );
        model_render::note_piece_changes(model);
        model_render::bridge_open(
            models.bridge,
            unit_region(renderer, model, terrain_height(&models, model.unit->position))
        );
        model_render::draw_linked_model(renderer, &models.bridge.surface, model, idle);
        bridge_holds_draws = true;
    };
    // A record's shatter fragment, drawn into
    // the model bridge and written back before the record's sprite.
    const auto draw_fragment = [&](const oa::sim::effect_particles::ParticleDraw& item) {
        const auto& fragment = *item.fragment;
        const auto& look = fragment.look;
        if (look.model == nullptr)
            return;
        const auto* prepared =
            prepared_type_model(models.library, loaded_commander_types_, *look.model);
        if (prepared == nullptr || look.object >= prepared->objects.size() ||
            look.primitive >= prepared->objects[look.object].primitives.size())
            return;
        const oa::formats::objects3d::FixedVector3 position{
            wrapping_sub(item.position.x, camera_fixed(renderer.camera_x)),
            item.position.y,
            wrapping_sub(item.position.z, camera_fixed(renderer.camera_y))
        };
        const int32_t x = high_word(position.x) + renderer.origin_x;
        const int32_t y = high_word(position.z) - (high_word(position.y) >> 1) + renderer.origin_y;
        // A turned point stays within the sum of its magnitudes.
        int32_t reach = 0;
        for (const auto& point : fragment.points)
            reach = std::max(
                reach,
                std::abs(high_word(point.x)) + std::abs(high_word(point.y)) +
                    std::abs(high_word(point.z))
            );
        reach += kModelRegionMargin;
        oa::present::DisplayContext* outer_display = oa::present::display_context();
        oa::present::bind_display(&models.display.context);
        model_render::bridge_open(models.bridge, {x - reach, y - reach, x + reach, y + reach});
        model_render::draw_shatter_fragment(
            renderer,
            &models.bridge.surface,
            position,
            fragment,
            prepared->objects[look.object].primitives[look.primitive],
            {item.spin[0], item.spin[1], item.spin[2]}
        );
        model_render::bridge_end(models.bridge);
        oa::present::bind_display(outer_display);
    };
    // Explosion records and the effect layers. Flashes need
    // the shade table and are skipped.
    auto draw_effect = [&](const oa::sim::effect_particles::ParticleDraw& item) {
        if (item.kind == oa::sim::effect_particles::DrawKind::fragment) {
            draw_fragment(item);
            return;
        }
        const std::array<uint32_t, 3> at{
            static_cast<uint32_t>(item.position.x),
            static_cast<uint32_t>(item.position.y),
            static_cast<uint32_t>(item.position.z)
        };
        try {
            if (item.sight_gated && !match_->point_visible(match_view_player(), at))
                return;
        } catch (const std::exception&) {
            return;
        }
        const auto screen = project_match_point(viewport, at);
        const auto pal = static_cast<std::size_t>(item.color) * 4U;
        if (item.kind == oa::sim::effect_particles::DrawKind::pixel) {
            put_match_pixel(
                world_surface,
                screen.x,
                screen.y,
                {match_palette_[pal], match_palette_[pal + 1], match_palette_[pal + 2]}
            );
            return;
        }
        if (item.kind != oa::sim::effect_particles::DrawKind::sprite || item.sequence == nullptr ||
            item.frame < 0 || static_cast<std::size_t>(item.frame) >= item.sequence->frames.size())
            return;
        const auto& frame = item.sequence->frames[static_cast<std::size_t>(item.frame)];
        const auto rendered = oa::formats::gaf::render_normal(frame);
        if (rendered.ok())
            blit_gaf_hotspot(
                world_surface, *rendered.frame, screen, match_palette_, viewport.scale
            );
    };
    const auto visit = [](void* context, const oa::sim::effect_particles::ParticleDraw& item) {
        (*static_cast<decltype(draw_effect)*>(context))(item);
    };
    const auto draw_effect_layers = [&](uint16_t first, uint16_t last) {
        commit_models();
        for (uint16_t layer = first; layer <= last; ++layer)
            oa::sim::effect_particles::draw_layer(match_->effects(), layer, &draw_effect, visit);
    };

    // The features and units this frame draws, placed in the draw order: a
    // feature by the plot holding it, a unit by its depth.
    struct FeatureToDraw {
        MatchFeatureDraw* object{};
        const MatchGafFeatureDraw* sprite{};
        oa::ui::hud::FeatureDraw plan{};
        oa::present::world_renderer::ScreenPoint screen{};
    };

    const auto feature_height = [&](uint16_t feature_index) -> int8_t {
        return feature_index < world_record.feature_def_count
                   ? world_record.feature_defs[feature_index].height
                   : int8_t{0};
    };
    std::vector<FeatureToDraw> features_to_draw;
    std::vector<oa::present::world_renderer::FeatureDrawSite> feature_sites;
    for (auto& feature : match_features_) {
        if (feature_hidden_by_fog(feature.feature_index, feature.cell_x, feature.cell_z))
            continue;
        const auto feature_screen = project_match_point(
            viewport,
            {static_cast<uint32_t>(feature.position.x),
             static_cast<uint32_t>(feature.position.y),
             static_cast<uint32_t>(feature.position.z)}
        );
        if (!on_battlefield(feature_screen.x, feature_screen.y))
            continue;
        features_to_draw.push_back({&feature, nullptr, {}, feature_screen});
        feature_sites.push_back(
            {feature.cell_x, feature.cell_z, feature_height(feature.feature_index)}
        );
    }
    for (const auto& feature : match_gaf_features_) {
        if (feature_hidden_by_fog(feature.feature_index, feature.cell_x, feature.cell_z))
            continue;
        const auto plan =
            oa::ui::hud::plan_feature_draw(world_record, feature.cell_x, feature.cell_z);
        if (plan.object)
            continue;
        const auto screen = project_match_point(
            viewport,
            {static_cast<uint32_t>(feature.position.x),
             static_cast<uint32_t>(feature.position.y),
             static_cast<uint32_t>(feature.position.z)}
        );
        if (!on_battlefield(screen.x, screen.y))
            continue;
        features_to_draw.push_back({nullptr, &feature, plan, screen});
        feature_sites.push_back(
            {feature.cell_x, feature.cell_z, feature_height(feature.feature_index)}
        );
    }
    std::vector<uint16_t> units_to_draw;
    std::vector<oa::present::world_renderer::UnitDrawSite> unit_sites;
    for (auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || slot.record.type_index == 0)
            continue;
        try {
            if (!match_->unit_visible(static_cast<uint8_t>(match_view_player()), slot.unit_index))
                continue;
        } catch (const std::exception&) {
            continue;
        }
        const auto unit_screen = project_match_point(viewport, slot.unit->position);
        if (!on_battlefield(unit_screen.x, unit_screen.y))
            continue;
        const auto model = unit_model(models, slot.unit_index);
        if (model.instance == nullptr)
            continue;
        units_to_draw.push_back(slot.unit_index);
        unit_sites.push_back({model.unit->position.z, model.unit->flags});
    }
    oa::present::world_renderer::BattlefieldDrawPlan draw_plan;
    oa::present::world_renderer::plan_battlefield_draws(
        feature_sites, unit_sites, renderer.camera_y, draw_plan
    );
    const auto draw_feature = [&](uint32_t index) {
        auto& feature = features_to_draw[index];
        if (feature.object != nullptr)
            draw_object_feature(*feature.object);
        else
            draw_sprite_feature(*feature.sprite, feature.plan, feature.screen);
    };
    // The battlefield draws far to near: effect layers 0 to 2 (wakes among
    // them), the lying features, layers 3 and 4, then row by row the ground
    // units and the standing features, layers 5 and 6, the projectiles,
    // debris and explosions, layer 7, the units off the ground, layer 8, the
    // order overlays and layer 9 (smoke) over everything.
    draw_effect_layers(0, 2);
    for (const auto index : draw_plan.lying_features)
        draw_feature(index);
    draw_effect_layers(3, 4);
    for (const auto& draw : draw_plan.ground) {
        if (draw.kind == oa::present::world_renderer::DrawnThing::unit)
            draw_unit(units_to_draw[draw.index]);
        else
            draw_feature(draw.index);
    }
    draw_effect_layers(5, 6);
    draw_projectile_models(models);
    bridge_holds_draws = true;
    commit_models();
    draw_match_projectiles(world_surface, viewport);
    // Nano streams are the type-6 particles; the beam line is a debug aid.
    const auto debug_beams = options_.debug_order_lines
                                 ? match_->nano_lasers()
                                 : std::vector<oa::sim::match_runtime::Match::NanoLaser>{};
    for (const auto& beam : debug_beams) {
        const auto from = project_match_point(viewport, beam.from);
        const std::array<uint32_t, 3> dest{
            static_cast<uint32_t>(beam.to_origin[0] + beam.to_extent[0] / 2),
            static_cast<uint32_t>(beam.to_origin[1] + beam.to_extent[1] / 2),
            static_cast<uint32_t>(beam.to_origin[2] + beam.to_extent[2] / 2)
        };
        const auto to = project_match_point(viewport, dest);
        const auto pal = static_cast<std::size_t>(0xa1) * 4U;
        const std::array<uint8_t, 3> color =
            pal + 2 < match_palette_.size()
                ? std::array<
                      uint8_t,
                      3>{match_palette_[pal], match_palette_[pal + 1], match_palette_[pal + 2]}
                : std::array<uint8_t, 3>{80, 255, 80};
        draw_match_line(world_surface, from.x, from.y, to.x, to.y, color);
    }
    // The battlefield rectangle the load screen sets, over the visible map area.
    const oa::sim::effect_particles::ExplosionView explosion_view{
        renderer.camera_x,
        renderer.camera_y,
        {oa::sim::effect_particles::battlefield_screen_x,
         oa::sim::effect_particles::battlefield_screen_y,
         oa::sim::effect_particles::battlefield_screen_x + vis_w - 1,
         oa::sim::effect_particles::battlefield_screen_y + vis_h - 1}
    };
    // The in-bounds pass starts with the debris pieces: each piece's object is turned
    // by its spin and drawn at its origin, culled on
    // that origin, in its unit's team colour.
    std::vector<oa::formats::objects3d::FixedVector3> debris_points;
    const oa::Rect32 debris_view{0, 0, vis_w - 1, vis_h - 1};
    auto draw_debris_piece = [&](const oa::sim::effect_particles::DebrisPiece& piece) {
        if (piece.model == nullptr || piece.object >= piece.model->objects.size())
            return;
        const auto& object = piece.model->objects[piece.object];
        const auto* prepared =
            prepared_type_model(models.library, loaded_commander_types_, *piece.model);
        if (prepared == nullptr)
            return;
        const auto* owner = oa::world_unit_at(&world_record, piece.unit);
        const uint8_t team = renderer.team_colors[(owner != nullptr ? owner->owner_index : 0) % 10];
        const int32_t x =
            high_word(wrapping_sub(piece.position.x, camera_fixed(renderer.camera_x))) +
            renderer.origin_x;
        const int32_t y =
            high_word(wrapping_sub(piece.position.z, camera_fixed(renderer.camera_y))) -
            (high_word(piece.position.y) >> 1) + renderer.origin_y;
        // A turned point stays within the sum of its magnitudes.
        int32_t reach = 0;
        for (const auto& vertex : object.vertices)
            reach = std::max(
                reach,
                std::abs(high_word(vertex.x)) + std::abs(high_word(vertex.y)) +
                    std::abs(high_word(vertex.z))
            );
        reach += kModelRegionMargin;
        oa::present::DisplayContext* outer_display = oa::present::display_context();
        oa::present::bind_display(&models.display.context);
        model_render::bridge_open(models.bridge, {x - reach, y - reach, x + reach, y + reach});
        model_render::draw_rotated_debris(
            renderer,
            &models.bridge.surface,
            debris_view,
            object,
            prepared->objects[piece.object],
            {piece.spin[2], piece.spin[1], piece.spin[0]},
            {piece.position.x, piece.position.y, piece.position.z},
            team,
            debris_points
        );
        model_render::bridge_end(models.bridge);
        oa::present::bind_display(outer_display);
    };
    // As with the texture animations, a tick drawn more than once starts the
    // debris particles on its first draw only.
    const bool start_debris_particles = !models.debris_drawn || models.debris_tick != renderer.tick;
    models.debris_drawn = true;
    models.debris_tick = renderer.tick;
    oa::sim::effect_particles::draw_debris(
        match_->effects(),
        world_record.game,
        match_->effect_host(),
        start_debris_particles,
        &draw_debris_piece,
        [](void* context, const oa::sim::effect_particles::DebrisPiece& piece) {
            (*static_cast<decltype(draw_debris_piece)*>(context))(piece);
        }
    );
    oa::sim::effect_particles::draw_explosions(
        match_->effects(), explosion_view, &draw_effect, visit
    );
    draw_effect_layers(7, 7);
    for (const auto index : draw_plan.raised_units)
        draw_unit(units_to_draw[index]);
    draw_effect_layers(8, 8);
    // The overlay pass draws the local player's order overlays while Shift is
    // held (asked of the keyboard, not the pointer word).
    if (control_key_down(oa::ui::gui_input::ControlKey::shift))
        (void)draw_order_overlays(world_surface, viewport);
    draw_effect_layers(9, 9);
    oa::present::bind_display(bound_display);
    mark_profile(OA_PROFILE_RENDER_STUFF);
    apply_match_fog(
        world_surface,
        camera_x,
        camera_y,
        viewport.destination_x,
        viewport.destination_y,
        bf_w,
        bf_h
    );
    // The HUD overlay outlines the build site or the drag box after the fog tiles.
    draw_build_ghost(world_surface, viewport);
    draw_selection_band(world_surface, viewport);
    mark_profile(OA_PROFILE_RENDER_FOG);
    match_world_cpu_ = {world_surface.width, world_surface.height, std::move(world_surface.rgb)};
    // The HUD overlay: resource bars, bottom panel, then radar at
    // Game.radar_offset_x and radar_offset_y, then the GUI's child gadgets last.
    paint_on(PaintLayer::hud);
    blit_match_minimap();
    // The HUD overlay then clips to the game view and draws the status strip.
    draw_status_panel();
    if (selected_match_unit_ == 0 && !match_paused_)
        fill_source_rect(0, 128, 128, 352, 10);
    paint_on(PaintLayer::battlefield);
    for (const auto& slot : match_->world().slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || slot.record.type_index == 0 ||
            slot.unit->type == nullptr)
            continue;
        try {
            if (slot.owner_index != match_view_player() &&
                !match_->unit_visible(static_cast<uint8_t>(match_view_player()), slot.unit_index))
                continue;
        } catch (const std::exception&) {
            continue;
        }
        const auto screen = project_match_point(viewport, slot.unit->position);
        const int bar_x = screen.x;
        const int bar_y = screen.y + 10;
        if (const auto& world = match_->state();
            oa::ui::hud::draws_health_bar(world, slot.record)) {
            const auto* def = oa::world_unit_def_of(&world, &slot.record);
            oa::ui::hud::HealthBar bar{};
            if (def != nullptr &&
                oa::ui::hud::unit_health_bar(world.game, slot.record, *def, bar_x, bar_y, bar)) {
                const auto fill = [this](const oa::Rect32& rect, uint8_t color) {
                    fill_hud_rect(
                        rect.x1, rect.y1, rect.x2 - rect.x1 + 1, rect.y2 - rect.y1 + 1, color
                    );
                };
                fill(bar.trough, bar.trough_color);
                fill(bar.fill, bar.fill_color);
            }
        }
        if (slot.unit->squad != 0) {
            // The squad digit (Unit.squad) of a unit in a squad, beside its bars.
            draw_match_label(bar_x - 4, bar_y + 4, std::to_string(slot.unit->squad), 255);
        }
        if (const auto count = match_->self_destruct_remaining(slot.unit_index); count != 0)
            draw_match_label(bar_x - 4, bar_y - 12, std::to_string(count), 1);
    }
    paint_on(PaintLayer::hud);
    // The unit panel shows the unit under the cursor alone, never the
    // selection.
    draw_unit_panel();
    draw_resource_readout();
    draw_build_captions();
    if (extension_.draw_match_hud != nullptr)
        extension_.draw_match_hud(extension_.context, *this);
    draw_chat_entry();
    paint_on(PaintLayer::battlefield);
    draw_match_kill_board();
    draw_chat_overlay();
    draw_extension_overlay();
    draw_unit_info_panel();
    draw_profile_bars();
    // The outcome, the paused title (a menu's hold or the pause bit) and the
    // menus over the battlefield.
    draw_end_overlay();
    outcome_frame_drawn_ = match_finished_;
    overlay_target_ = nullptr;
    hud_source_space_ = false;
    paint_origin_ = {};
    if (!match_use_layers_)
        compose_match_frame(surface_);
}

} // namespace oa::app
