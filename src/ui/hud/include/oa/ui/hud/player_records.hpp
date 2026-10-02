// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Per-player starting resources, controllers and the saved-game "Players"
// section.
#pragma once

#include "oa/core/world.h"
#include "oa/data/campaign/campaign_file.hpp"
#include "oa/data/persist/hapibank.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

/// Starting shared storage never goes below this.
inline constexpr int32_t kMinimumStartStorage = 200;
/// PlayerSetupInfo.role bit of the host slot.
inline constexpr uint8_t kSetupRoleHost = 0x01;
/// Player.resource_flags bit: the player's storage adds to allied storage.
inline constexpr uint8_t kResourceSharesStorage = 0x01;

#pragma pack(push, 1)

/// Starting resources a campaign mission sets for each player (the campaign
/// file's metal and energy, data::campaign::CampaignFile).
struct MissionResources {
    float metal[OA_PLAYER_COUNT]{};
    float energy[OA_PLAYER_COUNT]{};
};

/// One player's row of the skirmish settings block Game.skirmish_info
/// points at.
struct SkirmishSlot {
    int32_t controller{}; // Player.status the slot starts with
    int32_t side{};       // PlayerSetupInfo.side
    int32_t alliance{};   // shared by allies; kNoRosterAlliance for none
    int32_t metal{};
    int32_t energy{};
    int32_t color{}; // PlayerSetupInfo.color
};

#pragma pack(pop)
static_assert(sizeof(MissionResources) == 0x50);
static_assert(sizeof(SkirmishSlot) == 0x18);
static_assert(offsetof(SkirmishSlot, side) == 0x4);
static_assert(offsetof(SkirmishSlot, alliance) == 0x8);
static_assert(offsetof(SkirmishSlot, metal) == 0xc);
static_assert(offsetof(SkirmishSlot, energy) == 0x10);
static_assert(offsetof(SkirmishSlot, color) == 0x14);

/// SkirmishSlot.alliance of a player allied with nobody.
inline constexpr int32_t kNoRosterAlliance = 5;
/// Players a skirmish roster can seat: one per player record.
inline constexpr int32_t kRosterCapacity = OA_PLAYER_RECORD_COUNT;

/// Resets a player record for a new game in the given status.
///
/// The record's alliance rows hold only itself; it takes the slot's index as
/// its number, board row, start position and transport id, joins no team, is
/// not ready and has no controller. Its info record takes the status (except
/// for OA_PLAYER_STATUS_MIRRORED), and a local or computer player reports the
/// machine's memory as physical_memory / 1 MiB + 1. In a campaign or skirmish
/// a human is named "Player" and a computer "Arm" or "Core" after its side;
/// the second name copies the name.
///
/// @param[in,out] world World holding the player records and their info records.
/// @param index Player record index; an index without a record does nothing.
/// @param status Player.status the record starts in.
/// @param session_kind CampaignFile.kind of the game.
/// @param physical_memory Machine memory in bytes.
void init_player_slot(
    World& world,
    uint8_t index,
    uint8_t status,
    oa::data::campaign::SessionKind session_kind,
    int32_t physical_memory
);

/// Finds the next roster entry from `start` on that fights beside `entry`.
///
/// @param roster Skirmish roster rows.
/// @param count Number of rows in `roster`.
/// @param entry Row whose allies are wanted.
/// @param start First row to test.
/// @return The first row at or after `start` that is an enabled entry of the
///         same alliance (not kNoRosterAlliance), or `entry` itself; -1 when
///         none is left or an argument is out of range.
[[nodiscard]] int32_t
next_roster_ally(const SkirmishSlot* roster, int32_t count, int32_t entry, int32_t start) noexcept;

/// Resets the first `count` player records from the skirmish roster.
///
/// A human or computer entry gives its record its side and colour, resets it
/// through init_player_slot and allies it with every entry that fights beside
/// it; the last human becomes the local and viewing player; any other entry's
/// record is reset free.
///
/// @param[in,out] world World whose player records and viewpoint change.
/// @param roster Skirmish roster rows.
/// @param count Number of rows; capped at kRosterCapacity.
/// @param session_kind CampaignFile.kind passed to init_player_slot.
/// @param physical_memory Machine memory in bytes, passed to init_player_slot.
void init_player_slots_from_roster(
    World& world,
    const SkirmishSlot* roster,
    int32_t count,
    oa::data::campaign::SessionKind session_kind,
    int32_t physical_memory
);

/// Seeds each player's starting energy and metal for a new game.
///
/// A campaign takes the mission's amounts, also as the player's shared
/// storage (at least kMinimumStartStorage) with kResourceSharesStorage set; a
/// skirmish takes the slots' amounts; a multiplayer game takes the host
/// player's settings (hundreds of metal and energy), or each player's own
/// when no slot is the host. Resumed games keep their saved values.
///
/// @param[in,out] world World whose players are seeded.
/// @param session_kind CampaignFile.kind of the game.
/// @param mission Mission starting resources, used in a campaign; may be null.
/// @param skirmish Skirmish slots, used in a skirmish; may be null.
void set_starting_resources(
    World& world,
    oa::data::campaign::SessionKind session_kind,
    const MissionResources* mission,
    const SkirmishSlot* skirmish
);

/// Reads each "Player<i>" controller into the skirmish slots and player status.
///
/// @param[in,out] world World whose Player.status values are set.
/// @param[in,out] bank Saved-game bank; its current account moves to each player's.
/// @param[out] skirmish Skirmish slots whose controllers are set; may be null.
/// @note Players the bank lacks get controller 0.
void load_player_controllers(World& world, data::persist::Bank& bank, SkirmishSlot* skirmish);

/// Restores the "Players" section of a saved game.
///
/// Reads the human player (who also becomes the viewpoint), the game clock
/// blob and, for each saved player, resources, totals, storage, kills and
/// losses, timers, colour, side and alliances. Players the bank lacks are set
/// free; an alliance blob of the wrong size is ignored, and each player stays
/// allied with itself.
///
/// @param[in,out] world World whose game clock and players are restored.
/// @param[in,out] bank Saved-game bank to read.
/// @return false when the "GameTime" blob is short.
bool load_players_section(World& world, data::persist::Bank& bank);

/// Writes the "Players" section for every player with a status.
///
/// @param world World whose game clock and players are saved.
/// @param[in,out] bank Saved-game bank to write.
void save_players_section(World& world, data::persist::Bank& bank);

/// A byte grid whose buffer is rounded up to a multiple of eight bytes.
struct ByteGrid {
    uint8_t* data{};
    int32_t width{};
    int32_t height{};
    uint32_t capacity{};
};

/// Replaces the grid's buffer with an uninitialised `width` x `height` one.
///
/// @param[in,out] grid Grid whose old buffer is freed.
/// @param width Grid width in bytes.
/// @param height Grid height in rows.
/// @return false when allocation fails; an empty grid has no buffer and succeeds.
bool byte_grid_resize(ByteGrid& grid, int32_t width, int32_t height);

/// Frees the grid's buffer and resets it to empty.
///
/// @param[in,out] grid Grid to release.
void byte_grid_free(ByteGrid& grid);

} // namespace oa::ui::hud
