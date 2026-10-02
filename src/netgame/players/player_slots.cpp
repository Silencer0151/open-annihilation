// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/player_slots.hpp"

namespace oa::netgame {

uint32_t player_slot_id(const Game& game, uint8_t slot) noexcept {
    if (slot >= OA_PLAYER_COUNT || game.players[slot].status == OA_PLAYER_STATUS_FREE)
        return no_player_id;
    return game.players[slot].player_id;
}

uint8_t player_slot_of(const Game& game, uint32_t player_id) noexcept {
    if (player_id == no_player_id)
        return no_player_slot;
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot)
        if (player_slot_id(game, slot) == player_id)
            return slot;
    return no_player_slot;
}

const Player* player_of_id(const Game& game, uint32_t player_id) noexcept {
    const auto slot = player_slot_of(game, player_id);
    return slot == no_player_slot ? nullptr : &game.players[slot];
}

Player* player_of_id(Game& game, uint32_t player_id) noexcept {
    const auto slot = player_slot_of(game, player_id);
    return slot == no_player_slot ? nullptr : &game.players[slot];
}

uint32_t first_local_player_id(const Game& game) noexcept {
    for (const auto& player : game.players)
        if (player.status == OA_PLAYER_STATUS_LOCAL)
            return player.player_id;
    return no_player_id;
}

uint8_t free_player_slot(const Game& game) noexcept {
    for (uint8_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const auto& player = game.players[slot];
        if (player.in_use == 0 && player.status != OA_PLAYER_STATUS_CLOSED)
            return slot;
    }
    return no_player_slot;
}

} // namespace oa::netgame
