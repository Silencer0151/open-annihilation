// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit info labels drawn on the HUD.
#pragma once

#include <cstddef>
#include <cstdint>
#include <span>

namespace oa::ui::hud {

/// Maps an English interface string to the active language; null keeps it.
///
/// The text returned need only last until the next lookup: a caller uses or
/// copies each word before it looks up another.
using Localize = const char* (*)(void* user, const char* text);

/// Kills at which a unit is labelled a veteran.
inline constexpr uint16_t kVeteranKills = 5;

/// Formats a unit's kill count and veteran label, as 3.1c's unit panel does.
///
/// "<n> kills" ("kill" for exactly one), with " - Veteran" appended from
/// kVeteranKills on: "1 kill", "2 kills", "7 kills - Veteran". The words go
/// through `localize`.
///
/// @param[out] out Destination buffer.
/// @param size Size of `out` in bytes.
/// @param kills Unit's kill count.
/// @param localize Language lookup; may be null.
/// @param user Context passed to `localize`.
void format_kill_count(char* out, std::size_t size, uint16_t kills, Localize localize, void* user);

/// The caption a resurrection that cannot name its unit type speaks, in the
/// game's own spelling.
inline constexpr const char* kResurrectionFailedMisspelled = "Ressurection failed";
/// The same caption spelled correctly, as the resurrection that finds no
/// feature speaks it.
inline constexpr const char* kResurrectionFailed = "Resurrection failed";

/// Returns a unit's spoken caption as it is shown: the misspelled
/// resurrection failure respelled under ui.interface-fixes resurrect-spelling.
///
/// @param caption the caption the unit speaks; may be null
/// @param respell the resurrect-spelling fix is in force
/// @return kResurrectionFailed for the misspelled caption when respelling,
///     else `caption`
[[nodiscard]] const char* shown_unit_caption(const char* caption, bool respell) noexcept;

/// Returns a unit's veterancy level for its label: how many of `thresholds`
/// its kill count has reached.
///
/// The kill count is read as a signed 16-bit number and widened, so a count
/// from 32768 on compares above every threshold and reaches them all.
///
/// @param thresholds the type's kill thresholds, ascending
/// @param kills the unit's kill count
/// @return the number of thresholds at or below the kill count; 0 for no thresholds
[[nodiscard]] uint32_t
veterancy_label_level(std::span<const uint16_t> thresholds, uint16_t kills) noexcept;

/// Formats a unit's kill count with a veterancy-level label (ui.veterancy-label).
///
/// From level 1 on the line reads "<n> kills - Vet<level>", the noun always
/// plural; below it reads "<n> kills" ("kill" for one), at any kill count.
/// The nouns go through `localize`; "Vet" does not.
///
/// @param[out] out Destination buffer.
/// @param size Size of `out` in bytes.
/// @param kills Unit's kill count.
/// @param level the unit's veterancy level (veterancy_label_level)
/// @param localize Language lookup; may be null.
/// @param user Context passed to `localize`.
void format_kill_count_at_level(
    char* out, std::size_t size, uint16_t kills, uint32_t level, Localize localize, void* user
);

} // namespace oa::ui::hud
