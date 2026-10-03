// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Votes to reject a player (network.vote-reject): every machine keeps the
// same board of open votes, counts the yes and no votes it hears and
// removes the player itself once a vote passes. A vote is asked for by a
// player (manual) or opened by every machine on its own when a player stops
// answering (timeout). Times are the connection's 1/30 s clock.

#include "oa/core/world.h"

#include <cstdint>

namespace oa::netgame::match {

inline constexpr uint32_t vote_time_ticks_per_second = 30;
/// A manual vote that fails keeps its target from a new vote this long.
inline constexpr uint32_t vote_cooldown_seconds = 90;
/// Yes votes a timeout vote needs when more than one player takes part.
inline constexpr uint8_t timeout_vote_quorum = 2;

/// One open vote.
struct Vote {
    uint32_t target_id{}; ///< the player the vote would reject; 0 for a free entry
    uint8_t flag{};       ///< vote_flag_manual or vote_flag_timeout
    uint32_t deadline{};  ///< connection time the vote ends at
    uint16_t yes{};       ///< one bit per voting slot
    uint16_t no{};        ///< one bit per voting slot
};

/// A failed vote's target, kept from a new vote until a time.
struct VoteCooldown {
    uint32_t target_id{};
    uint32_t until{};
};

/// Every open vote of a match.
struct VoteBoard {
    Vote votes[OA_PLAYER_COUNT]{};
    VoteCooldown cooldowns[OA_PLAYER_COUNT]{};
};

/// Who votes on one vote.
struct VoteElectorate {
    uint8_t count{};          ///< players in the game: slots in use with a transport id
    uint16_t target_allies{}; ///< slots allied both ways with the target; one of them must vote yes
};

enum class VoteResult : uint8_t {
    open,
    passed, ///< the target is rejected
    failed, ///< the target stays
};

/// Returns the open vote on a player.
///
/// @param board the board
/// @param target_id the player's transport id
/// @return the vote, or null when none is open
[[nodiscard]] Vote* vote_find(VoteBoard& board, uint32_t target_id) noexcept;

/// Opens a vote on a player.
///
/// A vote already open on the player, or a player on cooldown, opens
/// nothing.
///
/// @param[in,out] board the board
/// @param target_id the player's transport id
/// @param flag vote_flag_manual or vote_flag_timeout
/// @param now connection time
/// @param window how long the vote stays open, in connection time
/// @return the new vote, or null when none was opened
Vote* vote_open(
    VoteBoard& board, uint32_t target_id, uint8_t flag, uint32_t now, uint32_t window
) noexcept;

/// Notes one player's vote.
///
/// A no withdraws an earlier yes and a yes an earlier no.
///
/// @param[in,out] vote the vote
/// @param voter_slot the voter's slot, 0..9
/// @param yes true for yes
void vote_cast(Vote& vote, uint8_t voter_slot, bool yes) noexcept;

/// Returns how many yes votes a vote needs.
///
/// @param vote the vote
/// @param electorate how many players take part
/// @return a manual vote max(1, (2n + 2) / 3); a timeout vote 2, or 1 with one player
[[nodiscard]] uint8_t vote_needed(const Vote& vote, uint8_t electorate) noexcept;

/// Counts a vote.
///
/// A vote passes with the yes votes it needs and, when the target has
/// allies, a yes from one of them. It fails once too many players voted no
/// for the rest to reach the yes votes it needs. When its time runs out
/// undecided, a timeout vote passes and a manual one fails.
///
/// @param vote the vote
/// @param electorate who votes
/// @param now connection time
/// @return open, passed or failed
[[nodiscard]] VoteResult
vote_tally(const Vote& vote, const VoteElectorate& electorate, uint32_t now) noexcept;

/// Closes a vote; a failed manual vote puts its target on cooldown.
///
/// @param[in,out] board the board
/// @param[in,out] vote the vote, which is freed
/// @param result how it ended
/// @param now connection time
void vote_close(VoteBoard& board, Vote& vote, VoteResult result, uint32_t now) noexcept;

/// Seconds a vote has left.
///
/// @param vote the vote
/// @param now connection time
/// @return whole seconds, 0 once it ran out
[[nodiscard]] uint32_t vote_seconds_left(const Vote& vote, uint32_t now) noexcept;

} // namespace oa::netgame::match
