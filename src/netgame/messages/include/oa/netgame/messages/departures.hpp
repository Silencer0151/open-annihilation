// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The departure notice a network game posts to the message log once a
// player has left.

#include "oa/core/world.h"
#include "oa/sim/messages.hpp"

#include <cstdint>

namespace oa::netgame::messages {

inline constexpr uint32_t departure_message_count =
    8; // picked by the low three bits of a random draw
inline constexpr const char* departure_messages[departure_message_count] = {
    "has left the scene",
    "has been shown the door",
    "has gone to a better place",
    "has bowed out",
    "has terminated",
    "has been eradicated",
    "has been liquidated",
    "has been obliterated",
};
// Player.reject_reason of the local record that keeps departures unannounced.
inline constexpr uint8_t reject_reason_departures_muted = 1;

/// Posts the notice that a player has left: its name and one of eight random phrases.
///
/// Silent while the local record's reject reason is 1. The player is the
/// first of the ten records the id names. The phrase is translated through
/// the hooks and the line posted as an elimination message.
///
/// @param[in,out] world World whose message log receives the line.
/// @param net_id Transport id of the departed player; unknown ids post nothing.
/// @param hooks Message hooks supplying the random roll (low three bits pick the phrase) and translation.
void post_departure(World& world, uint32_t net_id, const sim::messages::Hooks& hooks);

} // namespace oa::netgame::messages
