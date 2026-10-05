// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/messages.hpp"

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

using namespace oa;
using namespace oa::sim::messages;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

struct Recorder {
    int arrived = 0;
    int refreshed = 0;
    int shared = 0;
    int recorded = 0;
    int32_t kind = 3;
    uint32_t roll = 4;
    int centred = 0;
    int32_t camera[3]{};
};

Hooks hooks_for(Recorder& r) {
    Hooks h{};
    h.context = &r;
    h.play_sound = [](void* c, const char* name) {
        if (std::strcmp(name, "MessageArrived") == 0)
            ++static_cast<Recorder*>(c)->arrived;
    };
    h.refresh_panel = [](void* c) { ++static_cast<Recorder*>(c)->refreshed; };
    h.random = [](void* c) { return static_cast<Recorder*>(c)->roll; };
    h.share_chat = [](void* c, const char*) { ++static_cast<Recorder*>(c)->shared; };
    h.record_chat = [](void* c, const char*) { ++static_cast<Recorder*>(c)->recorded; };
    h.game_kind = [](void* c) { return static_cast<Recorder*>(c)->kind; };
    h.center_camera = [](void* c, int32_t x, int32_t y, int32_t glide) {
        auto& rec = *static_cast<Recorder*>(c);
        ++rec.centred;
        rec.camera[0] = x;
        rec.camera[1] = y;
        rec.camera[2] = glide;
    };
    return h;
}

void set_capacity(Game& g, int32_t lines) {
    g.text_lines = lines;
}
} // namespace

