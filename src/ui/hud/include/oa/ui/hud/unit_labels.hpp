// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Unit info labels drawn on the HUD.
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::ui::hud {

/// Maps an English interface string to the active language; null keeps it.
using Localize = const char* (*)(void* user, const char* text);

/// Kills at which a unit is labelled a veteran.
inline constexpr uint16_t kVeteranKills = 5;

/// Formats a unit's kill count and veteran label.
///
/// "<n> kills" ("kill" for one), with " - Veteran" appended from
/// kVeteranKills on; the words go through `localize`.
///
/// @param[out] out Destination buffer.
/// @param size Size of `out` in bytes.
/// @param kills Unit's kill count.
/// @param localize Language lookup; may be null.
/// @param user Context passed to `localize`.
void format_kill_count(char* out, std::size_t size, uint16_t kills, Localize localize, void* user);

} // namespace oa::ui::hud
