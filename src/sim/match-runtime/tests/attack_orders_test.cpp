// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/match_runtime/attack_orders.hpp"
#include <cassert>
#include <cstdint>
#include <vector>
using namespace oa;

struct Host : sim::match_runtime::AttackOrderHost {
    std::vector<int> calls;
    bool reachable = true;
    sim::match_runtime::AttackPoint goal{};
    int32_t outer{};

    void announce() override { calls.push_back(1); }

    uint8_t selected_weapon() override {
        calls.push_back(2);
        return 2;
    }

    void enable_weapon(uint32_t i) override { calls.push_back(10 + static_cast<int>(i)); }

    void assign_target(sim::simulation_state::Unit&, int32_t i) override {
        calls.push_back(20 + i);
    }

    void reset_weapons() override { calls.push_back(3); }

    bool can_reach(sim::simulation_state::Unit&, uint8_t i) override {
        calls.push_back(30 + i);
        return reachable;
    }

    int32_t range(uint8_t i) override {
        calls.push_back(40 + i);
        return 100;
    }

    void clear_goal() override { calls.push_back(4); }

    void circle_goal(const sim::match_runtime::AttackPoint& p, int32_t r) override {
        calls.push_back(5);
        goal = p;
        outer = r;
    }

    uint32_t random(uint32_t n) override {
        calls.push_back(7);
        return n ? n / 2 : 0;
    }

    uint8_t morph_attack_command(sim::simulation_state::Unit* target) override {
        calls.push_back(8);
        return target ? 6 : 46;
    }

    void assign_ground(const sim::match_runtime::AttackPoint&, int32_t slot) override {
        calls.push_back(50 + slot);
    }
};

int main() {
    sim::unit_spawn::LegacyWorld world(3);
    auto& type = world.types()[1].simulation;
    auto& unit = world.unit(1);
    auto& target = world.unit(2);
    unit.type = &type;
    unit.object_present = true;
    unit.flags = 0x80000000u;
    target.position = {100u << 16, 20u << 16, 100u << 16};
    unit.position = {110u << 16, 20u << 16, 100u << 16};
    sim::simulation_state::Order order;
    sim::match_runtime::AttackOrderState state;
    state.target = &target;
    Host host;
    type.flags = 0x800;
    assert(sim::match_runtime::air_to_ground(unit, order, state, 0, 0, host) == 1);
    assert(order.phase == 0);
    order.phase = 1;
    host.calls.clear();
    assert(sim::match_runtime::air_to_ground(unit, order, state, 0, 10, host) == 1);
    assert(order.wait_events == 0xe1 && order.wake_tick == 40);
    type.flags = 0;
    order.phase = 0;
    assert(sim::match_runtime::air_to_ground(unit, order, state, 0, 0, host) == 7);

    uint32_t waits = 0;
    uint32_t wake = 0;
    assert(sim::match_runtime::wait_order(0, 12, waits, wake, 100) == 1);
    assert(waits == 1 && wake == 112);
    assert(sim::match_runtime::wait_order(1, 12, waits, wake, 100) == 5);
    assert(sim::match_runtime::wait_order(4, 12, waits, wake, 100) == 7);
}
