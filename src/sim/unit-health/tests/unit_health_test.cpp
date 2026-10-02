// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/unit_health.hpp"
#include <bit>
#include <cmath>
#include <iostream>
#include <limits>
#include <stdexcept>
#include <string>
#include <vector>
#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(#x);                                                          \
    } while (false)
using namespace oa::sim::unit_health;

struct Host final : RecoveryHost {
    EconomyDebit debit{};
    bool present{};
    uint8_t status{};
    RouteIdentity route{71}, fallback{99};
    std::vector<std::string> calls;
    std::vector<HealthEvent> applied, shared;

    EconomyDebit& energy_debit(Unit&) override {
        calls.push_back("debit");
        return debit;
    }

    bool target_is_live(const Unit&) override {
        calls.push_back("live");
        return true;
    }

    void apply_health_event(Unit&, const Unit*, const HealthEvent& e) override {
        calls.push_back("apply");
        applied.push_back(e);
    }

    bool target_owner_present(const Unit&) override {
        calls.push_back("present");
        return present;
    }

    uint8_t target_owner_status(const Unit&) override {
        calls.push_back("status");
        return status;
    }

    RouteIdentity source_owner_route(const Unit&) override {
        calls.push_back("source-route");
        return route;
    }

    RouteIdentity fallback_route() override {
        calls.push_back("fallback");
        return fallback;
    }

    void share_health_event(RouteIdentity r, const HealthEvent& e) override {
        calls.push_back("share:" + std::to_string(r));
        shared.push_back(e);
    }
};

