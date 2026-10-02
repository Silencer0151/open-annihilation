// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The steps of --check-multiplayer-menu that an extension built on network
// play reuses in its own part of the check: whether the main menu shows, a
// click as a player makes it, and one round of MULTI. Each drives the
// running game through the engine's check host (oa/app/check_host.hpp) and
// is defined by network play's extension library (oa-app-netgame-sdk).
#pragma once

#include <cstdint>

namespace oa::app {
struct CheckHost;
}

namespace oa::app::netgame::menu_check {

/// Tells whether the main menu shows: its screen, in the frontend's main-menu state.
///
/// @param host the running game's check host
/// @return true on the main menu
[[nodiscard]] bool on_main_menu(const oa::app::CheckHost& host);

/// Clicks a point of the 640x480 canvas as a player does.
///
/// The pointer moves there, presses and releases through the SDL
/// presenter, each followed by the frame the game runs after it; a few more
/// frames then let the click take effect.
///
/// @param host the running game's check host
/// @param x canvas column
/// @param y canvas row
void click(const oa::app::CheckHost& host, int32_t x, int32_t y);

/// Runs one round of MULTI from the main menu.
///
/// MULTI opens SELPROV listing only the TCP/IP provider, SELECT on it opens
/// TCP.GUI, Cancel returns to the list and Main Menu to the main menu. A
/// failure throws std::runtime_error.
///
/// @param host the running game's check host, on the main menu
void multiplayer_round(const oa::app::CheckHost& host);

} // namespace oa::app::netgame::menu_check
