// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Composition of the match battlefield frame.
#include "oa/app/runtime.hpp"
#include "director_state.hpp"
#include "match_models.hpp"
#include "oa/app/match_model_draws.hpp"
#include "presentation_interpolation.hpp"
#include "world_draws.hpp"
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

namespace {

namespace model_render = oa::present::model;
namespace unit_playout = oa::present::unit_playout;

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

/// Grows the unit slots' draw states (MatchModels::units) to hold every slot
/// with a model instance, as unit_model would grow them slot by slot.
///
/// @param[in,out] models the match's renderer state
void hold_unit_draw_states(MatchModels& models) {
    const uint32_t slots = models.offline->world().record.unit_slot_count;
    uint32_t highest = 0;
    for (uint32_t slot = 1; slot < slots; ++slot)
        if (models.offline->runtime_state(static_cast<uint16_t>(slot)).instance)
            highest = slot;
    if (models.units.size() <= highest)
        models.units.resize(static_cast<std::size_t>(highest) + 1);
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

/// Tells whether a draw between ticks shows a unit slot otherwise than its
/// tick does: the unit moved, turned or moved a piece since the tick before.
///
/// @param models the match's renderer state, its tick noted
/// @param slot unit slot
/// @return true when the slot's unit moved
bool unit_moved(const MatchModels& models, uint32_t slot) {
    const auto& units = models.presentation.units;
    return slot < units.size() && units[slot].seen && units[slot].moved;
}

/// Tells whether a unit or a unit it carries moved since the tick before.
///
/// @param models the match's renderer state, its tick noted
/// @param world the match's World
/// @param slot the unit's slot
/// @return true when the unit or one it carries moved
bool unit_or_cargo_moved(const MatchModels& models, const oa::World& world, uint16_t slot) {
    if (unit_moved(models, slot))
        return true;
    const oa::Unit* unit = oa::world_unit_at(&world, slot);
    if (unit == nullptr)
        return false;
    for (const oa::Unit* child = oa::world_unit(&world, unit->attach_first_child); child != nullptr;
         child = oa::world_unit(&world, child->attach_next))
        if (unit_moved(models, oa::world_unit_slot(&world, child)))
            return true;
    return false;
}

/// Returns the copies a unit draws from part of the way through the current
/// tick: its record and model instance, placed between its two poses when it
/// moved, with a draw state of their own. A unit of another machine's player
/// is placed, turned and tilted on its playout (mirrored_pose), whole ticks
/// included, and its pieces are placed between their two poses even when its
/// records moved it by a jump. The match's record, instance and draw state
/// are only read.
///
/// The copies are placed once a draw; a later call of the same draw returns
/// them as they are.
///
/// @param[in,out] models the match's renderer state, its tick noted
/// @param slot unit slot of a live unit with a model instance
/// @return the copies' model
model_render::ModelRef presented_unit(MatchModels& models, uint16_t slot) {
    auto& world = models.offline->world();
    auto& presentation = models.presentation;
    auto& runtime = models.offline->runtime_state(slot);
    if (presentation.units.size() <= slot || !runtime.instance)
        return unit_model(models, slot);
    auto& motion = presentation.units[slot];
    const oa::Unit& record = world.record.units[slot];
    if (motion.blended_draw != presentation.draw) {
        motion.record = record;
        motion.instance = runtime.instance->model();
        const auto pose = mirrored_pose(
            models, world.record, slot, playout_moment(presentation, presentation.fraction)
        );
        if (motion.moved)
            blend_unit_pose(
                motion.previous,
                motion.current,
                presentation.fraction,
                motion.record,
                motion.instance
            );
        else if (pose && motion.pieces_moved)
            // Placed on its playout, its pieces move between their ticks
            // however far its records moved it.
            blend_unit_pieces(
                motion.previous, motion.current, presentation.fraction, motion.instance
            );
        if (pose) {
            motion.record.position = pose->position;
            motion.record.heading = pose->heading;
            motion.record.pitch = pose->pitch;
            motion.record.bank = pose->bank;
        }
        // The pieces were set without the setters, which would mark the
        // transforms for rebuilding.
        motion.state.transforms_dirty = true;
        motion.blended_draw = presentation.draw;
    }
    return {
        &motion.instance,
        &model_render::prepare_model(models.library, motion.instance.model_handle()),
        &motion.state,
        &motion.record,
        oa::world_unit_def_of(&world.record, &record)
    };
}

model_render::ModelRef carried_model(void* user, const oa::Unit& unit) {
    auto* models = static_cast<MatchModels*>(user);
    const auto& world = models->offline->world().record;
    const auto slot = static_cast<uint16_t>(oa::world_unit_slot(&world, &unit));
    // A carrier drawn from its copies carries its units' copies.
    if (models->presentation.blending)
        return presented_unit(*models, slot);
    return unit_model(*models, slot);
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

/// Tells whether a unit's owner is another machine's player: in use with
/// OA_PLAYER_STATUS_MIRRORED. Only such a player's records move its units;
/// the units of a player in use but free or closed stay where the
/// simulation has them.
///
/// @param world the match's World
/// @param unit the unit
/// @return true for a unit of a mirrored player
bool mirrored_owner(const oa::World& world, const oa::Unit& unit) noexcept {
    if (unit.owner_index >= OA_PLAYER_COUNT)
        return false;
    const oa::Player& owner = world.game.players[unit.owner_index];
    return owner.in_use != 0 && owner.status == OA_PLAYER_STATUS_MIRRORED;
}

/// Draws the frame's battlefield draws (MatchModels::draws) band by band.
///
/// The bridge is split into one band for each thread the pool runs a job
/// on (bridge_split), and the bands draw at once on the pool, or the one
/// band covering the frame draws on the calling thread alone. The first band
/// draws with the models' renderer and buffers; each other band with its
/// own (MatchModels::band_scratch), the frame's renderer settings copied in,
/// and its own memory of colours outside the palette. A band that fails
/// lets the others finish; the failure of the first band in band order that
/// failed is then thrown on the calling thread.
///
/// @param[in,out] models the match's renderer state, its draws worked out
/// @param frame what every band draws with
/// @param pool the drawing threads; null draws on the calling thread alone
void draw_world_bands(
    MatchModels& models, const WorldFrameDraw& frame, oa::platform::job_pool::Pool* pool
) {
    const auto threads =
        pool != nullptr
            ? static_cast<int32_t>(std::min(pool->threads(), oa::platform::job_pool::max_threads))
            : 1;
    const int32_t made = model_render::bridge_split(models.bridge, threads, models.bands);
    models.most_bands = std::max(models.most_bands, made);
    if (models.band_scratch.size() + 1 < static_cast<std::size_t>(made))
        models.band_scratch.resize(static_cast<std::size_t>(made) - 1);
    for (int32_t band = 1; band < made; ++band) {
        auto& scratch = models.band_scratch[static_cast<std::size_t>(band) - 1];
        model_render::copy_renderer_settings(models.renderer, scratch.renderer);
        model_render::bridge_band_colours(
            models.bridge, models.bands[static_cast<std::size_t>(band)]
        );
    }
    // Each band notes its own failure; the first band's that failed is
    // thrown, whichever thread failed first.
    std::array<std::string, oa::platform::job_pool::max_threads> failures{};
    oa::platform::job_pool::run_bands(
        made > 1 ? pool : nullptr, static_cast<uint32_t>(made), [&](uint32_t band) noexcept {
            std::string& failure = failures[band];
            try {
                if (band == 0) {
                    draw_world_band(
                        models.draws,
                        frame,
                        models.bands.front(),
                        models.renderer,
                        models.supersample,
                        models.debris_points
                    );
                    return;
                }
                auto& scratch = models.band_scratch[band - 1];
                draw_world_band(
                    models.draws,
                    frame,
                    models.bands[band],
                    scratch.renderer,
                    scratch.supersample,
                    scratch.debris_points
                );
            } catch (const std::exception& error) {
                failure = error.what()[0] != '\0' ? error.what() : "a band failed";
            } catch (...) {
                failure = "a band failed";
            }
        }
    );
    for (auto& band : models.bands)
        model_render::bridge_join_band(models.bridge, band);
    for (int32_t band = 0; band < made; ++band)
        if (!failures[static_cast<std::size_t>(band)].empty())
            throw std::runtime_error(
                "cannot draw the battlefield: " + failures[static_cast<std::size_t>(band)]
            );
}

} // namespace

unit_playout::FrameTime
playout_moment(const MatchPresentation& presentation, uint32_t fraction) noexcept {
    return unit_playout::frame_time_between(
        presentation.tick - presentation.batch, presentation.tick, fraction
    );
}

std::optional<unit_playout::UnitPose> mirrored_pose(
    const MatchModels& models, const oa::World& world, uint16_t slot, unit_playout::FrameTime moment
) noexcept {
    const auto* playout = models.presentation.playout;
    if (playout == nullptr || !models.presentation.seen)
        return std::nullopt;
    const oa::Unit* unit = oa::world_unit_at(&world, slot);
    if (unit == nullptr || unit->type_index == 0 || !mirrored_owner(world, *unit))
        return std::nullopt;
    const oa::Unit* carrier =
        unit->attach_parent != 0 ? oa::world_unit(&world, unit->attach_parent) : nullptr;
    if (carrier == nullptr)
        return playout->unit_pose(slot, moment);
    // A carried unit goes where its carrier is drawn.
    const auto carrier_pose =
        mirrored_owner(world, *carrier)
            ? playout->unit_pose(
                  static_cast<uint32_t>(oa::world_unit_slot(&world, carrier)), moment
              )
            : std::nullopt;
    if (!carrier_pose)
        return std::nullopt;
    // Moved by as much as its carrier is drawn away from the carrier's place.
    const auto moved_with = [](int32_t at, int32_t carrier_at, int32_t carrier_drawn) {
        return wrapping_sub(at, wrapping_sub(carrier_at, carrier_drawn));
    };
    unit_playout::UnitPose moved{};
    moved.position = {
        moved_with(unit->position.x, carrier->position.x, carrier_pose->position.x),
        moved_with(unit->position.y, carrier->position.y, carrier_pose->position.y),
        moved_with(unit->position.z, carrier->position.z, carrier_pose->position.z)
    };
    moved.heading = unit->heading;
    moved.pitch = unit->pitch;
    moved.bank = unit->bank;
    return moved;
}

FixedVec3 shown_unit_position(
    const MatchModels& models, const oa::World& world, uint16_t slot, uint32_t fraction
) {
    if (const auto pose =
            mirrored_pose(models, world, slot, playout_moment(models.presentation, fraction)))
        return pose->position;
    if (fraction < whole_tick && unit_moved(models, slot)) {
        const auto& motion = models.presentation.units[slot];
        return blend_point(motion.previous.position, motion.current.position, fraction);
    }
    const oa::Unit* unit = oa::world_unit_at(&world, slot);
    return unit != nullptr ? unit->position : FixedVec3{};
}

FixedVec3 shown_unit_position(const MatchModels& models, const oa::World& world, uint16_t slot) {
    return shown_unit_position(models, world, slot, models.presentation.fraction);
}

void observe_match_tick(MatchModels& models, oa::sim::match_runtime::Match& match) {
    const uint32_t tick = match.simulation().tick;
    auto& presentation = models.presentation;
    if (!presentation.seen || presentation.tick != tick) {
        const uint32_t ticks = presentation.seen ? batch_ticks(presentation.tick, tick) : 0;
        presentation.batch = std::max<uint32_t>(ticks, 1);
        presentation.tick = tick;
        presentation.seen = true;
    }
    auto& world = match.world();
    const uint32_t slots = world.record.unit_slot_count;
    if (presentation.units.size() < slots)
        presentation.units.resize(slots);
    for (uint32_t slot = 1; slot < slots; ++slot) {
        auto& motion = presentation.units[slot];
        const oa::Unit& record = world.record.units[slot];
        auto* runtime =
            record.type_index != 0 ? &match.runtime_state(static_cast<uint16_t>(slot)) : nullptr;
        if (runtime == nullptr || !runtime->instance) {
            if (motion.seen)
                forget_unit(motion);
            continue;
        }
        observe_unit(
            motion, tick, runtime->instance_generation, record, runtime->instance->model()
        );
    }
    observe_shots(presentation.shots, tick, match.projectiles());
    const auto& effects = match.effects();
    observe_debris(presentation.debris, tick, effects.debris);
}

void Runtime::place_tracking_camera() {
    if (screen_ != Screen::match || match_paused_ || !match_ || !match_tracking_ || director_mode())
        return;
    const auto& world = match_->world().record;
    if (oa::world_unit_at(&world, tracked_match_unit_) == nullptr)
        return;
    auto& models = match_models();
    observe_match_tick(models, *match_);
    const FixedVec3 shown = shown_unit_position(
        models, world, tracked_match_unit_, tick_fraction(presentation_alpha())
    );
    set_camera_position(
        high_word(shown.x) - visible_map_width() / 2,
        high_word(shown.z) - visible_map_height() / 2,
        0
    );
}

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
    // The units of other machines' players are drawn on their playout.
    fresh.presentation.playout = &unit_playout_;
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

void Runtime::release_model_images() {
    if (!match_models_)
        return;
    // Images drawn finer (enhanced anti-aliasing) go with the game's.
    for (auto& tracked : match_models_->units) {
        tracked.state.image = {};
        tracked.state.shadow = {};
        tracked.state.finer.reset();
    }
    for (auto& motion : match_models_->presentation.units) {
        motion.state.image = {};
        motion.state.shadow = {};
        motion.state.finer.reset();
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
    for (const auto& motion : match_models_->presentation.units)
        count += motion.state.image.sprite.data != nullptr ? 1 : 0;
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
    // Director mode draws from the director's camera, which it keeps on the
    // map itself, and without the interface unless its presentation asks
    // for it; its draws never start debris particles.
    const bool directed = director_ != nullptr;
    const bool bare = directed && !director_->presentation.show_interface;
    const auto map_width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
    const auto bf_w = match_layout_.battlefield_width();
    const auto bf_h = match_layout_.battlefield_height();
    const auto vis_w = visible_map_width();
    const auto vis_h = visible_map_height();
    const auto camera_x = static_cast<uint32_t>(
        directed ? std::max(0, match_camera_x_)
                 : std::clamp(match_camera_x_, 0, std::max(0, map_width - vis_w))
    );
    const auto camera_y = static_cast<uint32_t>(
        directed ? std::max(0, match_camera_z_)
                 : std::clamp(match_camera_z_, 0, std::max(0, map_height - vis_h))
    );
    match_camera_x_ = static_cast<int32_t>(camera_x);
    match_camera_z_ = static_cast<int32_t>(camera_y);
    // The frame's place between the ticks. Each draw notes the tick the
    // match is on first, so that the poses of the tick before are at hand.
    auto& models = match_models();
    observe_match_tick(models, *match_);
    auto& presentation = models.presentation;
    presentation.fraction = tick_fraction(presentation_alpha());
    // The moment the units of other machines' players are drawn at on their
    // playout, which the pointer then picks against.
    presentation.drawn_moment = playout_moment(presentation, presentation.fraction);
    const auto moment = presentation.drawn_moment;

    // Outside a draw, every unit shows where its tick puts it, a mirrored
    // unit where its playout has it at the tick.
    struct WholeTickAfter {
        MatchPresentation& presentation;

        explicit WholeTickAfter(MatchPresentation& shown) : presentation(shown) {}

        WholeTickAfter(const WholeTickAfter&) = delete;
        WholeTickAfter& operator=(const WholeTickAfter&) = delete;

        ~WholeTickAfter() { presentation.fraction = whole_tick; }
    } whole_tick_after{presentation};

    const bool between_ticks = presentation.fraction < whole_tick;
    // Each draw places the copies it draws from afresh: a mirrored unit draws
    // from them on whole ticks too.
    ++presentation.draw;
    {
        const uint32_t tick = match_->simulation().tick;
        const auto shots = match_->projectiles();
        presentation.presented_shots.resize(shots.size());
        for (std::size_t index = 0; index < shots.size(); ++index)
            presentation.presented_shots[index] = presented_shot(
                presentation.shots, tick, index, shots[index], presentation.fraction
            );
    }
    ensure_radar_surfaces();
    if (directed)
        bind_director_view();
    else
        bind_match_view();
    // The frame's on-screen list, after the ticks and before the drawing: the
    // pointer and the under-attack notice test the list the frame drawn last
    // built. In director mode the notice tests the director's view instead,
    // so that it does not hang on which frames were drawn.
    rebuild_on_screen_units();
    if (directed)
        offline_services_.set_on_screen_test(&DirectorState::unit_in_view, &match_->state());
    else
        track_match_drag();
    auto viewport = live_viewport(camera_x, camera_y);
    match_use_layers_ = sdl_.renderer != nullptr && !options_.headless_check;
    renderer::Surface hud;
    if (bare) {
        // No interface: the HUD layer stays empty.
    } else if (match_hud_) {
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
            else if (
                // The pointer over a button leaves it as it is: a button shows
                // pressed while a press on it is held with the pointer still
                // over it, or while its order is lit.
                (match_hud_held_ == index && hovered_ == index) || match_command_lit(index)
            )
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
            // The button's quick key is underlined in its caption, and the
            // record holding the keyboard focus is ringed while the match's
            // panels take the keyboard.
            if (const auto* button = std::get_if<oa::ui::gui_layout::ButtonFields>(&gadget.fields))
                presentation.back().quick_key = static_cast<char>(button->quick_key);
            presentation.back().focused =
                match_panels_keyboard_ && static_cast<int32_t>(index) == match_hud_focus_;
        }
        // A team panel's logos, row icons and recipient list.
        std::vector<renderer::ListPresentation> lists;
        present_team_panel(presentation, lists);
        const auto hud_start = std::chrono::steady_clock::now();
        hud = renderer::render_screen(*match_hud_, presentation, lists);
        if (auto* scrolls = hud_scrolls()) {
            renderer::refresh_layout_scrolls(*scrolls, match_hud_->layout);
            renderer::draw_layout_scrolls(
                hud,
                renderer::grayed_paint(*match_hud_, hud_gray_table_),
                &hud_own_art_,
                match_hud_->sprites,
                *scrolls,
                0,
                0
            );
        }
        // A placed dialog's panels below go under it as it is composed.
        if (match_hud_placement_ == 0)
            compose_panels_below(hud);
        place_preferences_rows(hud);
        compose_match_dialog(hud);
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
    // A director's camera redraws the terrain at any change of zoom, however
    // small, so that what a frame shows never hangs on the frames before it.
    if (terrain_cache_cam_x_ != camera_x || terrain_cache_cam_y_ != camera_y ||
        std::abs(terrain_cache_zoom_ - match_zoom()) > 1.0e-4F ||
        (directed && terrain_cache_zoom_ != match_zoom())) {
        if (auto error = oa::present::world_renderer::fill_scaled_viewport(
                *selected_tnt_,
                match_palette_,
                camera_x,
                camera_y,
                static_cast<uint32_t>(bf_w),
                static_cast<uint32_t>(bf_h),
                match_zoom(),
                match_terrain_cache_.rgb.data(),
                static_cast<uint32_t>(bf_w),
                draw_pool_.get()
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
    // The frame's battlefield draws, worked out in order before any is drawn
    // (world_draws.hpp): everything drawing builds or changes on the way,
    // the piece transforms, the units' cached images, the texture
    // animations, the decoded particle frames, is done here, once, and every
    // band then draws the list.
    auto& draw_list = models.draws;
    clear_world_draws(draw_list);
    const auto add_draw = [&draw_list](WorldDrawKind kind, std::size_t index) {
        add_world_draw(draw_list, kind, index);
    };
    // Projectile render types 1, 3 and 6: the projectile's ground shadow,
    // then its model; a missile also draws its first child
    // (the flame or propeller) while it still has flight time.
    const auto plan_projectile_models = [&](MatchModels& models) {
        auto& renderer = models.renderer;
        const auto shots = match_->projectiles();
        const auto& shown_shots = models.presentation.presented_shots;
        for (std::size_t index = 0; index < shots.size(); ++index) {
            const auto& shot = shots[index];
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
            // Where the shot shows in this frame (presented_shot).
            const ShotPose shown =
                shown_shots.size() == shots.size() ? shown_shots[index] : shot_pose(shot);
            const auto shown_position = oa::sim::match_runtime::fixed_words(shown.position);
            ProjectileDraw drawn{};
            drawn.position = {
                wrapping_sub(shown.position.x, camera_fixed(renderer.camera_x)),
                shown.position.y,
                wrapping_sub(shown.position.z, camera_fixed(renderer.camera_y))
            };
            const int32_t x = high_word(drawn.position.x) + renderer.origin_x;
            const int32_t z = high_word(drawn.position.z) + renderer.origin_y;
            const int32_t y = z - (high_word(drawn.position.y) >> 1);
            // As Projectile.plot_height: the terrain height under the shot.
            const int32_t shadow_y =
                z - (match_->map_height(shown_position[0], shown_position[2]) >> 1);
            drawn.region = {
                x - kProjectileReach,
                y - kProjectileReach,
                x + kProjectileReach,
                y + kProjectileReach
            };
            include(
                drawn.region,
                x - kProjectileReach,
                shadow_y - kProjectileReach,
                2 * kProjectileReach,
                2 * kProjectileReach
            );
            drawn.shadow = models.projectile_shadow.data != nullptr;
            drawn.x = x;
            drawn.shadow_y = shadow_y;
            // Type 3 draws unrotated.
            if (type == kRenderMissile)
                drawn.rotation = {
                    0,
                    static_cast<int16_t>(shown.heading + kHalfTurn),
                    static_cast<int16_t>(shown.pitch + kHalfTurn)
                };
            else if (type == kRenderModel)
                drawn.rotation = {
                    0, static_cast<int16_t>(shown.heading), static_cast<int16_t>(shown.pitch)
                };
            drawn.object = &model->objects[0];
            drawn.prepared = &prepared.objects[0];
            const auto child = model->objects[0].first_child;
            if (type == kRenderMissile && child != oa::formats::objects3d::kNoObject &&
                child < model->objects.size() &&
                static_cast<int32_t>(shot.lifetime_tick) > static_cast<int32_t>(renderer.tick)) {
                drawn.child = &model->objects[child];
                drawn.child_prepared = &prepared.objects[child];
                drawn.child_rotation = drawn.rotation;
                // This match keeps no spin angle for a propeller: it draws
                // with no roll.
                if ((weapon->flags & OA_WEAPON_FLAG_PROPELLER) != 0)
                    drawn.child_rotation.xy = 0;
            }
            draw_list.projectiles.push_back(drawn);
            add_draw(WorldDrawKind::projectile, draw_list.projectiles.size() - 1);
        }
    };

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
    // A unit slot's draw state is found by its place in the slot list,
    // which must not move while the frame's draws point into it: it grows
    // now to hold every slot with a model instance, which is every slot
    // the draws can reach (unit_model).
    hold_unit_draw_states(models);
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
    // them (a commit), and the next model captures the frame again.
    const auto plan_commit = [&] { add_draw(WorldDrawKind::commit, 0); };
    // A model readied and planned for the frame's draws.
    const auto add_model = [&](const model_render::ModelRef& model,
                               const oa::Rect32& region,
                               bool idle,
                               model_render::UnitSupersampling level,
                               int32_t stand_in) {
        draw_list.models.emplace_back();
        ModelDraw& drawn = draw_list.models.back();
        drawn.model = model;
        drawn.stand_in = stand_in;
        model_render::plan_unit_supersampled(
            renderer, models.bridge, model, region, idle, level, drawn.plan
        );
        if (stand_in >= 0)
            drawn.model.unit = nullptr;
        add_draw(WorldDrawKind::model, draw_list.models.size() - 1);
    };
    const auto plan_object_feature = [&](MatchFeatureDraw& feature) {
        // The placed record's object, rotation words and position
        // go to the stand-in unit, which then draws like any unit; the
        // frame's draws keep a copy of it as it draws this feature.
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
        draw_list.stand_ins.push_back(models.feature_unit);
        add_model(
            model,
            unit_region(renderer, model, terrain_height(&models, models.feature_unit.position)),
            false,
            model_render::UnitSupersampling::off,
            static_cast<int32_t>(draw_list.stand_ins.size() - 1)
        );
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
    const auto plan_sprite_feature = [&](const MatchGafFeatureDraw& feature,
                                         const oa::ui::hud::FeatureDraw& plan,
                                         const oa::present::world_renderer::ScreenPoint& screen) {
        plan_commit();
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
            draw_list.sprites.push_back({image, screen});
            add_draw(
                sprite.translucent ? WorldDrawKind::blended_sprite : WorldDrawKind::sprite,
                draw_list.sprites.size() - 1
            );
        }
    };
    // Selection boxes go to the bridge: the overlay projects about the
    // game's battlefield corner, the bridge's 8-bit view starts at it, and
    // each line captures the tiles it crosses before drawing.
    oa::present::world_renderer::OverlayRaster selection_raster{};
    selection_raster.user = &draw_list;
    selection_raster.line =
        [](
            void* user, oa::Surface*, int32_t x0, int32_t y0, int32_t x1, int32_t y1, uint8_t color
        ) {
            auto& list = *static_cast<WorldDrawList*>(user);
            constexpr int32_t left = oa::present::world_renderer::battlefield_origin_x;
            constexpr int32_t top = oa::present::world_renderer::battlefield_origin_y;
            list.lines.push_back({x0 - left, y0 - top, x1 - left, y1 - top, {}, color});
            add_world_draw(list, WorldDrawKind::selection_line, list.lines.size() - 1);
        };

    // While a unit draws from its copies, the units it carries draw from
    // theirs (carried_model).
    struct Blending {
        MatchPresentation& presentation;

        explicit Blending(MatchPresentation& shown) : presentation(shown) {
            presentation.blending = true;
        }

        Blending(const Blending&) = delete;
        Blending& operator=(const Blending&) = delete;

        ~Blending() { presentation.blending = false; }
    };

    // What the frame drew of the units, for "+stats" and a --frame-rate
    // run's frame log: each unit drawn, those drawn from copies placed
    // between two ticks, and where the unit the log follows was drawn.
    const auto note_unit_drawn = [&](uint16_t unit_index, const oa::Unit& drawn, bool blended) {
        ++frame_draws_.units_drawn;
        frame_draws_.units_between_ticks += blended ? 1 : 0;
        if (unit_index == frame_draws_.probe_unit) {
            frame_draws_.probe_drawn = true;
            frame_draws_.probe_x = drawn.position.x;
            frame_draws_.probe_z = drawn.position.z;
        }
    };
    // How finely units are drawn (enhanced anti-aliasing); director frames
    // draw them as without it.
    const auto unit_level = directed ? model_render::UnitSupersampling::off : unit_supersampling_;
    const auto plan_unit = [&](uint16_t unit_index) {
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
        // A unit of another machine's player draws from its copies on every
        // frame, whole ticks included: they stand on its playout, near its
        // simulated place and moved on ahead of it between records.
        const bool on_playout = mirrored_pose(models, world_record, unit_index, moment).has_value();
        if ((between_ticks && unit_or_cargo_moved(models, world_record, unit_index)) ||
            on_playout) {
            // The match reads the piece transforms a draw rebuilds, so they
            // are rebuilt as a draw of the whole tick rebuilds them, and the
            // unit draws from its copies placed between the ticks. The image
            // cache of the unit's own draw state waits for a draw of a whole
            // tick, which leaves it as it would have been.
            // A carried unit is drawn, and its transforms rebuilt, by its
            // carrier (draw_linked_model).
            const bool carried = model.unit->attach_parent != 0;
            model_render::note_piece_changes(model);
            if (!carried)
                model_render::update_linked_transforms(renderer, model);
            const Blending blending{presentation};
            const auto shown = presented_unit(models, unit_index);
            note_unit_drawn(unit_index, *shown.unit, true);
            if (!bare && (shown.unit->flags & OA_UNIT_FLAG_SELECTED) != 0 &&
                !shown.instance->model().objects.empty())
                oa::present::world_renderer::overlay_selection_box(
                    world_record.game,
                    selection_raster,
                    &models.bridge.surface,
                    *shown.unit,
                    shown.instance->model().objects.front()
                );
            model_render::note_piece_changes(shown);
            if (carried)
                return;
            model_render::update_linked_transforms(renderer, shown);
            add_model(
                shown,
                unit_region(renderer, shown, terrain_height(&models, shown.unit->position)),
                idle,
                unit_level,
                -1
            );
            return;
        }
        note_unit_drawn(unit_index, *model.unit, false);
        // A selected unit's box is outlined before the unit draws, unless the
        // interface is not drawn.
        if (!bare && (model.unit->flags & OA_UNIT_FLAG_SELECTED) != 0 &&
            !model.instance->model().objects.empty())
            oa::present::world_renderer::overlay_selection_box(
                world_record.game,
                selection_raster,
                &models.bridge.surface,
                *model.unit,
                model.instance->model().objects.front()
            );
        model_render::note_piece_changes(model);
        add_model(
            model,
            unit_region(renderer, model, terrain_height(&models, model.unit->position)),
            idle,
            unit_level,
            -1
        );
    };
    // A record's shatter fragment, drawn into
    // the model bridge and written back before the record's sprite.
    const auto plan_fragment = [&](const oa::sim::effect_particles::ParticleDraw& item) {
        const auto& fragment = *item.fragment;
        const auto& look = fragment.look;
        if (look.model == nullptr)
            return;
        const auto* prepared =
            prepared_type_model(models.library, loaded_commander_types_, *look.model);
        if (prepared == nullptr || look.object >= prepared->objects.size() ||
            look.primitive >= prepared->objects[look.object].primitives.size())
            return;
        // Part of the way through the ticks' flight and spin.
        const FixedVec3 at =
            point_along_step(item.position, item.motion, presentation.batch, presentation.fraction);
        FragmentDraw drawn{};
        drawn.spin = {item.spin[0], item.spin[1], item.spin[2]};
        if (between_ticks)
            drawn.spin = {
                angle_along_step(
                    item.spin[0], item.spin_motion[0], presentation.batch, presentation.fraction
                ),
                angle_along_step(
                    item.spin[1], item.spin_motion[1], presentation.batch, presentation.fraction
                ),
                angle_along_step(
                    item.spin[2], item.spin_motion[2], presentation.batch, presentation.fraction
                )
            };
        drawn.position = {
            wrapping_sub(at.x, camera_fixed(renderer.camera_x)),
            at.y,
            wrapping_sub(at.z, camera_fixed(renderer.camera_y))
        };
        const int32_t x = high_word(drawn.position.x) + renderer.origin_x;
        const int32_t y =
            high_word(drawn.position.z) - (high_word(drawn.position.y) >> 1) + renderer.origin_y;
        // A turned point stays within the sum of its magnitudes.
        int32_t reach = 0;
        for (const auto& point : fragment.points)
            reach = std::max(
                reach,
                std::abs(high_word(point.x)) + std::abs(high_word(point.y)) +
                    std::abs(high_word(point.z))
            );
        reach += kModelRegionMargin;
        drawn.region = {x - reach, y - reach, x + reach, y + reach};
        drawn.fragment = &fragment;
        drawn.primitive = &prepared->objects[look.object].primitives[look.primitive];
        draw_list.fragments.push_back(drawn);
        add_draw(WorldDrawKind::fragment, draw_list.fragments.size() - 1);
    };
    // Explosion records and the effect layers. Flashes need
    // the shade table and are skipped.
    auto plan_effect = [&](const oa::sim::effect_particles::ParticleDraw& item) {
        if (item.kind == oa::sim::effect_particles::DrawKind::fragment) {
            plan_fragment(item);
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
        // Part of the way through the ticks' steps.
        const auto screen = project_match_point(
            viewport,
            oa::sim::match_runtime::fixed_words(point_along_step(
                item.position, item.motion, presentation.batch, presentation.fraction
            ))
        );
        const auto pal = static_cast<std::size_t>(item.color) * 4U;
        if (item.kind == oa::sim::effect_particles::DrawKind::pixel) {
            const std::array<uint8_t, 3> color{
                match_palette_[pal], match_palette_[pal + 1], match_palette_[pal + 2]
            };
            const auto side = oa::present::world_renderer::screen_span(
                viewport, oa::sim::effect_particles::pixel_item_side
            );
            // The square, its top left corner at the point, clipped to the
            // frame before it is filled.
            draw_list.squares.push_back(
                {static_cast<int32_t>(std::max<int64_t>(screen.x, 0)),
                 static_cast<int32_t>(std::max<int64_t>(screen.y, 0)),
                 static_cast<int32_t>(
                     std::min<int64_t>(int64_t{screen.x} + side, world_surface.width)
                 ),
                 static_cast<int32_t>(
                     std::min<int64_t>(int64_t{screen.y} + side, world_surface.height)
                 ),
                 color}
            );
            add_draw(WorldDrawKind::pixel_square, draw_list.squares.size() - 1);
            return;
        }
        if (item.kind != oa::sim::effect_particles::DrawKind::sprite || item.sequence == nullptr ||
            item.frame < 0 || static_cast<std::size_t>(item.frame) >= item.sequence->frames.size())
            return;
        const auto* decoded =
            decoded_frame(draw_list, item.sequence->frames[static_cast<std::size_t>(item.frame)]);
        if (decoded == nullptr)
            return;
        draw_list.sprites.push_back({decoded, screen});
        add_draw(WorldDrawKind::sprite, draw_list.sprites.size() - 1);
    };
    const auto visit = [](void* context, const oa::sim::effect_particles::ParticleDraw& item) {
        (*static_cast<decltype(plan_effect)*>(context))(item);
    };
    const auto plan_effect_layers = [&](uint16_t first, uint16_t last) {
        plan_commit();
        for (uint16_t layer = first; layer <= last; ++layer)
            oa::sim::effect_particles::draw_layer(match_->effects(), layer, &plan_effect, visit);
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
        // A unit drawn on its playout is kept and ordered where it is drawn;
        // whether it is seen stays with its simulated place.
        const auto pose = mirrored_pose(models, world_record, slot.unit_index, moment);
        const auto unit_screen = project_match_point(
            viewport,
            pose ? oa::sim::match_runtime::fixed_words(pose->position) : slot.unit->position
        );
        if (!on_battlefield(unit_screen.x, unit_screen.y))
            continue;
        const auto model = unit_model(models, slot.unit_index);
        if (model.instance == nullptr)
            continue;
        units_to_draw.push_back(slot.unit_index);
        unit_sites.push_back({pose ? pose->position.z : model.unit->position.z, model.unit->flags});
    }
    oa::present::world_renderer::BattlefieldDrawPlan draw_plan;
    oa::present::world_renderer::plan_battlefield_draws(
        feature_sites, unit_sites, renderer.camera_y, draw_plan
    );
    const auto plan_feature = [&](uint32_t index) {
        auto& feature = features_to_draw[index];
        if (feature.object != nullptr)
            plan_object_feature(*feature.object);
        else
            plan_sprite_feature(*feature.sprite, feature.plan, feature.screen);
    };
    // The battlefield draws far to near: effect layers 0 to 2 (wakes among
    // them), the lying features, layers 3 and 4, then row by row the ground
    // units and the standing features, layers 5 and 6, the projectiles,
    // debris and explosions, layer 7, the units off the ground, then layers 8
    // and 9 (smoke); the fog goes over them all, and the order overlays over
    // the fog.
    plan_effect_layers(0, 2);
    for (const auto index : draw_plan.lying_features)
        plan_feature(index);
    plan_effect_layers(3, 4);
    for (const auto& draw : draw_plan.ground) {
        if (draw.kind == oa::present::world_renderer::DrawnThing::unit)
            plan_unit(units_to_draw[draw.index]);
        else
            plan_feature(draw.index);
    }
    plan_effect_layers(5, 6);
    plan_projectile_models(models);
    // Every captured tile goes back to the frame after the projectiles.
    add_draw(WorldDrawKind::commit_always, 0);
    plan_match_projectiles(draw_list, viewport);
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
        draw_list.lines.push_back({from.x, from.y, to.x, to.y, color, 0});
        add_draw(WorldDrawKind::line, draw_list.lines.size() - 1);
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
    const auto& debris_table = match_->effects().debris;
    auto plan_debris_piece = [&](const oa::sim::effect_particles::DebrisPiece& live) {
        // Part of the way through the tick's fall and spin, by the piece's slot.
        const auto slot = static_cast<std::size_t>(&live - std::begin(debris_table));
        const auto piece =
            presented_debris(presentation.debris, renderer.tick, slot, live, presentation.fraction);
        if (piece.model == nullptr || piece.object >= piece.model->objects.size())
            return;
        const auto& object = piece.model->objects[piece.object];
        const auto* prepared =
            prepared_type_model(models.library, loaded_commander_types_, *piece.model);
        if (prepared == nullptr)
            return;
        const auto* owner = oa::world_unit_at(&world_record, piece.unit);
        DebrisDraw drawn{};
        drawn.team = renderer.team_colors[(owner != nullptr ? owner->owner_index : 0) % 10];
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
        drawn.region = {x - reach, y - reach, x + reach, y + reach};
        drawn.object = &object;
        drawn.prepared = &prepared->objects[piece.object];
        drawn.spin = {piece.spin[2], piece.spin[1], piece.spin[0]};
        drawn.origin = {piece.position.x, piece.position.y, piece.position.z};
        draw_list.debris.push_back(drawn);
        add_draw(WorldDrawKind::debris, draw_list.debris.size() - 1);
    };
    // The pieces' particles start in the match's tick, never here.
    oa::sim::effect_particles::draw_debris(
        match_->effects(),
        &plan_debris_piece,
        [](void* context, const oa::sim::effect_particles::DebrisPiece& piece) {
            (*static_cast<decltype(plan_debris_piece)*>(context))(piece);
        }
    );
    oa::sim::effect_particles::draw_explosions(
        match_->effects(), explosion_view, &plan_effect, visit
    );
    plan_effect_layers(7, 7);
    for (const auto index : draw_plan.raised_units)
        plan_unit(units_to_draw[index]);
    plan_effect_layers(8, 8);
    plan_effect_layers(9, 9);
    // Every band of the frame draws the list: on the drawing threads at
    // once, or in order on this one.
    WorldFrameDraw frame_draw{};
    frame_draw.target = {
        world_surface.rgb.data(),
        static_cast<int32_t>(world_surface.width),
        static_cast<int32_t>(world_surface.height),
        world_pixel_clip_.x,
        world_pixel_clip_.y,
        world_pixel_clip_.w,
        world_pixel_clip_.h,
        0,
        static_cast<int32_t>(world_surface.height)
    };
    frame_draw.palette = &match_palette_;
    frame_draw.scale = viewport.scale;
    frame_draw.bridge = &models.bridge;
    frame_draw.display = &models.display;
    frame_draw.projectile_shadow = &models.projectile_shadow;
    frame_draw.debris_view = {0, 0, vis_w - 1, vis_h - 1};
    draw_world_bands(models, frame_draw, draw_pool_.get());
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
    // Over the fog tiles: the local player's order overlays while Shift is
    // held (asked of the keyboard, not the pointer word), so that orders
    // queued onto never-mapped or unseen ground stay in view (in 3.1c the
    // smoke and the fog cover them), then the HUD overlay's outline of the
    // build site or the drag box. The fog's profile category takes the time
    // of both.
    if (!bare) {
        if (control_key_down(oa::ui::gui_input::ControlKey::shift))
            (void)draw_order_overlays(world_surface, viewport);
        draw_build_ghost(world_surface, viewport);
        draw_selection_band(world_surface, viewport);
    }
    mark_profile(OA_PROFILE_RENDER_FOG);
    match_world_cpu_ = {world_surface.width, world_surface.height, std::move(world_surface.rgb)};
    // Without the interface the frame is the world alone: no panel, bar,
    // label, message, board or overlay is painted over it.
    if (bare) {
        overlay_target_ = nullptr;
        hud_source_space_ = false;
        paint_origin_ = {};
        return;
    }
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
        // The bars and digits stay with the unit where it shows.
        const bool shown_elsewhere =
            between_ticks ||
            mirrored_pose(models, world_record, slot.unit_index, moment).has_value();
        const auto screen = shown_elsewhere
                                ? project_match_point(
                                      viewport,
                                      oa::sim::match_runtime::fixed_words(
                                          shown_unit_position(models, world_record, slot.unit_index)
                                      )
                                  )
                                : project_match_point(viewport, slot.unit->position);
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
    // Director mode composes its own frame (draw_director_frame).
    if (!match_use_layers_ && !directed)
        compose_match_frame(surface_);
}

} // namespace oa::app
