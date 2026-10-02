// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/health_bar.hpp"

#include <cstdint>

namespace oa::ui::hud {
namespace {

bool viewpoint_owns(const World& world, const Unit& unit) noexcept {
    const Player* owner = world_unit_owner(&world, &unit);
    return owner != nullptr && owner->index == world.game.viewpoint_player;
}

} // namespace

bool draws_health_bar(const World& world, const Unit& unit) noexcept {
    return (world.game.graphics_flags & kGraphicsDamageBars) != 0 && viewpoint_owns(world, unit);
}

bool draws_squad_digit(const World& world, const Unit& unit) noexcept {
    return unit.squad != 0 && draws_health_bar(world, unit);
}

char squad_digit(const Unit& unit) noexcept {
    return static_cast<char>(static_cast<uint8_t>('0' + static_cast<uint8_t>(unit.squad)));
}

bool panel_shows_damage(const World& world, const Unit& unit, const UnitDef& def) noexcept {
    return viewpoint_owns(world, unit) || (def.flags & OA_UNIT_DEF_FLAG_HIDE_DAMAGE) == 0;
}

bool unit_health_bar(
    const Game& game, const Unit& unit, const UnitDef& def, int32_t x, int32_t y, HealthBar& bar
) noexcept {
    const int32_t health = unit.health;
    if (health <= 0 || def.max_damage == 0)
        return false;
    bar.trough = {
        x - kHealthBarHalfWidth,
        y - kHealthBarHalfHeight,
        x + kHealthBarHalfWidth,
        y + kHealthBarHalfHeight,
    };
    bar.trough_color = game.ui_colors[kHealthTroughColor];
    const auto fill_left = bar.trough.x1 + 1;
    const auto filled = (static_cast<uint32_t>(health) << kHealthBarFillShift) / def.max_damage;
    bar.fill = {
        fill_left,
        bar.trough.y1 + 1,
        static_cast<int32_t>(filled + static_cast<uint32_t>(fill_left)),
        bar.trough.y2 - 1,
    };
    const auto third = def.max_damage / 3u;
    int32_t slot = kHealthLowColor;
    if (health > static_cast<int32_t>(third * 2u))
        slot = kHealthHighColor;
    else if (health > static_cast<int32_t>(third))
        slot = kHealthMidColor;
    bar.fill_color = game.ui_colors[slot];
    return true;
}

} // namespace oa::ui::hud
