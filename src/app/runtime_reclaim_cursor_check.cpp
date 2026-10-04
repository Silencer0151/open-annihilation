// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The cursor a selected commander shows over the map's vegetation, another
// reclaimable feature and a wreck, driven through synthetic SDL pointer motion.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include "oa/sim/gameplay_input/order_cursor.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {
namespace {

namespace input = oa::sim::gameplay_input;

// Kinds of vegetation (flammable reclaimable features) hovered, at most.
constexpr std::size_t kVegetationKinds = 4;
// Cells around the commander searched for features, and the wreck's site.
constexpr int32_t kSearchCells = 40;
constexpr int32_t kWreckNearest = 6;
// Screen pixels between pointer samples, and around a footprint's drawn box.
constexpr int32_t kSampleStep = 2;
constexpr int32_t kSampleMargin = 12;
constexpr uint16_t kNoFeature = 0xffff;
// The ground Reclaim order's kind (VTOL_Reclaim is 58).
constexpr uint8_t kReclaimKind = 32;
constexpr std::string_view kWreckName = "armsolar_dead";

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("reclaim cursor check: " + what);
}

std::string feature_name(const oa::FeatureDef& def) {
    return {def.name, ::strnlen(def.name, sizeof def.name)};
}

struct Hovered {
    int32_t cell_x{};
    int32_t cell_z{};
    const oa::FeatureDef* def{};
};

} // namespace

