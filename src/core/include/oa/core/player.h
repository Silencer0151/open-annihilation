// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

/* Player record. */
#ifndef OA_CORE_PLAYER_H
#define OA_CORE_PLAYER_H

#include "oa/core/types.h"

#define OA_PLAYER_COUNT 10
/* Records the game state holds: the ten slots and the record at index 10
 * (OA_PLAYER_COUNT), which lookups that find no player resolve to. */
#define OA_PLAYER_RECORD_COUNT (OA_PLAYER_COUNT + 1)
#define OA_PLAYER_NO_TEAM 5

/* Values of Player.status. */
#define OA_PLAYER_STATUS_FREE 0
#define OA_PLAYER_STATUS_LOCAL 1
#define OA_PLAYER_STATUS_COMPUTER 2
#define OA_PLAYER_STATUS_MIRRORED 3
#define OA_PLAYER_STATUS_CLOSED 4

/* Player.resource_flags bit: the player shares its allies' storage. */
#define OA_PLAYER_RESOURCE_SHARES_STORAGE 0x01u

OA_CORE_BEGIN

#pragma pack(push, 1)

/* The 0x14b-byte player record; eleven live inline in the game state. */
typedef struct Player {
    uint32_t in_use;
    uint32_t player_id;
    uint32_t join_tick;        /* ? time the player joined the game setup */
    uint32_t machine_group;    /* ? 0 none, 1..10 */
    uint8_t update_count[0x4]; /* updates heard from the player's machine */
    int32_t latency;           /* ? round-trip time, milliseconds */
    int32_t last_sim_tick;
    uint32_t last_update_time; /* when the player's machine was last heard from */
    uint8_t load_progress;     /* percent of a multiplayer game the player has loaded, 0..100 */
    uint8_t machine_flags;     /* ? bit 0: the player's machine answered a probe; bit 1: taken
                                  from the session's setup options, cleared for a computer player */
    uint8_t reject_reason;     /* 0 live */
    uint8_t reserved_after_reject_reason[0x4]; /* zero from creation; the engine never reads it */
    oa_ref32 info;                             /* PlayerSetupInfo */
    char name[30];
    char second_name[30];   /* ? a copy of name once a slot is reset for a game */
    oa_ref32 first_unit;    /* Unit; inclusive range */
    oa_ref32 last_unit;     /* Unit; inclusive range */
    uint16_t base_unit_id;  /* Unit.id of first_unit */
    uint16_t last_unit_id;  /* Unit.id of last_unit */
    uint8_t status;         /* PLAYER_STATUS_* */
    oa_ref32 controller;    /* ? computer player; released with the slot */
    oa_ref32 squads;        /* ten 0x20-byte squad records */
    oa_ref32 coverage_grid; /* byte grid */
    uint32_t sight_width;
    uint32_t sight_height;
    uint8_t reserved_after_sight_height[0x4]; /* zero from creation; the engine never reads it */
    float energy;
    float energy_produced;
    float energy_requested;
    float metal;
    float metal_produced;
    float metal_requested;
    float energy_storage;
    float metal_storage;
    double energy_produced_total;
    double metal_produced_total;
    double energy_requested_total;
    double metal_requested_total;
    double energy_wasted_total;
    double metal_wasted_total;
    float shared_energy_storage;
    float shared_metal_storage;
    float metal_share_threshold; /* SetShareMetal level; the resource bar marks it */
    float energy_share_threshold;
    oa_ref32 economy; /* UnitEconomy-shaped staging block */
    uint32_t next_economy_tick;
    int32_t win_lose_time; /* the save file stores it as "WinLoseTime"; the engine only saves
                              and restores it */
    int32_t display_timer; /* the resource bar refreshes its income figures on the first update
                              after this tick, then advances it by the refresh interval; the
                              save file stores it as "DisplayTimer" */
    int16_t kills;
    int16_t losses;
    uint8_t reserved_after_losses[0x4]; /* zero from creation; the engine never reads it */
    int16_t commanders_killed;
    int16_t commanders_lost;
    uint8_t alliance[11];  /* indexed by other Player.index */
    uint8_t allied_by[11]; /* other Player.index allies with this player */
    uint8_t economy_requested[0xb];
    uint8_t economy_processed[11]; /* indexed by Player.index; set rows skip self-destruct losses */
    uint8_t economy_answered[0xb];
    uint8_t team; /* OA_PLAYER_NO_TEAM when unset */
    uint32_t units_created;
    uint16_t unit_count; /* live units */
    uint8_t index;       /* 10 = inactive */
    uint8_t
        start_position; /* ? start position: the index at setup, then the value from the game-start record */
    uint8_t board_row;      /* row on the kills board, 0 leads; starts at the index */
    uint8_t resource_flags; /* bit0 shares allied storage */
    uint8_t reserved_after_resource_flags[0x1]; /* zero from creation; the engine never reads it */
} Player;

#pragma pack(pop)

