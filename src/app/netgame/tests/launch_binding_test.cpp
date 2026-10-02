// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The launch block as the running game binds it: the answers the
// screens ask of a launch (a tournament game, its connection type), a
// launch applied and ended, the setup request, the multiplayer screens'
// link, the hooks an extension built on network play sets (the match
// report's among them), and the lines posted to the battle, wrapped as the message
// log's notices are; each through network play's API
// (oa/app/netgame/extension_api.hpp) too. The launch bound to a record, which the
// tests of an extension built on network play use, keeps what the API asks.
#include "battle_lines.hpp"
#include "launch_binding.hpp"

#include "oa/app/netgame/extension_api.hpp"
#include "oa/test/netgame_launch_record.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

namespace api = oa::app::netgame::extension_api;
namespace mp = oa::ui::frontend_multiplayer;
namespace nl = oa::app::netgame::launch;
using namespace oa::app;

int g_failures = 0;

void check(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAIL: %s\n", what);
        ++g_failures;
    }
}

nl::LaunchBlock g_block{};
nl::LaunchSwitches g_switches{};
uint8_t g_pending = 0;
int32_t g_app_mode = 0;
// The leaves asked for: 1 with the disconnect reason, 0 without.
std::vector<int> g_leaves;

void test_leave(void*, bool with_reason) {
    g_leaves.push_back(with_reason ? 1 : 0);
}

void bind_test_launch(CloseHandlers* handlers) {
    LaunchBinding launch{};
    launch.block = &g_block;
    launch.switches = &g_switches;
    launch.launch_pending = &g_pending;
    launch.app_mode_pending = &g_app_mode;
    launch.leave_game = test_leave;
    launch.close_handlers = handlers;
    bind_launch(launch);
}

// The team panel's question about the launch: the tournament game from the
// launch block's field, which the last launch wrote and nothing clears.
void test_launch_answers() {
    bind_test_launch(nullptr);
    g_block = nl::LaunchBlock{};
    set_launch_active(false);
    check(!launch_tournament(), "no tournament without a launch");
    g_block.tournament = 2;
    check(launch_tournament(), "the tournament field alone withholds CONTROL");
    set_launch_active(true);
    check(launch_tournament(), "a tournament launch withholds CONTROL");
    g_block.tournament = 0;
    check(!launch_tournament(), "an ordinary launch offers CONTROL");
    // After a tournament battle's launch has ended, a game hosted from
    // MULTI is still one.
    g_block.tournament = 2;
    set_launch_active(false);
    check(launch_tournament(), "the launch's end kept the tournament game");
    g_block = nl::LaunchBlock{};
}

// The block's connection type, which the connection selection takes.
void test_connection_type() {
    bind_launch({});
    set_launch_active(false);
    check(launch_connection_type(nullptr) == 0, "no connection type without a block");
    check(launch_block() == nullptr && !launch_active(nullptr), "no block, no launch");
    check(api::connection_type() == 0, "the API's connection type without a block");
    api::launch_block().connection_type = nl::connection_type::modem;
    check(launch_block() == nullptr, "the API's block without a binding is no game's");
    bind_test_launch(nullptr);
    g_block.connection_type = nl::connection_type::ipx;
    check(
        launch_connection_type(nullptr) == nl::connection_type::ipx, "the block's connection type"
    );
    check(api::connection_type() == nl::connection_type::ipx, "the API reads the same type");
    check(launch_block() == &g_block && &api::launch_block() == &g_block, "the bound block");
    g_block = nl::LaunchBlock{};
}

// A launch applied: the player's name becomes the nickname, the connection
// selection is asked to apply the launch and a close request asks for
// confirmation.
void test_apply_launch() {
    CloseHandlers handlers{};
    bind_test_launch(&handlers);
    set_launch_active(false);
    g_pending = 0;
    std::snprintf(g_block.user_name, sizeof g_block.user_name, "%s", "Ghost_Commander_And_More");
    api::apply_launch();
    check(g_pending == 1, "the connection selection is asked to apply the launch");
    check(
        std::strcmp(mp::lobby_nickname(mp::multiplayer_game()), "Ghost_Commander_") == 0,
        "the block's user name becomes the nickname, 16 characters at most"
    );
    check(
        handlers.installed == CloseHandler::exit_confirm, "a close request asks for confirmation"
    );
    check(api::launch_active() && launch_active(nullptr), "the applied launch is active");
    api::end_launch();
    check(!api::launch_active(), "the ended launch is not");
    api::end_launch();
    check(!api::launch_active(), "ending no launch changes nothing");
    g_block.user_name[0] = '\0';
    apply_launch();
    check(mp::lobby_nickname(mp::multiplayer_game())[0] == '\0', "no name, an empty nickname");
    end_launch();
    bind_test_launch(nullptr);
    g_pending = 0;
    g_block = nl::LaunchBlock{};
}

