// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The integrity check (network.vercheck): each machine challenges every
// other human player with 32 random bytes over the private chat channel and
// expects two keyed answers back, one over the program and one over the game
// data; at tick 600 it reports the players whose answers were missing or
// differed. Nothing is ever blocked over it. This machine answers every
// challenge with keyed digests of its own identity, so two machines with the
// same program and game data agree and any other program reports a
// mismatch.

#include "oa/core/world.h"
#include "oa/netgame/private_channel.hpp"

#include <cstdint>

namespace oa::netgame::match {

inline constexpr uint32_t integrity_challenge_tick = 180;
inline constexpr uint32_t integrity_retry_end_tick = 450;
inline constexpr uint32_t integrity_period_ticks = 30;
inline constexpr uint32_t integrity_periodic_end_tick = 3600;
inline constexpr uint32_t integrity_report_tick = 600;

/// What this machine answers about itself.
struct IntegrityIdentity {
    uint8_t program[integrity_answer_bytes]{};   ///< digest of the program
    uint8_t game_data[integrity_answer_bytes]{}; ///< digest of the game data the match loaded
};

/// One challenged player.
struct IntegrityPeer {
    uint32_t player_id{}; ///< 0 for a free entry
    uint8_t nonce[integrity_nonce_bytes]{};
    bool challenged{};
    bool program_answered{};
    bool data_answered{};
    bool program_matches{};
    bool data_matches{};
};

/// The integrity check of one match.
struct IntegrityCheckState {
    IntegrityIdentity identity{};
    IntegrityPeer peers[OA_PLAYER_COUNT]{};
    bool reported{};
};

/// Computes the keyed answer to a challenge: HMAC-SHA-256 keyed with the challenge.
///
/// @param nonce the challenge's 32 bytes
/// @param digest what is answered for, 32 bytes
/// @param[out] out the 32-byte answer
void integrity_answer(
    const uint8_t (&nonce)[integrity_nonce_bytes],
    const uint8_t (&digest)[integrity_answer_bytes],
    uint8_t (&out)[integrity_answer_bytes]
) noexcept;

/// Tells whether a tick sends challenges.
///
/// @param form when the check challenges
/// @param tick the game tick
/// @return true at tick 180, and for the periodic form every 30 ticks after it until tick 3600
[[nodiscard]] bool integrity_challenge_due(IntegrityCheck form, uint32_t tick) noexcept;

/// Returns the entry of a player, taking a free one when it has none.
///
/// @param[in,out] state the check
/// @param player_id the player's transport id
/// @return the entry, or null when every entry is taken
[[nodiscard]] IntegrityPeer*
integrity_peer(IntegrityCheckState& state, uint32_t player_id) noexcept;

/// Notes an answer from a challenged player and whether it matches this machine's own.
///
/// @param[in,out] peer the player's entry; an answer to no challenge is ignored
/// @param op integrity_op_module_reply or integrity_op_data_reply
/// @param answer the 32 bytes received
/// @param identity this machine's identity
void integrity_note_answer(
    IntegrityPeer& peer, uint8_t op, const uint8_t* answer, const IntegrityIdentity& identity
) noexcept;

/// Counts the challenged players whose answers were missing or did not match.
///
/// @param state the check
/// @return how many players the report names
[[nodiscard]] int32_t integrity_issue_count(const IntegrityCheckState& state) noexcept;

} // namespace oa::netgame::match
