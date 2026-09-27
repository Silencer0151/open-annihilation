// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Canonical-state accessors for match-runtime sources. Modules migrating off
// the legacy views (oa/sim/unit_spawn/legacy_views.hpp) read and write oa::World through these.
#pragma once

#include "oa/sim/match_runtime.hpp"
#include <array>
#include <cstdint>
#include <cstring>
#include <stdexcept>

namespace oa::sim::match_runtime {
inline oa::Unit& match_unit(Match& match, std::size_t slot) {
    auto* unit = oa::world_unit_at(&match.state(), static_cast<uint32_t>(slot));
    if (!unit)
        throw std::out_of_range("unit slot outside the match pool");
    return *unit;
}

inline const oa::Unit& match_unit(const Match& match, std::size_t slot) {
    const auto* unit = oa::world_unit_at(&match.state(), static_cast<uint32_t>(slot));
    if (!unit)
        throw std::out_of_range("unit slot outside the match pool");
    return *unit;
}

inline oa::Player& match_player(Match& match, std::size_t index) {
    auto* player = oa::world_player(&match.state(), static_cast<uint32_t>(index));
    if (!player)
        throw std::out_of_range("player index outside the ten players");
    return *player;
}

inline const oa::UnitDef& match_unit_def(const Match& match, const oa::Unit& unit) {
    const auto* def = oa::world_unit_def_of(&match.state(), &unit);
    if (!def)
        throw std::logic_error("unit has no canonical type");
    return *def;
}

inline sim::simulation_state::OrderQueue& match_orders(Match& match, std::size_t slot) {
    return match.orders(static_cast<uint16_t>(slot));
}

inline oa::UnitEconomy& match_player_economy(Match& match, std::size_t index) {
    auto* block = oa::world_player_economy(&match.state(), &match_player(match, index));
    if (!block)
        throw std::logic_error("player has no economy staging block");
    return *block;
}

// Words 0..11 of an economy block (energy then metal accumulators) as raw bits.
inline std::array<uint32_t, 12> economy_words(const oa::UnitEconomy& block) noexcept {
    static_assert(offsetof(oa::UnitEconomy, player) == 12 * sizeof(uint32_t));
    std::array<uint32_t, 12> words;
    std::memcpy(words.data(), &block, sizeof words);
    return words;
}

inline void
store_economy_words(oa::UnitEconomy& block, const std::array<uint32_t, 12>& words) noexcept {
    std::memcpy(&block, words.data(), sizeof words);
}

// Attachment links (Unit.attach_parent, attach_first_child and attach_next)
// as unit slot indices; 0 is none.

/// Returns the unit slot of a unit's carrier (Unit.attach_parent).
///
/// @param unit Unit record.
/// @return The carrier's slot, 0 for none.
inline uint16_t link_parent(const oa::Unit& unit) noexcept {
    return static_cast<uint16_t>(oa::oa_unit_slot_from_ref(unit.attach_parent));
}

/// Returns the unit slot of the first unit a unit carries
/// (Unit.attach_first_child).
///
/// @param unit Unit record.
/// @return The first carried unit's slot, 0 for none.
inline uint16_t link_first_child(const oa::Unit& unit) noexcept {
    return static_cast<uint16_t>(oa::oa_unit_slot_from_ref(unit.attach_first_child));
}

/// Returns the unit slot of the next unit carried with this one
/// (Unit.attach_next).
///
/// @param unit Unit record.
/// @return The next carried unit's slot, 0 for none.
inline uint16_t link_next(const oa::Unit& unit) noexcept {
    return static_cast<uint16_t>(oa::oa_unit_slot_from_ref(unit.attach_next));
}

inline void set_link_parent(oa::Unit& unit, uint32_t slot) noexcept {
    unit.attach_parent = oa::oa_unit_ref_from_slot(slot);
}

inline void set_link_first_child(oa::Unit& unit, uint32_t slot) noexcept {
    unit.attach_first_child = oa::oa_unit_ref_from_slot(slot);
}

inline void set_link_next(oa::Unit& unit, uint32_t slot) noexcept {
    unit.attach_next = oa::oa_unit_ref_from_slot(slot);
}
} // namespace oa::sim::match_runtime
