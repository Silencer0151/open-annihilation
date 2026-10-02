// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Network play's long options through oa-game's command line: the loopback,
// host-not-found and demo options, with the options the engine keeps, none
// of them without the extension, and "-y", which network play leaves to an
// extension built on it; the names the frontend's entry takes from a launch;
// and the multiplayer frontend states' answers about joining and leaving a
// session.
#include "oa/app/app.hpp"
#include "net_options.hpp"

#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

struct Parsed {
    oa::app::NetOptions net;
    oa::app::Options app;
};

// Each parse starts from a fresh network play context, as a start does.
Parsed parse(const std::vector<const char*>& arguments, bool extended = true) {
    std::vector<std::string> storage{"open-annihilation"};
    storage.insert(storage.end(), arguments.begin(), arguments.end());
    std::vector<char*> argv;
    for (auto& argument : storage)
        argv.push_back(argument.data());
    oa::app::NetgameContext context{};
    oa::app::Extension table{};
    if (extended)
        oa::app::fill_option_hooks(table, context);
    auto app = oa::app::parse_options(static_cast<int>(argv.size()), argv.data(), table);
    return {context.options, std::move(app)};
}

// The message parse_options rejects the arguments with, or "" when it takes them.
std::string rejection(const std::vector<const char*>& arguments, bool extended = true) {
    try {
        (void)parse(arguments, extended);
    } catch (const std::runtime_error& error) {
        return error.what();
    }
    return {};
}

bool g_launch_active = false;

/// Answers NetgameContext::launch_active from the test's flag.
///
/// @param context unused
/// @return the flag
bool answer_launch_active(void*) {
    return g_launch_active;
}

/// Checks the frontend's entry: the "-h" game name always, and while a
/// launch is active the launch's user name as the nickname.
void check_frontend_entry() {
    oa::app::NetgameContext context{};
    oa::app::Extension table{};
    oa::app::fill_option_hooks(table, context);
    auto& block = context.launch.block;
    std::snprintf(block.session_name, sizeof block.session_name, "%s", "Room");
    std::snprintf(block.user_name, sizeof block.user_name, "%s", "Launcher");
    oa::app::FrontendEntry entry{};
    table.frontend_entry(table.context, entry);
    expect(
        entry.game_name != nullptr && std::strcmp(entry.game_name, "Room") == 0 &&
            entry.nickname == nullptr,
        "without a launch only the game name"
    );
    context.launch_active = answer_launch_active;
    g_launch_active = true;
    entry = {};
    table.frontend_entry(table.context, entry);
    expect(
        entry.nickname != nullptr && std::strcmp(entry.nickname, "Launcher") == 0 &&
            std::strcmp(entry.game_name, "Room") == 0,
        "an active launch names the nickname and the game"
    );
    g_launch_active = false;
}

/// Checks the answers the multiplayer frontend states get about joining and leaving a session.
///
/// Joining never waits for a match report to start, every game plays over TCP/IP,
/// so leaving the battle room returns to the game list, and opening a
/// provider's session is left to the dispatcher's 0.
void check_frontend_state_queries() {
    namespace query = oa::netgame::frontend::query;
    oa::app::ScreenRegistry registry{};
    oa::app::register_frontend_state_queries(&registry);
    expect(registry.rejected == nullptr && registry.query_count == 2, "two queries are answered");
    const auto answer = [&](oa::ui::frontend_state::Query asked, uint32_t* out) {
        const oa::app::QueryDesc* desc = oa::app::query_find(&registry, asked);
        if (desc == nullptr)
            return false;
        *out = desc->run(nullptr, desc->state);
        return true;
    };
    uint32_t value = 7;
    expect(
        answer(query::init_score_reporting, &value) && value == 0,
        "a join never waits for the match report"
    );
    expect(
        answer(query::transport_kind, &value) &&
            value == oa::netgame::frontend::transport_kind::tcpip,
        "every game plays over TCP/IP"
    );
    expect(
        !answer(query::open_service_session, &value), "opening a provider's session keeps the 0"
    );
}

} // namespace

int main() {
    check_frontend_entry();
    check_frontend_state_queries();
    const auto plain = parse({"--headless-check"});

    const auto demo = parse({"--play-demo", "game.tad", "--trace-digest", "demo.trace"});
    expect(
        demo.net.play_demo == "game.tad" && demo.app.trace_digest == "demo.trace",
        "demo runs take a trace"
    );

    // The 3.1c network switches reach network play's handler.
    const auto hosted = parse({"-nTCPIP", "-hHost", "-t60", "-e5"});
    expect(hosted.app.launch.unavailable_switch == 0, "the network switches are taken");

    // A network loopback run is unattended, so it opens no folder dialog.
    expect(parse({"--net-loopback-check", "300"}).app.unattended, "--net-loopback-check");
    expect(
        rejection({"--net-loopback-check", "300", "--choose-game-dir"})
                .find("--choose-game-dir opens a dialog") == 0,
        "--net-loopback-check with --choose-game-dir"
    );
    // The host-not-found check plays through the main loop, unattended and
    // without the intro.
    const auto host_not_found = parse({"--check-host-not-found"});
    expect(
        host_not_found.net.check_host_not_found && host_not_found.app.unattended &&
            host_not_found.app.skip_intro && !host_not_found.app.headless_check,
        "--check-host-not-found runs unattended through the main loop, without the intro"
    );
    expect(!plain.net.check_host_not_found, "no host-not-found check without the flag");
    expect(!plain.net.dplay_port, "the standard DirectPlay port unless --dplay-port gives one");
    expect(
        parse({"--dplay-port", "51234"}).net.dplay_port == uint16_t{51234},
        "--dplay-port takes a port"
    );
    expect(
        rejection({"--dplay-port", "0"}) == "--dplay-port expects a port from 1 through 65535" &&
            rejection({"--dplay-port", "65536"}) ==
                "--dplay-port expects a port from 1 through 65535" &&
            rejection({"--dplay-port", "x"}) == "--dplay-port expects a port from 1 through 65535",
        "--dplay-port rejects anything but 1 through 65535"
    );

    // Without the extension the demo and loopback flags do not exist.
    for (const char* flag :
         {"--play-demo",
          "--demo-unit-table",
          "--net-loopback-check",
          "--net-loopback-watcher",
          "--check-host-not-found"})
        expect(rejection({flag}, false) == std::string("unknown option: ") + flag, flag);
    // "-y" is not network play's: with network play alone the build refuses it.
    expect(rejection({"-y"}) == "-y is not handled by this build", "-y is not network play's");
    return failures == 0 ? 0 : 1;
}