bool g_host_waiting = false;

bool test_launch_ended() {
    return g_host_waiting;
}

// A launch marked active without being applied, which a check ends as the
// game list begins to wait for the host (LaunchBinding::launch_ended).
void test_launch_ended_by_binding() {
    LaunchBinding launch{};
    launch.block = &g_block;
    launch.launch_ended = test_launch_ended;
    bind_launch(launch);
    g_pending = 0;
    set_launch_active(true);
    check(launch_active(nullptr) && g_pending == 0, "marked active, nothing applied");
    g_host_waiting = true;
    check(!launch_active(nullptr), "the end the binding tells ends it");
    g_host_waiting = false;
    check(!launch_active(nullptr), "an ended launch stays ended");
    bind_test_launch(nullptr);
}

// The setup request "-y" makes, which the switches' values hold.
void test_setup_request() {
    bind_launch({});
    api::request_setup();
    check(g_switches.netsetup == 0, "no switches, no request");
    bind_test_launch(nullptr);
    api::request_setup();
    check(g_switches.netsetup == 1, "the request marks the switches' setup");
    g_switches = nl::LaunchSwitches{};
}

// The launch bound to a record keeps the setup request, the application
// mode asked for and the leaves, and unbinding it binds nothing.
void test_launch_record() {
    oa::test::netgame::LaunchRecord record{};
    oa::test::netgame::bind_launch_record(&record);
    api::request_setup();
    api::request_app_mode(1);
    api::leave_game(true);
    api::leave_game(false);
    api::leave_game(true);
    check(record.switches.netsetup == 1, "the record keeps the setup request");
    check(record.app_mode == 1, "the record keeps the application mode asked for");
    check(
        record.leaves_with_reason == 2 && record.leaves_without_reason == 1,
        "the record counts the leaves with and without the reason"
    );
    oa::test::netgame::bind_launch_record(nullptr);
    api::request_setup();
    api::leave_game(true);
    check(record.leaves_with_reason == 2, "an unbound record hears nothing");
    bind_test_launch(nullptr);
}

// The multiplayer screens' link: the same block and answer, a return's
// application mode, and the leaves with or without the reason.
std::vector<std::string> g_failed_joins;
std::vector<std::string> g_pages;

void test_join_failed(void*, const char* text) {
    g_failed_joins.emplace_back(text);
}

void test_page(void*, const char* user, const char* text) {
    g_pages.emplace_back(std::string(user) + ": " + text);
}

void test_report_event(void*, oa::World&, int32_t) {
}

void test_report_close(void*) {
}

// The multiplayer screens' link: the same block and answer, a return's
// application mode, the leaves with or without the reason, and a failed
// join, which reaches the extension's hook when there is one.
void test_launch_link() {
    bind_test_launch(nullptr);
    const mp::LaunchLink link = launch_link();
    set_launch_active(false);
    check(link.block == &g_block && !link.launch_active(link.context), "no launch is active");
    set_launch_active(true);
    check(link.launch_active(link.context), "the multiplayer screens read the same answer");
    set_launch_active(false);
    g_app_mode = 0;
    link.request_app_mode(link.context, mp::kAppModeFrontend);
    check(g_app_mode == 1, "a return asks the next frontend pass for mode 1");
    api::request_app_mode(mp::kAppModeFrontend + 1);
    check(g_app_mode == 2, "the API asks the same");
    g_leaves.clear();
    link.leave_game(link.context, true);
    link.leave_game(link.context, false);
    api::leave_game(true);
    check(
        g_leaves == std::vector<int>({1, 0, 1}),
        "the screens' leaves and the API's reach the bound leave"
    );
    g_failed_joins.clear();
    api::set_hooks({});
    link.join_failed(link.context, "Host not found");
    check(g_failed_joins.empty(), "without the hook a failed join shows nothing");
    api::Hooks hooks{};
    hooks.join_failed = test_join_failed;
    api::set_hooks(hooks);
    link.join_failed(link.context, "Host not found");
    check(
        g_failed_joins == std::vector<std::string>({"Host not found"}),
        "a failed join reaches the extension's hook"
    );
    api::set_hooks({});
}

