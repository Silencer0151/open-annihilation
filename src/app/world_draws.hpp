// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The battlefield's draws of one frame, in the order they draw, so that the
// frame can be drawn in horizontal bands. The drawing thread works the list
// out first (Runtime::render_match_surface): everything drawing builds or
// changes on the way is done then, once, in the draws' order: the piece
// transforms, the units' cached images and silhouettes, the texture
// animations, the projectiles' models, the GAF frames decoded for the
// particles. Each band then draws the whole list with its own rows of the
// frame alone (draw_world_band), reading the list and the models and
// writing only its rows of the frame and of the model bridge, so that the
// bands give the frame drawn whole byte for byte, one after another or at
// the same time.
#pragma once

#include "oa/core/world.h"
#include "oa/formats/gaf.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/objects3d.hpp"
#include "oa/present/model/model_draw.hpp"
#include "oa/present/model/model_library.hpp"
#include "oa/present/model/rgb_bridge.hpp"
#include "oa/present/model/shadow_fade.hpp"
#include "oa/present/model/unit_supersampling.hpp"
#include "oa/present/world_renderer.hpp"
#include "oa/sim/effect_particles.hpp"
#include "oa/sim/model_runtime/instance.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <unordered_map>
#include <vector>

namespace oa::app {

/// The frame a draw of the battlefield writes, and what it may write: the
/// RGB battlefield, the visible world rectangle every draw clips to, and the
/// rows of the band being drawn.
struct WorldTarget {
    uint8_t* rgb{};        ///< packed RGB rows of `width` pixels
    int32_t width{};       ///< pixels in a row
    int32_t height{};      ///< rows
    int32_t clip_x{};      ///< the visible world rectangle's left column
    int32_t clip_y{};      ///< its top row
    int32_t clip_width{};  ///< its columns
    int32_t clip_height{}; ///< its rows
    /// The first row the draw may change. A line or sprite over the band's
    /// edge works out its pixels as over the whole frame and writes only
    /// those in the band.
    int32_t first_row{};
    int32_t end_row{}; ///< the row after the last one the draw may change
};

/// Writes one pixel of the battlefield frame, clipped to the visible world
/// rectangle, the frame and the band.
///
/// @param target the frame and what may be written
/// @param x frame column
/// @param y frame row
/// @param color RGB colour
void put_world_pixel(
    const WorldTarget& target, int x, int y, const std::array<uint8_t, 3>& color
) noexcept;

/// Draws a line on the battlefield frame, clipped to the visible world
/// rectangle first; only its pixels in the band are written.
///
/// Clipping before stepping keeps off-map endpoints (a build ghost under an
/// off-map cursor, say) from walking billions of steps or overflowing.
///
/// @param target the frame and what may be written
/// @param x0 first endpoint column
/// @param y0 first endpoint row
/// @param x1 second endpoint column
/// @param y1 second endpoint row
/// @param color RGB colour
void draw_world_line(
    const WorldTarget& target, int x0, int y0, int x1, int y1, const std::array<uint8_t, 3>& color
) noexcept;

/// Blits the covered pixels of a rendered GAF frame onto the battlefield frame, scaled nearest-pixel.
///
/// @param target the frame and what may be written
/// @param frame rendered frame
/// @param destination_x frame column of the frame's left edge
/// @param destination_y frame row of the frame's top edge
/// @param palette 4 bytes per colour
/// @param scale size factor; 0 or less draws at 1
/// @param shadow_level how much of the frame's colour each covered pixel
///     takes, in steps of 1 / oa::present::model::shadow_full_level
///     (oa::present::model::fade_shadow_channel): the frame's colour
///     alone at shadow_full_level, as every frame but a faded shadow draws
void blit_world_frame(
    const WorldTarget& target,
    const oa::formats::gaf::RenderedFrame& frame,
    int destination_x,
    int destination_y,
    const oa::PaletteBytes& palette,
    float scale,
    uint32_t shadow_level = oa::present::model::shadow_full_level
) noexcept;

/// Blits a rendered GAF frame onto the battlefield frame with its origin at a screen point.
///
/// @param target the frame and what may be written
/// @param frame rendered frame
/// @param screen frame point the GAF origin lands on
/// @param palette 4 bytes per colour
/// @param scale size factor; 0 or less draws at 1
/// @param shadow_level as blit_world_frame's
void blit_world_hotspot(
    const WorldTarget& target,
    const oa::formats::gaf::RenderedFrame& frame,
    const oa::present::world_renderer::ScreenPoint& screen,
    const oa::PaletteBytes& palette,
    float scale,
    uint32_t shadow_level = oa::present::model::shadow_full_level
) noexcept;

/// Blends a rendered GAF frame's covered pixels into the battlefield frame
/// through the display's alpha table, its origin at a screen point.
///
/// Each covered pixel takes table[source * 256 + destination], the
/// destination's palette index read back from the frame through the model
/// bridge's colour lookup, mixed with the frame's colour under it by a
/// shadow level. Nothing is drawn without an alpha table.
///
/// @param target the frame and what may be written
/// @param frame rendered frame
/// @param screen frame point the GAF origin lands on
/// @param scale size factor; 0 or less draws at 1
/// @param display the models' display: its alpha table and palette
/// @param[in,out] bridge the frame's model bridge, whose colour lookup remembers nearest entries
/// @param[in,out] band the band being drawn, whose own colour memory is used
///     when it has one; null for the bridge's
/// @param shadow_level how much of the blend each covered pixel takes, as
///     blit_world_frame's: the blend alone at shadow_full_level
void blit_world_blended_hotspot(
    const WorldTarget& target,
    const oa::formats::gaf::RenderedFrame& frame,
    const oa::present::world_renderer::ScreenPoint& screen,
    float scale,
    const oa::present::model::ModelDisplay& display,
    oa::present::model::RgbBridge& bridge,
    oa::present::model::BridgeBand* band,
    uint32_t shadow_level = oa::present::model::shadow_full_level
);

/// What one draw of the battlefield is; WorldDraw::index names it in the
/// list of its kind.
enum class WorldDrawKind : uint8_t {
    commit,         ///< the bridge's draws go to the frame, when it holds any
    commit_always,  ///< the bridge's captured tiles go to the frame
    pixel_square,   ///< a square of one colour (WorldDrawList::squares)
    sprite,         ///< a GAF frame in the palette's colours (WorldDrawList::sprites)
    blended_sprite, ///< a GAF frame blended through the alpha table (sprites)
    line,           ///< a line of one colour (WorldDrawList::lines)
    selection_line, ///< a selection box's line through the bridge (selection_lines)
    model,          ///< a unit or a 3D feature (WorldDrawList::models)
    projectile,     ///< a projectile's 3DO object (WorldDrawList::projectiles)
    debris,         ///< a debris piece (WorldDrawList::debris)
    fragment,       ///< a shatter fragment (WorldDrawList::fragments)
};

/// One draw of the battlefield.
struct WorldDraw {
    WorldDrawKind kind{};
    uint32_t index{}; ///< its place in its kind's list; unused for the commits
};

/// A particle's square, already clipped to the frame: the rows from `top` up
/// to `bottom` and the columns from `left` up to `right`.
struct SquareDraw {
    int32_t left{};
    int32_t top{};
    int32_t right{};
    int32_t bottom{};
    std::array<uint8_t, 3> color{};
};

/// A GAF frame drawn at a screen point.
struct SpriteDraw {
    const oa::formats::gaf::RenderedFrame* frame{};
    oa::present::world_renderer::ScreenPoint screen{};
    /// A feature's shadow frame, drawn as dark as the list's shadow_level.
    bool shadow{};
};

/// A line of the frame (WorldDrawKind::line) in RGB, or of the bridge
/// (WorldDrawKind::selection_line) in a palette index, in its pixels.
struct LineDraw {
    int32_t x0{};
    int32_t y0{};
    int32_t x1{};
    int32_t y1{};
    std::array<uint8_t, 3> color{}; ///< the RGB of a frame's line
    uint8_t palette_index{};        ///< the palette index of a bridge's line
};

/// A unit or a 3D feature, as plan_unit_supersampled planned it.
struct ModelDraw {
    oa::present::model::ModelRef model{};
    /// A 3D feature draws through a stand-in unit record
    /// (WorldDrawList::stand_ins), whose place is kept here: model.unit is
    /// set to it when the feature draws. -1 for a unit.
    int32_t stand_in{-1};
    oa::present::model::SupersampledUnitPlan plan{};
};

/// A projectile drawn as a 3DO object (render types 1, 3 and 6): its ground
/// shadow, its model and, for a missile with flight time left, its first
/// child.
struct ProjectileDraw {
    /// The bridge region captured, in its 8-bit pixels. Rect32 is packed, so
    /// each draw's region is aligned: the bridge takes its fields by reference.
    alignas(4) oa::Rect32 region{};
    bool shadow{};                                   ///< the shadow sprite is drawn
    int32_t x{};                                     ///< the shadow's column
    int32_t shadow_y{};                              ///< the shadow's row
    oa::formats::objects3d::FixedVector3 position{}; ///< 16.16, camera-relative
    const oa::formats::objects3d::Object* object{};
    const oa::present::model::PreparedObject* prepared{};
    oa::sim::model_runtime::RotationWords rotation{};
    const oa::formats::objects3d::Object* child{}; ///< null for none
    const oa::present::model::PreparedObject* child_prepared{};
    oa::sim::model_runtime::RotationWords child_rotation{};
};

/// A debris piece, turned by its spin and drawn at its origin.
struct DebrisDraw {
    alignas(4) oa::Rect32 region{}; ///< the bridge region captured, in its 8-bit pixels
    const oa::formats::objects3d::Object* object{};
    const oa::present::model::PreparedObject* prepared{};
    oa::sim::model_runtime::RotationWords spin{};
    oa::formats::objects3d::FixedVector3 origin{}; ///< 16.16 world origin
    uint8_t team{};                                ///< its unit's team colour
};

/// A shatter fragment, drawn into the bridge and written back at once.
struct FragmentDraw {
    alignas(4) oa::Rect32 region{}; ///< the bridge region captured, in its 8-bit pixels
    oa::formats::objects3d::FixedVector3 position{}; ///< 16.16, camera-relative
    const oa::sim::effect_particles::ShatterFragment* fragment{};
    const oa::present::model::PreparedPrimitive* primitive{};
    oa::sim::model_runtime::RotationWords spin{};
};

/// The battlefield's draws of one frame, in order, and what they draw from.
struct WorldDrawList {
    /// How dark the frame's shadows are drawn, from 0 to
    /// oa::present::model::shadow_full_level, as the game draws them; at 0
    /// the list holds no shadow (set_frame_shadows).
    uint32_t shadow_level{oa::present::model::shadow_full_level};
    std::vector<WorldDraw> draws; ///< in the order they draw
    std::vector<SquareDraw> squares;
    std::vector<SpriteDraw> sprites;
    std::vector<LineDraw> lines;
    std::vector<ModelDraw> models;
    std::vector<oa::Unit> stand_ins; ///< 3D features' stand-in records
    std::vector<ProjectileDraw> projectiles;
    std::vector<DebrisDraw> debris;
    std::vector<FragmentDraw> fragments;
    /// GAF frames decoded for this frame's draws (particles, plasma and
    /// flames), each once: a deque, so that the draws keep pointing at them.
    std::deque<oa::formats::gaf::RenderedFrame> decoded;
    /// The decoded frame of each GAF frame, by the GAF frame's address.
    std::unordered_map<const oa::formats::gaf::Frame*, const oa::formats::gaf::RenderedFrame*>
        decoded_of;
};

/// Empties a draw list for the next frame, keeping its buffers.
///
/// The shadow level is left as it is.
///
/// @param[in,out] list the list
void clear_world_draws(WorldDrawList& list);

/// Sets how dark a frame's shadows are drawn, from the zoom the frame is
/// drawn at (oa::present::model::shadow_level), before the frame's draws
/// are worked out.
///
/// The list takes the level. At the game's own darkness the renderer's
/// shadows blend through the display's alpha table, as the game draws
/// them; lighter, through the table's faded rows for the models'
/// silhouettes and the projectiles' shadow sprite; at 0 the renderer's
/// shadow option is cleared for the frame, so that no unit or 3D feature
/// casts one, and the planner adds no shadow of a sprite feature or a
/// projectile (shadows_drawn).
///
/// @param[in,out] list the frame's list
/// @param[in,out] renderer the models' renderer, its graphics flags already the game's
/// @param[in,out] table the faded table, kept from frame to frame
/// @param display the models' display: its alpha table and palette
/// @param projectile_shadow the projectiles' shadow sprite; null or no data for none
/// @param zoom window pixels per map pixel
void set_frame_shadows(
    WorldDrawList& list,
    oa::present::model::ModelRenderer& renderer,
    oa::present::model::ShadowTable& table,
    const oa::present::model::ModelDisplay& display,
    const oa::Sprite* projectile_shadow,
    float zoom
);

/// Tells whether a list's frame draws any shadow.
///
/// @param list the list
/// @return false at shadow level 0
[[nodiscard]] inline bool shadows_drawn(const WorldDrawList& list) noexcept {
    return list.shadow_level != 0;
}

/// Returns a GAF frame decoded for a frame's draws, decoding it the first time it is asked for.
///
/// @param[in,out] list the frame's list, which keeps the decoded frame
/// @param frame the GAF frame
/// @return the decoded frame, or null when it cannot be decoded
const oa::formats::gaf::RenderedFrame*
decoded_frame(WorldDrawList& list, const oa::formats::gaf::Frame& frame);

/// Adds a draw of a kind to the end of a list.
///
/// @param[in,out] list the list
/// @param kind its kind
/// @param index its place in its kind's list
void add_world_draw(WorldDrawList& list, WorldDrawKind kind, std::size_t index);

/// What every band of one frame draws with.
struct WorldFrameDraw {
    /// The whole frame; each band narrows its rows to its own.
    WorldTarget target{};
    const oa::PaletteBytes* palette{}; ///< the match's palette, 4 bytes a colour
    float scale{1.0F};                 ///< frame pixels per map pixel, as the GAF draws scale
    oa::present::model::RgbBridge* bridge{};
    /// The models' display: each band binds it on the thread drawing it.
    oa::present::model::ModelDisplay* display{};
    const oa::Sprite* projectile_shadow{}; ///< FX.GAF "shadow" frame 0; no data for none
    alignas(4) oa::Rect32 debris_view{};   ///< the rectangle debris origins are culled to
    /// Pixels across and down a frame line (a laser, lightning or a debug
    /// beam) is drawn thick, so that it stays visible once the frame is
    /// reduced to the screen; 1 draws it as the game always has.
    int32_t line_thickness{1};
    /// Pixels across and down a selection line is drawn thick in the model
    /// bridge, at one pixel per map pixel; 1 draws it as the game always has.
    int32_t bridge_line_thickness{1};
};

/// The buffers one band draws with, apart from the bridge band.
struct WorldBandScratch {
    oa::present::model::ModelRenderer
        renderer{}; ///< the frame's renderer settings, its own composite
    oa::present::model::SupersampleScratch supersample{};
    std::vector<oa::formats::objects3d::FixedVector3> debris_points;
};

/// Draws one band of a frame's battlefield draws.
///
/// Every draw of the list is drawn in order, each changing only the band's
/// rows of the frame and of the model bridge: the band's own bridge rows are
/// captured, drawn and written back, sprites, squares and lines write their
/// pixels in the band. Builds nothing and changes nothing but the band's
/// rows, the bridge's tiles and capture copy in them, the band and the
/// scratch. The models' display is bound on the calling thread while the
/// band draws.
///
/// @param list the frame's draws
/// @param frame what every band draws with
/// @param[in,out] band the band of the frame's bridge (bridge_split)
/// @param[in,out] renderer the renderer the band draws with: the frame's
///     settings, its composite the band's own
/// @param[in,out] supersample the band's buffers for units drawn finer
/// @param[in,out] debris_points the band's buffer for turned debris points
void draw_world_band(
    const WorldDrawList& list,
    const WorldFrameDraw& frame,
    oa::present::model::BridgeBand& band,
    oa::present::model::ModelRenderer& renderer,
    oa::present::model::SupersampleScratch& supersample,
    std::vector<oa::formats::objects3d::FixedVector3>& debris_points
);

} // namespace oa::app
