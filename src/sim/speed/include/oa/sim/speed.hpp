// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game speed: the 1..20 simulation rate the +/- keys step (10 runs at 30
// ticks a second) and the message-log line that announces a change. A mod's
// rules may narrow or widen the range, down to 0 (the simulation stands still
// without being paused).
#pragma once

#include "oa/core/world.h"
#include "oa/data/match_rules.hpp"
#include "oa/sim/messages.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::sim::speed {

inline constexpr int32_t slowest = 1;
inline constexpr int32_t normal = 10;
inline constexpr int32_t fastest = 20;
inline constexpr size_t message_bytes = 100;

/// The speeds a game may be set to, both ends included.
struct Range {
    int32_t slowest{speed::slowest}; ///< 0..20; 1 in 3.1c
    int32_t fastest{speed::fastest}; ///< at least `slowest`; 20 in 3.1c
};

/// Returns the speeds a game may be set to under a match's rules.
///
/// @param rule console.game-speed-range
/// @return its min and max while the rule is on; 1..20 otherwise
[[nodiscard]] constexpr Range
range_of(const data::match_rules::ConsoleGameSpeedRange& rule) noexcept {
    if (!rule.enabled)
        return {};
    return {rule.minimum, rule.maximum};
}

/// A host's speed lock is typed relative to normal speed: 0 is normal.
inline constexpr int32_t lock_offset = normal;

/// Returns the speeds a host's speed lock (.syncon low high) leaves of a range.
///
/// Each limit, typed relative to normal, becomes the speed 10 above it, held
/// within the range; the fastest is then raised to the slowest when it lies
/// below it.
///
/// @param range the speeds the rules allow
/// @param low the slowest speed allowed, relative to normal
/// @param high the fastest speed allowed, relative to normal
/// @return the speeds the lock allows, within the range
[[nodiscard]] constexpr Range locked(Range range, int32_t low, int32_t high) noexcept {
    const auto within = [&range](int32_t typed) {
        const int32_t speed = typed + lock_offset;
        return speed < range.slowest ? range.slowest
                                     : (speed > range.fastest ? range.fastest : speed);
    };
    Range out{within(low), within(high)};
    if (out.fastest < out.slowest)
        out.fastest = out.slowest;
    return out;
}

/// What a chat line asks of the speed lock.
enum class LockRequest : uint8_t {
    none,   ///< the line is no lock command
    lock,   ///< .syncon low high
    unlock, ///< .syncoff
};

/// A speed lock command read from a chat line.
struct LockLine {
    LockRequest request{LockRequest::none};
    int32_t low{};  ///< .syncon's first number, relative to normal; 0 when absent
    int32_t high{}; ///< .syncon's second number, relative to normal; 0 when absent
};

/// Reads a speed lock command from a typed chat line.
///
/// After leading spaces the line is ".syncon", optionally followed by two
/// whole numbers that may carry a '-', or ".syncoff"; the command word is
/// matched without case and ends the line or is followed by a space. A
/// number that is absent or not a whole number reads as 0.
///
/// @param text the typed line; null reads as no command
/// @return the command, or LockRequest::none
[[nodiscard]] LockLine read_lock_line(const char* text) noexcept;

inline constexpr const char* text_normal = "Game Speed Normal";
inline constexpr const char* text_prefix = "Game Speed";

/// Formats the message-log line announcing a game speed.
///
/// "Game Speed Normal" at 10; otherwise "Game Speed", two spaces, a sign column that is
/// '+' when faster and a space when slower (the number then carries its own '-'), the
/// offset from 10 and a newline.
///
/// @param[out] out receives the line
/// @param speed game speed 0..20
/// @param hooks message formatting services
void format_message(
    char (&out)[message_bytes], int32_t speed, const messages::Hooks& hooks
) noexcept;

/// Sets the game speed.
///
/// Clamps to the range (first to its fastest, then to its slowest), posts the line
/// when that differs from the requested speed, and stores it as both the requested
/// and the current speed. Telling the other players of the change is left to the
/// caller.
///
/// @param[in,out] world game speed fields and message log
/// @param speed requested speed
/// @param hooks message services
/// @param range the speeds allowed; 1..20 in 3.1c
/// @return the stored speed
uint16_t set_speed(World& world, int32_t speed, const messages::Hooks& hooks, Range range = {});

/// Steps the game speed one faster while below the range's fastest.
///
/// @param[in,out] world game speed fields and message log
/// @param hooks message services
/// @param range the speeds allowed; 1..20 in 3.1c
/// @return the requested speed
uint16_t raise_speed(World& world, const messages::Hooks& hooks, Range range = {});

/// Steps the game speed one slower while above the range's slowest.
///
/// @param[in,out] world game speed fields and message log
/// @param hooks message services
/// @param range the speeds allowed; 1..20 in 3.1c
/// @return the requested speed
uint16_t lower_speed(World& world, const messages::Hooks& hooks, Range range = {});

} // namespace oa::sim::speed
