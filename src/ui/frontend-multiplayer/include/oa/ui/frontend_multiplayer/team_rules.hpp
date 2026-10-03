// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The setup and team rules a mod profile can turn on for the battle room and
// the start of a multiplayer game, worked out over a plain view of the ten
// player slots so that the battle room and the network match apply them
// alike: the reaction to a player's team number, alliance requests, the
// alliances the battle room sends again with each player's block, the teams
// +autoteam and +randomteam deal, the start positions that keep team-mates
// together, and the names of computer players.
//
// Each function answers what to do; the caller applies it with its own
// records (the battle room's or the match's), so the rules hold no state.
#pragma once

#include "oa/core/types.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::ui::frontend_multiplayer::team_rules {

/// The number of player slots.
inline constexpr std::size_t slot_count = 10;
/// The team number of a player on no team.
inline constexpr int8_t no_team = 5;
/// The team numbers a player can hold: 0 to 4, and 5 for none.
inline constexpr uint8_t team_values = 6;
/// The bit of a team record's value that sets the team without touching any alliance.
inline constexpr uint8_t team_keeps_alliances = 0x80;
/// The bits of a team record's value that hold the team number.
inline constexpr uint8_t team_number_bits = 0x7f;
/// The both-sides word of an alliance record that asks the first player's
/// machine to set that player's alliance and announce it.
inline constexpr uint32_t alliance_request = 0xffffffffU;
/// The fewest and most teams +autoteam and +randomteam deal, and the count
/// they deal when the command names none.
inline constexpr int32_t fewest_dealt_teams = 2;
inline constexpr int32_t most_dealt_teams = 5;
/// The bytes of a computer player's name, its terminator not counted.
inline constexpr std::size_t computer_name_bytes = 16;

/// Player.status values the rules read.
inline constexpr uint8_t status_local = 1;    ///< a human on this machine
inline constexpr uint8_t status_computer = 2; ///< a computer player on this machine
inline constexpr uint8_t status_remote = 3;   ///< a player on another machine

/// PlayerSetupInfo.state values the rules read.
inline constexpr uint8_t setup_human = 1;    ///< the slot holds a human
inline constexpr uint8_t setup_computer = 2; ///< the slot holds a computer player

/// One player slot as the rules read it.
struct TeamSlot {
    bool in_use{};         ///< Player.in_use is set
    uint8_t status{};      ///< Player.status
    bool watcher{};        ///< the slot's setup block marks it watching
    uint8_t setup_state{}; ///< PlayerSetupInfo.state: setup_human or setup_computer
    int8_t team{no_team};  ///< Player.team, compared as a signed byte
    std::array<uint8_t, slot_count> alliance{}; ///< Player.alliance: nonzero allies that slot
    std::array<char, 30> name{};                ///< Player.name, NUL-terminated when shorter
};

/// The ten slots, by index.
using TeamSlots = std::array<TeamSlot, slot_count>;

/// One step the caller carries out, in order.
struct TeamStep {
    /// What the step does.
    enum class Kind : uint8_t {
        /// Player `from` sets its alliance with `to` to `value` (see set_alliance).
        alliance,
        /// Player `from` takes team `value`; the team goes out with team_keeps_alliances.
        team,
    };
    Kind kind{Kind::alliance}; ///< what the step does
    uint8_t from{};            ///< the player slot acting
    uint8_t to{};              ///< the other player slot of an alliance step
    uint8_t value{};           ///< 1 allied, 0 not; or the team number
};

/// A run of steps, held in place.
struct TeamSteps {
    std::array<TeamStep, 2 * slot_count * slot_count> items{}; ///< the steps in order
    uint16_t count{};                                          ///< steps in use

    /// Appends a step; one past the capacity is dropped.
    ///
    /// @param step the step
    void push(const TeamStep& step) noexcept;
};

/// Tells whether a slot holds a player the setup rules count: seated and not watching.
///
/// @param slot the slot
/// @return true for a seated player that is not a watcher
[[nodiscard]] bool counted(const TeamSlot& slot) noexcept;

/// Tells whether this machine runs a slot's player: a local human or a local computer player.
///
/// @param slot the slot
/// @return true for status_local or status_computer
[[nodiscard]] bool runs_here(const TeamSlot& slot) noexcept;

/// What a received team record does.
struct TeamNumberResult {
    bool store{};    ///< the sender's team becomes `team`
    int8_t team{};   ///< the team the sender takes
    TeamSteps steps; ///< the alliance steps that follow, in order
};

/// Works out what a team record from a player does.
///
/// The team is the record's value; a value of 6 or more is ignored. With
/// `keeps_alliances_bit` the value's team_keeps_alliances bit is taken off
/// first, and a value with it set stores the team and changes no alliance.
/// Otherwise every counted slot this machine runs other than the sender
/// allies the sender when both are on the same team and unallies it
/// otherwise, in both directions, wherever that alliance differs from it
/// now; when the sender joins no team, players that are themselves on no
/// team are left as they are.
///
/// @param slots the slots as they stand
/// @param sender the slot the record names
/// @param value the record's team value
/// @param keeps_alliances_bit whether the team_keeps_alliances bit is read
/// @return whether and what to store, and the alliance steps
[[nodiscard]] TeamNumberResult receive_team_number(
    const TeamSlots& slots, uint8_t sender, uint8_t value, bool keeps_alliances_bit
) noexcept;

