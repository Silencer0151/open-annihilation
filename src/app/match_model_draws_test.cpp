// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The draw states the match renderer keeps between frames: a unit slot's
// state begins afresh for each new instance, even one built where the freed
// one stood, and a feature keeps its own state when the list erases an
// earlier one or grows.
#include "oa/app/match_model_draws.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <vector>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using oa::app::MatchFeatureDraw;
using oa::app::UnitDrawState;
using oa::present::model::ModelState;

constexpr int32_t unit_fixed = 0x10000;
constexpr int32_t image_side = 4;

// What drawing left in a state: its cached image's side and fill, and its
// draw count, which also sets the one piece snapshot's height.
struct Drawn {
    int32_t side{};
    uint8_t fill{};
    uint32_t draws{};
};

// A one-piece model with a vertical edge, which every unit and feature of
// the tests shares.
std::shared_ptr<const oa::formats::objects3d::Model> post_model() {
    auto model = std::make_shared<oa::formats::objects3d::Model>();
    oa::formats::objects3d::Object object;
    object.name = "base";
    object.vertices = {{0, 0, 0}, {0, 8 * unit_fixed, 0}};
    model->objects.push_back(object);
    return model;
}

// Leaves a state as drawing does: a cached image, a draw count and a piece snapshot.
void draw_into(ModelState& state, const Drawn& drawn) {
    oa::present::model::allocate_image(state.image, drawn.side, drawn.side);
    std::fill(state.image.pixels.begin(), state.image.pixels.end(), drawn.fill);
    state.cache.draws = drawn.draws;
    state.cache.has_image = true;
    state.transforms_dirty = false;
    state.pieces.push_back({{0, static_cast<int32_t>(drawn.draws) * unit_fixed, 0}, {}, 0});
}

// Tells whether a state is as a new one begins: nothing drawn yet.
bool fresh(const ModelState& state) {
    return state.image.sprite.data == nullptr && state.image.pixels.empty() &&
           state.cache.draws == 0 && !state.cache.has_image && state.transforms_dirty &&
           state.pieces.empty();
}

// Tells whether a state still holds what draw_into left, its image pointing
// into its own pixels.
bool holds(const ModelState& state, const Drawn& drawn) {
    return state.image.sprite.data == state.image.pixels.data() &&
           state.image.sprite.width == drawn.side && state.image.sprite.height == drawn.side &&
           std::all_of(
               state.image.pixels.begin(),
               state.image.pixels.end(),
               [&](uint8_t pixel) { return pixel == drawn.fill; }
           ) &&
           state.cache.draws == drawn.draws && state.pieces.size() == 1 &&
           state.pieces.front().translation.y == static_cast<int32_t>(drawn.draws) * unit_fixed;
}

// A slot's draw state is keyed by the slot's instance generation, never by an
// instance's address, which a unit made after a death can reuse: the slot's
// next generation begins afresh and another slot keeps its state. The test
// checks the helper; the renderer passes it SlotRuntime::instance_generation.
void test_unit_state_follows_instance_generation() {
    constexpr uint16_t slot = 3;
    constexpr uint16_t other_slot = 1;
    constexpr uint32_t dead_generation = 1;
    constexpr uint32_t next_generation = 2;
    constexpr Drawn dead{image_side, 7, 5};
    constexpr Drawn other{image_side, 9, 3};
    std::vector<UnitDrawState> units;

    draw_into(oa::app::unit_draw_state(units, other_slot, dead_generation), other);
    auto& dead_state = oa::app::unit_draw_state(units, slot, dead_generation);
    CHECK(units.size() == slot + 1u);
    CHECK(fresh(dead_state));
    draw_into(dead_state, dead);
    CHECK(holds(oa::app::unit_draw_state(units, slot, dead_generation), dead));

    CHECK(fresh(oa::app::unit_draw_state(units, slot, next_generation)));
    CHECK(holds(oa::app::unit_draw_state(units, other_slot, dead_generation), other));
}

// Three features of one model; the map replaces the first, which the list
// erases, and then wrecks grow the list past its capacity. Each remaining
// feature keeps its own cached image, draw count and snapshot, and each
// wreck begins with a fresh state.
void test_feature_keeps_its_own_state() {
    constexpr int32_t placed = 3;
    constexpr int32_t replaced_cell = 0;
    constexpr int32_t wreck_cell = placed;
    const auto model = post_model();
    // What each feature's draws left, by its cell_x.
    const auto drawn = [](int32_t cell_x) {
        return Drawn{
            image_side + cell_x,
            static_cast<uint8_t>(11 + cell_x),
            static_cast<uint32_t>(2 + cell_x)
        };
    };
    std::vector<MatchFeatureDraw> features;
    for (int32_t cell_x = 0; cell_x < placed; ++cell_x) {
        MatchFeatureDraw draw;
        draw.instance = oa::sim::model_runtime::make_instance(model);
        draw.cell_x = cell_x;
        draw_into(draw.state, drawn(cell_x));
        features.push_back(std::move(draw));
    }
    std::erase_if(features, [](const MatchFeatureDraw& draw) {
        return draw.cell_x == replaced_cell;
    });
    CHECK(features.size() == static_cast<std::size_t>(placed - 1));
    CHECK(features[0].cell_x == 1 && features[1].cell_x == 2);
    const auto placed_keep_their_own = [&] {
        for (const auto& draw : features)
            if (draw.cell_x != wreck_cell)
                CHECK(holds(draw.state, drawn(draw.cell_x)));
    };
    placed_keep_their_own();

    const auto* before = features.data();
    const std::size_t grown = features.capacity() + 1;
    while (features.size() < grown) {
        MatchFeatureDraw wreck;
        wreck.instance = oa::sim::model_runtime::make_instance(model);
        wreck.cell_x = wreck_cell;
        features.push_back(std::move(wreck));
    }
    CHECK(features.data() != before);
    for (const auto& draw : features)
        if (draw.cell_x == wreck_cell)
            CHECK(fresh(draw.state));
    placed_keep_their_own();
}

} // namespace

int main() {
    test_unit_state_follows_instance_generation();
    test_feature_keeps_its_own_state();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("match model draws passed");
    return EXIT_SUCCESS;
}
