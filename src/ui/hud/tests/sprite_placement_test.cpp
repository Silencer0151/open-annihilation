// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/sprite_placement.hpp"

#include "oa/sim/feature_runtime.hpp"

#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

constexpr int32_t kMapCells = 4;
constexpr int32_t kCellX = 1;
constexpr int32_t kCellZ = 2;

// A 4x4-cell map whose plot (1, 2) holds the one feature definition: a 2x1
// sprite with a body and a shadow sequence.
struct FeatureWorld {
    World* world = world_create();
    std::vector<MapPlot> plots = std::vector<MapPlot>(kMapCells * kMapCells);
    std::vector<sim::feature_runtime::PlacedFeature> records =
        std::vector<sim::feature_runtime::PlacedFeature>(2);

    FeatureWorld() {
        WorldCapacity capacity{1, 1, 1};
        CHECK(world_alloc_tables(world, &capacity) != 0);
        world->plots = plots.data();
        world->game.map_width = kMapCells;
        world->game.map_height = kMapCells;
        world->placed_features = reinterpret_cast<uint8_t*>(records.data());
        world->placed_feature_count = 2;
        FeatureDef& def = feature();
        def.footprint_x = 2;
        def.footprint_z = 1;
        def.seq_name = 1;
        def.seq_name_shadow = 2;
        def.flags = OA_FEATURE_FLAG_SPRITE;
        plot(kCellX, kCellZ).feature = 0;
    }

    ~FeatureWorld() { world_destroy(world); }

    FeatureWorld(const FeatureWorld&) = delete;
    FeatureWorld& operator=(const FeatureWorld&) = delete;

    FeatureDef& feature() { return world->feature_defs[0]; }

    MapPlot& plot(int32_t x, int32_t z) { return plots[static_cast<size_t>(z * kMapCells + x)]; }

    FeatureDraw draw() { return plan_feature_draw(*world, kCellX, kCellZ); }
};

bool sprite_is(const FeatureSprite& sprite, bool shadow, FeatureFrame frame, bool translucent) {
    return sprite.shadow == shadow && sprite.frame == frame && sprite.translucent == translucent;
}

// A feature is placed at half its footprint past the plot's corner,
// less the camera, with the battlefield at (0x80, 0x20), lifted by the sum of
// the four corner heights shifted right by 3.
void position() {
    FeatureWorld test;
    test.world->game.camera_x = 16;
    test.world->game.camera_y = 8;
    test.plot(kCellX, kCellZ).height = 40;
    test.plot(kCellX + 1, kCellZ).height = 24;
    test.plot(kCellX, kCellZ + 1).height = 16;
    const FeatureDraw draw = test.draw();
    CHECK(draw.x == 2 * 16 / 2 - 16 + (kCellX + 8) * 16);
    CHECK(draw.y == 1 * 16 / 2 - (40 + 24 + 16) / 8 - 8 + (kCellZ + 2) * 16);
    CHECK(!draw.object);
}

// A still sprite: its shadow's frame 0 first while FeatureShadows (bit 0x10)
// is on, blended when shadtrans is set, then its body's frame 0.
void still_sprite() {
    FeatureWorld test;
    test.world->game.graphics_flags = kGraphicsShadows;
    test.feature().flags = OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_SHAD_TRANS;
    FeatureDraw draw = test.draw();
    CHECK(draw.sprite_count == 2);
    CHECK(sprite_is(draw.sprites[0], true, FeatureFrame::first, true));
    CHECK(sprite_is(draw.sprites[1], false, FeatureFrame::first, false));

    test.world->game.graphics_flags = 0;
    draw = test.draw();
    CHECK(draw.sprite_count == 1);
    CHECK(sprite_is(draw.sprites[0], false, FeatureFrame::first, false));

    test.world->game.graphics_flags = kGraphicsShadows;
    test.feature().seq_name_shadow = 0;
    draw = test.draw();
    CHECK(draw.sprite_count == 1);
    CHECK(!draw.sprites[0].shadow);
}

// An animating definition draws both sequences at its running cursors; the
// body blends when animtrans is set and the shadow stays solid without
// shadtrans.
void animating_sprite() {
    FeatureWorld test;
    test.world->game.graphics_flags = kGraphicsShadows;
    test.feature().flags =
        OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_ANIMATING | OA_FEATURE_FLAG_ANIM_TRANS;
    const FeatureDraw draw = test.draw();
    CHECK(draw.sprite_count == 2);
    CHECK(sprite_is(draw.sprites[0], true, FeatureFrame::animated, false));
    CHECK(sprite_is(draw.sprites[1], false, FeatureFrame::animated, true));
}

// A plot whose record animates draws the record's frames solid whatever the
// definition's transparency: its shadow only when the record's state has
// state_has_shadow and FeatureShadows is on. A 3D definition goes to the
// linked-object draw.
void placed_record() {
    FeatureWorld test;
    test.world->game.graphics_flags = kGraphicsShadows;
    test.feature().flags =
        OA_FEATURE_FLAG_SPRITE | OA_FEATURE_FLAG_ANIM_TRANS | OA_FEATURE_FLAG_SHAD_TRANS;
    MapPlot& plot = test.plot(kCellX, kCellZ);
    plot.flags = OA_PLOT_FLAG_ANIMATING_FEATURE;
    plot.feature_record = 1;
    uint8_t& record_state = test.records[1].state;
    record_state = kPlacedFeatureShadowAnim;
    FeatureDraw draw = test.draw();
    CHECK(draw.sprite_count == 2);
    CHECK(sprite_is(draw.sprites[0], true, FeatureFrame::placed, false));
    CHECK(sprite_is(draw.sprites[1], false, FeatureFrame::placed, false));

    test.world->game.graphics_flags = 0;
    draw = test.draw();
    CHECK(draw.sprite_count == 1);
    CHECK(sprite_is(draw.sprites[0], false, FeatureFrame::placed, false));

    test.world->game.graphics_flags = kGraphicsShadows;
    record_state = 0;
    draw = test.draw();
    CHECK(draw.sprite_count == 1);
    CHECK(!draw.sprites[0].shadow);

    test.feature().flags = 0;
    draw = test.draw();
    CHECK(draw.object);
    CHECK(draw.sprite_count == 0);
}

} // namespace

int main() {
    position();
    still_sprite();
    animating_sprite();
    placed_record();
    return 0;
}
