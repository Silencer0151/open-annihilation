// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-multiplayer-menu with network play, which drives the running game
// through the engine's check host (oa/app/check_host.hpp). The steps of it
// that an extension built on network play reuses in its own part of the
// check are network play's public ones (oa/app/netgame/menu_check.hpp).
#pragma once

namespace oa::app {

struct CheckHost;
struct Options;

/// Checks --check-multiplayer-menu with network play.
///
/// Twice from the main menu a round of MULTI
/// (oa::app::netgame::menu_check::multiplayer_round). Then once more on to hosting:
/// 127.0.0.1 typed into TCP.GUI and OK open the game list, New opens
/// NEWMULTI and a typed game name fills its field, while the cursor follows
/// the pointer on every screen; Previous, Previous Menu and Main Menu lead
/// back, and text input is off again on the main menu. Every click and key
/// goes through the SDL presenter as a player's does, and typed text
/// arrives only while SDL has text input on. With --snapshot, the hosting
/// round's frames are written as <stem>-<step>.ppm beside it. A failure, or
/// a run without the game's window, throws std::runtime_error.
///
/// @param host the running game's check host
/// @param options the game's command-line options, for --snapshot
void check_multiplayer_screens(const CheckHost& host, const Options& options);

} // namespace oa::app
