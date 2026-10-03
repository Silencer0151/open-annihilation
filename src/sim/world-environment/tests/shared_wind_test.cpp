// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The wind of a mod's deterministic-wind rule: the shared generator against
// the standard library's MT19937, the host-seed lookup, and the scheduler's
// draws, deadlines, strengths and directions.
#include "oa/sim/world_environment/wind.hpp"

#include <bit>
#include <cstdint>
#include <iostream>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <vector>

using namespace oa::sim::world_environment;

namespace {

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x))                                                                                  \
            throw std::runtime_error(std::string(#x) + " at line " + std::to_string(__LINE__));    \
    } while (false)

// A rand() and shared stream that fails the test when the scheduler draws.
struct NoRandom final : WindRandomHost {
    std::vector<uint32_t> shared;
    std::size_t position{};

    uint32_t lcg_rand_15() override { return 0; }

    uint32_t shared_random(uint32_t) override { return shared.at(position++); }
};

/// The generator's outputs equal the standard MT19937's for several seeds,
/// across many regenerations of its block, and the standard's 10000th output
/// for the default seed.
void generator_matches_the_standard() {
    for (const uint32_t seed : {5489u, 1u, 0u, 0x1234u, 0xffffffffu, 0x80000000u}) {
        WindGenerator generator{};
        seed_wind_generator(generator, seed);
        CHECK(generator.seeded == 1 && generator.next == wind_generator_words);
        std::mt19937 reference(seed);
        for (int i = 0; i < 5000; ++i)
            CHECK(draw_wind_generator(generator) == reference());
    }
    WindGenerator standard{};
    seed_wind_generator(standard, 5489u);
    CHECK(draw_wind_generator(standard) == 3499211612u);
    uint32_t last = 0;
    for (int i = 1; i < 10000; ++i)
        last = draw_wind_generator(standard);
    CHECK(last == 4123659995u);
    std::cout << "generator matches the standard passed\n";
}

/// The bookkeeping words a save restores are checked.
void generator_validity() {
    WindGenerator generator{};
    CHECK(wind_generator_valid(generator));
    seed_wind_generator(generator, 7);
    CHECK(wind_generator_valid(generator));
    generator.next = wind_generator_words + 1;
    CHECK(!wind_generator_valid(generator));
    generator.next = 0;
    generator.seeded = 2;
    CHECK(!wind_generator_valid(generator));
    std::cout << "generator validity passed\n";
}

/// Returns a wind state due for a change at tick 101.
///
/// @param minimum Game.wind_min
/// @param maximum Game.wind_max
/// @return the state
WindState due_state(int32_t minimum, int32_t maximum) {
    WindState state{};
    state.minimum_strength = minimum;
    state.maximum_strength = maximum;
    state.strength_divisor = default_wind_strength_divisor;
    state.change_deadline = 100;
    state.current_tick = 101;
    state.direction = 0x1111;
    return state;
}

/// A change draws its interval, strength and direction from the generator in
/// that order; the vector and normalized strength are those 3.1c's scheduler
/// gives the same strength and direction.
void change_draws_interval_strength_direction() {
    for (const uint32_t seed : {1u, 42u, 0xdeadbeefu}) {
        auto state = due_state(1000, 3000);
        WindGenerator generator{};
        CHECK(refresh_shared_wind(state, generator, seed) == WindRefresh::changed);
        std::mt19937 reference(seed);
        const auto interval = reference();
        const auto strength = reference();
        const auto direction = reference();
        CHECK(state.change_deadline == 100 + 150 + 30 * (interval % 10));
        CHECK(state.strength == static_cast<int32_t>(1000 + strength % 2000));
        CHECK(state.direction == static_cast<uint16_t>(direction));
        CHECK(state.changed == wind_sample_changed);
        CHECK(generator.seeded == 1 && generator.next == 3);
        // 3.1c's scheduler with the same strength and direction.
        auto classic = due_state(1000, 3000);
        NoRandom random;
        random.shared = {static_cast<uint32_t>(strength % 2000), static_cast<uint16_t>(direction)};
        CHECK(refresh_wind(classic, random) == WindRefresh::changed);
        CHECK(classic.strength == state.strength && classic.direction == state.direction);
        CHECK(classic.vector_x == state.vector_x && classic.vector_z == state.vector_z);
        CHECK(
            std::bit_cast<uint32_t>(classic.normalized_strength) ==
            std::bit_cast<uint32_t>(state.normalized_strength)
        );
        // The interval stays within 150..420 ticks, in steps of 30.
        const auto step = state.change_deadline - 100;
        CHECK(step >= 150 && step <= 420 && step % 30 == 0);
    }
    std::cout << "change draws interval, strength and direction passed\n";
}

/// The generator is seeded once: a later seed changes nothing, and the next
/// change continues the same sequence from the deadline it left.
void generator_is_seeded_once() {
    auto state = due_state(0, 5000);
    WindGenerator generator{};
    CHECK(refresh_shared_wind(state, generator, 99) == WindRefresh::changed);
    std::mt19937 reference(99);
    const auto first_interval = reference();
    (void)reference();
    if (state.strength != 0)
        (void)reference();
    const auto deadline = state.change_deadline;
    CHECK(deadline == 100 + 150 + 30 * (first_interval % 10));
    state.current_tick = deadline + 1;
    const auto drawn = generator.next;
    CHECK(refresh_shared_wind(state, generator, 12345) == WindRefresh::changed);
    const auto second_interval = reference();
    CHECK(state.change_deadline == deadline + 150 + 30 * (second_interval % 10));
    CHECK(generator.next > drawn);
    std::cout << "generator is seeded once passed\n";
}

/// Before the deadline nothing is drawn and the generator is not seeded; the
/// changed flag clears.
void waiting_draws_nothing() {
    auto state = due_state(1000, 3000);
    state.current_tick = 100;
    state.changed = wind_sample_changed;
    WindGenerator generator{};
    CHECK(refresh_shared_wind(state, generator, 5) == WindRefresh::waiting);
    CHECK(generator.seeded == 0 && generator.next == 0);
    CHECK(state.changed == wind_sample_unchanged && state.change_deadline == 100);
    // Game start waits at tick 0 the same way.
    WindState start{};
    start.minimum_strength = 1000;
    start.maximum_strength = 3000;
    CHECK(initialize_shared_wind(start, generator, 5) == WindRefresh::waiting);
    CHECK(start.strength_divisor == default_wind_strength_divisor);
    CHECK(start.change_deadline == initial_change_deadline && generator.seeded == 0);
    std::cout << "waiting draws nothing passed\n";
}

/// A maximum not above the minimum (as signed numbers) gives the minimum with
/// no strength draw; a calm strength keeps the direction with no draw.
void flat_and_calm_ranges() {
    {
        auto state = due_state(1500, 1500);
        WindGenerator generator{};
        CHECK(refresh_shared_wind(state, generator, 3) == WindRefresh::changed);
        std::mt19937 reference(3);
        (void)reference();
        CHECK(state.strength == 1500 && generator.next == 2);
        CHECK(state.direction == static_cast<uint16_t>(reference()));
    }
    {
        auto state = due_state(2000, -5);
        WindGenerator generator{};
        CHECK(refresh_shared_wind(state, generator, 3) == WindRefresh::changed);
        CHECK(state.strength == 2000 && generator.next == 2);
    }
    {
        auto state = due_state(0, 0);
        WindGenerator generator{};
        CHECK(refresh_shared_wind(state, generator, 3) == WindRefresh::changed);
        CHECK(state.strength == 0 && state.direction == 0x1111 && generator.next == 1);
        CHECK(state.vector_x == 0 && state.vector_z == 0 && state.normalized_strength == 0.0F);
    }
    {
        // A negative minimum: the draw modulo the range is unsigned and the
        // sum wraps.
        auto state = due_state(-100, 100);
        WindGenerator generator{};
        CHECK(refresh_shared_wind(state, generator, 8) == WindRefresh::changed);
        std::mt19937 reference(8);
        (void)reference();
        CHECK(state.strength == static_cast<int32_t>(reference() % 200u) - 100);
    }
    {
        // A zero strength divisor is reported, as 3.1c's scheduler does.
        auto state = due_state(1000, 3000);
        state.strength_divisor = 0;
        WindGenerator generator{};
        CHECK(refresh_shared_wind(state, generator, 8) == WindRefresh::zero_strength_divisor);
    }
    std::cout << "flat and calm ranges passed\n";
}

/// The host is the first slot in use on the host's machine with a host setup
/// state; its network id seeds the generator when it is above 0.
void host_seed_lookup() {
    auto world = std::make_unique<oa::World>();
    CHECK(shared_wind_seed(*world, 77) == 77);
    auto set_player = [&](size_t slot, uint32_t id, uint32_t group, uint8_t state) {
        auto& player = world->game.players[slot];
        player.in_use = 1;
        player.player_id = id;
        player.machine_group = group;
        player.info = static_cast<oa::oa_ref32>(slot + 1);
        world->player_info[slot].state = state;
    };
    // A computer player on the host's machine is not the host; another
    // machine's player is not either.
    set_player(0, 0x111, host_machine_group, 2);
    set_player(1, 0x222, 2, host_setup_state);
    CHECK(shared_wind_seed(*world, 77) == 77);
    set_player(2, 0x333, host_machine_group, host_setup_state);
    CHECK(shared_wind_seed(*world, 77) == 0x333);
    // Only the group's low byte counts.
    world->game.players[2].machine_group = 0x101;
    CHECK(shared_wind_seed(*world, 77) == 0x333);
    // A host id not above 0 as a signed number gives the fallback; later
    // slots are not searched.
    set_player(4, 0x444, host_machine_group, host_setup_state);
    world->game.players[2].player_id = 0x80000000u;
    CHECK(shared_wind_seed(*world, 77) == 77);
    world->game.players[2].player_id = 0;
    CHECK(shared_wind_seed(*world, 77) == 77);
    // A slot not in use, or without a setup record, is skipped.
    world->game.players[2].in_use = 0;
    CHECK(shared_wind_seed(*world, 77) == 0x444);
    world->game.players[4].info = 0;
    CHECK(shared_wind_seed(*world, 77) == 77);
    std::cout << "host seed lookup passed\n";
}

} // namespace

int main() {
    try {
        generator_matches_the_standard();
        generator_validity();
        change_draws_interval_strength_direction();
        generator_is_seeded_once();
        waiting_draws_nothing();
        flat_and_calm_ranges();
        host_seed_lookup();
    } catch (const std::exception& error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
    std::cout << "shared wind tests passed\n";
    return 0;
}
