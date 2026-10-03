// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/world_environment/wind.hpp"

#include "oa/sim/unit_movement/movement.hpp"

#include <bit>

namespace oa::sim::world_environment {
namespace {

int32_t bits(uint32_t value) noexcept {
    return std::bit_cast<int32_t>(value);
}

int32_t wrap_add(int32_t left, uint32_t right) noexcept {
    return bits(std::bit_cast<uint32_t>(left) + right);
}

int32_t doubled_negation(int32_t value) noexcept {
    return bits((0u - std::bit_cast<uint32_t>(value)) << negated_vector_shift);
}

// The divisor is not zero.
float normalized_strength(int32_t numerator, int32_t denominator) noexcept {
    // The quotient is rounded to a double, then to a float, as in 3.1c.
    const double quotient = static_cast<double>(numerator) / static_cast<double>(denominator);
    return static_cast<float>(quotient);
}

// MT19937's constants: the block's middle offset, the twist matrix, the
// tempering masks and shifts, and the seeding multiplier.
constexpr size_t generator_middle = 397;
constexpr uint32_t generator_matrix = 0x9908b0dfu;
constexpr uint32_t generator_upper_mask = 0x80000000u;
constexpr uint32_t generator_lower_mask = 0x7fffffffu;
constexpr uint32_t generator_seed_multiplier = 1812433253u;
constexpr unsigned generator_seed_shift = 30;
constexpr unsigned temper_shift_u = 11, temper_shift_s = 7, temper_shift_t = 15,
                   temper_shift_l = 18;
constexpr uint32_t temper_mask_b = 0x9d2c5680u, temper_mask_c = 0xefc60000u;

// Regenerates the generator's block of words.
void twist(WindGenerator& generator) noexcept {
    auto& words = generator.words;
    for (size_t i = 0; i < wind_generator_words; ++i) {
        const auto joined = (words[i] & generator_upper_mask) |
                            (words[(i + 1) % wind_generator_words] & generator_lower_mask);
        auto next = words[(i + generator_middle) % wind_generator_words] ^ (joined >> 1);
        if ((joined & 1u) != 0)
            next ^= generator_matrix;
        words[i] = next;
    }
    generator.next = 0;
}

// Computes the vector and the normalized strength of a new sample from its
// strength and direction, and marks the sample changed.
WindRefresh finish_wind_sample(WindState& state) noexcept {
    state.vector_x =
        doubled_negation(sim::unit_movement::sine_scaled(state.direction, state.strength));
    state.vector_z =
        doubled_negation(sim::unit_movement::cosine_scaled(state.direction, state.strength));
    if (state.strength_divisor == 0)
        return WindRefresh::zero_strength_divisor;
    state.normalized_strength = normalized_strength(state.strength, state.strength_divisor);
    if (static_cast<double>(state.normalized_strength) > normalized_strength_limit) {
        state.normalized_strength = static_cast<float>(normalized_strength_limit);
    }
    state.changed = wind_sample_changed;
    return WindRefresh::changed;
}

} // namespace

void seed_wind_generator(WindGenerator& generator, uint32_t seed) noexcept {
    auto& words = generator.words;
    words[0] = seed;
    for (size_t i = 1; i < wind_generator_words; ++i) {
        const auto previous = words[i - 1];
        words[i] = generator_seed_multiplier * (previous ^ (previous >> generator_seed_shift)) +
                   static_cast<uint32_t>(i);
    }
    generator.next = wind_generator_words;
    generator.seeded = 1;
}

uint32_t draw_wind_generator(WindGenerator& generator) noexcept {
    if (generator.next >= wind_generator_words)
        twist(generator);
    auto value = generator.words[generator.next++];
    value ^= value >> temper_shift_u;
    value ^= (value << temper_shift_s) & temper_mask_b;
    value ^= (value << temper_shift_t) & temper_mask_c;
    value ^= value >> temper_shift_l;
    return value;
}

bool wind_generator_valid(const WindGenerator& generator) noexcept {
    return generator.seeded <= 1 && generator.next <= wind_generator_words;
}

uint32_t shared_wind_seed(const World& world, uint32_t fallback) noexcept {
    for (size_t slot = 0; slot < OA_PLAYER_COUNT; ++slot) {
        const auto& player = world.game.players[slot];
        if (player.in_use == 0 ||
            static_cast<int8_t>(static_cast<uint8_t>(player.machine_group)) != host_machine_group)
            continue;
        const auto* info = world_player_info(&world, &player);
        if (info == nullptr || info->state != host_setup_state)
            continue;
        return std::bit_cast<int32_t>(player.player_id) > 0 ? player.player_id : fallback;
    }
    return fallback;
}

WindRefresh
refresh_shared_wind(WindState& state, WindGenerator& generator, uint32_t seed) noexcept {
    // The deadline test is refresh_wind's.
    if (state.change_deadline >= state.current_tick) {
        state.changed &= wind_sample_unchanged;
        return WindRefresh::waiting;
    }
    if (generator.seeded == 0)
        seed_wind_generator(generator, seed);
    state.change_deadline +=
        shared_cadence_bias_ticks +
        cadence_tick_scale * (draw_wind_generator(generator) % shared_cadence_steps);
    if (state.maximum_strength > state.minimum_strength) {
        const auto range = std::bit_cast<uint32_t>(state.maximum_strength) -
                           std::bit_cast<uint32_t>(state.minimum_strength);
        state.strength = wrap_add(state.minimum_strength, draw_wind_generator(generator) % range);
    } else {
        state.strength = state.minimum_strength;
    }
    if (state.strength != 0)
        state.direction = static_cast<uint16_t>(draw_wind_generator(generator));
    return finish_wind_sample(state);
}

WindRefresh
initialize_shared_wind(WindState& state, WindGenerator& generator, uint32_t seed) noexcept {
    state.strength_divisor = default_wind_strength_divisor;
    state.change_deadline = initial_change_deadline;
    return refresh_shared_wind(state, generator, seed);
}

WindRefresh refresh_wind(WindState& state, WindRandomHost& random) {
    // The comparison is unsigned. Equality therefore waits too.
    if (state.change_deadline >= state.current_tick) {
        state.changed &= wind_sample_unchanged;
        return WindRefresh::waiting;
    }

    const auto lcg = random.lcg_rand_15();
    if (lcg >= lcg_divisor)
        return WindRefresh::random_out_of_range;
    // lcg is below lcg_divisor, so the product stays within 32 bits.
    const auto steps = (lcg * cadence_multiplier) / lcg_divisor + cadence_bias;
    state.change_deadline += steps * cadence_tick_scale;

    const auto range = std::bit_cast<uint32_t>(state.maximum_strength) -
                       std::bit_cast<uint32_t>(state.minimum_strength);
    state.strength = wrap_add(state.minimum_strength, random.shared_random(range));
    if (state.strength != 0) {
        state.direction = static_cast<uint16_t>(random.shared_random(direction_span));
    }

    return finish_wind_sample(state);
}

WindRefresh initialize_wind(WindState& state, WindRandomHost& random) {
    state.strength_divisor = default_wind_strength_divisor;
    state.change_deadline = initial_change_deadline;
    return refresh_wind(state, random);
}

WindState wind_state(const Game& game) noexcept {
    WindState state{};
    state.vector_x = game.wind_vector.x;
    state.vector_y = game.wind_vector.y;
    state.vector_z = game.wind_vector.z;
    state.change_deadline = game.wind_change_tick;
    state.strength_divisor = game.wind_strength_divisor;
    state.direction = game.wind_direction;
    state.strength = game.wind_strength;
    state.normalized_strength = game.wind_factor;
    state.changed = game.wind_changed;
    state.minimum_strength = game.wind_min;
    state.maximum_strength = game.wind_max;
    state.current_tick = game.tick;
    return state;
}

void store_wind_state(Game& game, const WindState& state) noexcept {
    game.wind_vector.x = state.vector_x;
    game.wind_vector.y = state.vector_y;
    game.wind_vector.z = state.vector_z;
    game.wind_change_tick = state.change_deadline;
    game.wind_strength_divisor = state.strength_divisor;
    game.wind_direction = state.direction;
    game.wind_strength = state.strength;
    game.wind_factor = state.normalized_strength;
    game.wind_changed = state.changed;
}

namespace {
bool wind_error(WindRefresh result) noexcept {
    return result != WindRefresh::waiting && result != WindRefresh::changed;
}
} // namespace

WindRefresh refresh_wind(Game& game, WindRandomHost& random) {
    auto state = wind_state(game);
    const auto result = refresh_wind(state, random);
    if (!wind_error(result))
        store_wind_state(game, state);
    return result;
}

WindRefresh initialize_wind(Game& game, WindRandomHost& random) {
    auto state = wind_state(game);
    const auto result = initialize_wind(state, random);
    if (!wind_error(result))
        store_wind_state(game, state);
    return result;
}

} // namespace oa::sim::world_environment
