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

    // A mod's speed range: off, or on at 3.1c's own values, keeps 1..20.
    data::match_rules::ConsoleGameSpeedRange rule{};
    CHECK(speed::range_of(rule).slowest == speed::slowest);
    CHECK(speed::range_of(rule).fastest == speed::fastest);
    rule.minimum = 0;
    CHECK(speed::range_of(rule).slowest == speed::slowest);
    rule.enabled = true;
    rule.minimum = 1;
    CHECK(speed::range_of(rule).slowest == 1 && speed::range_of(rule).fastest == 20);

    // Down to 0: '-' steps to it, and it is announced as -10. The game is
    // not paused; it runs at no speed.
    rule.minimum = 0;
    const speed::Range unlocked = speed::range_of(rule);
    g.requested_speed = 1;
    g.current_speed = 1;
    g.sim_run_flags = 0;
    CHECK(speed::lower_speed(*w, hooks) == 1);
    CHECK(speed::lower_speed(*w, hooks, unlocked) == 0);
    CHECK(g.requested_speed == 0 && g.current_speed == 0 && g.sim_run_flags == 0);
    CHECK(std::strcmp(newest(g)->text, "Game Speed   -10\n") == 0);
    CHECK(speed::lower_speed(*w, hooks, unlocked) == 0);
    CHECK(speed::set_speed(*w, -3, hooks, unlocked) == 0);
    // Without the range, a set from 0 clamps back up to 1.
    CHECK(speed::set_speed(*w, 0, hooks) == 1);

    // A narrower range clamps every set and stops both keys at its ends.
    rule.minimum = 8;
    rule.maximum = 12;
    const speed::Range locked = speed::range_of(rule);
    CHECK(speed::set_speed(*w, 20, hooks, locked) == 12);
    CHECK(speed::raise_speed(*w, hooks, locked) == 12);
    CHECK(speed::set_speed(*w, 1, hooks, locked) == 8);
    CHECK(speed::lower_speed(*w, hooks, locked) == 8);
    CHECK(speed::raise_speed(*w, hooks, locked) == 9);
    // The fastest end is applied first, so a range whose ends cross keeps the
    // slowest.
    CHECK(speed::set_speed(*w, 10, hooks, speed::Range{14, 12}) == 14);

    // A host's lock is typed relative to normal and kept within the rules' range.
    const speed::Range whole{0, 20};
    CHECK(speed::locked(whole, 0, 5).slowest == 10 && speed::locked(whole, 0, 5).fastest == 15);
    CHECK(
        speed::locked(whole, -10, 10).slowest == 0 && speed::locked(whole, -10, 10).fastest == 20
    );
    CHECK(
        speed::locked(whole, -30, 30).slowest == 0 && speed::locked(whole, -30, 30).fastest == 20
    );
    // A fastest below the slowest is raised to it.
    CHECK(speed::locked(whole, 5, 0).slowest == 15 && speed::locked(whole, 5, 0).fastest == 15);
    // A narrower range narrows the lock further.
    CHECK(speed::locked(speed::Range{5, 15}, -10, 2).slowest == 5);
    CHECK(speed::locked(speed::Range{5, 15}, -10, 2).fastest == 12);
    CHECK(speed::locked(speed::Range{5, 15}, 8, 9).slowest == 15);

    auto lock = speed::read_lock_line(".syncon 0 5");
    CHECK(lock.request == speed::LockRequest::lock && lock.low == 0 && lock.high == 5);
    lock = speed::read_lock_line("  .SyncOn -5 20");
    CHECK(lock.request == speed::LockRequest::lock && lock.low == -5 && lock.high == 20);
    lock = speed::read_lock_line(".syncon");
    CHECK(lock.request == speed::LockRequest::lock && lock.low == 0 && lock.high == 0);
    lock = speed::read_lock_line(".syncon x 3");
    CHECK(lock.request == speed::LockRequest::lock && lock.low == 0 && lock.high == 3);
    CHECK(speed::read_lock_line(".syncoff").request == speed::LockRequest::unlock);
    CHECK(speed::read_lock_line(".SYNCOFF now").request == speed::LockRequest::unlock);
    CHECK(speed::read_lock_line(".synconce 1 2").request == speed::LockRequest::none);
    CHECK(speed::read_lock_line("syncon 1 2").request == speed::LockRequest::none);
    CHECK(speed::read_lock_line("hello").request == speed::LockRequest::none);
    CHECK(speed::read_lock_line(nullptr).request == speed::LockRequest::none);

    messages::clear_messages(g);
    CHECK(g.chat_head == 0 && g.chat_tail == 0);

    world_destroy(w);
    if (failures != 0)
        return 1;
    std::puts("sim speed ok");
    return 0;
}
