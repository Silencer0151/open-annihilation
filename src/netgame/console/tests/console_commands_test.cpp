// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The console's network and service commands registered through the engine
// console's extend hook: their state changes and notices, the page command,
// the traffic reset at setup and per-console hosts.
#include "oa/netgame/console/console_commands.hpp"

#include "oa/ui/console/game_fields.hpp"

#include <cstdio>
#include <cstring>
#include <initializer_list>
#include <string>
#include <vector>

namespace {

namespace ui = oa::ui::console;
namespace nc = oa::netgame::console;

int g_failures = 0;

#define CHECK(cond)                                                                                \
    do {                                                                                           \
        if (!(cond)) {                                                                             \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #cond);          \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

constexpr uint32_t kOptions = ui::command_class::option | ui::command_class::private_echo;

struct Recorder {
    std::vector<std::string> messages;
    std::vector<std::string> calls;
    int traffic_resets = 0;
    bool launched = true;
};

Recorder g_rec;

// "<kind>:<text>", with "@<sender>" after it for a line a player sent; the
// console's lines come from no player.
void rec_post(void*, const char* text, uint8_t kind, uint8_t sender) {
    std::string line = std::to_string(kind) + ":" + text;
    if (sender != ui::kMessageNoSender) {
        line += '@';
        line += std::to_string(sender);
    }
    g_rec.messages.push_back(line);
}

char* rec_read(void*, const char* path, int32_t*) {
    g_rec.calls.push_back(std::string("read ") + path);
    return nullptr;
}

void rec_reset(void* context) {
    ++g_rec.traffic_resets;
    g_rec.calls.push_back(std::string("reset ") + static_cast<const char*>(context));
}

bool rec_launch_active(void*) {
    return g_rec.launched;
}

void rec_page(void* context, const char* user, const char* text) {
    g_rec.calls.push_back(
        std::string("page ") + static_cast<const char*>(context) + " " + user + "|" + text
    );
}

struct Fixture {
    oa::World* world;
    nc::CommandHost commands;
    ui::ConsoleHost host;
    ui::Console con;

    explicit Fixture(const char* name = "a", bool extended = true)
        : world(oa::world_create()), commands{}, host{}, con{} {
        g_rec = Recorder{};
        oa::Game& g = world->game;
        for (uint8_t i = 0; i < 2; ++i) {
            oa::Player& p = g.players[i];
            p.in_use = 1;
            p.status = i == 0 ? OA_PLAYER_STATUS_LOCAL : OA_PLAYER_STATUS_COMPUTER;
            p.index = i;
            p.info = oa::oa_ref_from_index(i);
        }
        g.local_player_index = 0;
        commands.context = const_cast<char*>(name);
        commands.reset_traffic_stats = rec_reset;
        commands.launch_active = rec_launch_active;
        commands.send_page = rec_page;
        host.post_message = rec_post;
        host.read_text_file = rec_read;
        if (extended) {
            host.extension_context = &commands;
            host.extend = nc::register_console_commands;
        }
        CHECK(ui::console_init(&con, world, &host));
    }

    ~Fixture() { oa::world_destroy(world); }

    Fixture(const Fixture&) = delete;
    Fixture& operator=(const Fixture&) = delete;

    oa::Game& game() { return world->game; }

    uint32_t run(const char* text, uint32_t mask = ui::command_class::all) {
        return ui::console_execute(&con, text, mask);
    }
};

// Setup resets the traffic statistics once; NetStats resets them again.
void test_setup_and_netstats() {
    Fixture f;
    CHECK(g_rec.traffic_resets == 1);
    CHECK(f.run("NetStats", kOptions) == kOptions);
    CHECK(g_rec.traffic_resets == 2);
    CHECK((g_rec.calls == std::vector<std::string>{"reset a", "reset a"}));
    CHECK(g_rec.messages.empty());
}

void test_values() {
    Fixture f;
    f.run("BPS");
    CHECK(f.game().show_bandwidth == 1);
    f.run("BPS", kOptions);
    CHECK(f.game().show_bandwidth == 0);
    f.run("Senderror 40");
    CHECK(f.game().send_error_percent == 40);
    f.run("Senderror 140");
    CHECK(f.game().send_error_percent == 0);
    f.run("Senderror 30");
    f.run("Senderror 40 50");
    CHECK(f.game().send_error_percent == 30);
    f.run("Senderror -1");
    CHECK(f.game().send_error_percent == 0);
    // A developer command: the option mask does not reach it.
    f.run("Senderror 30");
    CHECK(f.run("Senderror 40", kOptions) == 0);
    CHECK(f.game().send_error_percent == 30);
    f.run("Drop 0");
    CHECK(ui::console_flags(f.game()) == ui::console_flag::no_drop);
    f.run("Drop 1");
    CHECK(ui::console_flags(f.game()) == 0);
    f.run("Drop");
    CHECK(ui::console_flags(f.game()) == ui::console_flag::no_drop);
    f.run("Drop 7");
    CHECK(ui::console_flags(f.game()) == 0);
}

void test_compression() {
    Fixture f;
    f.run("Compression");
    CHECK(f.game().compression_off == 0); // not a live game
    CHECK(g_rec.messages.empty());
    f.game().session_flags = 1;
    CHECK(f.run("Compression", kOptions) == kOptions);
    CHECK(f.game().compression_off == 1);
    f.run("Compression");
    CHECK(f.game().compression_off == 0);
    CHECK(
        (g_rec.messages == std::vector<std::string>{
                               "2:Ok.  Outgoing packet compression turned OFF",
                               "2:Ok.  Outgoing packet compression turned ON",
                           })
    );
}

void test_page() {
    Fixture f;
    f.run("Page");
    f.run("P averyveryverylongname hi");
    f.run("page Bob hello   there");
    CHECK(g_rec.messages.size() == 3);
    CHECK(g_rec.messages[0] == "4:Syntax: page <user> <text>");
    CHECK(g_rec.messages[1] == "4:Invalid user name (too long): ");
    CHECK(g_rec.messages[2] == "4:A page request for Bob has been sent.");
    CHECK(g_rec.calls.back() == "page a Bob|hello there");
    g_rec.launched = false;
    f.run("p Bob hi");
    CHECK(g_rec.messages.back() == "4:Page command requires game launch from the Boneyards.");
    CHECK(g_rec.calls.back() == "page a Bob|hello there");
    g_rec.launched = true;
    nc::console_page_user(&f.con, "page Ann one two");
    CHECK(g_rec.messages.back() == "4:A page request for Ann has been sent.");
    CHECK(g_rec.calls.back() == "page a Ann|one two");
    CHECK(ui::console_active() == nullptr);
    f.commands.launch_active = nullptr;
    nc::console_page_user(&f.con, "page Ann one");
    CHECK(g_rec.messages.back() == "4:Page command requires game launch from the Boneyards.");
}

// Two consoles each reach their own host, as the two machines of an
// in-process check do.
void test_hosts_per_console() {
    Fixture a("a");
    Fixture b("b");
    g_rec = Recorder{};
    b.run("NetStats");
    a.run("page Bob x");
    b.run("page Bob y");
    CHECK((g_rec.calls == std::vector<std::string>{"reset b", "page a Bob|x", "page b Bob|y"}));
}

// Without the extension the commands are unknown: the option mask finds
// nothing, and "Senderror" reaches the developer fallback, which looks for a
// debugdat script of that name.
void test_absent_without_extension() {
    Fixture f("a", false);
    f.game().session_flags = 1;
    for (const char* command :
         {"NetStats", "Drop 0", "Compression", "BPS", "Page Bob hi", "P Bob hi"})
        CHECK(f.run(command, kOptions) == 0);
    CHECK(ui::console_flags(f.game()) == 0);
    CHECK(f.game().show_bandwidth == 0);
    CHECK(f.game().compression_off == 0);
    CHECK(g_rec.messages.empty() && g_rec.traffic_resets == 0);
    f.run("Senderror 40");
    CHECK(f.game().send_error_percent == 0);
    CHECK((g_rec.calls == std::vector<std::string>{"read debugdat\\Senderror.txt"}));
}

} // namespace

int main() {
    test_setup_and_netstats();
    test_values();
    test_compression();
    test_page();
    test_hosts_per_console();
    test_absent_without_extension();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", g_failures);
        return 1;
    }
    std::puts("net console commands: all checks passed");
    return 0;
}
