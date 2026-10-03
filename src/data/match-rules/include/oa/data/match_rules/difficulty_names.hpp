// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Which name each difficulty carries (ai.difficulty-names). 3.1c keeps three
// tables of difficulty names, indexed by the game's difficulty: the AI
// profile keywords that select plan sections ("easy"), the upper-case labels
// of the AI weight report ("EASY") and the labels the menus show ("Easy").
// The rule says which of 3.1c's names each difficulty takes; every table
// follows it.
#pragma once

#include "oa/data/match_rules.hpp"

#include <cstdint>

namespace oa::data::match_rules {

/// Returns the position in 3.1c's name tables of the name a difficulty carries.
///
/// @param rule the match's ai.difficulty-names record; its baseline gives each
///        difficulty its own position
/// @param difficulty the game's difficulty (OA_DIFFICULTY_*: 0 easy, 1 medium,
///        2 hard in 3.1c)
/// @return 0 for 3.1c's easy names, 1 for medium, 2 for hard; a difficulty
///         outside 0..2 is returned unchanged
[[nodiscard]] constexpr int32_t
difficulty_name_index(const AiDifficultyNames& rule, int32_t difficulty) noexcept {
    if (difficulty < 0 || difficulty >= static_cast<int32_t>(rule.names.size()))
        return difficulty;
    return static_cast<int32_t>(rule.names[static_cast<size_t>(difficulty)]);
}

} // namespace oa::data::match_rules
