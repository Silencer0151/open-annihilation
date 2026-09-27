// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game speed: the 1..20 simulation rate the +/- keys step (10 runs at 30
// ticks a second) and the message-log line that announces a change.
#pragma once

#include "oa/core/world.h"
#include "oa/sim/messages.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::sim::speed {

inline constexpr int32_t slowest = 1;
inline constexpr int32_t normal = 10;
inline constexpr int32_t fastest = 20;
inline constexpr size_t message_bytes = 100;

inline constexpr const char* text_normal = "Game Speed Normal";
inline constexpr const char* text_prefix = "Game Speed";

/// Formats the message-log line announcing a game speed.
///
/// "Game Speed Normal" at 10; otherwise "Game Speed", two spaces, a sign column that is
/// '+' when faster and a space when slower (the number then carries its own '-'), the
/// offset from 10 and a newline.
///
/// @param[out] out receives the line
/// @param speed game speed 1..20
/// @param hooks message formatting services
void format_message(
    char (&out)[message_bytes], int32_t speed, const messages::Hooks& hooks
) noexcept;

/// Sets the game speed.
///
/// Clamps to 1..20, posts the line when that differs from the requested speed, and
/// stores it as both the requested and the current speed. Telling the other players
/// of the change is left to the caller.
///
/// @param[in,out] world game speed fields and message log
/// @param speed requested speed
/// @param hooks message services
/// @return the stored speed
uint16_t set_speed(World& world, int32_t speed, const messages::Hooks& hooks);

/// Steps the game speed one faster while below 20.
///
/// @param[in,out] world game speed fields and message log
/// @param hooks message services
/// @return the requested speed
uint16_t raise_speed(World& world, const messages::Hooks& hooks);

/// Steps the game speed one slower while above 1.
///
/// @param[in,out] world game speed fields and message log
/// @param hooks message services
/// @return the requested speed
uint16_t lower_speed(World& world, const messages::Hooks& hooks);

} // namespace oa::sim::speed