void Runtime::check_reclaim_cursor() {
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    start_benchmark_skirmish();
    apply_output_mode();
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            break;
        }
    if (commander == 0)
        fail("found no local commander");
    auto& world = match_->state();
    const auto commander_x = static_cast<int32_t>(slots[commander].unit->position[0] >> 20);
    const auto commander_z = static_cast<int32_t>(slots[commander].unit->position[2] >> 20);

    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!frame_to_window(sdl_.renderer, x, y, &window_x, &window_y))
            fail(SDL_GetError());
        SDL_Event event{};
        event.type = type;
        if (type == SDL_EVENT_MOUSE_MOTION) {
            event.motion.windowID = SDL_GetWindowID(sdl_.window);
            event.motion.x = window_x;
            event.motion.y = window_y;
        } else {
            event.button.windowID = SDL_GetWindowID(sdl_.window);
            event.button.button = button;
            event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
            event.button.clicks = 1;
            event.button.x = window_x;
            event.button.y = window_y;
        }
        dispatch_event(event, running);
    };
    const auto click = [&](float x, float y) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
    };
    const auto viewport = [&] {
        return live_viewport(
            static_cast<uint32_t>(std::max(0, match_camera_x_)),
            static_cast<uint32_t>(std::max(0, match_camera_z_))
        );
    };

    center_camera_on_unit(commander);
    render_match_surface();
    const auto at_commander = project_match_point(viewport(), slots[commander].unit->position);
    click(static_cast<float>(at_commander.x), static_cast<float>(at_commander.y));
    if (selected_match_unit_ != commander ||
        (slots[commander].unit->flags & OA_UNIT_FLAG_SELECTED) == 0)
        fail("did not select the commander with a click");

    const auto origin_word = [&](int32_t x, int32_t z) {
        const auto* plot = oa::world_plot(&world, x, z);
        return plot != nullptr ? plot->feature : kNoFeature;
    };
    const auto visible_cell = [&](int32_t x, int32_t z) {
        const auto height = match_->map_height(
            static_cast<uint32_t>(x * OA_MAP_CELL_PIXELS + 8) << 16U,
            static_cast<uint32_t>(z * OA_MAP_CELL_PIXELS + 8) << 16U
        );
        return match_->point_visible(
            match_local_player_,
            {static_cast<uint32_t>(x * OA_MAP_CELL_PIXELS + 8) << 16U,
             static_cast<uint32_t>(std::max(height, 0)) << 16U,
             static_cast<uint32_t>(z * OA_MAP_CELL_PIXELS + 8) << 16U}
        );
    };
    const auto footprint_visible = [&](int32_t x, int32_t z, const oa::FeatureDef& def) {
        for (int32_t row = 0; row < def.footprint_z; ++row)
            for (int32_t column = 0; column < def.footprint_x; ++column)
                if (!visible_cell(x + column, z + row))
                    return false;
        return true;
    };

    // The nearest visible feature of each kind, flammable vegetation first,
    // then one that does not burn.
    std::vector<Hovered> hovered;
    std::vector<uint16_t> kinds;
    const auto pick_kinds = [&](bool vegetation, std::size_t wanted) {
        std::size_t taken = 0;
        for (int32_t ring = 0; ring < kSearchCells && taken < wanted; ++ring)
            for (int32_t dz = -ring; dz <= ring && taken < wanted; ++dz)
                for (int32_t dx = -ring; dx <= ring && taken < wanted; ++dx) {
                    if (dx != -ring && dx != ring && dz != -ring && dz != ring)
                        continue;
                    const auto x = commander_x + dx;
                    const auto z = commander_z + dz;
                    const auto word = origin_word(x, z);
                    if (word >= world.feature_def_count ||
                        std::find(kinds.begin(), kinds.end(), word) != kinds.end())
                        continue;
                    const auto& def = world.feature_defs[word];
                    const bool flammable = (def.flags & OA_FEATURE_FLAG_FLAMABLE) != 0;
                    if ((def.flags & OA_FEATURE_FLAG_RECLAIMABLE) == 0 || flammable != vegetation ||
                        !footprint_visible(x, z, def))
                        continue;
                    kinds.push_back(word);
                    hovered.push_back({x, z, &def});
                    ++taken;
                }
        return taken;
    };
    if (pick_kinds(true, kVegetationKinds) < 2)
        fail("found fewer than two kinds of visible vegetation near the commander");
    (void)pick_kinds(false, 1);

    // A wreck placed on open ground the commander sees.
    const auto wreck_index =
        oa::sim::map_runtime::find_feature_index(feature_table_, kWreckName.data());
    if (wreck_index >= world.feature_def_count)
        fail("found no " + std::string(kWreckName) + " definition");
    const auto& wreck_def = world.feature_defs[wreck_index];
    bool wreck_placed = false;
    for (int32_t ring = kWreckNearest; ring < kSearchCells && !wreck_placed; ++ring)
        for (int32_t dz = -ring; dz <= ring && !wreck_placed; ++dz)
            for (int32_t dx = -ring; dx <= ring && !wreck_placed; ++dx) {
                const auto x = commander_x + dx;
                const auto z = commander_z + dz;
                bool open = footprint_visible(x, z, wreck_def);
                for (int32_t row = -1; open && row <= wreck_def.footprint_z; ++row)
                    for (int32_t column = -1; open && column <= wreck_def.footprint_x; ++column) {
                        const auto* plot = oa::world_plot(&world, x + column, z + row);
                        open = plot != nullptr && plot->feature == kNoFeature &&
                               plot->ground_unit == 0;
                    }
                if (open && console_place_feature(kWreckName.data(), x, z)) {
                    hovered.push_back({x, z, &wreck_def});
                    wreck_placed = true;
                }
            }
    if (!wreck_placed)
        fail("found no open ground for " + std::string(kWreckName));

    // A click with RECLAIM armed at `point` on the first tree's footprint.
    const auto check_reclaim_click = [&](const Hovered& tree, std::array<float, 2> point) {
        match_command_ = MatchCommand::reclaim;
        click(point[0], point[1]);
        std::vector<oa::sim::match_runtime::Match::QueuedCommandView> queue;
        match_->visit_primary_queue(commander, [&](const auto& order) { queue.push_back(order); });
        const auto& def = *tree.def;
        const auto column = queue.empty() ? -1 : (queue.front().destination[0] >> 20) - tree.cell_x;
        const auto row = queue.empty() ? -1 : (queue.front().destination[2] >> 20) - tree.cell_z;
        if (queue.empty() || queue.front().kind != kReclaimKind || column < 0 || row < 0 ||
            column >= def.footprint_x || row >= def.footprint_z)
            fail(
                "a RECLAIM click on " + feature_name(def) + " gave the commander no Reclaim there"
            );
    };

    std::size_t reclaim_samples = 0;
    std::size_t move_samples = 0;
    for (const auto& feature : hovered) {
        const auto& def = *feature.def;
        const auto centre_x =
            feature.cell_x * OA_MAP_CELL_PIXELS + def.footprint_x * OA_MAP_CELL_PIXELS / 2;
        const auto centre_z =
            feature.cell_z * OA_MAP_CELL_PIXELS + def.footprint_z * OA_MAP_CELL_PIXELS / 2;
        match_camera_x_ = centre_x - visible_map_width() / 2;
        match_camera_z_ = centre_z - visible_map_height() / 2;
        render_match_surface();
        const auto view = viewport();
        int32_t left = std::numeric_limits<int32_t>::max();
        int32_t top = left;
        int32_t right = std::numeric_limits<int32_t>::min();
        int32_t bottom = right;
        for (int32_t row = 0; row <= def.footprint_z; ++row)
            for (int32_t column = 0; column <= def.footprint_x; ++column) {
                const auto x =
                    static_cast<uint32_t>((feature.cell_x + column) * OA_MAP_CELL_PIXELS);
                const auto z = static_cast<uint32_t>((feature.cell_z + row) * OA_MAP_CELL_PIXELS);
                const auto height = std::max(
                    match_->map_height(x << 16U, z << 16U),
                    static_cast<int32_t>(world.game.sea_level)
                );
                const auto screen = project_match_point(
                    view, {x << 16U, static_cast<uint32_t>(height) << 16U, z << 16U}
                );
                left = std::min(left, screen.x);
                right = std::max(right, screen.x);
                top = std::min(top, screen.y);
                bottom = std::max(bottom, screen.y);
            }
        std::vector<bool> reached(static_cast<std::size_t>(def.footprint_x * def.footprint_z));
        std::size_t over = 0;
        std::optional<std::array<float, 2>> first_over;
        for (auto y = top - kSampleMargin; y <= bottom + kSampleMargin; y += kSampleStep)
            for (auto x = left - kSampleMargin; x <= right + kSampleMargin; x += kSampleStep) {
                const auto fx = static_cast<float>(x);
                const auto fy = static_cast<float>(y);
                if (x < match_layout_.left || y < match_layout_.top ||
                    x >= match_layout_.left + match_layout_.battlefield_width() ||
                    y >= match_layout_.top + match_layout_.battlefield_height() ||
                    radar_contains(fx, fy))
                    continue;
                send(SDL_EVENT_MOUSE_MOTION, 0, fx, fy);
                if (hovered_ || hovered_match_unit_ != 0)
                    continue;
                const auto ground = match_world_point(fx, fy);
                if (!ground || !match_->point_visible(
                                   match_local_player_,
                                   {static_cast<uint32_t>((*ground)[0]),
                                    static_cast<uint32_t>((*ground)[1]),
                                    static_cast<uint32_t>((*ground)[2])}
                               ))
                    continue;
                const auto cell_x = (*ground)[0] >> 20;
                const auto cell_z = (*ground)[2] >> 20;
                const auto column = cell_x - feature.cell_x;
                const auto row = cell_z - feature.cell_z;
                const bool on_footprint =
                    column >= 0 && row >= 0 && column < def.footprint_x && row < def.footprint_z;
                const auto* plot = oa::world_plot(&world, cell_x, cell_z);
                if (!on_footprint && (plot == nullptr || plot->feature != kNoFeature))
                    continue;
                const auto cursor = static_cast<input::OrderCursor>(pick_match_cursor());
                const auto expected =
                    on_footprint ? input::OrderCursor::reclaim : input::OrderCursor::move;
                if (cursor != expected)
                    fail(
                        "pointer " + std::to_string(x) + "," + std::to_string(y) + " on cell " +
                        std::to_string(cell_x) + "," + std::to_string(cell_z) + " (" +
                        (on_footprint
                             ? feature_name(def) + " at " + std::to_string(feature.cell_x) + "," +
                                   std::to_string(feature.cell_z)
                             : std::string("open ground")) +
                        ") shows cursor " + std::to_string(static_cast<int>(cursor)) + ", not " +
                        std::to_string(static_cast<int>(expected))
                    );
                if (!on_footprint) {
                    ++move_samples;
                    continue;
                }
                reached[static_cast<std::size_t>(row * def.footprint_x + column)] = true;
                ++over;
                if (!first_over)
                    first_over = std::array<float, 2>{fx, fy};
            }
        if (std::find(reached.begin(), reached.end(), false) != reached.end()) {
            std::string cells;
            for (const bool cell : reached)
                cells += cell ? '1' : '0';
            fail(
                "the pointer reached only cells " + cells + " of " + feature_name(def) +
                "'s footprint at " + std::to_string(feature.cell_x) + "," +
                std::to_string(feature.cell_z)
            );
        }
        reclaim_samples += over;
        std::cout << "reclaim cursor check: " << feature_name(def) << " (" << def.footprint_x << "x"
                  << def.footprint_z << ") at cell " << feature.cell_x << "," << feature.cell_z
                  << " shows Reclaim at " << over << " pointer positions\n";
        if (&feature == &hovered.front())
            check_reclaim_click(feature, *first_over);
    }

    std::cout << "reclaim cursor check: " << hovered.size() << " features, " << reclaim_samples
              << " pointer positions over their footprints show Reclaim, " << move_samples
              << " over open ground Move; a RECLAIM click on " << feature_name(*hovered.front().def)
              << " gave the commander Reclaim\n";
}

} // namespace oa::app
