// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A construction kbot sent on PATROL through synthetic SDL input, with the
// player's stores under a fifth of their capacity, reclaims a map feature
// near its route and patrols on.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <bit>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

// Rings of cells around the feature searched for the kbot's start, which
// stands this far from it on one side and patrols as far to the other.
constexpr int32_t kStartNearest = 6;
constexpr int32_t kStartFarthest = 10;
// Ticks the kbot gets to walk, reclaim a feature and turn back to its patrol.
constexpr int kPatrolTicks = 2400;
// A reclaiming patrol looks for features only while a store is under this share of
// its capacity.
constexpr float kLowStoreShare = 0.2F;

[[noreturn]] void fail(std::string_view what) {
    throw std::runtime_error("patrol reclaim check: " + std::string(what));
}

} // namespace

void Runtime::check_patrol_reclaim() {
    namespace orders = oa::sim::match_runtime;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    start_benchmark_skirmish();
    apply_output_mode();
    const auto kbot = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMCK");
    if (kbot == 0)
        fail("lacks ARMCK");
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
    const auto& spatial = match_->spatial();
    const auto width = static_cast<int32_t>(spatial.terrain_width);
    const auto height = static_cast<int32_t>(spatial.terrain_height);
    if (width == 0 || spatial.plots.empty())
        fail("needs the map plots");
    const auto feature_at = [&](std::size_t index) -> const oa::FeatureDef* {
        const auto word = spatial.plots[index].feature_word;
        if (word >= OA_PLOT_FEATURE_RESERVED)
            return nullptr;
        return oa::world_feature_def(&world, oa::oa_ref_from_index(word));
    };
    const auto auto_reclaimable = [](const oa::FeatureDef& def) {
        constexpr auto flags = OA_FEATURE_FLAG_RECLAIMABLE | OA_FEATURE_FLAG_AUTO_RECLAIMABLE;
        return (def.flags & flags) == flags && (def.metal != 0.0F || def.energy != 0.0F);
    };
    const auto commander_x = static_cast<int32_t>(slots[commander].unit->position[0] >> 20);
    const auto commander_z = static_cast<int32_t>(slots[commander].unit->position[2] >> 20);
    std::optional<std::pair<int32_t, int32_t>> start, feature;
    int64_t nearest = 0;
    for (std::size_t index = 0; index < spatial.plots.size(); ++index) {
        const auto* def = feature_at(index);
        if (def == nullptr || !auto_reclaimable(*def))
            continue;
        const auto x = static_cast<int32_t>(index % static_cast<std::size_t>(width));
        const auto z = static_cast<int32_t>(index / static_cast<std::size_t>(width));
        const int64_t dx = x - commander_x;
        const int64_t dz = z - commander_z;
        const auto distance = dx * dx + dz * dz;
        if (feature && distance >= nearest)
            continue;
        // The kbot needs open ground on one side and the map on the other.
        std::optional<std::pair<int32_t, int32_t>> site;
        for (int32_t ring = kStartNearest; ring <= kStartFarthest && !site; ++ring)
            for (int32_t sz = -ring; sz <= ring && !site; ++sz)
                for (int32_t sx = -ring; sx <= ring && !site; ++sx) {
                    if (sx != -ring && sx != ring && sz != -ring && sz != ring)
                        continue;
                    const auto far_x = x - sx;
                    const auto far_z = z - sz;
                    if (far_x < 2 || far_z < 2 || far_x >= width - 2 || far_z >= height - 2)
                        continue;
                    if (match_->building_site(kbot, x + sx, z + sz, 0))
                        site = std::pair{x + sx, z + sz};
                }
        if (!site)
            continue;
        nearest = distance;
        feature = std::pair{x, z};
        start = site;
    }
    if (!feature || !start)
        fail("found no auto-reclaimable feature with open ground beside it");

    const auto& kbot_type = spawn_types_[kbot];
    const auto start_x = static_cast<uint32_t>((start->first * 2 + kbot_type.footprint_x) * 8);
    const auto start_z = static_cast<uint32_t>((start->second * 2 + kbot_type.footprint_z) * 8);
    oa::sim::unit_spawn::Request request;
    request.player = match_local_player_;
    request.type = kbot;
    request.finished = true;
    request.state = kGroundOccupancyState;
    request.position = {
        start_x << 16,
        static_cast<uint32_t>(match_->map_height(start_x << 16, start_z << 16)) << 16,
        start_z << 16
    };
    auto* placed = match_->create(request);
    if (placed == nullptr || placed->unit == nullptr)
        fail("could not place ARMCK");
    const auto builder = placed->unit_index;

    auto& player = world.game.players[match_local_player_];
    // A full energy store would leave a tree (energy only) unreclaimed.
    player.metal = 0.0F;
    player.energy = 0.0F;
    const auto store_low = [&] {
        return player.metal < player.metal_storage * kLowStoreShare ||
               player.energy < player.energy_storage * kLowStoreShare;
    };

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
    const auto click_action = [&](std::string_view action) {
        if (!match_hud_)
            fail("has no order panel");
        for (const auto& gadget : match_hud_->layout.gadgets) {
            const auto& common = gadget.common;
            if (match_hud_action(common.name) != action)
                continue;
            const auto point = oa::ui::display_layout::source_to_canvas(
                match_layout_, common.x + common.width / 2, common.y + common.height / 2
            );
            click(static_cast<float>(point.x), static_cast<float>(point.y));
            return true;
        }
        return false;
    };
    const auto primary_queue = [&] {
        std::vector<orders::Match::QueuedCommandView> queue;
        match_->visit_primary_queue(builder, [&](const auto& view) { queue.push_back(view); });
        return queue;
    };

    const auto cell_centre = [](int32_t cell) {
        return static_cast<uint32_t>(cell * OA_MAP_CELL_PIXELS + OA_MAP_CELL_PIXELS / 2) << 16;
    };
    const std::array<uint32_t, 3> feature_world{
        cell_centre(feature->first), slots[builder].unit->position[1], cell_centre(feature->second)
    };
    match_camera_x_ = static_cast<int32_t>(feature_world[0] >> 16) - visible_map_width() / 2;
    match_camera_z_ = static_cast<int32_t>(feature_world[2] >> 16) - visible_map_height() / 2;
    const auto viewport = live_viewport(match_camera_x_, match_camera_z_);
    const auto on_screen = project_match_point(viewport, slots[builder].unit->position);
    click(static_cast<float>(on_screen.x), static_cast<float>(on_screen.y));
    if (selected_match_unit_ != builder ||
        (slots[builder].unit->flags & OA_UNIT_FLAG_SELECTED) == 0)
        fail("did not select the ARMCK with a click");
    if (!click_action("PATROL") && !(click_action("ORDERS") && click_action("PATROL")))
        fail("found no PATROL button for the ARMCK");
    if (match_command_ != MatchCommand::patrol)
        fail("did not arm PATROL");
    const std::array<uint32_t, 3> far_world{
        cell_centre(feature->first * 2 - start->first),
        slots[builder].unit->position[1],
        cell_centre(feature->second * 2 - start->second)
    };
    const auto far_screen = project_match_point(viewport, far_world);
    const auto destination =
        match_world_point(static_cast<float>(far_screen.x), static_cast<float>(far_screen.y));
    if (!destination)
        fail("found no ground at the far side of the feature");
    click(static_cast<float>(far_screen.x), static_cast<float>(far_screen.y));
    if (match_command_ != MatchCommand::none)
        fail("kept PATROL armed after the click");
    const auto issued = primary_queue();
    if (issued.size() != 1 || issued.front().kind != orders::repair_patrol_kind ||
        issued.front().destination != *destination)
        fail(
            "gave no RepairPatrol at the clicked point (" + std::to_string(issued.size()) +
            " orders, first kind " +
            std::to_string(issued.empty() ? 0 : static_cast<int>(issued.front().kind)) + ")"
        );
    std::printf(
        "patrol reclaim check: ARMCK %u at cell %d,%d sent on RepairPatrol past the feature at "
        "cell %d,%d to %d,%d; metal %.0f of %.0f, energy %.0f of %.0f\n",
        static_cast<unsigned>(builder),
        start->first,
        start->second,
        feature->first,
        feature->second,
        (*destination)[0] >> 20,
        (*destination)[2] >> 20,
        static_cast<double>(player.metal),
        static_cast<double>(player.metal_storage),
        static_cast<double>(player.energy),
        static_cast<double>(player.energy_storage)
    );
    std::fflush(stdout);
    if (!store_low())
        fail("could not bring a store under a fifth of its capacity");

    struct Reclaimed {
        std::size_t origin{};
        uint16_t word{};
        const oa::FeatureDef* def{};
        int tick{};
    };

    std::optional<Reclaimed> reclaimed;
    bool cleared = false, patrolling_on = false;
    bool credited_metal = false, credited_energy = false;
    double metal_before = player.metal_produced_total;
    double energy_before = player.energy_produced_total;
    for (int tick = 1; tick <= kPatrolTicks && !patrolling_on; ++tick) {
        step_match_simulation();
        const auto metal_gain = player.metal_produced_total - metal_before;
        const auto energy_gain = player.energy_produced_total - energy_before;
        metal_before = player.metal_produced_total;
        energy_before = player.energy_produced_total;
        const auto queue = primary_queue();
        if (!reclaimed && !queue.empty() && queue.front().kind == orders::reclaim_kind) {
            const auto& point = queue.front().destination;
            const auto x = point[0] >> 20;
            const auto z = point[2] >> 20;
            if (x < 0 || z < 0 || x >= width || z >= height)
                fail("sent the Reclaim off the map");
            auto index = static_cast<std::size_t>(z) * static_cast<std::size_t>(width) +
                         static_cast<std::size_t>(x);
            const auto& plot = spatial.plots[index];
            if (plot.feature_word == oa::sim::spatial_state::feature_continuation)
                index -= static_cast<std::size_t>(plot.feature_back_z) *
                             static_cast<std::size_t>(width) +
                         plot.feature_back_x;
            const auto* def = feature_at(index);
            if (def == nullptr || !auto_reclaimable(*def))
                fail("sent the Reclaim to no auto-reclaimable feature");
            bool behind = false;
            for (std::size_t i = 1; i < queue.size(); ++i)
                behind = behind || queue[i].kind == orders::repair_patrol_kind;
            if (!behind)
                fail("dropped its RepairPatrol for the Reclaim");
            reclaimed = Reclaimed{index, spatial.plots[index].feature_word, def, tick};
            std::printf(
                "patrol reclaim check: tick %d: Reclaim of '%.*s' (metal %.0f, energy %.0f) at "
                "cell %d,%d pushed ahead of the patrol\n",
                tick,
                static_cast<int>(::strnlen(def->name, sizeof def->name)),
                def->name,
                static_cast<double>(def->metal),
                static_cast<double>(def->energy),
                static_cast<int>(index % static_cast<std::size_t>(width)),
                static_cast<int>(index / static_cast<std::size_t>(width))
            );
            std::fflush(stdout);
        }
        if (!reclaimed)
            continue;
        // The unit's own income is a fraction of a unit per tick; the credit
        // lands in a single tick.
        credited_metal = credited_metal || (reclaimed->def->metal != 0.0F &&
                                            metal_gain >= reclaimed->def->metal * 0.5);
        credited_energy = credited_energy || (reclaimed->def->energy != 0.0F &&
                                              energy_gain >= reclaimed->def->energy * 0.5);
        cleared = cleared || spatial.plots[reclaimed->origin].feature_word != reclaimed->word;
        const bool credited = (reclaimed->def->metal == 0.0F || credited_metal) &&
                              (reclaimed->def->energy == 0.0F || credited_energy);
        patrolling_on = cleared && credited && !queue.empty() &&
                        queue.front().kind == orders::repair_patrol_kind;
        if (patrolling_on)
            std::printf(
                "patrol reclaim check: tick %d: feature cleared, metal +%s energy +%s credited, "
                "RepairPatrol leads the queue again\n",
                tick,
                credited_metal ? "yes" : "none",
                credited_energy ? "yes" : "none"
            );
    }
    if (!reclaimed)
        fail("never stopped to reclaim within " + std::to_string(kPatrolTicks) + " ticks");
    if (!patrolling_on)
        fail(
            std::string("reclaimed but ") + (cleared ? "the feature went" : "the feature stayed") +
            ", metal credit " + (credited_metal ? "seen" : "not seen") + ", energy credit " +
            (credited_energy ? "seen" : "not seen") + ", and did not patrol on"
        );
    std::fflush(stdout);
}

} // namespace oa::app
