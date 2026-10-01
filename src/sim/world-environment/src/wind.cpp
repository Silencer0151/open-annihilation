// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/world_environment/wind.hpp"

#include "oa/sim/unit_movement/movement.hpp"

#include <bit>
#include <stdexcept>

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

float normalized_strength(int32_t numerator, int32_t denominator) {
    if (denominator == 0) {
        throw std::domain_error("Wind-strength divisor is zero");
    }
    // The quotient is rounded to a double, then to a float, as in 3.1c.
    const double quotient = static_cast<double>(numerator) / static_cast<double>(denominator);
    return static_cast<float>(quotient);
}

} // namespace

bool refresh_wind(WindState& state, WindRandomHost& random) {
    // The comparison is unsigned. Equality therefore waits too.
    if (state.change_deadline >= state.current_tick) {
        state.changed &= wind_sample_unchanged;
        return false;
    }

    const auto lcg = random.lcg_rand_15();
    if (lcg >= lcg_divisor) {
        throw std::invalid_argument("rand() callback returned a value above 32767");
    }
    // lcg is below lcg_divisor, so the product stays within 32 bits.
    const auto steps = (lcg * cadence_multiplier) / lcg_divisor + cadence_bias;
    state.change_deadline += steps * cadence_tick_scale;

    const auto range = std::bit_cast<uint32_t>(state.maximum_strength) -
                       std::bit_cast<uint32_t>(state.minimum_strength);
    state.strength = wrap_add(state.minimum_strength, random.shared_random(range));
    if (state.strength != 0) {
        state.direction = static_cast<uint16_t>(random.shared_random(direction_span));
    }

    state.vector_x =
        doubled_negation(sim::unit_movement::sine_scaled(state.direction, state.strength));
    state.vector_z =
        doubled_negation(sim::unit_movement::cosine_scaled(state.direction, state.strength));
    state.normalized_strength = normalized_strength(state.strength, state.strength_divisor);
    if (static_cast<double>(state.normalized_strength) > normalized_strength_limit) {
        state.normalized_strength = static_cast<float>(normalized_strength_limit);
    }
    state.changed = wind_sample_changed;
    return true;
}

void initialize_wind(WindState& state, WindRandomHost& random) {
    state.strength_divisor = default_wind_strength_divisor;
    state.change_deadline = initial_change_deadline;
    refresh_wind(state, random);
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

bool refresh_wind(Game& game, WindRandomHost& random) {
    auto state = wind_state(game);
    const bool changed = refresh_wind(state, random);
    store_wind_state(game, state);
    return changed;
}

void initialize_wind(Game& game, WindRandomHost& random) {
    auto state = wind_state(game);
    initialize_wind(state, random);
    store_wind_state(game, state);
}

} // namespace oa::sim::world_environment
