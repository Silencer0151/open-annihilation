// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The files the system opens in the game, caught as SDL delivers them: a
// .oamod file double-clicked in the Finder, opened from the iOS Files app
// or dropped on the window arrives as a drop event, which whatever polls
// SDL's queue at that moment might drop. A watch on SDL's events copies
// each into the mod packages' inbox (oa/app/mod_install/inbox.hpp) as it
// is queued.
#pragma once

namespace oa::app {

/// Watches SDL's events for the files the system opens in the game, and
/// takes those already queued. SDL drops its watches and its queue whenever
/// its video stops, so this is called right after each start of SDL's
/// video, before anything pumps its events; a watch already there is
/// replaced, never added twice.
void watch_opened_files() noexcept;

} // namespace oa::app
