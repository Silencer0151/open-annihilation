// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless reclaim check (--reclaim-check with --match-ticks): the local
// commander is sent to reclaim the nearest metal-bearing map feature through
// the simulation's Reclaim mission, and the run fails unless the feature
// leaves its plots and the player's metal production jumps by its value.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <optional>
#include <stdexcept>
#include <string>

namespace oa::app {

namespace {

constexpr uint16_t first_reserved_feature_word = OA_PLOT_FEATURE_RESERVED;
constexpr int32_t cell_pixels = 16;

} // namespace

void Runtime::begin_reclaim_check() {
    if (!match_)
        throw std::runtime_error("reclaim check needs an active match");
    const oa::sim::unit_spawn::Slot* commander = nullptr;
    for (const auto& slot : match_->world().slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = &slot;
            break;
        }
    if (commander == nullptr)
        throw std::runtime_error("reclaim check needs the local commander");
    auto& world = match_->state();
    const auto& spatial = match_->spatial();
    const auto width = static_cast<std::size_t>(spatial.terrain_width);
    if (width == 0 || spatial.plots.empty())
        throw std::runtime_error("reclaim check needs the map plots");
    const auto unit_x = static_cast<int32_t>(commander->unit->position[0] >> 16) / cell_pixels;
    const auto unit_z = static_cast<int32_t>(commander->unit->position[2] >> 16) / cell_pixels;
    std::optional<ReclaimCheck> best;
    int64_t best_distance = 0;
    const oa::FeatureDef* best_def = nullptr;
    for (std::size_t index = 0; index < spatial.plots.size(); ++index) {
        const auto word = spatial.plots[index].feature_word;
        if (word >= first_reserved_feature_word)
            continue;
        const auto* def = oa::world_feature_def(&world, oa::oa_ref_from_index(word));
        if (def == nullptr || !(def->flags & OA_FEATURE_FLAG_RECLAIMABLE) || def->metal <= 0.0F)
            continue;
        const auto cell_x = static_cast<int32_t>(index % width);
        const auto cell_z = static_cast<int32_t>(index / width);
        const int64_t dx = cell_x + def->footprint_x / 2 - unit_x;
        const int64_t dz = cell_z + def->footprint_z / 2 - unit_z;
        const auto distance = dx * dx + dz * dz;
        if (best && distance >= best_distance)
            continue;
        best_distance = distance;
        best_def = def;
        best = ReclaimCheck{commander->unit_index, cell_x, cell_z, index, word, def->metal};
    }
    if (!best)
        throw std::runtime_error("reclaim check found no reclaimable metal feature on the map");
    const auto& player = world.game.players[match_local_player_];
    best->produced_before = player.metal_produced_total;
    best->produced_last = player.metal_produced_total;
    best->store_before = player.metal;
    const oa::sim::ground_orders::Point destination{
        (best->cell_x * cell_pixels + best_def->footprint_x * cell_pixels / 2) << 16,
        0,
        (best->cell_z * cell_pixels + best_def->footprint_z * cell_pixels / 2) << 16
    };
    (void)match_->issue_feature_reclaim(best->builder, destination, false);
    reclaim_check_ = best;
    std::printf(
        "reclaim check: commander %u at cell %d,%d ordered to reclaim '%.*s' (feature %u, metal "
        "%.0f, "
        "energy %.0f) at cell %d,%d; %zu feature definitions bound\n",
        static_cast<unsigned>(best->builder),
        unit_x,
        unit_z,
        static_cast<int>(::strnlen(best_def->name, sizeof best_def->name)),
        best_def->name,
        static_cast<unsigned>(best->feature_word),
        static_cast<double>(best_def->metal),
        static_cast<double>(best_def->energy),
        best->cell_x,
        best->cell_z,
        static_cast<std::size_t>(world.feature_def_count)
    );
    std::fflush(stdout);
}

void Runtime::tick_reclaim_check() {
    if (!reclaim_check_ || !match_)
        return;
    auto& check = *reclaim_check_;
    const auto& player = match_->state().game.players[match_local_player_];
    const auto delta = player.metal_produced_total - check.produced_last;
    check.produced_last = player.metal_produced_total;
    // The commander's own metal income is a fraction of a unit per tick; the
    // reclaim credit lands in a single tick.
    if (!check.credited && delta >= static_cast<double>(check.feature_metal) * 0.5)
        check.credited = true;
}

void Runtime::finish_reclaim_check() {
    if (!reclaim_check_ || !match_)
        throw std::runtime_error("reclaim check did not start");
    const auto& check = *reclaim_check_;
    const auto& spatial = match_->spatial();
    const auto& player = match_->state().game.players[match_local_player_];
    const auto word_now = spatial.plots[check.plot].feature_word;
    const bool cleared = word_now != check.feature_word;
    std::printf(
        "reclaim check: feature word at cell %d,%d %u -> %u (%s); metal produced %.1f -> %.1f, "
        "store %.1f -> %.1f (cap %.1f, requested %.2f, wasted %.1f); credit of %.0f %s\n",
        check.cell_x,
        check.cell_z,
        static_cast<unsigned>(check.feature_word),
        static_cast<unsigned>(word_now),
        cleared ? "cleared" : "still placed",
        check.produced_before,
        player.metal_produced_total,
        static_cast<double>(check.store_before),
        static_cast<double>(player.metal),
        static_cast<double>(player.metal_storage),
        static_cast<double>(player.metal_requested),
        player.metal_wasted_total,
        static_cast<double>(check.feature_metal),
        check.credited ? "seen" : "not seen"
    );
    std::fflush(stdout);
    if (!cleared || !check.credited)
        throw std::runtime_error(
            std::string("reclaim check failed: feature ") + (cleared ? "cleared" : "still placed") +
            ", credit " + (check.credited ? "seen" : "not seen") +
            " (more --match-ticks may be needed)"
        );
}

} // namespace oa::app
