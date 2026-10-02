// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The 3.1c command line's network play switches -e -h -n -p -t, and the
// setup request -y asks for (enable_netsetup). Network play does not take
// -c or -y: unhandled, they stop the parse as reserved letters.
// launch_switch_handler returns
// the handler oa::app::command_line::parse hands them to; the parse applies
// the intro skip -n asks for.
#pragma once

#include "oa/app/command_line.hpp"
#include "oa/ui/frontend_multiplayer/launch_block.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::app::netgame::launch {

using oa::ui::frontend_multiplayer::launch::LaunchBlock;
namespace connection_type = oa::ui::frontend_multiplayer::launch::connection_type;

using oa::app::command_line::kTextBytes;

inline constexpr int32_t kDefaultNetTimeoutSeconds = 30;
inline constexpr int32_t kMaximumNetTimeoutSeconds = 300;
inline constexpr int32_t kMaximumSendErrorPercent = 100;
// "-p" followed by an argument that starts with '-'.
inline constexpr int32_t kPacingFromDash = -1;
// "-n<type>": only these connection types are stored; type 1 takes an
// address after ':'.
inline constexpr int32_t kFirstConnectionType = 1;
inline constexpr int32_t kLastConnectionType = 4;
inline constexpr int32_t kAddressedConnectionType = 1;

// What the network switches set. Only the net timeout and send-error
// percentage are reset by a parse; everything else keeps earlier values.
// "-n" and "-h" write the launch block (its connection type, address,
// hosting flag and session name); the *_copy fields belong to its second
// copy, which a launching extension may refresh from the first.
struct LaunchSwitches {
    int32_t net_timeout_seconds{kDefaultNetTimeoutSeconds}; // Game.player_timeout_seconds, -t
    int32_t send_error_percent{};                           // Game.send_error_percent, -e
    uint8_t netsetup{};                                     // -y
    uint8_t send_pacing_set{};                              // -p seen with an argument
    int32_t send_pacing{};                                  // sends per second, -p
    // The game's launch block: -n, -n1:<address>, -h and -h<name> write it
    // and a launching extension fills it.
    LaunchBlock block{};
    int32_t connection_type_copy{};
    char host_game_name_copy[kTextBytes]{};     // each -h name appends
    char connection_address_copy[kTextBytes]{}; // each address appends
};

/// Returns the switch handler that parses the network switches into `switches`.
///
/// A switch's argument is the text attached to its letter or, when none is,
/// the next token, which is then not parsed as a switch. Its reset sets
/// net_timeout_seconds to kDefaultNetTimeoutSeconds and send_error_percent to
/// 0. The letters it takes:
///  - "-e <percent>" stores the send-error percentage; outside 0..100 or an
///    argument starting with '-' stores 0.
///  - "-h<name>" hosts under the name (set_host_game); a separate name that is
///    missing, empty or starts with '-' is consumed and nothing is stored.
///  - "-n<type>[:<address>]" stores the connection type (set_connection_type)
///    and, for type 1, the address after ':' (set_connection_address).
///  - "-p <n>" stores sends per second, kPacingFromDash for a '-' argument.
///  - "-t <seconds>" stores the net timeout; outside 30..300 or an argument
///    starting with '-' stores kDefaultNetTimeoutSeconds.
/// "-n" also asks the parse to skip the intro, with or without an argument.
/// "-c" and "-y" are not taken.
///
/// @param switches Receives the parsed values; must outlive the handler.
/// @return A handler for oa::app::command_line::parse whose context is `switches`.
[[nodiscard]]
oa::app::command_line::SwitchHandler launch_switch_handler(LaunchSwitches* switches) noexcept;

/// Stores the hosting flag and the game name that "-h" gives.
///
/// The name replaces the block's session name but is appended to
/// host_game_name_copy, so a repeated "-h" accumulates there; both keep at
/// most 63 characters.
///
/// @param[in,out] switches Receives block.hosting, block.session_name and host_game_name_copy.
/// @param hosting Nonzero to host; stored as 1 or 0.
/// @param name Game name to host under; null leaves both names unchanged.
void set_host_game(LaunchSwitches& switches, uint32_t hosting, const char* name) noexcept;

/// Stores the address to connect to that "-n1:<address>" gives.
///
/// The address replaces the block's address but is appended to
/// connection_address_copy; both keep at most 63 characters.
///
/// @param[in,out] switches Receives block.address and connection_address_copy.
/// @param address Address text after the ':'.
void set_connection_address(LaunchSwitches& switches, const char* address) noexcept;

/// Stores the connection type that "-n<type>" gives in the block and its copy.
///
/// @param[in,out] switches Receives block.connection_type and connection_type_copy.
/// @param type Connection type; values outside kFirstConnectionType..kLastConnectionType
///             are ignored.
void set_connection_type(LaunchSwitches& switches, int32_t type) noexcept;

/// Requests the network setup the main menu runs on its first update, as "-y" does.
///
/// @param[out] switches Receives netsetup = 1.
void enable_netsetup(LaunchSwitches& switches) noexcept;

} // namespace oa::app::netgame::launch
