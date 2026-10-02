// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/model/sprite_placement.hpp"

#include "oa/sim/feature_runtime.hpp"

namespace oa::present::model {
namespace {

constexpr int32_t kCellPixels = 16;
/// Screen-space offset of the battlefield inside the 640x480 HUD frame (the
/// order panel on the left, the resource bar on top).
constexpr int32_t kBattlefieldLeft = 0x80;
constexpr int32_t kBattlefieldTop = 0x20;
/// HUD frame pieces sit right of the 129-pixel order panel; the bottom bar
/// hangs 32 pixels above the screen bottom.
constexpr int32_t kFrameLeft = 0x81;
constexpr int32_t kBottomBarLift = 0x20;

static_assert(sizeof(sim::feature_runtime::PlacedFeature) == OA_PLACED_FEATURE_BYTES);
static_assert(kPlacedFeatureShadowAnim == sim::feature_runtime::state_has_shadow);

int16_t high16(int32_t value) noexcept {
    return static_cast<int16_t>(static_cast<uint32_t>(value) >> 16);
}

void include(SpriteBounds& into, const SpriteBounds& part) noexcept {
    if (part.left < into.left)
        into.left = part.left;
    if (into.right < part.right)
        into.right = part.right;
    if (part.top < into.top)
        into.top = part.top;
    if (into.bottom < part.bottom)
        into.bottom = part.bottom;
}

} // namespace

UnitSpriteDraw
prepare_unit_sprite(const Unit& unit, UnitSpriteCache& cache, bool movement_idle) noexcept {
    const bool building = (unit.flags & OA_UNIT_FLAG_BUILDING) != 0;
    UnitSpriteDraw draw{
        cache.draws == 0, building ? (unit.state_flags & kUnitStateActivated) == 0 : movement_idle
    };
    if (!cache.has_image) {
        cache.anchor_x = 0;
        cache.anchor_y = 0;
    }
    if (building && (!cache.has_image || (unit.build_remaining != 0.0F &&
                                          (unit.flags & OA_UNIT_FLAG_CONSTRUCTION_DIRTY) != 0 &&
                                          !cache.image_has_mask)))
        draw.rebuild = true;
    if ((unit.flags2 & OA_UNIT_FLAG2_Z_BUFFER) != 0 && !cache.has_image)
        draw.rebuild = true;
    if (draw.rebuild) {
        cache.anchor_x = 0;
        cache.anchor_y = 0;
    }
    ++cache.draws;
    return draw;
}

SpriteFrame unit_sprite_frame(
    const SpriteFrame& source, const SpriteBounds* parts, int32_t part_count
) noexcept {
    SpriteBounds bounds{0, 0, 0, 0};
    for (int32_t index = 0; index < part_count; ++index)
        include(bounds, parts[index]);
    SpriteBounds image{
        -source.origin_x,
        source.width - source.origin_x,
        -source.origin_y,
        source.height - source.origin_y
    };
    if (bounds.left < image.left)
        image.left = bounds.left;
    if (image.right < bounds.right)
        image.right = bounds.right;
    if (bounds.top < image.top)
        image.top = bounds.top;
    if (image.bottom < bounds.bottom)
        image.bottom = bounds.bottom;
    return {
        static_cast<uint16_t>(image.right - image.left),
        static_cast<uint16_t>(image.bottom - image.top),
        static_cast<int16_t>(-image.left),
        static_cast<int16_t>(-image.top),
    };
}

SpriteBounds attached_part_bounds(
    const Unit& parent, const Unit& child, const SpriteBounds& child_extent
) noexcept {
    const int32_t dx = high16(child.position.x - parent.position.x);
    const int32_t dy = child.position.y - parent.position.y;
    const int32_t dz = high16(child.position.z - parent.position.z);
    const auto lifted = static_cast<int16_t>(dz - static_cast<int16_t>(dy >> 17));
    return {
        child_extent.left + dx,
        child_extent.right + dx,
        child_extent.top + lifted,
        child_extent.bottom + lifted
    };
}

SpriteFrame compose_unit_sprite(
    World& world, const Unit& unit, const SpriteFrame& source, const SpriteComposer& composer
) {
    SpriteBounds bounds{0, 0, 0, 0};
    SpriteBounds extent{};
    if (composer.model_bounds != nullptr && composer.model_bounds(composer.user, unit, extent))
        include(bounds, extent);
    for (const Unit* child = world_unit(&world, unit.attach_first_child); child != nullptr;
         child = world_unit(&world, child->attach_next)) {
        if ((child->flags & kUnitFlagAttachedWithoutPiece) != 0)
            continue;
        SpriteBounds part{0, 0, 0, 0};
        if (composer.model_bounds != nullptr &&
            composer.model_bounds(composer.user, *child, extent))
            include(part, extent);
        include(bounds, attached_part_bounds(unit, *child, part));
    }
    const SpriteFrame frame = unit_sprite_frame(source, &bounds, 1);
    if (frame.width == source.width && frame.height == source.height) {
        if (composer.copy != nullptr)
            composer.copy(composer.user, frame);
    } else if (composer.redraw != nullptr) {
        composer.redraw(
            composer.user, frame, frame.origin_x - source.origin_x, frame.origin_y - source.origin_y
        );
    }
    if (composer.finish != nullptr)
        composer.finish(composer.user, frame);
    return frame;
}

FeatureDraw plan_feature_draw(World& world, int32_t cell_x, int32_t cell_z) {
    FeatureDraw draw{};
    const MapPlot* plot = world_plot(&world, cell_x, cell_z);
    if (plot == nullptr || plot->feature >= world.feature_def_count)
        return draw;
    const FeatureDef& def = world.feature_defs[plot->feature];
    const Game& game = world.game;
    const auto height = [&](int32_t dx, int32_t dz) -> int32_t {
        const MapPlot* corner = world_plot(&world, cell_x + dx, cell_z + dz);
        return corner != nullptr ? corner->height : 0;
    };
    const int32_t lift = (height(0, 0) + height(1, 0) + height(0, 1) + height(1, 1)) >> 3;
    draw.x = (def.footprint_x * kCellPixels) / 2 - static_cast<int32_t>(game.camera_x) +
             cell_x * kCellPixels + kBattlefieldLeft;
    draw.y = (def.footprint_z * kCellPixels) / 2 - lift - static_cast<int32_t>(game.camera_y) +
             cell_z * kCellPixels + kBattlefieldTop;
    const bool shadows = (game.graphics_flags & kGraphicsShadows) != 0;
    const auto add = [&](bool shadow, FeatureFrame frame, bool translucent) {
        draw.sprites[draw.sprite_count++] = {shadow, frame, translucent};
    };
    if ((plot->flags & OA_PLOT_FLAG_ANIMATING_FEATURE) != 0) {
        if ((def.flags & OA_FEATURE_FLAG_SPRITE) == 0) {
            draw.object = true;
            return draw;
        }
        const auto* records =
            reinterpret_cast<const sim::feature_runtime::PlacedFeature*>(world.placed_features);
        const sim::feature_runtime::PlacedFeature* record =
            records != nullptr && plot->feature_record < world.placed_feature_count
                ? &records[plot->feature_record]
                : nullptr;
        if (record != nullptr && (record->state & kPlacedFeatureShadowAnim) != 0 && shadows)
            add(true, FeatureFrame::placed, false);
        add(false, FeatureFrame::placed, false);
        return draw;
    }
    const FeatureFrame frame =
        (def.flags & OA_FEATURE_FLAG_ANIMATING) != 0 ? FeatureFrame::animated : FeatureFrame::first;
    if (def.seq_name_shadow != 0 && shadows)
        add(true, frame, (def.flags & OA_FEATURE_FLAG_SHAD_TRANS) != 0);
    if (def.seq_name != 0)
        add(false, frame, (def.flags & OA_FEATURE_FLAG_ANIM_TRANS) != 0);
    return draw;
}

HudFramePlacement
hud_frame_placement(const SpriteFrame (&pieces)[3], int32_t screen_height) noexcept {
    return {
        {pieces[0].origin_x + kFrameLeft, pieces[1].origin_x + kFrameLeft, pieces[2].origin_x},
        {pieces[0].origin_y,
         pieces[1].origin_y + screen_height - kBottomBarLift,
         pieces[2].origin_y},
    };
}

} // namespace oa::present::model
