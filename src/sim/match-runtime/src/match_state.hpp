// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Canonical-state accessors for match-runtime sources. Modules migrating off
// the legacy views (oa/sim/unit_spawn/legacy_views.hpp) read and write oa::World through these.
#pragma once

#include "oa/sim/match_runtime.hpp"
#include "oa/sim/match_runtime/attachment_links.hpp"
#include <array>
#include <cstdint>
#include <cstring>

namespace oa::sim::match_runtime {
/// Returns the unit record in a pool slot.
///
/// @param match The match.
/// @param slot Unit slot; one outside the pool is noted and gives the
///     reserved slot 0, which holds no unit.
/// @return The unit record.
inline oa::Unit& match_unit(Match& match, std::size_t slot) noexcept {
    auto* unit = oa::world_unit_at(&match.state(), static_cast<uint32_t>(slot));
    if (unit)
        return *unit;
    match.note_fault("unit slot outside the match pool");
    return *oa::world_unit_at(&match.state(), 0);
}

/// Returns the unit record in a pool slot.
///
/// @param match The match.
/// @param slot Unit slot; one outside the pool is noted and gives the
///     reserved slot 0, which holds no unit.
/// @return The unit record.
inline const oa::Unit& match_unit(const Match& match, std::size_t slot) noexcept {
    const auto* unit = oa::world_unit_at(&match.state(), static_cast<uint32_t>(slot));
    if (unit)
        return *unit;
    match.note_fault("unit slot outside the match pool");
    return *oa::world_unit_at(&match.state(), 0);
}

/// Returns a player record.
///
/// @param match The match.
/// @param index Player index 0..9; another is noted and gives the no-player
///     record (Game.no_player).
/// @return The player record.
inline oa::Player& match_player(Match& match, std::size_t index) noexcept {
    auto* player = oa::world_player(&match.state(), static_cast<uint32_t>(index));
    if (player)
        return *player;
    match.note_fault("player index outside the ten players");
    return match.state().game.no_player;
}

/// Returns a unit's canonical type record.
///
/// @param match The match.
/// @param unit The unit; one without a type is noted and gives reserved type 0.
/// @return The type record.
inline const oa::UnitDef& match_unit_def(const Match& match, const oa::Unit& unit) noexcept {
    const auto* def = oa::world_unit_def_of(&match.state(), &unit);
    if (def)
        return *def;
    match.note_fault("unit has no canonical type");
    return match.state().unit_defs[0];
}

/// Returns a unit's legacy order lists.
///
/// @param match The match.
/// @param slot Unit slot.
/// @return The unit's primary and secondary queue heads.
inline sim::simulation_state::OrderQueue& match_orders(Match& match, std::size_t slot) {
    return match.orders(static_cast<uint16_t>(slot));
}

/// Returns a player's economy staging block.
///
/// @param match The match.
/// @param index Player index 0..9.
/// @return The block, or null, which is noted, for a player without one.
inline oa::UnitEconomy* match_player_economy(Match& match, std::size_t index) noexcept {
    auto* block = oa::world_player_economy(&match.state(), &match_player(match, index));
    if (!block)
        match.note_fault("player has no economy staging block");
    return block;
}

/// Returns words 0..11 of an economy block, the energy then the metal
/// accumulators, as raw bits.
///
/// @param block The economy block.
/// @return The twelve words.
inline std::array<uint32_t, 12> economy_words(const oa::UnitEconomy& block) noexcept {
    static_assert(offsetof(oa::UnitEconomy, player) == 12 * sizeof(uint32_t));
    std::array<uint32_t, 12> words;
    std::memcpy(words.data(), &block, sizeof words);
    return words;
}

/// Writes words 0..11 of an economy block, the energy then the metal
/// accumulators, from raw bits.
///
/// @param[out] block The economy block.
/// @param words The twelve words.
inline void
store_economy_words(oa::UnitEconomy& block, const std::array<uint32_t, 12>& words) noexcept {
    std::memcpy(&block, words.data(), sizeof words);
}

} // namespace oa::sim::match_runtime
