// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/world_environment/wind.hpp"

#include <bit>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <vector>

using namespace oa::sim::world_environment;

namespace {
struct Random final : WindRandomHost {
    std::vector<uint32_t> lcg;
    std::vector<uint32_t> shared;
    std::size_t lcg_position{};
    std::size_t shared_position{};

    uint32_t lcg_rand_15() override { return lcg.at(lcg_position++); }

    uint32_t shared_random(uint32_t bound) override {
        const auto value = shared.at(shared_position++);
        if (value >= bound)
            throw std::runtime_error("invalid test random value");
        return value;
    }
};

void require(bool condition, const char* message) {
    if (!condition)
        throw std::runtime_error(message);
}
} // namespace

int main() {
    {
        oa::Game game{};
        game.wind_min = 1000;
        game.wind_max = 2000;
        game.wind_strength_divisor = 5000;
        game.wind_change_tick = 100;
        game.tick = 101;
        game.wind_vector.y = 42;
        Random random;
        random.lcg = {0};
        random.shared = {500, 0x4000};
        require(refresh_wind(game, random) == WindRefresh::changed, "Game refresh due");
        require(
            game.wind_change_tick == 250 && game.wind_strength == 1500 &&
                game.wind_direction == 0x4000 && game.wind_vector.x == -3000 &&
                game.wind_vector.y == 42 && game.wind_vector.z == 0 && game.wind_changed == 1 &&
                game.wind_factor == 0.3f,
            "Game wind fields updated in place"
        );
        game.tick = 250;
        require(
            refresh_wind(game, random) == WindRefresh::waiting && game.wind_changed == 0,
            "Game refresh waits for the deadline"
        );
    }
    {
        WindState state{};
        state.minimum_strength = 100;
        state.maximum_strength = 200;
        Random random;
        initialize_wind(state, random);
        require(
            state.strength_divisor == 5000 && state.change_deadline == 0 && state.changed == 0 &&
                random.lcg_position == 0,
            "initialize_wind zero-tick initialization"
        );
    }
    {
        WindState state{};
        state.change_deadline = 100;
        state.current_tick = 101;
        state.minimum_strength = 1000;
        state.maximum_strength = 2000;
        state.strength_divisor = 5000;
        Random random;
        random.lcg = {0};
        random.shared = {500, 0x4000};
        state.vector_y = 42;
        require(refresh_wind(state, random) == WindRefresh::changed, "due refresh");
        require(
            state.change_deadline == 250 && state.strength == 1500 && state.direction == 0x4000 &&
                state.vector_x == -3000 && state.vector_y == 42 && state.vector_z == 0 &&
                state.normalized_strength == 0.3f && state.changed == 1,
            "wind sample and transforms"
        );
        require(
            refresh_wind(state, random) == WindRefresh::waiting && state.changed == 0 &&
                random.lcg_position == 1 && random.shared_position == 2,
            "not-due refresh consumes no random values"
        );
    }
    {
        WindState state{};
        state.change_deadline = 0xfffffff0u;
        state.current_tick = 0xfffffff1u;
        state.minimum_strength = 0;
        state.maximum_strength = 1;
        state.strength_divisor = 5000;
        state.direction = 1234;
        Random random;
        random.lcg = {32767};
        random.shared = {0};
        refresh_wind(state, random);
        require(
            state.change_deadline == 0x00000194u && state.strength == 0 &&
                state.direction == 1234 && random.shared_position == 1,
            "deadline wraps and calm wind retains direction"
        );
    }
    {
        WindState state{};
        state.current_tick = 1;
        state.maximum_strength = 6001;
        state.strength_divisor = 5000;
        Random random;
        random.lcg = {0};
        random.shared = {6000, 0};
        refresh_wind(state, random);
        require(
            std::bit_cast<uint32_t>(state.normalized_strength) == 0x3f800000u,
            "normalized strength clamps to one"
        );
    }
    {
        // A zero divisor ends the run before the normalized strength and the
        // changed flag; a rand() value above 32767 ends it before anything.
        WindState state{};
        state.current_tick = 1;
        state.maximum_strength = 10;
        state.normalized_strength = 0.5f;
        Random random;
        random.lcg = {0};
        random.shared = {5, 0};
        require(
            refresh_wind(state, random) == WindRefresh::zero_strength_divisor &&
                state.strength == 5 && state.normalized_strength == 0.5f && state.changed == 0,
            "zero strength divisor"
        );
        WindState early{};
        early.current_tick = 1;
        early.strength_divisor = 5000;
        Random high;
        high.lcg = {0x8000};
        require(
            refresh_wind(early, high) == WindRefresh::random_out_of_range &&
                early.change_deadline == 0 && high.shared_position == 0,
            "rand() value above 32767"
        );
    }
    {
        struct Calls {
            int count{};
            int32_t code{};
        } calls;

        const SeaOccupyHost host{&calls, [](void* context, oa::Unit&, int32_t code) {
                                     auto* seen = static_cast<Calls*>(context);
                                     ++seen->count;
                                     seen->code = code;
                                 }};
        const auto set_height = [](oa::Unit& unit, int16_t height) {
            unit.position.y = static_cast<oa::oa_fixed>(
                static_cast<uint32_t>(static_cast<uint16_t>(height)) << 16
            );
        };
        oa::Unit unit{};
        oa::UnitDef def{};
        unit.flags = sea_occupy_ground_layer;
        const int32_t seven = 7;
        std::memcpy(unit.last_occupy_code, &seven, sizeof(seven));
        set_height(unit, 10);
        def.model_height = 20 << 16;
        update_sea_occupy(unit, def, 20, host);
        require(
            calls.count == 0 && sea_occupy(unit) == 7, "deep unit outside every sea band stays"
        );
        def.model_height = 0;
        update_sea_occupy(unit, def, 20, host);
        require(
            calls.count == 1 && calls.code == sea_occupy_submerged && sea_occupy(unit) == 3,
            "model top below sea is submerged"
        );
        set_height(unit, 20);
        def.water_line = 0;
        update_sea_occupy(unit, def, 20, host);
        require(calls.count == 2 && sea_occupy(unit) == sea_occupy_waterline, "waterline band");
        set_height(unit, 16);
        def.water_line = 1;
        def.model_height = 4 << 16;
        update_sea_occupy(unit, def, 20, host);
        require(
            calls.count == 3 && sea_occupy(unit) == sea_occupy_surface, "surface band above -5"
        );
        set_height(unit, 21);
        update_sea_occupy(unit, def, 20, host);
        require(calls.count == 4 && sea_occupy(unit) == sea_occupy_above, "above sea");
        unit.flags = 0;
        update_sea_occupy(unit, def, 20, host);
        require(
            calls.count == 5 && sea_occupy(unit) == sea_occupy_none, "other movement layers clear"
        );
        unit.flags = sea_occupy_air_layer;
        update_sea_occupy(unit, def, 20, host);
        require(
            calls.count == 6 && sea_occupy(unit) == sea_occupy_above, "air layer uses the sea bands"
        );
        update_sea_occupy(unit, def, 20, host);
        require(calls.count == 6, "unchanged occupy does not call the script");
    }
    {
        // The water state rules' reordered checks, beside 3.1c's, at sea level 20.
        struct Case {
            int16_t height;
            int8_t water_line;
            int16_t model_top;
            int32_t base;
            int32_t reordered;
            const char* what;
        };

        const Case cases[] = {
            {10, 0, 20, 7, sea_occupy_waterline, "deep unit with its waterline below the sea"},
            {17, 3, 4, sea_occupy_waterline, sea_occupy_surface, "shallow floater at the surface"},
            {18, 0, 4, sea_occupy_surface, sea_occupy_surface, "waterline below within 5"},
            {10, 0, 0, sea_occupy_submerged, sea_occupy_submerged, "model top below the sea"},
            {12, 30, 20, 7, 7, "waterline above the sea, deeper than 5"},
            {21, 0, 4, sea_occupy_above, sea_occupy_above, "above the sea"},
        };
        for (const auto& c : cases)
            for (const bool reordered : {false, true}) {
                SeaOccupyHost host{};
                host.reordered = reordered;
                oa::Unit unit{};
                oa::UnitDef def{};
                unit.flags = sea_occupy_ground_layer;
                const int32_t seven = 7;
                std::memcpy(unit.last_occupy_code, &seven, sizeof(seven));
                unit.position.y = static_cast<oa::oa_fixed>(
                    static_cast<uint32_t>(static_cast<uint16_t>(c.height)) << 16
                );
                def.water_line = c.water_line;
                def.model_height = static_cast<oa::oa_fixed>(
                    static_cast<uint32_t>(static_cast<uint16_t>(c.model_top)) << 16
                );
                update_sea_occupy(unit, def, 20, host);
                require(sea_occupy(unit) == (reordered ? c.reordered : c.base), c.what);
            }
    }
    std::cout << "world environment wind tests passed\n";
}
