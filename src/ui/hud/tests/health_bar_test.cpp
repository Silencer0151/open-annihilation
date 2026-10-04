// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/health_bar.hpp"

#include <cmath>
#include <memory>

using namespace oa;
using namespace oa::ui::hud;

namespace {

std::unique_ptr<Game> coloured_game() {
    auto game = std::make_unique<Game>();
    for (int slot = 0; slot < 0x100; ++slot)
        game->ui_colors[slot] = static_cast<uint8_t>(0x80 + slot);
    return game;
}

// The bar is (x-0x11, y-2)-(x+0x11, y+2) in ui colour 0, then the fill
// one pixel in from it, (health << 5) / max_damage columns past its first.
void trough_and_fill() {
    const auto game = coloured_game();
    Unit unit{};
    UnitDef def{};
    def.max_damage = 900;
    unit.health = 900;
    HealthBar bar{};
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar));
    CHECK(
        bar.trough.x1 == 83 && bar.trough.y1 == 48 && bar.trough.x2 == 117 && bar.trough.y2 == 52
    );
    CHECK(bar.trough_color == 0x80);
    CHECK(bar.fill.x1 == 84 && bar.fill.y1 == 49 && bar.fill.x2 == 116 && bar.fill.y2 == 51);
    unit.health = 450;
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar));
    CHECK(bar.fill.x2 == 84 + 16);
    // Overhealed units run past the trough: the fill is not clamped.
    unit.health = 1800;
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar));
    CHECK(bar.fill.x2 == 84 + 64);
}

// At the game's view and zoomed in the bars keep the game's size; zoomed
// out they shrink by the same share at each step of the zoom, to a third at
// six times as far out, and stay a third past it.
void scale_by_zoom() {
    const auto near = [](float value, float expected) {
        return std::abs(value - expected) < 1.0e-4F;
    };
    for (const float zoom : {1.0F, 1.15F, 2.0F, 4.0F, 100.0F})
        CHECK(health_bar_scale(zoom) == 1.0F);
    CHECK(near(health_bar_scale(0.5F), 0.65377F));
    CHECK(near(health_bar_scale(0.25F), 0.42742F));
    CHECK(health_bar_scale(1.0F / 6.0F) == 1.0F / 3.0F);
    CHECK(health_bar_scale(0.1F) == 1.0F / 3.0F);
    // Each wheel step out shrinks the bars by one share, however far out.
    const float step = 1.0F / 1.15F;
    const float first = health_bar_scale(step);
    for (float zoom = step; zoom * step > 1.0F / 6.0F; zoom *= step)
        CHECK(near(health_bar_scale(zoom * step) / health_bar_scale(zoom), first));
    CHECK(health_bar_scale(0.5F) < health_bar_scale(0.75F));
    CHECK(health_bar_scale(0.25F) < health_bar_scale(0.5F));
}

// The bars' sizes in whole pixels: at zoom 1 and in a trough 35 by 5 with
// its centre 10 rows below the unit's; 23 by 3, 7 below, twice as far out;
// 15 by 3, 4 below, four times out; 13 by 3, 3 below, six times out. The
// trough is never less than 3 rows, so that its fill is a row at least.
void size_by_zoom() {
    const auto is = [](const HealthBarSize& size, int32_t width, int32_t height, int32_t below) {
        return 2 * size.half_width + 1 == width && 2 * size.half_height + 1 == height &&
               size.below_unit == below;
    };
    for (const float zoom : {1.0F, 2.0F, 4.0F})
        CHECK(is(health_bar_size(zoom), 35, 5, 10));
    const HealthBarSize game{};
    CHECK(is(game, 35, 5, 10));
    CHECK(is(health_bar_size(0.75F), 29, 5, 8));
    CHECK(is(health_bar_size(0.5F), 23, 3, 7));
    CHECK(is(health_bar_size(1.0F / 3.0F), 19, 3, 5));
    CHECK(is(health_bar_size(0.25F), 15, 3, 4));
    CHECK(is(health_bar_size(1.0F / 6.0F), 13, 3, 3));
    CHECK(is(health_bar_size(0.05F), 13, 3, 3));
    // Smaller at each step out, never larger.
    HealthBarSize before = health_bar_size(1.0F);
    for (float zoom = 1.0F; zoom >= 1.0F / 6.0F; zoom /= 1.15F) {
        const HealthBarSize size = health_bar_size(zoom);
        CHECK(size.half_width <= before.half_width && size.half_height <= before.half_height);
        CHECK(size.half_height >= 1);
        before = size;
    }
}

