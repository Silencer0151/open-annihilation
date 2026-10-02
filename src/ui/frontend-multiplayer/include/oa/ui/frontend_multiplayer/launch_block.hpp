// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game's launch block: how a launching program (an extension built on
// network play) asks the game to open a network game, which connection,
// host or join, the session and its options. The "-n" and "-h" switches
// write its connection fields too. The multiplayer screens, the battle room
// and the session read it, most of them only while a launch is active
// (LaunchLink::launch_active).
#pragma once

#include <cstddef>
#include <cstdint>

namespace oa::ui::frontend_multiplayer::launch {

inline constexpr std::size_t kProviderLabelBytes = 0x10;
inline constexpr std::size_t kLaunchSessionNameBytes = 0x40;
inline constexpr std::size_t kLaunchMissionBytes = 0x40;
inline constexpr std::size_t kLaunchPasswordBytes = 0x20;
inline constexpr std::size_t kLaunchUserNameBytes = 0x20;
inline constexpr std::size_t kLaunchAddressBytes = 0x40;

// LaunchBlock::connection_type: the transport the connection selection
// takes without asking; 0 leaves the choice to the player.
namespace connection_type {
inline constexpr int32_t none = 0;
inline constexpr int32_t tcpip = 1;
inline constexpr int32_t ipx = 2;
inline constexpr int32_t modem = 3;
inline constexpr int32_t serial = 4;
} // namespace connection_type

// LaunchBlock::faction: the side the launch assigned.
namespace faction {
inline constexpr int32_t none = 0;
inline constexpr int32_t arm = 1;
inline constexpr int32_t core = 2;
} // namespace faction

// One launch. Texts are terminated within their fields.
struct LaunchBlock {
    // The transport to connect with (connection_type values); the connection
    // selection clears it once it has taken it.
    int32_t connection_type{};
    uint32_t hosting{};                 // 1: create the game; 0: join it
    int32_t launched_connection_type{}; // the launch's connection_type, kept; never read
    int32_t player_limit{};             // players the launch allows
    int32_t time_limit{};               // ? minutes; the score report passes it on
    int32_t pfs{};                      // ? meaning unresolved; the score report passes it on
    char provider_label[kProviderLabelBytes]{};   // the launcher's name for the match's menus
    char session_name[kLaunchSessionNameBytes]{}; // the game's name ("-h" writes it too)
    char mission[kLaunchMissionBytes]{};          // the map the host plays
    char password[kLaunchPasswordBytes]{};        // the game's password
    char user_name[kLaunchUserNameBytes]{};       // the player's name with the launcher
    uint32_t lock_options{};                      // 1: the battle room's option controls are locked
    int32_t max_units{};
    int32_t energy{};                    // starting energy
    int32_t metal{};                     // starting metal
    int32_t death_option{};              // 1 continues, 2 ends, 3 deathmatch on commander death
    int32_t los{};                       // 1 true, 2 circular, 3 permanent line of sight
    int32_t cheats{};                    // 2 allows the cheat codes
    int32_t location{};                  // 1 fixed start positions
    int32_t mapping{};                   // 1 the map starts unmapped
    int32_t watching{};                  // 2 allows watchers
    char address[kLaunchAddressBytes]{}; // the host's address ("-n1:" writes it too)
    int32_t tournament{};                // nonzero for a tournament game
    int32_t faction{};                   // faction values
    int32_t team{};
};

} // namespace oa::ui::frontend_multiplayer::launch
