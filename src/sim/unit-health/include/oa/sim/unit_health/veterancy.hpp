// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What a unit's kills are worth: its veterancy level and each effect the
// level has, under the match's veterancy rule (veterancy.model).
//
// 3.1c counts a level per 5 kills (Unit.veteran_level holds the kills) and
// caps each effect at 5 levels. A profile may instead give each unit type its
// own ascending kill thresholds, a level per threshold reached, with caps,
// percentages, the aim-lead gate, the accuracy rate and the capture level of
// its own. Every function answers with 3.1c's result while the rule is at its
// baseline.
//
// The shooter's machine scales both sides of a hit: the shooter's bonus by its
// own type's thresholds and the victim's reduction by the victim's type's.
#pragma once

#include "oa/data/match_rules.hpp"

#include <cstdint>

namespace oa::sim::unit_health {

/// Kills per level while the level comes from the kill count (3.1c).
inline constexpr uint32_t kills_per_veteran_level = 5;
/// Kills a unit must exceed before it leads a moving target, in 3.1c.
inline constexpr uint16_t kills_before_lead = 5;
/// Levels a captured unit adds to its capture time's base of 10.
inline constexpr int32_t capture_base_level = 10;
/// The whole of a percentage.
inline constexpr int32_t whole_percent = 100;

/// Returns a unit's veterancy level.
///
/// With `level-source: kills-div-5` the level is kills / 5, uncapped; with
/// `thresholds` it is the number of the type's thresholds at or below the
/// kills (the type's own list, else `default-thresholds`).
///
/// @param rules the match's rules
/// @param type_index the unit's type index (Unit.type_index)
/// @param kills the unit's kills (Unit.veteran_level)
/// @return the level, before any effect's cap
/// @quirk Under thresholds a kill count past 32767 reads as negative and so
///        counts as past every threshold.
[[nodiscard]] uint32_t veterancy_level(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

/// Returns the percentage of damage a veteran victim still takes.
///
/// 100 - damage-taken-per-level × min(level, damage-taken-cap), never below
/// 0; in 3.1c 100 - 4 × min(kills / 5, 5).
///
/// @param rules the match's rules
/// @param type_index the victim's type index
/// @param kills the victim's kills
/// @return the percentage, 0 to 100
[[nodiscard]] int32_t veteran_damage_taken_percent(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

/// Returns the percentage of its weapon's damage a veteran shooter deals.
///
/// 100 + damage-dealt-per-level × min(level, damage-dealt-cap), with no cap
/// when the cap is none; in 3.1c 100 + 6 × min(kills / 5, 5).
///
/// @param rules the match's rules
/// @param type_index the shooter's type index
/// @param kills the shooter's kills
/// @return the percentage, 100 or more
[[nodiscard]] int32_t veteran_damage_dealt_percent(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

/// Returns the percentage of its weapon's reload a veteran shooter waits.
///
/// 100 - reload-per-level × min(level, reload-cap), never below 0; in 3.1c
/// 100 - 6 × min(kills / 5, 5).
///
/// @param rules the match's rules
/// @param type_index the shooter's type index
/// @param kills the shooter's kills
/// @return the percentage, 0 to 100
[[nodiscard]] int32_t veteran_reload_percent(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

/// Tells whether a shooter is veteran enough to lead a moving target.
///
/// `lead-after: kills-gt-5` (3.1c) leads above 5 kills; `first-threshold`
/// above the first of the type's thresholds.
///
/// @param rules the match's rules
/// @param type_index the shooter's type index
/// @param kills the shooter's kills
/// @return true when it leads
/// @quirk Kills equal to the first threshold do not lead, though they count
///        for the level.
[[nodiscard]] bool veteran_leads(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

/// Returns how many times a shooter's ballistic spread is divided.
///
/// The kills divided by the accuracy rate (the type's own, else
/// `accuracy-rate-default`; 12 in 3.1c), truncated toward zero; the spread is
/// divided by it only when it exceeds 1. A rate of 0 gives 0, no gain.
///
/// @param rules the match's rules
/// @param type_index the shooter's type index
/// @param kills the shooter's kills
/// @return the divisor, which keeps growing past the last threshold
[[nodiscard]] int32_t veteran_accuracy_divisor(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

/// Returns the level that lengthens the capture of a veteran unit.
///
/// `capture-level: kills-div-5` (3.1c) is kills / 5, uncapped. `extended` is
/// the threshold level, continued past the last threshold at the step of the
/// last two: n + (kills - last) / (last - previous), or kills / threshold for
/// a type with one threshold.
///
/// @param rules the match's rules
/// @param type_index the captured unit's type index
/// @param kills the captured unit's kills
/// @return the level the capture time's base of 10 is raised by
/// @quirk A zero step between the last two thresholds, or a single threshold
///        of 0, leaves the level at the number of thresholds.
[[nodiscard]] uint32_t veteran_capture_level(
    const data::match_rules::MatchRulesView& rules, uint16_t type_index, uint16_t kills
) noexcept;

} // namespace oa::sim::unit_health