OA_ASSERT_SIZE(Player, 0x14b);
OA_ASSERT_OFFSET(Player, in_use, 0x0);
OA_ASSERT_OFFSET(Player, player_id, 0x4);
OA_ASSERT_OFFSET(Player, join_tick, 0x8);
OA_ASSERT_OFFSET(Player, machine_group, 0xc);
OA_ASSERT_OFFSET(Player, update_count, 0x10);
OA_ASSERT_OFFSET(Player, latency, 0x14);
OA_ASSERT_OFFSET(Player, last_sim_tick, 0x18);
OA_ASSERT_OFFSET(Player, last_update_time, 0x1c);
OA_ASSERT_OFFSET(Player, load_progress, 0x20);
OA_ASSERT_OFFSET(Player, machine_flags, 0x21);
OA_ASSERT_OFFSET(Player, reject_reason, 0x22);
OA_ASSERT_OFFSET(Player, reserved_after_reject_reason, 0x23);
OA_ASSERT_OFFSET(Player, info, 0x27);
OA_ASSERT_OFFSET(Player, name, 0x2b);
OA_ASSERT_OFFSET(Player, second_name, 0x49);
OA_ASSERT_OFFSET(Player, first_unit, 0x67);
OA_ASSERT_OFFSET(Player, last_unit, 0x6b);
OA_ASSERT_OFFSET(Player, base_unit_id, 0x6f);
OA_ASSERT_OFFSET(Player, last_unit_id, 0x71);
OA_ASSERT_OFFSET(Player, status, 0x73);
OA_ASSERT_OFFSET(Player, controller, 0x74);
OA_ASSERT_OFFSET(Player, squads, 0x78);
OA_ASSERT_OFFSET(Player, coverage_grid, 0x7c);
OA_ASSERT_OFFSET(Player, sight_width, 0x80);
OA_ASSERT_OFFSET(Player, sight_height, 0x84);
OA_ASSERT_OFFSET(Player, reserved_after_sight_height, 0x88);
OA_ASSERT_OFFSET(Player, energy, 0x8c);
OA_ASSERT_OFFSET(Player, energy_produced, 0x90);
OA_ASSERT_OFFSET(Player, energy_requested, 0x94);
OA_ASSERT_OFFSET(Player, metal, 0x98);
OA_ASSERT_OFFSET(Player, metal_produced, 0x9c);
OA_ASSERT_OFFSET(Player, metal_requested, 0xa0);
OA_ASSERT_OFFSET(Player, energy_storage, 0xa4);
OA_ASSERT_OFFSET(Player, metal_storage, 0xa8);
OA_ASSERT_OFFSET(Player, energy_produced_total, 0xac);
OA_ASSERT_OFFSET(Player, metal_produced_total, 0xb4);
OA_ASSERT_OFFSET(Player, energy_requested_total, 0xbc);
OA_ASSERT_OFFSET(Player, metal_requested_total, 0xc4);
OA_ASSERT_OFFSET(Player, energy_wasted_total, 0xcc);
OA_ASSERT_OFFSET(Player, metal_wasted_total, 0xd4);
OA_ASSERT_OFFSET(Player, shared_energy_storage, 0xdc);
OA_ASSERT_OFFSET(Player, shared_metal_storage, 0xe0);
OA_ASSERT_OFFSET(Player, metal_share_threshold, 0xe4);
OA_ASSERT_OFFSET(Player, energy_share_threshold, 0xe8);
OA_ASSERT_OFFSET(Player, economy, 0xec);
OA_ASSERT_OFFSET(Player, next_economy_tick, 0xf0);
OA_ASSERT_OFFSET(Player, win_lose_time, 0xf4);
OA_ASSERT_OFFSET(Player, display_timer, 0xf8);
OA_ASSERT_OFFSET(Player, kills, 0xfc);
OA_ASSERT_OFFSET(Player, losses, 0xfe);
OA_ASSERT_OFFSET(Player, reserved_after_losses, 0x100);
OA_ASSERT_OFFSET(Player, commanders_killed, 0x104);
OA_ASSERT_OFFSET(Player, commanders_lost, 0x106);
OA_ASSERT_OFFSET(Player, economy_processed, 0x129);
OA_ASSERT_OFFSET(Player, alliance, 0x108);
OA_ASSERT_OFFSET(Player, allied_by, 0x113);
OA_ASSERT_OFFSET(Player, economy_requested, 0x11e);
OA_ASSERT_OFFSET(Player, economy_answered, 0x134);
OA_ASSERT_OFFSET(Player, team, 0x13f);
OA_ASSERT_OFFSET(Player, units_created, 0x140);
OA_ASSERT_OFFSET(Player, unit_count, 0x144);
OA_ASSERT_OFFSET(Player, index, 0x146);
OA_ASSERT_OFFSET(Player, start_position, 0x147);
OA_ASSERT_OFFSET(Player, board_row, 0x148);
OA_ASSERT_OFFSET(Player, resource_flags, 0x149);
OA_ASSERT_OFFSET(Player, reserved_after_resource_flags, 0x14a);

/// Returns the energy produced at the last economy settlement.
///
/// @param player player record read
/// @return Player.energy_produced
static inline float player_energy_produced(const Player* player) {
    return player->energy_produced;
}

/// Returns the energy requested at the last economy settlement.
///
/// @param player player record read
/// @return Player.energy_requested
static inline float player_energy_requested(const Player* player) {
    return player->energy_requested;
}

/// Returns energy produced less energy requested at the last settlement.
///
/// @param player player record read
/// @return the difference at double precision
/// @quirk 3.1c keeps the difference at double precision: it is not rounded
///        back to float before callers compare it.
static inline double player_energy_surplus(const Player* player) {
    return (double)player->energy_produced - (double)player->energy_requested;
}

/// Returns the metal produced at the last economy settlement.
///
/// @param player player record read
/// @return Player.metal_produced
static inline float player_metal_produced(const Player* player) {
    return player->metal_produced;
}

/// Returns the metal requested at the last economy settlement.
///
/// @param player player record read
/// @return Player.metal_requested
static inline float player_metal_requested(const Player* player) {
    return player->metal_requested;
}

/// Returns metal produced less metal requested at the last settlement.
///
/// @param player player record read
/// @return the difference at double precision
/// @quirk 3.1c keeps the difference at double precision: it is not rounded
///        back to float before callers compare it.
static inline double player_metal_surplus(const Player* player) {
    return (double)player->metal_produced - (double)player->metal_requested;
}

OA_CORE_END

#endif
