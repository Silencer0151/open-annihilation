// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/scenario/commander_rules.hpp"

#include "oa/core/player_setup.h"
#include "oa/core/player.h"
#include "oa/sim/simulation_state.hpp"

namespace oa::sim::scenario {

namespace {

using sim::simulation_state::player_active;
using sim::simulation_state::player_participating;

constexpr uint8_t human_setup_state = 1;  // player-info state of a human, numbered as Player.status
constexpr uint8_t setup_role_host = 0x01; // player-info role bit of the host

bool watcher(const oa::World& world, const oa::Player& player) noexcept {
    const auto* info = world_player_info(&world, &player);
    return info != nullptr && (info->options & OA_SETUP_OPTION_WATCHER) != 0;
}

bool counts_allied_victory(const oa::World& world, const oa::Player& player) noexcept {
    const auto* info = world_player_info(&world, &player);
    return info != nullptr && (info->status & OA_SETUP_STATUS_ALLIED_VICTORY) != 0;
}

/// Tests whether a player simulated elsewhere is seated as a human.
///
/// @param world player-info records
/// @param player player record
/// @return true for an in-use player of that status whose player-info state is human
bool mirrored_human(const oa::World& world, const oa::Player& player) noexcept {
    const auto* info = world_player_info(&world, &player);
    return player.in_use != 0 && player.status == OA_PLAYER_STATUS_MIRRORED && info != nullptr &&
           info->state == human_setup_state;
}

} // namespace

int16_t board_score(const oa::Game& game, const oa::Player& player) noexcept {
    return deathmatch(game) ? player.commanders_killed : player.kills;
}

bool promote_on_kill_board(oa::World& world, oa::Player& killer) noexcept {
    const uint8_t row = killer.board_row;
    if (row == 0)
        return false;
    const int16_t score = board_score(world.game, killer);
    uint8_t best = row;
    for (const oa::Player& player : world.game.players) {
        if (player.status == OA_PLAYER_STATUS_FREE || watcher(world, player))
            continue;
        if (board_score(world.game, player) < score && player.board_row < best)
            best = player.board_row;
    }
    if (best >= row)
        return false;
    for (oa::Player& player : world.game.players) {
        if (best <= player.board_row && player.board_row < row)
            ++player.board_row;
    }
    killer.board_row = best;
    return best == 0;
}

int32_t connected_participants(const oa::World& world) noexcept {
    int32_t count = 0;
    for (const oa::Player& player : world.game.players) {
        if (!player_participating(player))
            continue;
        if (player.status != OA_PLAYER_STATUS_LOCAL && !mirrored_human(world, player))
            continue;
        if (!watcher(world, player))
            ++count;
    }
    return count;
}

int32_t computer_participants(const oa::World& world) noexcept {
    int32_t count = 0;
    for (const oa::Player& player : world.game.players)
        if (player.in_use != 0 && player.status == OA_PLAYER_STATUS_COMPUTER &&
            player_participating(player))
            ++count;
    return count;
}

bool defeated_player_watches(const oa::World& world) noexcept {
    const uint8_t local = world.game.local_player_index;
    if (local >= OA_PLAYER_COUNT || world.game.players[local].reject_reason != 0)
        return false;
    const auto* host =
        world_player_info(&world, world_player_record(&world, host_player_index(world)));
    const bool allowed = host != nullptr && (host->options & OA_SETUP_OPTION_WATCHING_ALLOWED) != 0;
    return allowed || computer_participants(world) > 0;
}

uint8_t host_player_index(const oa::World& world) noexcept {
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const oa::Player& player = world.game.players[index];
        const auto* info = world_player_info(&world, &player);
        if (player.status != OA_PLAYER_STATUS_FREE && info != nullptr &&
            (info->role & setup_role_host) != 0)
            return index;
    }
    return OA_PLAYER_COUNT;
}

bool session_cheats_allowed(
    oa::data::campaign::SessionKind kind, const oa::World& world, bool previous
) noexcept {
    switch (kind) {
    case oa::data::campaign::SessionKind::campaign:
        return false;
    case oa::data::campaign::SessionKind::skirmish:
        return true;
    case oa::data::campaign::SessionKind::multiplayer: {
        const uint8_t host = host_player_index(world);
        if (host == OA_PLAYER_COUNT)
            return true;
        const auto* info = world_player_info(&world, &world.game.players[host]);
        return (info->options & OA_SETUP_OPTION_CHEATS_ALLOWED) != 0;
    }
    default:
        return previous;
    }
}

bool multiplayer_victory(const oa::World& world) noexcept {
    if (deathmatch(world.game))
        return false;
    const uint8_t local_index = world.game.local_player_index;
    if (local_index >= OA_PLAYER_COUNT)
        return false;
    const oa::Player& local = world.game.players[local_index];
    for (uint8_t index = 0; index < OA_PLAYER_COUNT; ++index) {
        const oa::Player& other = world.game.players[index];
        if (index == local_index || !player_active(other) || watcher(world, other))
            continue;
        if (other.units_created == 0)
            return false;
        if (!player_participating(other))
            continue;
        if (!counts_allied_victory(world, other) || !counts_allied_victory(world, local) ||
            local.alliance[index] == 0 || local.allied_by[index] == 0)
            return false;
        for (uint8_t third = 0; third < OA_PLAYER_COUNT; ++third) {
            if (player_participating(world.game.players[third]) && other.alliance[third] == 0)
                return false;
        }
    }
    return true;
}

} // namespace oa::sim::scenario