/// Tells whether an alliance record asks this machine to set an alliance
/// for one of its players: its both-sides word is alliance_request and its
/// first player is one this machine runs.
///
/// @param first the slot of the record's first player
/// @param both_sides the record's both-sides word
/// @return true when the record is such a request
[[nodiscard]] bool alliance_requested(const TeamSlot& first, uint32_t both_sides) noexcept;

/// How the battle room sends a player's alliances again with its block.
enum class AllianceResend : uint8_t {
    /// Each alliance as it stands.
    stored,
    /// Allied exactly when both players are on the same team; a pair where
    /// either is on no team keeps its alliance.
    from_teams,
};

/// Works out the alliances the battle room sends again after a player's block.
///
/// For every other seated slot, in slot order, the player sets its
/// alliance with it, as `resend` decides.
///
/// @param slots the slots as they stand
/// @param player the slot whose block went out
/// @param resend how each alliance is decided
/// @return the alliance steps
[[nodiscard]] TeamSteps
resend_alliances(const TeamSlots& slots, uint8_t player, AllianceResend resend) noexcept;

/// The team count +autoteam or +randomteam deals from its argument.
///
/// @param argument the command's first argument; empty for none
/// @return the leading number of `argument` clamped to fewest_dealt_teams
///         ..most_dealt_teams, or fewest_dealt_teams without an argument
[[nodiscard]] int32_t dealt_team_count(std::string_view argument) noexcept;

/// Draws a value below a bound for a shuffle.
struct ShuffleRandom {
    void* context{};
    /// Returns a value from 0 to bound - 1; null leaves every order as it is.
    uint32_t (*below)(void* context, uint32_t bound){};
};

/// Shuffles slot indices: each index from the second on swaps with one at
/// or before it.
///
/// @param[in,out] order the indices
/// @param random the draws; with none the order stays
void shuffle_slots(std::array<int32_t, slot_count>& order, const ShuffleRandom& random) noexcept;

/// Works out the steps of +autoteam or +randomteam.
///
/// The counted players, taken in the shuffled slot order `order`, are named
/// one after another; each name is looked up again among the slots, the
/// first seated slot of that name standing for it. First every pair of
/// counted players with an alliance either way is unallied both ways; then
/// the k-th named player takes team k modulo `teams`; then each named
/// player allies the other named players of its team and unallies the rest.
///
/// @param slots the slots as they stand
/// @param order the slot order the names are taken in
/// @param teams the number of teams, fewest_dealt_teams..most_dealt_teams
/// @return the steps in order
[[nodiscard]] TeamSteps deal_teams(
    const TeamSlots& slots, const std::array<int32_t, slot_count>& order, int32_t teams
) noexcept;

/// Start positions per slot; -1 for a slot that takes none.
using StartPositions = std::array<int32_t, slot_count>;

/// Works out the alliances of teams dealt by start position (the in-game
/// "autoteam"): each player with a start position allies every other with
/// a position equal to its own modulo `teams` and unallies the rest.
///
/// @param positions each slot's start position, -1 for none
/// @param teams the number of teams, fewest_dealt_teams..most_dealt_teams
/// @return the alliance steps, in slot order
[[nodiscard]] TeamSteps
alliances_by_position(const std::array<int32_t, slot_count>& positions, int32_t teams) noexcept;

/// Works out the start positions that keep team-mates together.
///
/// Each counted player gets a team: its team number when every counted
/// player is on a team, otherwise its group of players linked by
/// alliances. When some team has more than one player, team t starts at
/// position t - 1 and its players take every n-th position from there, n
/// being the number of teams; a position already taken, or one past the
/// last slot, is replaced in a second pass by the lowest free one. Players
/// are placed in slot order, or in a shuffled order with `random`. When no
/// team has two players, the players take positions one after another, in
/// slot order or shuffled. On a map with neutral units, computer players
/// this machine runs that are alone on their team are placed after the
/// others (then one fewer team spreads the positions when there are more
/// than two), and in the end the computer player with the highest position
/// swaps with the human holding the highest one when the human's is higher.
///
/// @param slots the slots as they stand
/// @param random whether the order is shuffled (start positions are not fixed)
/// @param neutral_units whether the map places units for the neutral player
/// @param shuffle the draws a shuffled order takes
/// @return the start position of every slot
[[nodiscard]] StartPositions team_start_positions(
    const TeamSlots& slots, bool random, bool neutral_units, const ShuffleRandom& shuffle
) noexcept;

/// Formats a computer player's name.
///
/// `format` holds one %s, for the local player's name, and at most one %d
/// after it, for the computer player's slot; any other text is copied. The
/// result is cut to computer_name_bytes.
///
/// @param[out] out the name, NUL-terminated
/// @param format the profile's name format
/// @param local_name the local player's name
/// @param slot the computer player's slot, 0..9
void format_computer_name(
    char (&out)[computer_name_bytes + 1],
    std::string_view format,
    const char* local_name,
    int32_t slot
) noexcept;

} // namespace oa::ui::frontend_multiplayer::team_rules
