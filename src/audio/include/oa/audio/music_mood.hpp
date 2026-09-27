// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"
#include "oa/core/game_state.h"

#include <cstddef>
#include <cstdint>

namespace oa::audio {

inline constexpr std::size_t music_activity_slots = 30;

// Combat-activity history that switches the music between calm (kind 0) and
// battle (kind 1) tracks.
struct MusicMood {
    uint32_t last_update{};
    int32_t ticks_since_switch{};
    int32_t ring_index{};
    int32_t activity[music_activity_slots]{};
    int32_t applied_kind{-1}; // no kind applied yet
};

/// Advances the combat-activity ring and switches between calm and battle music.
///
/// Runs at most once per 30 clock ticks during a live game. After ten
/// updates it sums the whole ring and its five newest slots: heavy activity
/// (a ring total of 51 or five-slot total of 31) with a force of at least 31
/// units selects battle music; a nearly silent ring for 61 updates returns
/// to calm music. Each run then moves to and clears the next ring slot.
///
/// @param[in,out] mood Activity history and switch state.
/// @param[in,out] mixer Mixer whose music kind is switched.
/// @param game Game state supplying the live-game gates and the local player's unit count.
/// @param now Current clock, in ticks.
/// @return True when the update ran; the caller then steps the game's
///         countdown timer.
bool music_mood_update(MusicMood& mood, Mixer& mixer, const Game& game, uint32_t now) noexcept;

/// Adds combat activity to the current ring slot.
///
/// @param[in,out] mood Activity history.
/// @param amount Activity to add.
/// @return The slot's new total.
int32_t music_mood_add_activity(MusicMood& mood, int32_t amount) noexcept;

} // namespace oa::audio
