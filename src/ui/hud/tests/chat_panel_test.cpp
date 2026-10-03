// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"
#include "fixtures.hpp"

#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/game_fields.hpp"
#include "oa/ui/hud/player_records.hpp"

#include "oa/sim/messages.hpp"
#include "oa/base/text.hpp"

#include <array>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

struct Posted {
    std::string text;
    std::string target;
    uint8_t mode{};
    uint8_t targets[OA_PLAYER_COUNT]{};
};

struct Host {
    std::vector<Posted> posts;
    std::vector<std::string> commands;
    Game* game = nullptr;
    uint8_t command_mode = 0xff;
    int slot_refreshes = 0;

    ChatHost host() {
        ChatHost h{};
        h.user = this;
        h.submit_command = [](void* u, const char* line, uint8_t mode) -> uint8_t {
            auto* self = static_cast<Host*>(u);
            if (line[0] != '+')
                return mode;
            self->commands.emplace_back(line + 1);
            return self->command_mode != 0xff ? self->command_mode : mode;
        };
        h.post_chat =
            [](void* u, const Player&, const char* text, uint8_t kind, const char* target) {
                auto* self = static_cast<Host*>(u);
                CHECK(kind == kMessageKindChat);
                Posted p{text, target != nullptr ? target : "", self->game->chat_mode, {}};
                std::memcpy(p.targets, self->game->chat_targets, sizeof p.targets);
                self->posts.push_back(p);
            };
        h.refresh_player_slots = [](void* u) { ++static_cast<Host*>(u)->slot_refreshes; };
        return h;
    }
};

struct Chat {
    hud_test::TestWorld match;
    hud_test::FakePanel panel{
        {"TALK", "SENDTO", "SENDTYPE", "LIVEPLYR1", "PLAYER1", "LIVEPLYR2", "PLAYER2"}
    };
    hud_test::Sounds sounds;
    Host host;
    ChatDraft draft;

    Chat() {
        match.add_player(0, OA_PLAYER_STATUS_LOCAL);
        match.add_player(1, OA_PLAYER_STATUS_MIRRORED);
        match.add_player(2, OA_PLAYER_STATUS_MIRRORED);
        match.game().local_player_index = 0;
        host.game = &match.game();
    }

    ChatClick click(
        const char* name,
        oa::data::campaign::SessionKind session = oa::data::campaign::SessionKind::multiplayer
    ) {
        return chat_panel_click(
            *match.world,
            session,
            name,
            name != nullptr ? panel.index(name) : -1,
            draft,
            panel.loader(),
            panel.controls(),
            host.host(),
            sounds.events()
        );
    }

    void type(const char* text) {
        panel.texts["TALK"] = text;
        click("TALK");
    }
};

void test_send_prefixes() {
    Chat chat;
    Game& game = chat.match.game();
    game.chat_mode = OA_CHAT_MODE_EVERYONE;
    chat.type("  hello");
    CHECK(chat.host.posts.size() == 1 && chat.host.posts[0].text == "hello");
    CHECK(chat.host.posts[0].target.empty() && chat.host.posts[0].mode == OA_CHAT_MODE_EVERYONE);

    chat.type("a:attack now");
    CHECK(chat.host.posts[1].text == "attack now" && chat.host.posts[1].target == "Allies");
    CHECK(chat.host.posts[1].mode == OA_CHAT_MODE_ALLIES);
    chat.type("E;gg");
    CHECK(
        chat.host.posts[2].target == "Enemies" && chat.host.posts[2].mode == OA_CHAT_MODE_ENEMIES
    );

    // A digit picks one player for this line only.
    game.chat_targets[1] = 1;
    chat.type("2,psst");
    const auto& whisper = chat.host.posts[3];
    CHECK(
        whisper.text == "psst" && whisper.target == "Player 2" &&
        whisper.mode == OA_CHAT_MODE_CHOSEN
    );
    CHECK(whisper.targets[2] == 1 && whisper.targets[1] == 0);
    CHECK(
        game.chat_mode == OA_CHAT_MODE_EVERYONE && game.chat_targets[1] == 1 &&
        game.chat_targets[2] == 0
    );

    // A digit naming an empty slot sends nothing.
    chat.type("7:nobody");
    CHECK(chat.host.posts.size() == 4);
    // A separator must follow directly and be printable.
    chat.type("a hello");
    CHECK(chat.host.posts[4].text == "a hello");

    // Commands run and set the echo mode.
    chat.host.command_mode = kChatModeLocalOnly;
    chat.type("+clock");
    CHECK(chat.host.commands.size() == 1 && chat.host.commands[0] == "clock");
    CHECK(chat.host.posts[5].text == "+clock" && chat.host.posts[5].mode == kChatModeLocalOnly);
    CHECK(game.chat_mode == OA_CHAT_MODE_EVERYONE);
    // Empty text posts nothing but still clears the draft and team flag.
    set_chat_to_team(game, true);
    oa::base::text::copy_terminated(chat.draft.text, "draft");
    chat.type("   ");
    CHECK(chat.host.posts.size() == 6 && chat.draft.text[0] == '\0' && !chat_to_team(game));
}

