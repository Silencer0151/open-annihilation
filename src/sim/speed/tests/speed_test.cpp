// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/speed.hpp"

#include <cstdio>
#include <cstring>

using namespace oa;
using namespace oa::sim;

namespace {
int failures = 0;
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: %s\n", __FILE__, __LINE__, #x);                           \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

void set_capacity(Game& g, int32_t lines) {
    g.text_lines = lines;
}

const messages::MessageLine* newest(Game& g) {
    const uint32_t index = g.chat_head == 0 ? OA_CHAT_LINE_COUNT - 1 : g.chat_head - 1u;
    return messages::message_line(g, index);
}
} // namespace

int main() {
    World* w = world_create();
    if (w == nullptr)
        return 1;
    Game& g = w->game;
    set_capacity(g, 10);
    const messages::Hooks hooks{};
    g.requested_speed = speed::normal;
    g.current_speed = 7;

    char text[speed::message_bytes];
    speed::format_message(text, 10, hooks);
    CHECK(std::strcmp(text, "Game Speed Normal") == 0);
    speed::format_message(text, 13, hooks);
    CHECK(std::strcmp(text, "Game Speed  +3\n") == 0);
    // Slower speeds leave the sign column blank; the number carries the '-'.
    speed::format_message(text, 6, hooks);
    CHECK(std::strcmp(text, "Game Speed   -4\n") == 0);

    CHECK(speed::raise_speed(*w, hooks) == 11);
    CHECK(g.requested_speed == 11 && g.current_speed == 11);
    const auto* line = newest(g);
    CHECK(line != nullptr && std::strcmp(line->text, "Game Speed  +1\n") == 0);
    CHECK((line->kind & 0x0f) == messages::kind_status && line->sender == messages::sender_none);
    CHECK(g.chat_head == 1);

    CHECK(speed::lower_speed(*w, hooks) == 10);
    CHECK(std::strcmp(newest(g)->text, "Game Speed Normal") == 0 && g.chat_head == 2);

    // Clamped; an unchanged requested speed posts nothing but still resets
    // the current speed.
    CHECK(speed::set_speed(*w, 99, hooks) == speed::fastest && g.chat_head == 3);
    g.current_speed = 12;
    CHECK(speed::set_speed(*w, 25, hooks) == speed::fastest && g.chat_head == 3);
    CHECK(g.current_speed == speed::fastest);
    CHECK(speed::raise_speed(*w, hooks) == speed::fastest && g.chat_head == 3);
    CHECK(speed::set_speed(*w, -5, hooks) == speed::slowest && g.chat_head == 4);
    CHECK(std::strcmp(newest(g)->text, "Game Speed   -9\n") == 0);
    CHECK(speed::lower_speed(*w, hooks) == speed::slowest && g.chat_head == 4);

    // A translation replaces the words, not the number.
    messages::Hooks french{};
    french.translate = [](void*, const char* source) -> const char* {
        return std::strcmp(source, "Game Speed") == 0 ? "Vitesse" : nullptr;
    };
    speed::format_message(text, 12, french);
    CHECK(std::strcmp(text, "Vitesse  +2\n") == 0);

    messages::clear_messages(g);
    CHECK(g.chat_head == 0 && g.chat_tail == 0);

    world_destroy(w);
    if (failures != 0)
        return 1;
    std::puts("sim speed ok");
    return 0;
}