// The match report's hooks the extension sets, which the runtime reads
// through extension_hooks.
void test_report_hooks() {
    check(
        extension_hooks().report_start == nullptr && extension_hooks().report_close == nullptr,
        "no report hooks until an extension sets them"
    );
    api::Hooks hooks{};
    hooks.report_event = test_report_event;
    hooks.report_close = test_report_close;
    api::set_hooks(hooks);
    check(
        extension_hooks().report_event == test_report_event &&
            extension_hooks().report_close == test_report_close &&
            extension_hooks().report_start == nullptr,
        "the runtime reads the extension's report hooks"
    );
    api::set_hooks({});
    check(extension_hooks().report_event == nullptr, "a second set replaces the first");
}

// The console's Page reaches the extension's hook (extension_hooks).
void test_page_hook() {
    check(extension_hooks().page == nullptr, "no page hook until an extension sets one");
    api::Hooks hooks{};
    hooks.page = test_page;
    api::set_hooks(hooks);
    g_pages.clear();
    extension_hooks().page(extension_hooks().context, "Ghost", "hello");
    check(g_pages == std::vector<std::string>({"Ghost: hello"}), "a page reaches the hook");
    api::set_hooks({});
    check(extension_hooks().page == nullptr, "a second call replaces the first");
}

std::vector<std::string> g_match_lines;
bool g_match_running = false;

void post_part(void*, const char* part) {
    g_match_lines.emplace_back(part);
}

bool test_post_to_match(void*, const char* line) {
    if (!g_match_running)
        return false;
    g_match_lines.emplace_back(line);
    return true;
}

// The lines posted to the battle: wrapped as the message log's notices are,
// to the running match or into the chat ring.
void test_battle_lines() {
    g_match_lines.clear();
    oa::Game game{};
    const std::string long_line =
        "Arena: the host player announces maintenance of the community servers tonight at "
        "midnight";
    post_wrapped_line(long_line.c_str(), post_part, nullptr, game);
    check(
        g_match_lines.size() == 2 && g_match_lines[0].size() <= 63 &&
            g_match_lines[0].back() == ' ' && g_match_lines[1].front() != ' ',
        "a long line is wrapped after a blank"
    );
    check((g_match_lines[0] + g_match_lines[1]) == long_line, "the wrap keeps every word");
    check((game.gui_flags & 1U) != 0, "the line raises gui flag 0");
    g_match_lines.clear();
    const std::string unbroken(80, 'x');
    post_wrapped_line(unbroken.c_str(), post_part, nullptr, game);
    check(
        g_match_lines.size() == 2 && g_match_lines[0].size() == 51,
        "without a blank the part stops 12 characters short"
    );

    // A running match takes the line; otherwise it goes into the chat ring
    // and the frontend Game's gui flag rises.
    bind_battle_lines(nullptr, test_post_to_match);
    g_match_lines.clear();
    g_match_running = true;
    oa::Game& frontend = mp::multiplayer_game();
    frontend.gui_flags = 0;
    api::post_battle_line("Arena: WARNING! DISCONNECTED!");
    check(g_match_lines.size() == 1 && frontend.gui_flags == 0, "a running match takes the line");
    g_match_running = false;
    post_battle_line("Arena: WARNING! DISCONNECTED!");
    check(
        g_match_lines.size() == 1 && (frontend.gui_flags & 1U) != 0,
        "without a match the line goes into the chat ring"
    );
    bind_battle_lines(nullptr, nullptr);

    mp::lobby_chat_head(frontend) = 3;
    mp::lobby_chat_tail(frontend) = 5;
    api::empty_chat_ring();
    check(
        mp::lobby_chat_head(frontend) == 0 && mp::lobby_chat_tail(frontend) == 0,
        "the chat ring empties"
    );
}

} // namespace

int main() {
    test_launch_answers();
    test_connection_type();
    test_apply_launch();
    test_launch_ended_by_binding();
    test_setup_request();
    test_launch_record();
    test_launch_link();
    test_report_hooks();
    test_page_hook();
    test_battle_lines();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::printf("launch binding tests passed\n");
    return 0;
}
