// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's command line in oa-game: the long options of the loopback,
// host-not-found and demo runs, the 3.1c network switches, and the parts of
// the extension table that need no running app; and a recording the engine
// hands over to replay.
#pragma once

#include "oa/app/extension.hpp"
#include "oa/app/command_line.hpp"
#include "oa/netgame/frontend/multiplayer_states.hpp"
#include "oa/app/netgame/launch_switches.hpp"
#include "oa/ui/screen_registry.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace oa::app {

namespace fs = std::filesystem;

constexpr std::size_t kDefaultDemoTicks = 600; // headless --play-demo without --match-ticks

struct NetOptions {
    // Headless in-process host/join over loopback: networked ticks to run.
    std::optional<std::size_t> net_loopback_ticks;
    bool net_loopback_watcher = false; // the loopback joiner only watches
    // The loopback host seats a computer player, which plays the joiner, or
    // the host while the joiner watches.
    bool net_loopback_computer = false;
    // A join whose host is not found, played through the main loop (with
    // --frames and "-n1:<address>" naming an address where no game is
    // hosted): a launch counts as active until the game list begins to wait
    // for the host, and no longer after, the one way to "Host not found.
    // Exiting...". Standard output reports that text and the leave it leads
    // to, with the reason and the exit status it ends the run with.
    bool check_host_not_found = false;
    // The DirectPlay enumeration port this game hosts on and asks for games
    // at, in place of the standard one; its stream and datagram ports are
    // then any the system has free. Checks that host or search give each run
    // its own, so they can run side by side.
    std::optional<uint16_t> dplay_port;
    // .tad recording to replay; match_ticks bounds a headless replay.
    fs::path play_demo;
    // Replay even when the recording's unit table differs from the installed
    // one (diagnostic: definition indices then name the wrong units).
    bool demo_ignore_unit_table = false;
    // Hand the engine's recording hook (Extension::open_recording) bytes that
    // are no recording and a recording's truncated header, then the
    // --play-demo recording, which it replays through the replay hooks.
    bool check_recording_hook = false;
};

// A recording the engine handed network play to replay
// (Extension::open_recording), held until start_demo_playback takes it in
// place of the --play-demo file.
struct StagedRecording {
    std::string name;           // the recording's name as the engine gave it
    std::vector<uint8_t> bytes; // the whole recording
    bool strict{};              // refuse a unit table that differs from the installed one
};

// The extension's context: what the command line gave network play, and the
// multiplayer frontend states the dispatcher runs.
struct NetgameContext {
    NetOptions options{};
    oa::app::netgame::launch::LaunchSwitches launch{};
    oa::app::command_line::SwitchHandler switches{};
    // Its session routines are all null: no provider resolves and no session
    // starts through the dispatcher; the multiplayer screens drive the network.
    oa::netgame::frontend::MultiplayerFrontend frontend{};
    // Tells whether a launch is active, which the frontend's entry follows;
    // null answers false.
    bool (*launch_active)(void* context){};
    // The recording the next start_demo_playback replays in place of
    // --play-demo's; empty otherwise.
    std::optional<StagedRecording> staged_recording{};
};

/// Returns the process's network play context, which oa_extension_init_netgame hands the table.
///
/// @return The one context, created empty on first use.
[[nodiscard]] NetgameContext& netgame_context();

/// Returns the long options network play took from the command line.
///
/// @return netgame_context().options.
[[nodiscard]] inline const NetOptions& net_options() {
    return netgame_context().options;
}

/// Returns the 3.1c network switches network play took from the command line.
///
/// @return netgame_context().launch.
[[nodiscard]] inline const oa::app::netgame::launch::LaunchSwitches& net_launch_switches() {
    return netgame_context().launch;
}

/// Fills the extension hooks that need no running app.
///
/// They cover the long options, the 3.1c network switches (the handler is
/// bound to context.launch here), the usage text, the frontend's entry, the
/// multiplayer frontend states and the launch values of a new match's Game
/// block.
///
/// @param[in,out] table Extension table; context, take_option,
///                      switch_handler, text, frontend_entry, frontend_states
///                      and match_game are set.
/// @param[in,out] context Context the hooks read and write; becomes `table.context`.
void fill_option_hooks(Extension& table, NetgameContext& context);

/// Binds the multiplayer frontend states' answers about joining and leaving a session to the dispatcher (query_register).
///
/// The states' session routines are null (NetgameContext::frontend): the
/// multiplayer screens open the provider's session, join and leave, and
/// start the match report as the match launches. So joining never waits for
/// a match report to start (init_score_reporting answers 0), and every game plays
/// over TCP/IP (transport_kind answers transport_kind::tcpip), so leaving the
/// battle room returns to the game list. open_service_session keeps the
/// dispatcher's 0 for a query no handler takes: the states ask it only once
/// the connection selection proceeds with a provider other than the modem,
/// serial and TCP/IP ones, or once a connection dialog has an address, and
/// nothing leads them there while the screens choose the provider.
///
/// @param[in,out] registry The runtime's registry.
void register_frontend_state_queries(ScreenRegistry* registry) noexcept;

} // namespace oa::app