// The app's chat line sends through the same TALK branch: an accepted cheat
// echoes to everyone while the panel sends to allies; a refused one keeps
// the allies mode; the mode is put back afterwards.
void test_send_chat_line() {
    Chat chat;
    Game& game = chat.match.game();
    game.chat_mode = OA_CHAT_MODE_ALLIES;
    chat.host.command_mode = OA_CHAT_MODE_EVERYONE;
    send_chat_line(*chat.match.world, " +atm", chat.host.host());
    CHECK(chat.host.commands.size() == 1 && chat.host.commands[0] == "atm");
    CHECK(chat.host.posts.size() == 1 && chat.host.posts[0].text == "+atm");
    CHECK(
        chat.host.posts[0].mode == OA_CHAT_MODE_EVERYONE && game.chat_mode == OA_CHAT_MODE_ALLIES
    );
    chat.host.command_mode = 0xff;
    send_chat_line(*chat.match.world, "+atm", chat.host.host());
    CHECK(chat.host.posts.size() == 2 && chat.host.posts[1].mode == OA_CHAT_MODE_ALLIES);
}

void test_target_controls() {
    Chat chat;
    Game& game = chat.match.game();
    chat.panel.values["LIVEPLYR2"] = 1;
    CHECK(chat.click("LIVEPLYR2") == ChatClick::clear_selection);
    CHECK(game.chat_mode == OA_CHAT_MODE_CHOSEN && game.chat_targets[2] == 1);
    CHECK(chat.panel.groups["SENDTYPE"] == OA_CHAT_MODE_CHOSEN);
    CHECK(chat.sounds.played.back() == "SmallButton");

    chat.panel.values["SENDTYPE"] = 9;
    CHECK(chat.click("SENDTYPE") == ChatClick::clear_selection);
    CHECK(game.chat_mode == OA_CHAT_MODE_EVERYONE);
    chat.panel.values["SENDTYPE"] = OA_CHAT_MODE_ALLIES;
    chat.click("SENDTYPE");
    CHECK(game.chat_mode == OA_CHAT_MODE_ALLIES);

    // SENDTO keeps the typed text and reloads the team layout.
    chat.draft.initialized = true;
    chat.panel.texts["TALK"] = "half typed";
    chat.panel.values["SENDTO"] = 1;
    CHECK(chat.click("SENDTO") == ChatClick::reopened);
    CHECK(chat_to_team(game) && chat.panel.closes == 1);
    CHECK(chat.panel.loads.back() == "TALK2.GUI" && chat.panel.texts["TALK"] == "half typed");
    CHECK(chat.panel.groups["SENDTYPE"] == OA_CHAT_MODE_ALLIES && chat.host.slot_refreshes == 1);

    CHECK(chat.click(nullptr) == ChatClick::closed);
    CHECK((game.frame_flags & kFrameChatOpen) == 0);
}

void test_open_panel() {
    Chat chat;
    Game& game = chat.match.game();
    CHECK(open_chat_panel(
        *chat.match.world,
        oa::data::campaign::SessionKind::skirmish,
        chat.draft,
        chat.panel.loader(),
        chat.panel.controls(),
        chat.host.host()
    ));
    CHECK(chat.panel.loads.back() == "TALK.GUI" && chat.panel.load_flags.back() == 0x880);
    CHECK((game.frame_flags & kFrameChatOpen) != 0 && chat.panel.focused == "TALK");
    CHECK(chat.panel.values["SENDTO"] == 0);
    // Team chat only in multiplayer.
    set_chat_to_team(game, true);
    open_chat_panel(
        *chat.match.world,
        oa::data::campaign::SessionKind::skirmish,
        chat.draft,
        chat.panel.loader(),
        chat.panel.controls(),
        chat.host.host()
    );
    CHECK(chat.panel.loads.back() == "TALK.GUI");
    open_chat_panel(
        *chat.match.world,
        oa::data::campaign::SessionKind::multiplayer,
        chat.draft,
        chat.panel.loader(),
        chat.panel.controls(),
        chat.host.host()
    );
    CHECK(chat.panel.loads.back() == "TALK2.GUI" && chat.panel.load_flags.back() == 0x800);
    // The unit info panel and watchers block the chat panel.
    const auto loads = chat.panel.loads.size();
    game.frame_flags |= kFrameUnitInfoOpen;
    CHECK(!open_chat_panel(
        *chat.match.world,
        oa::data::campaign::SessionKind::multiplayer,
        chat.draft,
        chat.panel.loader(),
        chat.panel.controls(),
        chat.host.host()
    ));
    game.frame_flags = 0;
    chat.match.world->player_info[0].options = OA_SETUP_OPTION_WATCHER;
    CHECK(!open_chat_panel(
        *chat.match.world,
        oa::data::campaign::SessionKind::multiplayer,
        chat.draft,
        chat.panel.loader(),
        chat.panel.controls(),
        chat.host.host()
    ));
    CHECK(chat.panel.loads.size() == loads);
}

