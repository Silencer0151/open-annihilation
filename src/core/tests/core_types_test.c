// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Compiled as C11: every canonical header must stay C-compatible. */
#include "oa/core/cob.h"
#include "oa/core/feature_def.h"
#include "oa/core/game_state.h"
#include "oa/core/player_setup.h"
#include "oa/core/map_plot.h"
#include "oa/core/move_class.h"
#include "oa/core/player.h"
#include "oa/core/projectile.h"
#include "oa/core/side.h"
#include "oa/core/types.h"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"
#include "oa/core/weapon_def.h"
#include "oa/core/world.h"

#include <stdio.h>
#include <string.h>

static int failures;

static void check_size(const char* name, size_t actual, size_t expected) {
    if (actual != expected) {
        fprintf(stderr, "%s: size %zu, expected %zu\n", name, actual, expected);
        ++failures;
    }
}

int main(void) {
    static Game game;
    Unit unit;

    check_size("Unit", sizeof(Unit), 0x118);
    check_size("Player", sizeof(Player), 0x14b);
    check_size("UnitDef", sizeof(UnitDef), 0x249);
    check_size("WeaponDef", sizeof(WeaponDef), 0x115);
    check_size("FeatureDef", sizeof(FeatureDef), 0x100);
    check_size("Side", sizeof(Side), 0x232);
    check_size("MoveClass", sizeof(MoveClass), 0x20);
    check_size("Projectile", sizeof(Projectile), 0x6b);
    check_size("CobThread", sizeof(CobThread), 0xa4);
    check_size("CobMachine", sizeof(CobMachine), 0x544);
    check_size("Game", sizeof(Game), 0x3924d);
    check_size("PlayerSetupInfo", sizeof(PlayerSetupInfo), 0xb9);
    check_size("MapPlot", sizeof(MapPlot), 0xd);
    check_size("ScoreEntry", sizeof(ScoreEntry), 0x3a);

    memset(&unit, 0, sizeof unit);
    unit.position.x = 0x10000;
    unit.weapons[2].flags = OA_UNIT_WEAPON_ENABLED;
    if (((const unsigned char*)&unit)[0x6c] != 1 || ((const unsigned char*)&unit)[0x57] != 2) {
        fprintf(stderr, "Unit field bytes misplaced\n");
        ++failures;
    }
    game.players[1].unit_count = 7;
    if (((const unsigned char*)&game)[0x1df2] != 7) {
        fprintf(stderr, "Game.players stride misplaced\n");
        ++failures;
    }
    game.weapon_defs[3].flags = OA_WEAPON_FLAG_LINE_OF_SIGHT | OA_WEAPON_FLAG_BALLISTIC;
    if (((const unsigned char*)&game)[0x2cf3 + 3 * 0x115 + 0x111] != 3) {
        fprintf(stderr, "WeaponDef.flags misplaced\n");
        ++failures;
    }
    if (OA_WEAPON_FLAG_VLAUNCH != 0x10u || OA_WEAPON_FLAG_TURRET != 0x80000u ||
        OA_WEAPON_FLAG_INTERCEPTOR != 0x40000000u || OA_UNIT_FLAG_SELECTED != 0x10u ||
        OA_UNIT_DEF_FLAG_HAS_WEAPONS != 0x10000u || OA_UNIT_DEF_FLAG_AVAILABLE != 0x800000u) {
        fprintf(stderr, "flag bit values\n");
        ++failures;
    }
    /* The income getters read energy_produced and metal_produced; the surplus
     * subtracts the requested amounts without rounding to float, so 1 - 2^-26
     * stays below 1. */
    game.players[2].energy_produced = 1.0f;
    game.players[2].energy_requested = 0x1p-26f;
    game.players[2].metal_produced = 4.5f;
    game.players[2].metal_requested = 0x1p-26f;
    if (((const unsigned char*)&game.players[2])[0x93] != 0x3f ||
        player_energy_produced(&game.players[2]) != 1.0f ||
        player_metal_produced(&game.players[2]) != 4.5f ||
        !(player_energy_surplus(&game.players[2]) < 1.0) ||
        player_metal_surplus(&game.players[2]) != 4.5 - 0x1p-26) {
        fprintf(stderr, "player income getters\n");
        ++failures;
    }
    return failures == 0 ? 0 : 1;
}
