// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_effects/effects.hpp"
#include "oa/sim/unit_effects/effects_offline.hpp"
#include <iostream>
#include <stdexcept>
#include <vector>
using namespace oa::sim::unit_effects;

struct T : Host, Sink {
    std::vector<Event> events;
    uint32_t n{}, refreshes{}, world_queries{};

    bool visible(const oa::sim::unit_spawn::Slot&) override { return true; }

    void refresh_transform(oa::sim::unit_spawn::Slot&) override { ++refreshes; }

    Position piece_start(const oa::sim::unit_spawn::Slot&, uint32_t) override { return {1, 2, 3}; }

    Position piece_end(const oa::sim::unit_spawn::Slot&, uint32_t) override { return {4, 5, 6}; }

    Position piece_origin(const oa::sim::unit_spawn::Slot&, uint32_t) override { return {7, 8, 9}; }

    Position piece_world(const oa::sim::unit_spawn::Slot&, uint32_t) override {
        ++world_queries;
        return {100, 200, 300};
    }

    int32_t sea_level_fixed() override { return 12 << 16; }

    uint32_t random_bounded(uint32_t x) override { return n++ % x; }

    void set_piece_visible(oa::sim::unit_spawn::Slot&, uint32_t, bool) override {}

    void effect(const Event& e) override { events.push_back(e); }
};

static void req(bool b, const char* m) {
    if (!b)
        throw std::runtime_error(m);
}

struct L : OfflineLifecycle {};

int main() {
    oa::sim::unit_spawn::LegacyWorld world(3);
    auto u = world.views().units();
    auto s = world.views().slots();
    for (int i = 1; i < 3; ++i) {
        u[i].object_present = true;
        u[i].flags = OA_UNIT_FLAG_LIVE;
    }
    u[1].position = {10, 20, 30};
    T t;
    Runtime r(t, t);
    {
        L lifecycle;
        OfflineEffects offline(lifecycle, t);
        const auto before = t.events.size();
        offline.emit_sfx(s[1], 0, 0);
        req(t.events.size() == before && !offline.bound(),
            "two-phase adapter emits nothing before bind");
    }
    r.emit_sfx(s[1], 2, 4);
    req(t.events.size() == 1 && t.events[0].kind == EventKind::wake &&
            t.events[0].first == Position{14, 25, 24},
        "emit world transform and reversal");
    r.explode_piece(s[1], 3, 0x120);
    req(t.events.size() == 2 && t.events[1].kind == EventKind::explosion_sprite &&
            t.events[1].code == 0 && t.events[1].first == Position{100, 200, 300},
        "explode suppress debris and emits flagged world effect");
    r.explode_piece(s[1], 3, 0x20);
    req(t.world_queries == 1, "explode without sprite flags skips piece world query");
    std::cout << "unit effects tests passed\n";
}
