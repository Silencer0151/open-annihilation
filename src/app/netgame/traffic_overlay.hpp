// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's traffic readouts, drawn over the battlefield through the
// engine's match overlay: the send and receive rates "BPS" shows, and the
// traffic line of the debug keys.
#pragma once

#include "oa/app/extension.hpp"
#include "oa/netgame/condenser.hpp"

#include <cstdint>

namespace oa::app {

/// Draws the traffic readouts over the battlefield.
///
/// While "BPS" is on (Game.show_bandwidth), "Send - x K/s" and under it
/// "Receive - x K/s", the rates in thousands of bytes a second with one
/// decimal, start 63 pixels above the bottom bar and 1 right of the
/// battlefield's edge. Each label is followed, one font height lower, by a
/// 65 by 9 pixel bar outlined in UI colour 15 and filled in it from the left
/// to the rate's share of 5600 bytes a second; the next label starts one
/// pixel under the bar. The labels are white, in the side panel's font, and
/// step by its height. With the debug keys on (Game.outcome_flags) in a live
/// game (Game.session_flags), the traffic debug line follows in the message
/// log's font and UI colour 15, 60 pixels right of the battlefield's edge
/// and four of that font's heights less 42 pixels below its top. Pixels are
/// the game's 640x480 ones, overlay.scale painter pixels each.
///
/// @param overlay the battlefield and its painter
/// @param[in,out] traffic the connection's counters, whose rate samples are
///        refreshed; null without a connection, when both rates read 0
/// @param now_time the connection's time in 1/30 s ticks; unused without traffic
/// @quirk The rates show whether or not the game is played over a
///        connection, 0.0 K/s in a skirmish, as in 3.1c.
/// @quirk With both debug bits of Game.outcome_flags set (the debug keys and
///        the 'i' toggle), the labels are in the message log's font and step
///        by its height, as in 3.1c.
void draw_traffic_overlay(
    const MatchOverlay& overlay, oa::netgame::TrafficStats* traffic, uint32_t now_time
);

} // namespace oa::app
