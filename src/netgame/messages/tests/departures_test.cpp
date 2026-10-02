// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/messages/departures.hpp"

#include <cstdio>
#include <cstring>

using namespace oa;
using namespace oa::netgame::messages;

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
    uint32_t roll = 4;
};

sim::messages::Hooks hooks_for(Recorder& r) {
    sim::messages::Hooks h{};
    h.context = &r;
    h.random = [](void* c) { return static_cast<Recorder*>(c)->roll; };
    return h;
}

/// Sets how many message lines the game keeps (Game.text_lines).
void set_capacity(Game& g, int32_t lines) {
    g.text_lines = lines;
}
} // namespace

int main() {
    World* v = world_create();
    if (v == nullptr)
        return 1;
    set_capacity(v->game, 30);
    Recorder r;
    const auto h = hooks_for(r);
    Player& p = v->game.players[1];
    p.index = 1;

    // Departure: Player.name, then the phrase the
    // low three bits of rand() pick, as an elimination line from that slot.
    Player& leaver = v->game.players[3];
    leaver.index = 3;
    leaver.status = OA_PLAYER_STATUS_MIRRORED;
    leaver.player_id = 0x77;
    std::memcpy(leaver.name, "Bob", 4);
    r.roll = 0x0d;
    post_departure(*v, 0x77, h);
    CHECK(v->game.chat_head == 1);
    CHECK(
        std::strcmp(sim::messages::message_line(v->game, 0)->text, "Bob has been eradicated") == 0
    );
    CHECK(
        sim::messages::message_line(v->game, 0)->sender == 3 &&
        (sim::messages::message_line(v->game, 0)->kind & 0xf) == sim::messages::kind_elimination
    );
    // An id no slot holds posts nothing, nor does a local record whose reject
    // reason is 1.
    post_departure(*v, 0x78, h);
    CHECK(v->game.chat_head == 1);
    v->game.local_player_index = 1;
    p.reject_reason = reject_reason_departures_muted;
    post_departure(*v, 0x77, h);
    CHECK(v->game.chat_head == 1);
    p.reject_reason = 2;
    r.roll = 0;
    post_departure(*v, 0x77, h);
    CHECK(
        std::strcmp(sim::messages::message_line(v->game, 1)->text, "Bob has left the scene") == 0
    );
    // The phrase is the key its translation is found by.
    auto translated = h;
    translated.translate = [](void*, const char* key) -> const char* {
        return std::strcmp(key, "has left the scene") == 0 ? "est parti" : nullptr;
    };
    post_departure(*v, 0x77, translated);
    CHECK(std::strcmp(sim::messages::message_line(v->game, 2)->text, "Bob est parti") == 0);
    world_destroy(v);
    if (failures != 0)
        return 1;
    std::puts("departure notices passed");
    return 0;
}
