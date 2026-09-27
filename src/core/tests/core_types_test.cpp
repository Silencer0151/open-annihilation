// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Compiled as C++20: the same headers must expose the records in namespace oa.
#include "oa/core/cob.h"
#include "oa/core/feature_def.h"
#include "oa/core/game_state.h"
#include "oa/core/move_class.h"
#include "oa/core/player.h"
#include "oa/core/projectile.h"
#include "oa/core/side.h"
#include "oa/core/types.h"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"
#include "oa/core/world.h"

#include <cstdio>
#include <cstring>
#include <type_traits>

static_assert(std::is_trivially_copyable_v<oa::Game>);
static_assert(std::is_standard_layout_v<oa::Unit>);
static_assert(sizeof(oa::Game::weapon_defs) == OA_WEAPON_DEF_COUNT * sizeof(oa::WeaponDef));
static_assert(
    offsetof(oa::Game, weapon_defs) + sizeof(oa::Game::weapon_defs) ==
    offsetof(oa::Game, projectile_count)
);
static_assert(
    offsetof(oa::Game, sides) + sizeof(oa::Game::sides) == offsetof(oa::Game, last_frame_time)
);
static_assert(
    offsetof(oa::CobMachine, threads) + sizeof(oa::CobMachine::threads) ==
    offsetof(oa::CobMachine, active_threads)
);

int main() {
    static oa::Game game{};
    oa::Unit unit{};
    int failures = 0;

    unit.health = 0x1234;
    unsigned char bytes[sizeof unit];
    std::memcpy(bytes, &unit, sizeof unit);
    if (bytes[0x108] != 0x34 || bytes[0x109] != 0x12) {
        std::fprintf(stderr, "Unit.health misplaced\n");
        ++failures;
    }
    game.weapon_defs[1].flags = OA_WEAPON_FLAG_STOCKPILE;
    const auto* raw = reinterpret_cast<const unsigned char*>(&game);
    if (raw[0x2cf3 + 0x115 + 0x114] != 0x10) {
        std::fprintf(stderr, "WeaponDef.flags misplaced\n");
        ++failures;
    }
    game.tick = 0x01020304;
    if (raw[0x38a47] != 0x04) {
        std::fprintf(stderr, "Game.tick misplaced\n");
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