// The bar at a smaller size: the trough about the centre, the fill one pixel
// inside it and spanning its inside at full health.
void trough_and_fill_scaled() {
    const auto game = coloured_game();
    Unit unit{};
    UnitDef def{};
    def.max_damage = 900;
    unit.health = 900;
    HealthBar bar{};
    // Six times as far out: 13 by 3.
    const HealthBarSize far = health_bar_size(1.0F / 6.0F);
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar, far));
    CHECK(
        bar.trough.x1 == 94 && bar.trough.y1 == 49 && bar.trough.x2 == 106 && bar.trough.y2 == 51
    );
    CHECK(bar.fill.x1 == 95 && bar.fill.y1 == 50 && bar.fill.x2 == 105 && bar.fill.y2 == 50);
    unit.health = 450;
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar, far));
    CHECK(bar.fill.x2 == 95 + 5);
    unit.health = 1;
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar, far));
    CHECK(bar.fill.x2 == bar.fill.x1);
    // Twice as far out: 23 by 3, the fill 21 pixels at full health.
    unit.health = 900;
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar, health_bar_size(0.5F)));
    CHECK(bar.trough.x1 == 89 && bar.trough.x2 == 111 && bar.trough.y1 == 49);
    CHECK(bar.fill.x1 == 90 && bar.fill.x2 == 110);
    // At the game's size the same as the default.
    HealthBar game_size{};
    CHECK(unit_health_bar(*game, unit, def, 100, 50, game_size, health_bar_size(2.0F)));
    CHECK(unit_health_bar(*game, unit, def, 100, 50, bar));
    CHECK(game_size.trough.x1 == bar.trough.x1 && game_size.trough.y2 == bar.trough.y2);
    CHECK(game_size.fill.x2 == bar.fill.x2 && game_size.fill.y1 == bar.fill.y1);
}

// Colour 10 above two thirds of max_damage, 14 above a third, 12 otherwise;
// the thirds are unsigned max_damage / 3 and the comparisons strict.
void colour_by_thirds() {
    const auto game = coloured_game();
    Unit unit{};
    UnitDef def{};
    def.max_damage = 100; // third 33, two thirds 66
    HealthBar bar{};
    unit.health = 67;
    CHECK(unit_health_bar(*game, unit, def, 0, 0, bar) && bar.fill_color == 0x80 + 10);
    unit.health = 66;
    CHECK(unit_health_bar(*game, unit, def, 0, 0, bar) && bar.fill_color == 0x80 + 14);
    unit.health = 34;
    CHECK(unit_health_bar(*game, unit, def, 0, 0, bar) && bar.fill_color == 0x80 + 14);
    unit.health = 33;
    CHECK(unit_health_bar(*game, unit, def, 0, 0, bar) && bar.fill_color == 0x80 + 12);
    unit.health = 1;
    CHECK(unit_health_bar(*game, unit, def, 0, 0, bar) && bar.fill_color == 0x80 + 12);
    CHECK(bar.fill.x2 == bar.fill.x1);
}

void no_bar_without_health() {
    const auto game = coloured_game();
    Unit unit{};
    UnitDef def{};
    def.max_damage = 100;
    HealthBar bar{};
    unit.health = 0;
    CHECK(!unit_health_bar(*game, unit, def, 0, 0, bar));
    unit.health = -5;
    CHECK(!unit_health_bar(*game, unit, def, 0, 0, bar));
}