int main() {
    World* w = world_create();
    if (w == nullptr)
        return 1;
    Recorder r;
    const Hooks h = hooks_for(r);

    post_message(*w, "ignored", 1, 0, 0, h);
    CHECK(w->game.chat_head == 0);
    set_capacity(w->game, 5);
    w->game.tick = 77;
    post_message(*w, "hello", 3, 9, 2, h);
    const MessageLine* line = message_line(w->game, 0);
    CHECK(std::strcmp(line->text, "hello") == 0 && line->tick == 77 && line->value == 9);
    CHECK(line->sender == 2 && (line->kind & 0xf) == 3);
    CHECK(w->game.chat_head == 1 && w->game.chat_tail == 0 && r.arrived == 1 && r.refreshed == 1);
    for (int i = 0; i < 4; ++i)
        post_message(*w, "x", 0, 0, sender_none, h);
    // Capacity 5: the fifth insert pushes the tail.
    CHECK(w->game.chat_head == 5 && w->game.chat_tail == 1 && r.arrived == 1);
    char long_text[80];
    std::memset(long_text, 'a', sizeof long_text);
    long_text[79] = '\0';
    post_message(*w, long_text, 0, 0, sender_none, h);
    CHECK(std::strlen(message_line(w->game, 5)->text) == 0x3f);
    // A UTF-8 character the 63rd byte would split goes whole: here a
    // three-byte one from byte 62 on.
    std::memset(long_text, 'a', 61);
    std::memcpy(long_text + 61, "\xE4\xB8\xAD\xE6\x97\xA5", 6);
    long_text[67] = '\0';
    post_message(*w, long_text, 0, 0, sender_none, h);
    CHECK(std::strlen(message_line(w->game, 6)->text) == 61);
    // Bytes of an 8-bit code page are cut at the 63rd byte as before.
    std::memset(long_text, '\xE9', 70);
    long_text[70] = '\0';
    post_message(*w, long_text, 0, 0, sender_none, h);
    CHECK(std::strlen(message_line(w->game, 7)->text) == 0x3f);

    // Wrapping: a space within reach splits after it; otherwise 51 characters.
    World* v = world_create();
    set_capacity(v->game, 30);
    char text[128];
    std::memset(text, 'b', 70);
    text[60] = ' ';
    text[70] = '\0';
    post_notice(*v, text, h);
    CHECK(v->game.chat_head == 2);
    CHECK(
        std::strlen(message_line(v->game, 0)->text) == 61 &&
        (message_line(v->game, 0)->kind & 0xf) == 0
    );
    CHECK(std::strlen(message_line(v->game, 1)->text) == 9);
    std::memset(text, 'c', 100);
    text[100] = '\0';
    post_notice(*v, text, h);
    CHECK(v->game.chat_head == 4 && std::strlen(message_line(v->game, 2)->text) == 51);
    CHECK(std::strlen(message_line(v->game, 3)->text) == 49);
    post_notice(*v, "   ", h);
    CHECK(v->game.chat_head == 4);

    Player& p = v->game.players[1];
    p.index = 1;
    p.info = oa_ref_from_index(1);
    v->player_info[1].side = 0;
    post_elimination(*v, p, h);
    CHECK(std::strcmp(message_line(v->game, 4)->text, "Arm vermin have been exterminated") == 0);
    CHECK(
        message_line(v->game, 4)->sender == 1 &&
        (message_line(v->game, 4)->kind & 0xf) == kind_elimination
    );

    // A profile's ending replaces the one the roll picks, and is not
    // translated; a null one leaves that ending to the translation hook.
    struct Endings {
        const char* replaced = nullptr;
        uint32_t asked = 99;
    } endings;

    Hooks profiled = h;
    profiled.context = &endings;
    profiled.random = [](void*) -> uint32_t { return 5; };
    profiled.translate = [](void*, const char* text) -> const char* {
        return std::strcmp(text, elimination_messages[2]) == 0 ? "ist fort" : nullptr;
    };
    profiled.elimination_ending = [](void* c, uint32_t index) {
        auto& e = *static_cast<Endings*>(c);
        e.asked = index;
        return e.replaced;
    };
    post_elimination(*v, p, profiled);
    CHECK(endings.asked == 2 && std::strcmp(message_line(v->game, 5)->text, "Arm ist fort") == 0);
    endings.replaced = "has been made-up";
    post_elimination(*v, p, profiled);
    CHECK(std::strcmp(message_line(v->game, 6)->text, "Arm has been made-up") == 0);

    // The kills board's new leader: a status line from no player.
    std::memcpy(p.name, "Ada", 4);
    post_kill_lead(*v, p, 7, h);
    CHECK(std::strcmp(message_line(v->game, 7)->text, "Ada has taken the lead with 7 kills") == 0);
    CHECK(
        message_line(v->game, 7)->sender == sender_none &&
        (message_line(v->game, 7)->kind & 0xf) == kind_status
    );
    Hooks lead = h;
    lead.kill_lead_text = [](void*) -> const char* { return "%d kills: %s leads"; };
    post_kill_lead(*v, p, 12, lead);
    CHECK(std::strcmp(message_line(v->game, 8)->text, "12 kills: Ada leads") == 0);

    // The text is never a format: each of %s and %d is put in once, %% is
    // one %, and everything else is shown as written.
    char lead_line[64];
    format_kill_lead(lead_line, sizeof lead_line, "%s %s %d %d %x %% %", "Ada", -3);
    CHECK(std::strcmp(lead_line, "Ada %s -3 %d %x % %") == 0);
    format_kill_lead(lead_line, sizeof lead_line, "%s leads", "Ada", 3);
    CHECK(std::strcmp(lead_line, "Ada leads") == 0);
    format_kill_lead(lead_line, sizeof lead_line, "A new leader", "Ada", 3);
    CHECK(std::strcmp(lead_line, "A new leader") == 0);
    format_kill_lead(lead_line, 6, "%s has %d", "Adalbert", 3);
    CHECK(std::strcmp(lead_line, "Adalb") == 0);

    std::memcpy(p.second_name, "Steve", 6);
    v->game.chat_mode = OA_CHAT_MODE_EVERYONE;
    post_chat(*v, p, "gg", 2, nullptr, h);
    CHECK(std::strcmp(message_line(v->game, 9)->text, "<Steve> gg") == 0);
    CHECK(r.shared == 1 && r.recorded == 1);
    v->game.chat_mode = OA_CHAT_MODE_ALLIES;
    post_chat(*v, p, "hi", 2, "Bob", h);
    CHECK(std::strcmp(message_line(v->game, 10)->text, "<Steve->Bob> hi") == 0);
    CHECK(r.shared == 2 && r.recorded == 1);
    // A line longer than the formatter holds is cut between whole UTF-8
    // characters: "<Steve> " and 63 hanzi of 80, not a part of the 64th.
    {
        static std::string shared;
        Hooks cut = h;
        cut.share_chat = [](void*, const char* line) { shared = line; };
        std::string hanzi;
        for (int i = 0; i < 80; ++i)
            hanzi += "\xe4\xbd\xa0"; // U+4F60
        v->game.chat_mode = OA_CHAT_MODE_EVERYONE;
        post_chat(*v, p, hanzi.c_str(), 2, nullptr, cut);
        CHECK(shared == "<Steve> " + hanzi.substr(0, 63 * 3));
        // A line that may go out as several records holds what the hooks
        // ask, up to 255 bytes: "<Steve> " and 82 hanzi; 0 asks for none.
        cut.shared_chat_line_bytes = [](void*) -> std::size_t { return 0x100; };
        hanzi += hanzi;
        post_chat(*v, p, hanzi.c_str(), 2, nullptr, cut);
        CHECK(shared == "<Steve> " + hanzi.substr(0, 82 * 3));
        cut.shared_chat_line_bytes = [](void*) -> std::size_t { return 0x1000; };
        post_chat(*v, p, hanzi.c_str(), 2, nullptr, cut);
        CHECK(shared == "<Steve> " + hanzi.substr(0, 82 * 3));
        cut.shared_chat_line_bytes = [](void*) -> std::size_t { return 0; };
        post_chat(*v, p, hanzi.c_str(), 2, nullptr, cut);
        CHECK(shared == "<Steve> " + hanzi.substr(0, 63 * 3));
    }
    // A shared line that went out as two records shows as those two lines.
    {
        Hooks split = h;
        split.shared_chat_line = [](void*, const char* line, std::size_t index) -> const char* {
            static const char* const parts[] = {"<Steve> first half", "<Steve> second half"};
            return std::strcmp(line, "<Steve> first half second half") == 0 && index < 2
                       ? parts[index]
                       : nullptr;
        };
        const auto head = v->game.chat_head;
        post_chat(*v, p, "first half second half", 2, nullptr, split);
        CHECK(std::strcmp(message_line(v->game, head)->text, "<Steve> first half") == 0);
        CHECK(std::strcmp(message_line(v->game, head + 1)->text, "<Steve> second half") == 0);
        CHECK(v->game.chat_head == head + 2);
        // Kept on this machine only, it shows as typed.
        v->game.chat_mode = chat_mode_local_only;
        post_chat(*v, p, "first half second half", 2, nullptr, split);
        CHECK(
            std::strcmp(message_line(v->game, head + 2)->text, "<Steve> first half second half") ==
            0
        );
        v->game.chat_mode = OA_CHAT_MODE_EVERYONE;
    }

    // Expiry: the oldest line goes once it has been up for TextScroll + 1
    // seconds, one line per call.
    World* e = world_create();
    set_log_options(e->game, 10, 2, filter_session_start, 1);
    CHECK(line_capacity(e->game) == 10 && text_scroll(e->game) == 2);
    CHECK(!expire_oldest_message(e->game));
    e->game.tick = 100;
    post_message(*e, "first", 1, 0, sender_none, h);
    e->game.tick = 130;
    post_message(*e, "second", 1, 0, sender_none, h);
    e->game.tick = 190;
    CHECK(!expire_oldest_message(e->game));
    e->game.tick = 191;
    CHECK(expire_oldest_message(e->game) && e->game.chat_tail == 1);
    CHECK(!expire_oldest_message(e->game));
    e->game.tick = 400;
    CHECK(expire_oldest_message(e->game) && e->game.chat_tail == 2 && e->game.chat_head == 2);
    CHECK(!expire_oldest_message(e->game));
    post_message(*e, "third", 1, 0, sender_none, h);
    clear_messages(e->game);
    CHECK(e->game.chat_head == 0 && e->game.chat_tail == 0);
    world_destroy(e);

    // Reported units: lines are scanned from the
    // oldest; a line needs a unit id, no visited bit (0x10) and a unit with
    // OA_UNIT_FLAG_LIVE. The camera gets the high words of x and z.
    World* t = world_create();
    WorldCapacity capacity{8, 1, 0};
    if (t == nullptr || !world_alloc_tables(t, &capacity))
        return 1;
    set_log_options(t->game, 10, 2, filter_session_start, 1);
    t->units[3].flags = OA_UNIT_FLAG_LIVE;
    t->units[3].position = {
        static_cast<int32_t>(300u << 16) + 0x1234, 0, static_cast<int32_t>(200u << 16)
    };
    t->units[4].flags = OA_UNIT_FLAG_LIVE;
    t->units[4].position = {
        static_cast<int32_t>(400u << 16), 0, static_cast<int32_t>(0xfffbu << 16)
    };
    t->units[5].flags = OA_UNIT_FLAG_SELECTED;
    post_message(*t, "no unit", kind_unit_report, 0, sender_none, h);
    post_message(*t, "gone", kind_unit_report, 5, sender_none, h);
    post_message(*t, "three", kind_unit_report, 3, sender_none, h);
    post_message(*t, "four", kind_unit_report, 4, sender_none, h);
    r = Recorder{};
    cycle_reported_units(*t, h);
    CHECK(r.centred == 1 && r.camera[0] == 300 && r.camera[1] == 200 && r.camera[2] == 1);
    CHECK(
        message_line(t->game, 2)->kind ==
        (kind_unit_report | line_flag_visited | line_flag_highlight)
    );
    CHECK(message_line(t->game, 1)->kind == kind_unit_report);
    cycle_reported_units(*t, h);
    CHECK(r.centred == 2 && r.camera[0] == 400 && r.camera[1] == -5);
    CHECK(message_line(t->game, 2)->kind == (kind_unit_report | line_flag_visited));
    CHECK(
        message_line(t->game, 3)->kind ==
        (kind_unit_report | line_flag_visited | line_flag_highlight)
    );
    CHECK(!track_next_reported_unit(*t, h) && r.centred == 2);
    // All visited: the visits are forgotten and the oldest goes again.
    cycle_reported_units(*t, h);
    CHECK(r.centred == 3 && r.camera[0] == 300);
    CHECK(message_line(t->game, 3)->kind == kind_unit_report);
    CHECK(
        message_line(t->game, 2)->kind ==
        (kind_unit_report | line_flag_visited | line_flag_highlight)
    );
    // A head outside the ring ends the scan instead of running forever.
    t->game.chat_head = OA_CHAT_LINE_COUNT;
    CHECK(!track_next_reported_unit(*t, h));
    world_destroy(t);
    world_destroy(v);
    world_destroy(w);
    if (failures != 0)
        return 1;
    std::puts("sim-messages: ok");
    return 0;
}