int main() {
    EconomyDebit debit{2.5F, 4.0F, 1.0F};
    CHECK(!debit_resource(debit, 3.0F));
    CHECK(debit.requested == 5.5F && debit.accepted == 4.0F);
    debit.gate = 0.0F;
    CHECK(debit_resource(debit, -2.0F));
    CHECK(debit.requested == 3.5F && debit.accepted == 2.0F);
    debit.gate = std::numeric_limits<float>::quiet_NaN();
    CHECK(debit_resource(debit, 1.0F));
    float block[6] = {3.0F, 4.0F, 5.0F, 2.0F, 0.0F, 0.0F};
    scale_resource_block(block, 1.0F, 1.0F);
    CHECK(block[0] == 0.0F && block[1] == 0.0F && block[2] == 0.0F && block[3] == 0.0F);
    CHECK(block[4] == 3.0F && block[5] == 4.0F);
    float stalled[6] = {0.0F, 0.0F, 8.0F, 2.0F, 0.0F, 0.0F};
    scale_resource_block(stalled, 0.0F, 0.0F);
    CHECK(stalled[2] == 0.0F && stalled[3] == 10.0F);
    // A shortage settles with each product rounded before its subtraction.
    // Fusing the stock term into one multiply-subtract gives 0x1.bcbfe4p+9,
    // the supply term 0x1.bcbfdap+9 and both 0x1.bcbfe2p+9, so a build that
    // contracts floating-point expressions fails here.
    float shortage[6] = {0.0F, 0.0F, 0x1.35407ap+12F, 0x1.e483d8p+9F, 0.0F, 0.0F};
    scale_resource_block(shortage, 0x1.cb0abcp-2F, 0x1.db479ep-1F);
    CHECK(shortage[2] == 0.0F && shortage[3] == 0x1.bcbfdcp+9F);
    CHECK(scale_computer_credit(20.0F, 2, 0) == 10.0F);
    CHECK(scale_computer_credit(20.0F, 2, 1) == 14.0F);
    CHECK(scale_computer_credit(20.0F, 2, 2) == 20.0F);
    CHECK(scale_computer_credit(20.0F, 1, 0) == 20.0F);
    // Reclaim credit: reclaiming passes (1 - buildfraction) * metal cost
    // for build fractions 0, 0.25 and 1. Human owners and hard computers take
    // the full amount; easy/medium computers scale like credit_energy. The medium
    // scale is the double 0.7, so 75 credits 52.5 exactly.
    {
        const float metal_cost = 100.0F;
        const float amounts[3] = {metal_cost, metal_cost * 0.75F, 0.0F};
        const float mediums[3] = {70.0F, 52.5F, 0.0F};
        for (size_t i = 0; i < 3; ++i) {
            const auto amount = amounts[i];
            float human = 0.0F;
            CHECK(credit_metal(human, amount, true, 1, 0) == amount);
            CHECK(human == amount);
            float absent = 0.0F;
            CHECK(credit_metal(absent, amount, false, 2, 0) == amount);
            CHECK(absent == amount);
            float easy = 0.0F;
            CHECK(credit_metal(easy, amount, true, 2, 0) == amount * 0.5F);
            CHECK(easy == amount * 0.5F);
            float medium = 0.0F;
            CHECK(credit_metal(medium, amount, true, 2, 1) == mediums[i]);
            CHECK(medium == mediums[i]);
            float hard = 0.0F;
            CHECK(credit_metal(hard, amount, true, 2, 2) == amount);
            CHECK(hard == amount);
        }
        float accumulate = 10.0F;
        CHECK(credit_metal(accumulate, 75.0F, true, 1, 0) == 75.0F);
        CHECK(accumulate == 85.0F);
    }
    uint8_t reaction = 0;
    record_hit_reaction(reaction, 10, 0);
    CHECK((reaction & 0x40) != 0);
    reaction = 0;
    record_hit_reaction(reaction, 0, 10);
    CHECK((reaction & 0x20) != 0);
    CHECK(!within_unit_limit(0, 10, -1, 0));
    CHECK(!within_unit_limit(10, 10, -1, 0));
    CHECK(within_unit_limit(3, 10, -1, 99));
    CHECK(within_unit_limit(3, 10, 5, 4));
    CHECK(!within_unit_limit(3, 10, 5, 5));
    const int32_t side0[] = {0, 0, 0, 5, 0};
    const int32_t side1[] = {0, 0, 0, -1, 0};
    const int32_t side5[] = {0, 0, 0, 9, 0};
    const int32_t* tables[6] = {side0, side1, nullptr, nullptr, nullptr, side5};
    CHECK(!within_unit_limit_row(0, 0, 10, nullptr, 0));
    CHECK(!within_unit_limit_row(2, 3, -1, nullptr, 0));
    CHECK(!within_unit_limit_row(0, 10, 10, tables, 0));
    CHECK(within_unit_limit_row(0, 3, 10, tables, 4));
    CHECK(!within_unit_limit_row(0, 3, 10, tables, 5));
    CHECK(within_unit_limit_row(0x101u, 3, 10, tables, 99));
    CHECK(within_unit_limit_row(0x105u, 3, 10, tables, 8));
    CHECK(!within_unit_limit_row(0x105u, 3, 10, tables, 9));

    UnitType type{0x8000, 600.0F, 300, 1000};
    Unit source{7, 0, 0, 100, &type};
    Unit target{9, damage_scaling_flag, 10, 500, &type};
    auto event = make_health_event(&source, target, 100, 1, 0x123);
    CHECK(
        event.target == 9 && event.source == 7 && event.amount == 46 && event.direction == 1 &&
        event.kind == 1
    );
    event = make_health_event(&source, target, 70000, healing_damage_kind, 0);
    CHECK(event.amount == static_cast<int16_t>(70000U) && event.kind == healing_damage_kind);

    Host host;
    host.present = true;
    host.status = mirrored_owner_status;
    submit_damage(&source, target, 100, 1, host, 5);
    CHECK(
        host.calls ==
        std::vector<std::string>({"live", "apply", "present", "status", "source-route", "share:71"})
    );
    CHECK(host.applied.size() == 1 && host.shared.size() == 1 && host.applied[0].amount == 46);
    host.calls.clear();
    host.applied.clear();
    host.shared.clear();
    submit_damage(nullptr, target, 100, unshared_damage_kind, host);
    CHECK(host.calls == std::vector<std::string>({"live", "apply", "present", "status"}));
    CHECK(host.shared.empty());

    host = {};
    host.present = true;
    host.status = mirrored_owner_status;
    host.debit.gate = 0.0F;
    const auto repair = recover_health(source, target, 30.0F, host);
    CHECK(repair.performed && repair.health_amount == 1 && repair.energy_amount == 1);
    CHECK(host.debit.requested == 1.0F && host.debit.accepted == 1.0F);
    CHECK(
        host.calls ==
        std::vector<std::string>({"debit", "live", "present", "status", "source-route", "share:71"})
    );
    CHECK(host.applied.empty() && target.health == 501);
    host = {};
    host.debit.gate = 2.0F;
    const auto blocked = recover_health(source, target, 30.0F, host);
    CHECK(!blocked.performed && host.debit.requested == 1.0F && host.debit.accepted == 0.0F);
    CHECK(host.calls == std::vector<std::string>({"debit"}));
    target.health = 1000;
    host.calls.clear();
    const auto full = recover_health(source, target, 30.0F, host);
    CHECK(!full.performed && host.calls.empty());
    target.health = 500;
    host = {};
    for (const auto rate :
         {std::numeric_limits<float>::quiet_NaN(),
          std::numeric_limits<float>::infinity(),
          std::numeric_limits<float>::max()}) {
        const auto invalid = recover_health(source, target, rate, host);
        CHECK(invalid.performed && invalid.health_amount == 0 && invalid.energy_amount == 0);
        CHECK(target.health == 500 && host.debit.requested == 0 && host.debit.accepted == 0);
    }

    struct BuildHost final : ConstructionHost {
        EconomyDebit energy{}, metal{};
        float refund{};
        int completes{};

        EconomyDebit& energy_debit(Unit&) override { return energy; }

        EconomyDebit& metal_debit(Unit&) override { return metal; }

        void refund_metal(Unit&, float amount) override { refund += amount; }

        void complete_construction(Unit&, Unit&) override { ++completes; }

        bool target_is_live(const Unit&) override { return true; }

        std::vector<HealthEvent> damage;

        void apply_health_event(Unit&, const Unit*, const HealthEvent& event) override {
            damage.push_back(event);
        }

        bool target_owner_present(const Unit&) override { return false; }

        uint8_t target_owner_status(const Unit&) override { return 0; }

        RouteIdentity source_owner_route(const Unit&) override { return 0; }

        RouteIdentity fallback_route() override { return 0; }

        void share_health_event(RouteIdentity, const HealthEvent&) override {}
    };

    UnitType building_type{0x10000, 1000.0F, 100, 200, 500.0F};
    Unit builder{1, 0, 0, 100, &building_type};
    Unit nano{2, 0, 0, 0, &building_type, 1.0F};
    BuildHost build;
    const auto started = apply_build_progress(builder, nano, 10.0F, build);
    CHECK(started.performed && !started.completed);
    CHECK(nano.build_remaining == 0.9F);
    CHECK(nano.health > 0 && nano.health < 200);
    const auto built_health = nano.health;
    CHECK((nano.events & construction_event) != 0);
    CHECK((nano.flags & construction_dirty_flag) != 0);
    CHECK(build.energy.requested > 99.0F && build.energy.requested < 101.0F);
    CHECK(build.metal.requested > 49.0F && build.metal.requested < 51.0F);
    CHECK(build.energy.accepted == build.energy.requested);
    CHECK(build.metal.accepted == build.metal.requested);
    build.energy.gate = 1.0F;
    const auto blocked_build = apply_build_progress(builder, nano, 10.0F, build);
    CHECK(!blocked_build.performed && nano.build_remaining == 0.9F && nano.health == built_health);
    build.energy.gate = 0.0F;
    nano.build_remaining = 0.05F;
    nano.health = 190;
    const auto finished = apply_build_progress(builder, nano, 10.0F, build);
    CHECK(finished.performed && finished.completed && nano.build_remaining == 0.0F);
    CHECK(build.completes == 1);
    nano.build_remaining = 0.2F;
    nano.health = 40;
    nano.events = 0;
    const auto decayed = apply_build_progress(builder, nano, -10.0F, build);
    CHECK(!decayed.performed && nano.build_remaining == 0.3F && nano.health < 40);
    CHECK(build.refund > 49.0F && build.refund < 51.0F);
    CHECK((nano.events & construction_event) == 0);

    // GetBuilt passes ticks 0xb. build_time 16 * 11 / energy 88 = -2.
    UnitType decay_type{0x10000, 88.0F, 16, 200, 80.0F};
    Unit decaying{4, 0, 0, 80, &decay_type, 0.5F};
    // The frame is its own builder.
    const auto decay = [&](int32_t ticks) {
        return apply_build_progress(decaying, decaying, build_decay_rate(decay_type, ticks), build);
    };
    build = {};
    const auto decayed_nano = decay(0xb);
    CHECK(!decayed_nano.performed && !decayed_nano.completed);
    CHECK(decaying.build_remaining == 0.625F);
    CHECK(decaying.health == 55);
    CHECK(build.refund == 10.0F);
    CHECK(build.energy.requested == 0.0F && build.metal.requested == 0.0F);
    CHECK((decaying.events & construction_event) == 0);
    CHECK((decaying.flags & construction_dirty_flag) != 0);
    CHECK(build.damage.empty());

    // The product 0x40000000 * 4 wraps to 0, so the rate is -0 and construction only latches.
    decay_type.build_time = 0x40000000;
    decaying.build_remaining = 0.5F;
    decaying.health = 40;
    decaying.events = 0;
    decaying.flags = 0;
    build.refund = 0.0F;
    const auto wrapped = decay(4);
    CHECK(!wrapped.performed && decaying.build_remaining == 0.5F && decaying.health == 40);
    CHECK(build.refund == 0.0F);
    CHECK((decaying.events & construction_event) != 0);

    // Negative ticks flip the wrapped product, so the step builds instead of decaying.
    decay_type.build_time = 16;
    decaying.build_remaining = 0.5F;
    decaying.health = 80;
    decaying.events = 0;
    decaying.flags = 0;
    build = {};
    const auto built = decay(-11);
    CHECK(built.performed && !built.completed);
    CHECK(decaying.build_remaining == 0.375F && decaying.health == 105);
    CHECK(build.energy.accepted == 11.0F && build.metal.accepted == 10.0F);
    CHECK((decaying.events & construction_event) != 0);

    // Rate -8 reaches unfinished 1 and emits the kind-9 cancel.
    decay_type.energy_cost = 22.0F;
    decay_type.metal_cost = 40.0F;
    decay_type.maximum_health = 100;
    decaying.build_remaining = 0.5F;
    decaying.health = 60;
    decaying.events = 0;
    decaying.flags = 0;
    build = {};
    const auto cancelled = decay(11);
    CHECK(!cancelled.performed && !cancelled.completed);
    CHECK(decaying.build_remaining == 1.0F && decaying.health == 10);
    CHECK(build.refund == 20.0F);
    CHECK(
        build.damage.size() == 1 && build.damage[0].kind == construction_cancel_kind &&
        build.damage[0].amount == static_cast<int16_t>(construction_cancel_amount) &&
        build.damage[0].target == decaying.identity && build.damage[0].source == decaying.identity
    );

    // A zero energy cost divides to -inf and still runs the build step.
    decay_type.energy_cost = 0.0F;
    decay_type.metal_cost = 80.0F;
    decay_type.maximum_health = 200;
    decaying.build_remaining = 0.5F;
    decaying.health = 80;
    decaying.events = 0;
    decaying.flags = 0;
    build = {};
    const auto zero_cost = decay(11);
    CHECK(!zero_cost.performed && decaying.build_remaining == 1.0F && decaying.health == 0);
    CHECK(build.refund == 40.0F && build.damage.size() == 1);

    decaying.build_remaining = 0.0F;
    decaying.health = 10;
    build.refund = 0.0F;
    build.damage.clear();
    const auto idle = decay(11);
    CHECK(
        !idle.performed && !idle.completed && decaying.health == 10 && build.refund == 0.0F &&
        build.damage.empty()
    );

    Unit typeless{8};
    const auto untyped = apply_build_progress(typeless, typeless, -1.0F, build);
    CHECK(untyped.target_untyped && !untyped.performed && build.damage.empty());

    // The build step keeps 53 bits: 1 - 1/3 is stored as 0x3F2AAAAB
    // (float arithmetic would give 0x3F2AAAAA), and the third step of a
    // three-tick build leaves 0x322AAAAB unfinished instead of completing.
    UnitType thirds_type{0x10000, 300.0F, 3, 100, 150.0F};
    Unit thirds_builder{10, 0, 0, 100, &thirds_type};
    Unit thirds{11, 0, 0, 0, &thirds_type, 1.0F};
    build = {};
    auto step = apply_build_progress(thirds_builder, thirds, 1.0F, build);
    CHECK(step.performed && !step.completed);
    CHECK(std::bit_cast<uint32_t>(thirds.build_remaining) == 0x3F2AAAABu && thirds.health == 34);
    CHECK(build.energy.requested == 100.0F && build.metal.requested == 50.0F);
    step = apply_build_progress(thirds_builder, thirds, 1.0F, build);
    CHECK(std::bit_cast<uint32_t>(thirds.build_remaining) == 0x3EAAAAABu && thirds.health == 67);
    step = apply_build_progress(thirds_builder, thirds, 1.0F, build);
    CHECK(step.performed && !step.completed && build.completes == 0);
    CHECK(std::bit_cast<uint32_t>(thirds.build_remaining) == 0x322AAAABu && thirds.health == 100);
    step = apply_build_progress(thirds_builder, thirds, 1.0F, build);
    CHECK(step.completed && thirds.build_remaining == 0.0F && build.completes == 1);

    // A NaN fraction or rate counts as zero: nothing moves, and only a
    // non-negative rate latches the construction event.
    const float nan = std::numeric_limits<float>::quiet_NaN();
    thirds.build_remaining = nan;
    thirds.events = 0;
    build = {};
    step = apply_build_progress(thirds_builder, thirds, 1.0F, build);
    CHECK(!step.performed && thirds.events == 0 && build.energy.requested == 0.0F);
    thirds.build_remaining = 0.5F;
    step = apply_build_progress(thirds_builder, thirds, nan, build);
    CHECK(!step.performed && thirds.events == 0 && thirds.build_remaining == 0.5F);
    step = apply_build_progress(thirds_builder, thirds, 0.0F, build);
    CHECK(!step.performed && (thirds.events & construction_event) != 0);

    // A zero build time divides to +inf and the frame completes.
    thirds_type.build_time = 0;
    thirds.health = 50;
    step = apply_build_progress(thirds_builder, thirds, 1.0F, build);
    CHECK(step.performed && step.completed && thirds.build_remaining == 0.0F);
    CHECK(thirds.health == 100 && build.energy.requested == 150.0F);

    // Repair with a zero build time: both out-of-range conversions read as
    // zero, so the repairer pays nothing and the kind-10 event heals nothing.
    thirds_type.build_time = 0;
    thirds.health = 40;
    Host zero;
    const auto no_heal = recover_health(thirds_builder, thirds, 30.0F, zero);
    CHECK(no_heal.performed && no_heal.health_amount == 0 && no_heal.energy_amount == 0);
    CHECK(thirds.health == 40 && zero.debit.requested == 0.0F);

    std::cout << "unit health tests passed\n";
}