// Health bars are drawn only while the DamageBars bit is set, and
// then only under units whose owner is the viewpoint player (Game.viewpoint_player),
// whatever the local player.
void health_bars_follow_ownership() {
    hud_test::TestWorld w;
    w.add_player(0, 1);
    w.add_player(1, 2);
    w.give_range(0, 1, 4);
    w.give_range(1, 5, 8);
    auto& own = w.spawn(1, 1);
    auto& enemy = w.spawn(5, 1);
    w.game().local_player_index = 0;
    w.game().viewpoint_player = 0;
    w.game().graphics_flags = 0;
    CHECK(!draws_health_bar(*w.world, own) && !draws_health_bar(*w.world, enemy));
    w.game().graphics_flags = kGraphicsDamageBars;
    CHECK(draws_health_bar(*w.world, own) && !draws_health_bar(*w.world, enemy));
    w.game().viewpoint_player = 1;
    CHECK(!draws_health_bar(*w.world, own) && draws_health_bar(*w.world, enemy));
    // The owner is found through Unit.owner, not the Unit.owner_index byte.
    enemy.owner_index = 0;
    CHECK(draws_health_bar(*w.world, enemy));
}

// A squad digit shows only under the viewpoint player's own units in a
// squad, and only with damage bars on: a computer player's squads (the ones
// it sorts its units into) and an ally's never show, and watching another
// player shows that player's squads instead of the local player's.
void squad_digits_follow_ownership() {
    hud_test::TestWorld w;
    w.add_player(0, OA_PLAYER_STATUS_LOCAL);
    w.add_player(1, OA_PLAYER_STATUS_COMPUTER);
    w.add_player(2, OA_PLAYER_STATUS_MIRRORED);
    w.give_range(0, 1, 4);
    w.give_range(1, 5, 8);
    w.give_range(2, 9, 12);
    // Player 2 is the local player's ally both ways.
    w.player(0).allied_by[2] = 1;
    w.player(2).allied_by[0] = 1;
    auto& own = w.spawn(1, 1);
    auto& own_loose = w.spawn(2, 1);
    auto& computer = w.spawn(5, 1);
    auto& ally = w.spawn(9, 1);
    own.squad = 3;
    computer.squad = 5;
    ally.squad = 2;
    w.game().local_player_index = 0;
    w.game().viewpoint_player = 0;
    w.game().graphics_flags = kGraphicsDamageBars;
    CHECK(draws_squad_digit(*w.world, own) && squad_digit(own) == '3');
    CHECK(!draws_squad_digit(*w.world, own_loose));
    CHECK(!draws_squad_digit(*w.world, computer));
    CHECK(!draws_squad_digit(*w.world, ally));
    w.game().graphics_flags = 0;
    CHECK(!draws_squad_digit(*w.world, own));
    w.game().graphics_flags = kGraphicsDamageBars;
    w.game().viewpoint_player = 1;
    CHECK(!draws_squad_digit(*w.world, own));
    CHECK(draws_squad_digit(*w.world, computer) && squad_digit(computer) == '5');
    CHECK(!draws_squad_digit(*w.world, ally));
}

// The unit panel draws its damage bar for the viewpoint player's
// own units and for other players' units unless their type sets HideDamage
// (UnitDef.flags, OA_UNIT_DEF_FLAG_HIDE_DAMAGE).
void panel_damage_follows_hide_damage() {
    hud_test::TestWorld w;
    w.add_player(0, 1);
    w.add_player(1, 2);
    w.give_range(0, 1, 4);
    w.give_range(1, 5, 8);
    const auto& own = w.spawn(1, 1);
    const auto& enemy = w.spawn(5, 1);
    w.game().viewpoint_player = 0;
    UnitDef plain{};
    UnitDef commander{};
    commander.flags = OA_UNIT_DEF_FLAG_HIDE_DAMAGE;
    static_assert(OA_UNIT_DEF_FLAG_HIDE_DAMAGE == 0x40u << 8);
    CHECK(panel_shows_damage(*w.world, own, plain) && panel_shows_damage(*w.world, own, commander));
    CHECK(panel_shows_damage(*w.world, enemy, plain));
    CHECK(!panel_shows_damage(*w.world, enemy, commander));
}

} // namespace

int main() {
    trough_and_fill();
    scale_by_zoom();
    size_by_zoom();
    trough_and_fill_scaled();
    colour_by_thirds();
    no_bar_without_health();
    health_bars_follow_ownership();
    squad_digits_follow_ownership();
    panel_damage_follows_hide_damage();
    return 0;
}