struct LogRecorder {
    std::vector<std::string> lines;
    std::vector<int32_t> xs;
    std::vector<uint8_t> colors;
    int logos = 0;
    const Player* logo_player = nullptr;  // of the last logo
    std::array<int32_t, 4> logo_square{}; // x0, y0, x1, y1 of the last logo

    MessageLogSink sink() {
        MessageLogSink s{};
        s.user = this;
        s.font_height = [](void*) { return 10; };
        s.set_color = [](void* u, uint8_t c) { static_cast<LogRecorder*>(u)->colors.push_back(c); };
        s.logo = [](void* u, const Player& player, int32_t x0, int32_t y0, int32_t x1, int32_t y1) {
            auto* self = static_cast<LogRecorder*>(u);
            ++self->logos;
            self->logo_player = &player;
            self->logo_square = {x0, y0, x1, y1};
        };
        s.text = [](void* u, const char* t, int32_t x, int32_t) {
            static_cast<LogRecorder*>(u)->lines.emplace_back(t);
            static_cast<LogRecorder*>(u)->xs.push_back(x);
        };
        return s;
    }
};

void put_line(Game& game, uint32_t index, const char* text, uint8_t kind, uint8_t sender) {
    sim::messages::MessageLine line{};
    std::snprintf(line.text, sizeof line.text, "%s", text);
    line.sender = sender;
    line.kind = kind;
    std::memcpy(game.chat_lines[index], &line, sizeof line);
}

void test_message_log() {
    hud_test::TestWorld match;
    Game& game = match.game();
    game.ui_colors[10] = 0x11;
    game.ui_colors[15] = 0x22;
    put_line(game, 0, "one", 2, 10);
    put_line(game, 1, "two", 4 | kMessageHighlight, 1);
    put_line(game, 2, "three", 8, 10);
    put_line(game, 3, "four", 2, 10);
    game.chat_tail = 0;
    game.chat_head = 4;
    set_message_lines(game, 4);
    game.message_filter = 2;
    LogRecorder log;
    draw_message_log(*match.world, log.sink());
    // The ring shows one line fewer than the count: lines 1..3, minus kind 8.
    CHECK(log.lines.size() == 2 && log.lines[0] == "two" && log.lines[1] == "four");
    CHECK(log.colors[0] == 0x11 && log.colors[1] == 0x22);
    CHECK(log.logos == 1 && log.xs[0] == 150 && log.xs[1] == kMessageLogLeft);

    log = {};
    set_message_lines(game, 10);
    game.message_filter = 1;
    draw_message_log(*match.world, log.sink());
    // Chat-only stops at the first other line.
    CHECK(log.lines.size() == 1 && log.lines[0] == "one");

    log = {};
    game.message_filter = 3;
    draw_message_log(*match.world, log.sink());
    CHECK(log.lines.size() == 2 && log.lines[0] == "two" && log.lines[1] == "three");
    game.screen_chat = 1;
    log = {};
    draw_message_log(*match.world, log.sink());
    CHECK(log.lines.size() == 4);

    log = {};
    game.message_filter = 0;
    draw_message_log(*match.world, log.sink());
    CHECK(log.lines.empty());
}

// A chat line another player sent shows under the session's filter even
// with ScreenChat off, and starts with the sender's logo, 0.8 of a line high,
// with its text 1.5 logo widths past the log's left edge. The local player's
// own chat line has no sender: no logo, and its text at the edge.
void test_received_chat_line() {
    hud_test::TestWorld match;
    Game& game = match.game();
    constexpr uint8_t sender = 2;
    put_line(game, 0, "older", sim::messages::kind_status, sim::messages::sender_none);
    put_line(game, 1, "<two> hi", sim::messages::kind_player_chat, sender);
    put_line(game, 2, "<me> hi", kMessageKindChat, sim::messages::sender_none);
    put_line(game, 3, "Game speed", sim::messages::kind_status, sim::messages::sender_none);
    game.chat_tail = 0;
    game.chat_head = 4;
    set_message_lines(game, 10);
    game.message_filter = sim::messages::filter_session_start;
    game.screen_chat = 0;
    LogRecorder log;
    draw_message_log(*match.world, log.sink());
    // Status lines hide while ScreenChat is off.
    CHECK(log.lines.size() == 2 && log.lines[0] == "<two> hi" && log.lines[1] == "<me> hi");
    constexpr int32_t logo_size = 8; // 0.8 of the recorder's 10-pixel lines
    CHECK(log.logos == 1 && log.logo_player == &game.players[sender]);
    CHECK((
        log.logo_square ==
        std::array<int32_t, 4>{
            kMessageLogLeft, kMessageLogTop, kMessageLogLeft + logo_size, kMessageLogTop + logo_size
        }
    ));
    CHECK(log.xs[0] == kMessageLogLeft + logo_size * 3 / 2 && log.xs[1] == kMessageLogLeft);

    // Filter 2 hides another player's chat and keeps the rest.
    log = {};
    game.message_filter = 2;
    draw_message_log(*match.world, log.sink());
    CHECK(log.logos == 0 && log.lines.size() == 3 && log.lines[0] == "older");
}

