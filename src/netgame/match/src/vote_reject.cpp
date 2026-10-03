// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/match/vote_reject.hpp"
#include "oa/netgame/private_channel.hpp"

#include <bit>

namespace oa::netgame::match {

namespace {

/// Tells whether a time lies at or after another on the wrapping clock.
bool reached(uint32_t now, uint32_t at) noexcept {
    return static_cast<int32_t>(now - at) >= 0;
}

} // namespace

Vote* vote_find(VoteBoard& board, uint32_t target_id) noexcept {
    if (target_id == 0)
        return nullptr;
    for (auto& vote : board.votes)
        if (vote.target_id == target_id)
            return &vote;
    return nullptr;
}

Vote* vote_open(
    VoteBoard& board, uint32_t target_id, uint8_t flag, uint32_t now, uint32_t window
) noexcept {
    if (target_id == 0 || vote_find(board, target_id) != nullptr)
        return nullptr;
    for (auto& cooldown : board.cooldowns)
        if (cooldown.target_id == target_id && !reached(now, cooldown.until))
            return nullptr;
    for (auto& vote : board.votes) {
        if (vote.target_id != 0)
            continue;
        vote = Vote{target_id, flag, now + window, 0, 0};
        return &vote;
    }
    return nullptr;
}

void vote_cast(Vote& vote, uint8_t voter_slot, bool yes) noexcept {
    if (voter_slot >= OA_PLAYER_COUNT)
        return;
    const auto bit = static_cast<uint16_t>(1u << voter_slot);
    if (yes) {
        vote.yes = static_cast<uint16_t>(vote.yes | bit);
        vote.no = static_cast<uint16_t>(vote.no & ~bit);
    } else {
        vote.no = static_cast<uint16_t>(vote.no | bit);
        vote.yes = static_cast<uint16_t>(vote.yes & ~bit);
    }
}

uint8_t vote_needed(const Vote& vote, uint8_t electorate) noexcept {
    if (vote.flag == vote_flag_timeout)
        return electorate <= 1 ? 1 : timeout_vote_quorum;
    const auto needed = (2 * static_cast<uint32_t>(electorate) + 2) / 3;
    return static_cast<uint8_t>(needed < 1 ? 1 : needed);
}

VoteResult vote_tally(const Vote& vote, const VoteElectorate& electorate, uint32_t now) noexcept {
    const auto needed = vote_needed(vote, electorate.count);
    const auto yes = static_cast<uint32_t>(std::popcount(vote.yes));
    const auto no = static_cast<uint32_t>(std::popcount(vote.no));
    const bool allies_agree =
        electorate.target_allies == 0 || (vote.yes & electorate.target_allies) != 0;
    if (yes >= needed && allies_agree)
        return VoteResult::passed;
    if (electorate.count >= needed && no > static_cast<uint32_t>(electorate.count - needed))
        return VoteResult::failed;
    if (reached(now, vote.deadline))
        return vote.flag == vote_flag_timeout ? VoteResult::passed : VoteResult::failed;
    return VoteResult::open;
}

void vote_close(VoteBoard& board, Vote& vote, VoteResult result, uint32_t now) noexcept {
    if (result == VoteResult::failed && vote.flag == vote_flag_manual) {
        VoteCooldown* slot = nullptr;
        for (auto& cooldown : board.cooldowns)
            if (cooldown.target_id == vote.target_id || cooldown.target_id == 0 ||
                reached(now, cooldown.until)) {
                slot = &cooldown;
                break;
            }
        if (slot != nullptr)
            *slot = {vote.target_id, now + vote_cooldown_seconds * vote_time_ticks_per_second};
    }
    vote = Vote{};
}

uint32_t vote_seconds_left(const Vote& vote, uint32_t now) noexcept {
    if (reached(now, vote.deadline))
        return 0;
    return (vote.deadline - now) / vote_time_ticks_per_second;
}

} // namespace oa::netgame::match
