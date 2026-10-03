// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's long options, their usage text and checks, the 3.1c network
// switches, and the values they hand the frontend and a new match.
#include "net_options.hpp"

#include "oa/app/app.hpp"
#include "oa/core/game_state.h"

#include <charconv>
#include <stdexcept>
#include <string>
#include <string_view>

namespace oa::app {
namespace {

/// Parses a UDP port number.
///
/// Anything but a whole number from 1 through 65535 throws std::runtime_error
/// "--dplay-port expects a port from 1 through 65535".
///
/// @param text The option's value.
/// @return The port.
[[nodiscard]] uint16_t parse_port(std::string_view text) {
    uint32_t value = 0;
    const auto result = std::from_chars(text.data(), text.data() + text.size(), value);
    if (result.ec != std::errc{} || result.ptr != text.data() + text.size() || value == 0 ||
        value > 65535)
        throw std::runtime_error("--dplay-port expects a port from 1 through 65535");
    return static_cast<uint16_t>(value);
}

/// Returns the NetgameContext behind an extension context pointer.
///
/// @param context The extension table's context.
/// @return The context it points to.
NetgameContext& context_of(void* context) {
    return *static_cast<NetgameContext*>(context);
}

/// Takes one of network play's long options (Extension::take_option).
///
/// --net-loopback-check makes the run an unattended headless one (the check
/// runs only in a headless run; without it the run would sit in the menus),
/// --check-host-not-found an unattended run that skips the intro,
/// --check-recording-hook an unattended headless run that skips the intro. A missing or malformed value throws
/// std::runtime_error.
///
/// @param context The NetgameContext whose options receive the value.
/// @param name Option name with its dashes.
/// @param values Arguments that follow the option.
/// @param[in,out] effects Gains the option_effect bits the option implies.
/// @return False when the option is not network play's.
bool take_option(void* context, const char* name, const OptionValues& values, uint32_t& effects) {
    auto& result = context_of(context).options;
    const std::string_view argument(name);
    auto value = [&values]() -> std::string_view { return values.next(values.arguments); };
    if (argument == "--net-loopback-check") {
        result.net_loopback_ticks = parse_count(value());
        effects |= option_effect::headless_check | option_effect::unattended;
    } else if (argument == "--net-loopback-watcher")
        result.net_loopback_watcher = true;
    else if (argument == "--net-loopback-computer")
        result.net_loopback_computer = true;
    else if (argument == "--check-host-not-found") {
        result.check_host_not_found = true;
        effects |= option_effect::unattended | option_effect::skip_intro;
    } else if (argument == "--dplay-port")
        result.dplay_port = parse_port(value());
    else if (argument == "--play-demo")
        result.play_demo = fs::path(value());
    else if (argument == "--check-recording-hook") {
        result.check_recording_hook = true;
        effects |=
            option_effect::headless_check | option_effect::unattended | option_effect::skip_intro;
    } else if (argument == "--net-record")
        result.net_record = fs::path(value());
    else if (argument == "--replay-viewer")
        result.replay_viewer = true;
    else if (argument == "--demo-unit-table") {
        const auto mode = std::string(value());
        if (mode != "strict" && mode != "ignore")
            throw std::runtime_error("--demo-unit-table expects strict or ignore");
        result.demo_ignore_unit_table = mode == "ignore";
    } else
        return false;
    return true;
}

/// Returns the handler of the 3.1c network switches (Extension::switch_handler).
///
/// @param context The NetgameContext holding the handler.
/// @return The handler fill_option_hooks bound to the context's LaunchSwitches.
const oa::app::command_line::SwitchHandler* switch_handler(void* context) {
    return &context_of(context).switches;
}

/// Returns network play's wording of the usage text and of the -r refusal (Extension::text).
///
/// @param context Extension context (unused).
/// @param which Text asked for.
/// @return The text; "" for no usage note.
const char* text(void* /*context*/, ExtensionText which) {
    switch (which) {
    case ExtensionText::usage_checks:
        return "[--check-recording-hook] ";
    case ExtensionText::usage_runs:
        return "[--net-loopback-check TICKS] [--net-loopback-watcher] [--net-loopback-computer] "
               "[--check-host-not-found] [--dplay-port PORT] [--play-demo FILE.tad] "
               "[--demo-unit-table strict|ignore] [--net-record FILE] [--replay-viewer] ";
    case ExtensionText::usage_switches:
        return "-t SECONDS -n TYPE[:ADDRESS] -hNAME ";
    case ExtensionText::usage_note:
        return "";
    case ExtensionText::register_switch:
        return "-r registers the game as a DirectPlay application, which this game does not "
               "need";
    }
    return nullptr;
}

/// Hands the frontend the launch's names (Extension::frontend_entry).
///
/// The game name is the launch block's session name, which "-h" and
/// a launch write. While a launch is active the launch's
/// user name is the nickname.
///
/// @param context The NetgameContext holding the switches.
/// @param[out] entry Receives game_name, and nickname under an active launch.
void frontend_entry(void* context, FrontendEntry& entry) {
    const auto& netgame = context_of(context);
    const auto& block = netgame.launch.block;
    entry.game_name = block.session_name;
    const bool launched = netgame.launch_active != nullptr && netgame.launch_active(nullptr);
    if (launched)
        entry.nickname = block.user_name;
}

/// Starts the multiplayer frontend states with the fields the network switches set (Extension::frontend_states).
///
/// A connection type ("-n") opens multiplayer from the main menu at once and
/// a setup request (request_setup, which "-y" makes) asks for step
/// setup_requested on the first main-menu update.
///
/// @param context The NetgameContext holding the switches and the states.
/// @param[out] handler Receives the handler that runs the states.
void frontend_states(void* context, oa::ui::frontend_state::StateHandler& handler) {
    auto& netgame = context_of(context);
    netgame.frontend.state.connection_type = netgame.launch.block.connection_type;
    netgame.frontend.state.setup_request = netgame.launch.netsetup;
    handler = oa::netgame::frontend::state_handler(netgame.frontend);
}

/// Answers init_score_reporting: 0, as joining never waits for a match report to start.
///
/// The report starts as the battle room's match launches instead
/// (NetworkPlay::start_reporter).
///
/// @param ctx Unused.
/// @param state Unused.
/// @return 0.
uint32_t query_score_reporting(ScreenContext* /*ctx*/, void* /*state*/) noexcept {
    return 0;
}

/// Answers transport_kind: TCP/IP, over which every game plays.
///
/// @param ctx Unused.
/// @param state Unused.
/// @return oa::netgame::frontend::transport_kind::tcpip.
uint32_t query_transport_kind(ScreenContext* /*ctx*/, void* /*state*/) noexcept {
    return oa::netgame::frontend::transport_kind::tcpip;
}

/// Writes the -t and -e values into a new match's Game block (Extension::match_game).
///
/// @param context The NetgameContext holding the switches.
/// @param[out] game Receives player_timeout_seconds and send_error_percent.
void match_game(void* context, oa::Game& game) {
    const auto& launch = context_of(context).launch;
    game.player_timeout_seconds = launch.net_timeout_seconds;
    game.send_error_percent = launch.send_error_percent;
}

} // namespace

NetgameContext& netgame_context() {
    static NetgameContext context{};
    return context;
}

void register_frontend_state_queries(ScreenRegistry* registry) noexcept {
    namespace query = oa::netgame::frontend::query;
    (void)query_register(registry, query::init_score_reporting, query_score_reporting, nullptr);
    (void)query_register(registry, query::transport_kind, query_transport_kind, nullptr);
}

void fill_option_hooks(Extension& table, NetgameContext& context) {
    context.switches = oa::app::netgame::launch::launch_switch_handler(&context.launch);
    table.context = &context;
    table.take_option = take_option;
    table.switch_handler = switch_handler;
    table.text = text;
    table.frontend_entry = frontend_entry;
    table.frontend_states = frontend_states;
    table.match_game = match_game;
}

} // namespace oa::app