// A line the sink writes in several rows takes a font height for each, and
// asks for its rows where its text starts, past a sender's logo. Past the
// most rows the oldest lines are left out; the newest always shows.
void test_message_log_rows() {
    hud_test::TestWorld match;
    Game& game = match.game();
    constexpr uint8_t sender = 1;
    put_line(game, 0, "one", sim::messages::kind_status, sim::messages::sender_none);
    put_line(game, 1, "two rows", sim::messages::kind_status, sender);
    put_line(game, 2, "three", sim::messages::kind_status, sim::messages::sender_none);
    put_line(game, 3, "four rows", sim::messages::kind_status, sim::messages::sender_none);
    game.chat_tail = 0;
    game.chat_head = 4;
    set_message_lines(game, 10);
    game.message_filter = 2;

    struct Rows {
        int32_t most{};
        std::vector<std::string> lines;
        std::vector<int32_t> tops;
        std::vector<int32_t> asked_at;
        int logos = 0;

        MessageLogSink sink() {
            MessageLogSink s{};
            s.user = this;
            s.font_height = [](void*) { return 28; };
            // A row for each word.
            s.rows = [](void* u, const char* text, int32_t x) {
                static_cast<Rows*>(u)->asked_at.push_back(x);
                int32_t rows = 1;
                for (const char* at = text; *at != '\0'; ++at)
                    rows += *at == ' ' ? 1 : 0;
                return rows;
            };
            s.logo = [](void* u, const Player&, int32_t, int32_t, int32_t, int32_t) {
                ++static_cast<Rows*>(u)->logos;
            };
            s.text = [](void* u, const char* text, int32_t, int32_t y) {
                static_cast<Rows*>(u)->lines.emplace_back(text);
                static_cast<Rows*>(u)->tops.push_back(y);
            };
            s.most_rows = most;
            return s;
        }
    };

    Rows all;
    draw_message_log(*match.world, all.sink());
    CHECK(all.lines.size() == 4);
    CHECK(
        (all.tops ==
         std::vector<int32_t>{
             kMessageLogTop, kMessageLogTop + 28, kMessageLogTop + 3 * 28, kMessageLogTop + 4 * 28
         })
    );
    // The sender's line asks for its rows past its logo, 0.8 of a 28-row line.
    constexpr int32_t logo = 22;
    CHECK(all.asked_at.size() == 4 && all.asked_at[1] == kMessageLogLeft + logo * 3 / 2);
    CHECK(all.asked_at[0] == kMessageLogLeft && all.logos == 1);

    // Six rows in five: the oldest line goes, and the rest move up.
    Rows five;
    five.most = 5;
    draw_message_log(*match.world, five.sink());
    CHECK(five.lines.size() == 3 && five.lines[0] == "two rows" && five.lines[2] == "four rows");
    CHECK(
        (five.tops ==
         std::vector<int32_t>{kMessageLogTop, kMessageLogTop + 2 * 28, kMessageLogTop + 3 * 28})
    );
    // Two rows hold the newest line alone; one row still shows it.
    for (const int32_t most : {2, 1}) {
        Rows few;
        few.most = most;
        draw_message_log(*match.world, few.sink());
        CHECK(few.lines.size() == 1 && few.lines[0] == "four rows" && few.logos == 0);
        CHECK(few.tops == std::vector<int32_t>{kMessageLogTop});
    }
}

void test_logo_blit() {
    const int32_t rect[4] = {10, 20, 30, 40};
    const auto blit = player_logo_blit(rect, 16, 12, 5);
    CHECK(blit.dest[0] == 10 && blit.dest[1] == 25 && blit.dest[4] == 30 && blit.dest[5] == 45);
    CHECK(blit.source[2] == 16 && blit.source[5] == 12 && blit.source[6] == 0);
}

} // namespace

int main() {
    test_send_prefixes();
    test_send_chat_line();
    test_target_controls();
    test_open_panel();
    test_message_log();
    test_received_chat_line();
    test_message_log_rows();
    test_logo_blit();
    return 0;
}
