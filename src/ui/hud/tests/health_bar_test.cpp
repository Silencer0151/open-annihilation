// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/health_bar.hpp"

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
    colour_by_thirds();
    no_bar_without_health();
    health_bars_follow_ownership();
    panel_damage_follows_hide_damage();
    return 0;
}
