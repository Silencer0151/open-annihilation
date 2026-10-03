// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The intel hacks' part of the match: allied vision
// (intel.allied-los-sharing) and allied jammers that do not jam
// (intel.allied-jammers-ignored). The sight stamps' part of allied vision is
// in the visibility state's stamp routines, the contact scan's in
// tick_detection.cpp.
#include "oa/sim/match_runtime.hpp"

#include <algorithm>
#include <array>
#include <span>

namespace oa::sim::match_runtime {
namespace {

/// The name of allied vision's rule-state table.
constexpr const char* allied_sight_state_name = "intel.allied-sight";

/// Position of the "has followed the players" byte in allied vision's state.
constexpr std::size_t followed_byte = 0;
/// Position of the followed viewpoint player in allied vision's state.
constexpr std::size_t viewpoint_byte = 1;
/// Position of the first alliance byte in allied vision's state.
constexpr std::size_t first_alliance_byte = 2;

} // namespace

void Match::add_allied_sight_state() {
    if (!rules().intel.allied_los_sharing.enabled)
        return;
    RuleStateTable table{};
    table.name = allied_sight_state_name;
    table.context = this;
    table.bytes = [](void* context) -> std::span<const uint8_t> {
        return static_cast<Match*>(context)->allied_sight_state_;
    };
    table.restore = [](void* context, std::span<const uint8_t> bytes) {
        auto& state = static_cast<Match*>(context)->allied_sight_state_;
        if (bytes.size() != state.size())
            return false;
        std::copy(bytes.begin(), bytes.end(), state.begin());
        return true;
    };
    if (!add_rule_state(rule_state_, table))
        fault_.note("allied vision's rule state cannot be added");
}

void Match::follow_alliances_in_sight() {
    if (!rules().intel.allied_los_sharing.enabled)
        return;
    const auto& game = state().game;
    std::array<uint8_t, allied_sight_state_bytes> now{};
    now[followed_byte] = 1;
    now[viewpoint_byte] = game.viewpoint_player;
    for (std::size_t player = 0; player < OA_PLAYER_COUNT; ++player)
        for (std::size_t other = 0; other < OA_PLAYER_COUNT; ++other)
            now[first_alliance_byte + player * OA_PLAYER_COUNT + other] =
                game.players[player].alliance[other];
    const bool changed = allied_sight_state_[followed_byte] != 0 && now != allied_sight_state_;
    allied_sight_state_ = now;
    if (changed)
        reset_sight_buffers(false);
}

bool Match::owner_allies(const oa::Unit& unit, uint8_t player) const noexcept {
    return unit.owner_index < OA_PLAYER_COUNT && player < OA_PLAYER_COUNT &&
           state().game.players[unit.owner_index].alliance[player] != 0;
}

bool Match::jammer_jams(const oa::Player& viewer, const oa::Unit& jammer) noexcept {
    const auto& rule = rules().intel.allied_jammers_ignored;
    if (!rule.enabled)
        return true;
    auto& world = state();
    const auto& game = world.game;
    const bool own_or_allied =
        jammer.owner_index == viewer.index ||
        (jammer.owner_index < OA_PLAYER_RECORD_COUNT && viewer.alliance[jammer.owner_index] != 0);
    // The view-switch branch: viewing another player than the local one in
    // a game with mapping or line of sight, only the alliance counts.
    if (rule.view_switch_branch &&
        (game.visibility_flags & (OA_VISIBILITY_MAPPING | OA_VISIBILITY_LINE_OF_SIGHT)) != 0 &&
        viewer.index != game.local_player_index)
        return !own_or_allied;
    // A watching local player sees no jamming at all, nor does a replay
    // viewer without a slot of its own.
    bool local_watching = slotless_viewer_ != SlotlessViewer::none;
    if (game.local_player_index < OA_PLAYER_COUNT)
        if (const auto* info =
                oa::world_player_info(&world, &world.game.players[game.local_player_index]))
            local_watching = local_watching || (info->options & OA_SETUP_OPTION_WATCHER) != 0;
    return !(local_watching || own_or_allied);
}

} // namespace oa::sim::match_runtime
