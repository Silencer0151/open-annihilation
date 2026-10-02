// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/messages/departures.hpp"

#include "oa/netgame/player_slots.hpp"

#include <cstdio>
#include <cstring>

namespace oa::netgame::messages {
namespace {
constexpr std::size_t formatted_bytes = 200;
} // namespace

void post_departure(World& world, uint32_t net_id, const sim::messages::Hooks& hooks) {
    const Player* local = world_player_record(&world, world.game.local_player_index);
    if (local != nullptr && local->reject_reason == reject_reason_departures_muted)
        return;
    const Player* player = player_of_id(world.game, net_id);
    if (player == nullptr)
        return;
    const uint32_t roll = hooks.random != nullptr ? hooks.random(hooks.context) : 0;
    const char* phrase =
        sim::messages::translate(hooks, departure_messages[roll & (departure_message_count - 1)]);
    char name[sizeof player->name + 1];
    std::memcpy(name, player->name, sizeof player->name);
    name[sizeof player->name] = '\0';
    char line[formatted_bytes];
    std::snprintf(line, sizeof line, "%s %s", name, phrase);
    sim::messages::post_message(
        world, line, sim::messages::kind_elimination, 0, player->index, hooks
    );
}

} // namespace oa::netgame::messages
