// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The network play switches parsed through oa::app::command_line::parse with the
// handler launch_switch_handler returns.
#include "oa/app/netgame/launch_switches.hpp"

#include <cstdio>
#include <cstring>
#include <string>

namespace cl = oa::app::command_line;
namespace nl = oa::app::netgame::launch;

namespace {
int failures = 0;

void require(bool condition, const char* message) {
    if (!condition) {
        std::fprintf(stderr, "launch switches test failed: %s\n", message);
        ++failures;
    }
}

struct Parsed {
    cl::Switches engine;
    nl::LaunchSwitches launch;
};

cl::Status parse(const char* line, Parsed& parsed) {
    const cl::SwitchHandler handler = nl::launch_switch_handler(&parsed.launch);
    return cl::parse(line, parsed.engine, &handler);
}

Parsed parsed(const char* line, cl::Status expected = cl::Status::run) {
    Parsed result;
    require(parse(line, result) == expected, line);
    return result;
}

void defaults_and_resets() {
    const auto none = parsed("");
    require(
        none.launch.net_timeout_seconds == 30 && none.launch.send_error_percent == 0, "defaults"
    );
    Parsed kept;
    kept.launch.net_timeout_seconds = 200;
    kept.launch.send_error_percent = 50;
    kept.launch.send_pacing = 9;
    kept.engine.skip_intro = 1;
    require(parse("english", kept) == cl::Status::run, "bare language");
    require(
        kept.launch.net_timeout_seconds == 30 && kept.launch.send_error_percent == 0 &&
            kept.engine.skip_intro == 0,
        "three fields reset before the scan"
    );
    require(kept.launch.send_pacing == 9, "other fields keep their values");
}

void timeouts_and_percentages() {
    require(parsed("-t120").launch.net_timeout_seconds == 120, "attached timeout");
    require(parsed("-T 300").launch.net_timeout_seconds == 300, "separate timeout, upper case");
    require(parsed("-t29").launch.net_timeout_seconds == 30, "below the range falls back to 30");
    require(parsed("-t301").launch.net_timeout_seconds == 30, "above the range falls back to 30");
    require(parsed("-t -e5").launch.net_timeout_seconds == 30, "dash argument falls back to 30");
    const auto consumed = parsed("-t -e5");
    require(consumed.launch.send_error_percent == 0, "the dash token is consumed as the argument");
    require(parsed("/e100").launch.send_error_percent == 100, "slash switch, full percentage");
    require(parsed("-e101").launch.send_error_percent == 0, "percentage above 100 is zero");
    require(parsed("-e 42x").launch.send_error_percent == 42, "decimal prefix");
}

void intro_and_connection() {
    const auto join = parsed("-n1:10.0.0.5");
    require(
        join.launch.block.connection_type == 1 && join.engine.skip_intro == 1,
        "typed connection skips intro"
    );
    require(std::strcmp(join.launch.block.address, "10.0.0.5") == 0, "address after the colon");
    const auto ipx = parsed("-n 2:ignored");
    require(
        ipx.launch.block.connection_type == 2 && ipx.launch.block.address[0] == '\0',
        "only type 1 stores an address"
    );
    const auto bad = parsed("-n5");
    require(
        bad.launch.block.connection_type == 0 && bad.engine.skip_intro == 1,
        "type outside 1..4 is ignored"
    );
    const auto dash = parsed("-n -x");
    require(
        dash.launch.block.connection_type == 0 && dash.engine.skip_intro == 1,
        "dash argument still skips"
    );
    // "-c" and "-y" are not network play's switches ("-y" asks for the
    // setup, enable_netsetup); unhandled, they stop the parse as reserved
    // letters.
    const auto module = parsed("-c module.cfg", cl::Status::unavailable_switch);
    require(module.engine.skip_intro == 0, "-c is not taken");
    const auto setup = parsed("-y", cl::Status::unavailable_switch);
    require(setup.engine.skip_intro == 0 && setup.launch.netsetup == 0, "-y is not taken");
}

void hosting() {
    const auto attached = parsed("-hMy Game");
    require(
        attached.launch.block.hosting == 1 &&
            std::strcmp(attached.launch.block.session_name, "My") == 0,
        "attached name ends at the blank"
    );
    require(
        std::strcmp(attached.engine.language, "Game") == 0, "the next token is a bare argument"
    );
    const auto separate = parsed("-h Lobby");
    require(
        separate.launch.block.hosting == 1 &&
            std::strcmp(separate.launch.block.session_name, "Lobby") == 0,
        "separate name"
    );
    const auto dash = parsed("-h -s");
    require(
        dash.launch.block.hosting == 0 && dash.engine.playback_suppressed == 0,
        "a dash token is consumed and not a name"
    );
    std::string long_name = "-h";
    long_name.append(80, 'x');
    const auto truncated = parsed(long_name.c_str());
    require(
        std::strlen(truncated.launch.block.session_name) == 63, "name truncated to 63 characters"
    );
}

void pacing_and_debug_switches() {
    const auto pacing = parsed("-p12");
    require(pacing.launch.send_pacing_set == 1 && pacing.launch.send_pacing == 12, "pacing value");
    const auto dash = parsed("-p -z");
    require(
        dash.launch.send_pacing_set == 1 && dash.launch.send_pacing == nl::kPacingFromDash,
        "dash pacing"
    );
    require(parsed("-p").launch.send_pacing_set == 0, "no pacing argument");
    const auto debug = parsed("-saveresources -dprinton -fpufussy -performancestatus");
    require(debug.launch.send_pacing_set == 0, "-performancestatus is not -p");
    const auto others = parsed("-s -w -x -z");
    require(
        others.engine.system_sound == 1 && others.engine.skip_intro == 0,
        "letters the handler does not know are left alone"
    );
}

void registration_stops_the_scan() {
    Parsed switches;
    require(
        parse("-s -r   C:\\TA\\Game.exe -t60", switches) == cl::Status::register_application,
        "-r stops the scan"
    );
    require(
        switches.engine.playback_suppressed == 1 && switches.launch.net_timeout_seconds == 30,
        "earlier switches applied, later ones not"
    );
}

void helpers() {
    nl::LaunchSwitches switches;
    nl::set_connection_type(switches, 4);
    nl::set_connection_type(switches, 0);
    require(switches.block.connection_type == 4, "type 0 ignored");
    nl::set_host_game(switches, 0, nullptr);
    require(switches.block.hosting == 0 && switches.block.session_name[0] == '\0', "no name");
    nl::set_host_game(switches, 7, "Name");
    require(switches.block.hosting == 1, "hosting flag is boolean");
    require(std::strcmp(switches.host_game_name_copy, "Name") == 0, "name copied");
    nl::set_host_game(switches, 1, "Two");
    require(
        std::strcmp(switches.block.session_name, "Two") == 0 &&
            std::strcmp(switches.host_game_name_copy, "NameTwo") == 0,
        "the copy accumulates names"
    );
    require(switches.connection_type_copy == 4, "type written to the copy");
    nl::set_connection_address(switches, "a");
    nl::set_connection_address(switches, "b");
    require(std::strcmp(switches.block.address, "b") == 0, "address replaced");
    require(std::strcmp(switches.connection_address_copy, "ab") == 0, "address appended");
    const std::string long_text(80, 'x');
    nl::set_connection_address(switches, long_text.c_str());
    require(
        std::strlen(switches.block.address) == 63 &&
            std::strlen(switches.connection_address_copy) == 63,
        "both address fields stop at 63 characters"
    );
    Parsed setup;
    nl::enable_netsetup(setup.launch);
    require(
        setup.launch.netsetup == 1 && setup.engine.skip_intro == 0, "netsetup byte alone is set"
    );
}

void switches_fill_the_block() {
    const auto host = parsed("-n1:10.0.0.9 -hBattle");
    require(
        host.launch.block.connection_type == nl::connection_type::tcpip &&
            host.launch.block.hosting == 1 &&
            std::strcmp(host.launch.block.session_name, "Battle") == 0 &&
            std::strcmp(host.launch.block.address, "10.0.0.9") == 0,
        "-n and -h fill the block's connection fields"
    );
}

} // namespace

int main() {
    defaults_and_resets();
    timeouts_and_percentages();
    intro_and_connection();
    hosting();
    pacing_and_debug_switches();
    registration_stops_the_scan();
    helpers();
    switches_fill_the_block();
    if (failures != 0)
        return 1;
    std::puts("launch switches passed");
    return 0;
}
