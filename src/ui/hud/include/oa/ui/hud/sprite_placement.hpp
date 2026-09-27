// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where the battlefield and HUD sprites go: cached building sprites, map
// feature sprites and the side's HUD frame.
#pragma once

#include "oa/core/world.h"

#include <cstdint>

namespace oa::ui::hud {

/// Unit.flags bit set while the unit is carried with no attach piece
/// (Unit.attach_piece is none); a carrier's cached sprite leaves such a
/// unit out.
inline constexpr uint32_t kUnitFlagAttachedWithoutPiece = 0x00020000u;
/// Unit.state_flags bit of a building that is switched on.
inline constexpr uint8_t kUnitStateActivated = 0x01u;

/// State of one unit's cached sprite.
struct UnitSpriteCache {
    uint32_t draws{};      // times drawn
    bool has_image{};      // a cached image exists
    bool image_has_mask{}; // the cached image has its mask plane
    int16_t anchor_x{};    // anchor of the cached image; reset with the image
    int16_t anchor_y{};
};

struct UnitSpriteDraw {
    bool rebuild; // re-render the cached image before drawing
    bool unlit;   // draw with the idle (unlit) shading
};

/// Decides whether a unit's cached sprite must be rebuilt this frame and counts the draw.
///
/// The first draw always rebuilds. A building also rebuilds when it has no
/// cached image, or while it is under construction with a dirty construction
/// state and an image without its mask; a z-buffered unit rebuilds without a
/// cached image. The anchor resets whenever there is no image or a rebuild.
/// A building draws unlit while switched off, a mobile unit while idle.
///
/// @param unit Unit drawn.
/// @param[in,out] cache The unit's sprite cache; the draw count advances.
/// @param movement_idle Whether a mobile unit's movement is idle; buildings ignore it.
/// @return Whether to rebuild the cached image and whether to draw it unlit.
UnitSpriteDraw
prepare_unit_sprite(const Unit& unit, UnitSpriteCache& cache, bool movement_idle) noexcept;

/// An 8-bit sprite image header: size plus the origin offset.
struct SpriteFrame {
    uint16_t width;
    uint16_t height;
    int16_t origin_x;
    int16_t origin_y;
};

/// Screen-space extent of a model, relative to its unit's position.
struct SpriteBounds {
    int32_t left, right, top, bottom;
};

/// Computes the frame that holds `source` together with the model extents of the unit and its carried units.
///
/// @param source The unit's own sprite image header.
/// @param parts Model extents relative to the unit's position, offsets already applied.
/// @param part_count Number of entries in `parts`.
/// @return Size and origin of the smallest frame holding the image and every part.
[[nodiscard]] SpriteFrame unit_sprite_frame(
    const SpriteFrame& source, const SpriteBounds* parts, int32_t part_count
) noexcept;

/// Offsets an attached unit's model extent into its parent's screen space.
///
/// x moves across by the position difference; z moves by its difference
/// lifted by half the height difference.
///
/// @param parent Carrying unit.
/// @param child Carried unit.
/// @param child_extent The child's model extent relative to its own position.
/// @return The extent relative to the parent's position.
[[nodiscard]] SpriteBounds attached_part_bounds(
    const Unit& parent, const Unit& child, const SpriteBounds& child_extent
) noexcept;

/// Compositing done by the renderer for unit_sprite_frame.
struct SpriteComposer {
    void* user{};
    /// Model extent of a unit drawn at the origin.
    bool (*model_bounds)(void* user, const Unit& unit, SpriteBounds& out){};
    /// Copies `source` unchanged into the cache (same size).
    void (*copy)(void* user, const SpriteFrame& frame){};
    /// Clears the cache to transparent and draws `source` at `x`,`y`.
    void (*redraw)(void* user, const SpriteFrame& frame, int32_t x, int32_t y){};
    /// Applies the unit's build effect to the composed sprite.
    void (*finish)(void* user, const SpriteFrame& frame){};
};

/// Rebuilds a unit's cached sprite frame around its image and the models of the units it carries.
///
/// Carried units with kUnitFlagAttachedWithoutPiece set are left out. The image is
/// copied unchanged when the frame keeps its size and redrawn at its new
/// origin otherwise; the build effect is then applied.
///
/// @param world World holding the attached units.
/// @param unit Unit whose sprite is cached.
/// @param source The unit's own sprite image header.
/// @param composer Renderer callbacks for model bounds and compositing.
/// @return The new frame.
SpriteFrame compose_unit_sprite(
    World& world, const Unit& unit, const SpriteFrame& source, const SpriteComposer& composer
);

/// How a feature sprite frame is chosen.
enum class FeatureFrame : uint8_t {
    first,    // frame 0 of the sequence
    animated, // the feature's running animation
    placed,   // the placed feature's own animation record
};

/// One sprite of a feature draw.
struct FeatureSprite {
    bool shadow; // shadow sequence (drawn first)
    FeatureFrame frame;
    bool translucent; // blend (SHAD_TRANS/ANIM_TRANS) instead of a plain blit
};

/// A feature draw: up to two sprites at one screen position, or a 3D
/// object when `object` is set.
struct FeatureDraw {
    int32_t x, y;
    bool object;
    int32_t sprite_count;
    FeatureSprite sprites[2];
};

/// PlacedFeature.state bit (sim::feature_runtime::state_has_shadow): the
/// placed-feature record animates a shadow too.
inline constexpr uint8_t kPlacedFeatureShadowAnim = 0x04u;
/// Game.graphics_flags bit of the shadow detail option.
inline constexpr uint16_t kGraphicsShadows = 0x0010u;

/// Plans the draw of the feature standing on a map cell.
///
/// The sprites sit over the feature's footprint centre, lifted by half the
/// mean height of the cell's corners. An animating plot draws a 3D object for
/// a non-sprite feature, otherwise the placed record's animation (with its
/// shadow when the record animates one and shadows are on). Other features
/// draw the shadow sequence (with shadows on) then the sprite sequence, from
/// the running animation or frame 0.
///
/// @param world World holding the map plots and feature definitions.
/// @param cell_x Map cell column.
/// @param cell_z Map cell row.
/// @return The draw; empty when the cell has no valid feature.
[[nodiscard]] FeatureDraw plan_feature_draw(World& world, int32_t cell_x, int32_t cell_z);

/// Positions of the side's three HUD frame pieces (top bar, bottom bar,
/// order panel), each drawn from its first frame's origin.
struct HudFramePlacement {
    int32_t x[3];
    int32_t y[3];
};

/// Places the side's HUD frame pieces.
///
/// The top and bottom bars sit right of the 129-pixel order panel; the bottom
/// bar hangs 32 pixels above the screen bottom.
///
/// @param pieces First frames of the top bar, bottom bar and order panel.
/// @param screen_height Screen height in pixels.
/// @return Screen position of each piece.
[[nodiscard]] HudFramePlacement
hud_frame_placement(const SpriteFrame (&pieces)[3], int32_t screen_height) noexcept;

} // namespace oa::ui::hud
