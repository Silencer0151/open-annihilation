// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The recorder test extensions: the recorder (recorder.cpp), which fills
// every hook of oa-game's extension table, and the follower (follower.cpp),
// an extension built on it whose library links the recorder's, which fills
// every hook but those one extension at most may fill. Both count their
// calls in the recorder's counts, which go to the file --record-hooks names
// when oa-game exits.
#pragma once

#include "oa/app/extension.hpp"

#include <cstdint>

namespace oa::app::hook_recorder {

/// Counts one call of a hook.
///
/// @param hook the hook's name, "follower." first for the follower's calls
/// @param detail the enumerator it was given, or another word that tells
///        the call apart; null for none
void record(const char* hook, const char* detail = nullptr);

/// Returns how many calls of a hook were counted.
///
/// @param hook the hook's name, as record() was given it
/// @param detail the word record() was given with it; null for none
/// @return the calls counted so far
[[nodiscard]] uint64_t calls(const char* hook, const char* detail = nullptr);

} // namespace oa::app::hook_recorder

/// Fills the recorder's table (its oa_add_extension INIT).
///
/// @param[out] table the recorder's table, zeroed on entry
void oa_extension_init_recorder(oa::app::Extension* table);

/// Fills the follower's table (its oa_add_extension INIT).
///
/// With OA_RECORDER_FOLLOWER_FRONTEND_GAME set to 1 in the environment it
/// fills frontend_game too, which the recorder fills: the game must refuse
/// to start.
///
/// @param[out] table the follower's table, zeroed on entry
void oa_extension_init_recorder_follower(oa::app::Extension* table);
