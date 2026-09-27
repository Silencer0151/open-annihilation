// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless check of the build ghost and the placing click over the sites the
// game's building-site test refuses, and over bare ground and a metal deposit,
// which it accepts alike.
#include "oa/app/runtime.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>

namespace oa::app {

namespace {

// The one rule of the site test a site breaks for a yard of level, unit, claim
// and feature cells ('o'), judged from the plots under it; `several` when it
// breaks more than one or has a blocking feature.
enum class SiteFault : uint8_t { none, edge, water, slope, occupied, several };

const char* fault_name(SiteFault fault) {
    switch (fault) {
    case SiteFault::edge:
        return "map edge";
    case SiteFault::water:
        return "water";
    case SiteFault::slope:
        return "slope";
    case SiteFault::occupied:
        return "occupied";
    case SiteFault::several:
        return "several faults";
    case SiteFault::none:
        break;
    }
    return "clear";
}

SiteFault site_fault(
    const oa::sim::spatial_state::World& spatial,
    const oa::data::unit_definitions::RuntimeDefinitionMetadata& limits,
    int32_t x,
    int32_t z,
    int32_t footprint_x,
    int32_t footprint_z
) {
    const auto width = static_cast<int32_t>(spatial.terrain_width);
    const auto height = static_cast<int32_t>(spatial.terrain_height);
    if (x < 0 || z < 0 || x + footprint_x > width || z + footprint_z > height)
        return SiteFault::several;
    int32_t low = 255, high = 0;
    bool occupied = false;
    for (int32_t row = 0; row < footprint_z; ++row)
        for (int32_t column = 0; column < footprint_x; ++column) {
            const auto& plot = spatial.plots
                                   [static_cast<std::size_t>(z + row) * spatial.terrain_width +
                                    static_cast<std::size_t>(x + column)];
            if (plot.blocking_feature)
                return SiteFault::several;
            low = std::min<int32_t>(low, plot.low_height);
            high = std::max<int32_t>(high, plot.high_height);
            occupied = occupied || plot.ground != oa::sim::spatial_state::no_unit ||
                       (plot.flags & oa::sim::spatial_state::plot_claimed) != 0;
        }
    const auto sea = static_cast<int32_t>(spatial.sea_level);
    const std::array<std::pair<SiteFault, bool>, 4> faults{{
        {SiteFault::edge, x < 1 || z < 1 || width <= x + footprint_x || height <= z + footprint_z},
        {SiteFault::water,
         low < sea - limits.max_water_depth || sea - limits.min_water_depth < high},
        {SiteFault::slope, static_cast<int32_t>(limits.max_slope) < high - low},
        {SiteFault::occupied, occupied},
    }};
    auto found = SiteFault::none;
    for (const auto& [fault, broken] : faults) {
        if (!broken)
            continue;
        if (found != SiteFault::none)
            return SiteFault::several;
        found = fault;
    }
    return found;
}

// The metal on the plots under a footprint.
int32_t metal_under(
    const oa::sim::spatial_state::World& spatial,
    int32_t x,
    int32_t z,
    int32_t footprint_x,
    int32_t footprint_z
) {
    int32_t metal = 0;
    for (int32_t row = 0; row < footprint_z; ++row)
        for (int32_t column = 0; column < footprint_x; ++column)
            metal += spatial
                         .plots
                             [static_cast<std::size_t>(z + row) * spatial.terrain_width +
                              static_cast<std::size_t>(x + column)]
                         .metal;
    return metal;
}

} // namespace

void Runtime::check_build_site_pointer(uint16_t builder, uint16_t type) {
    const auto& fields = offline_type_fields_.at(type);
    if (fields.runtime_metadata == nullptr)
        throw std::runtime_error("build site check: the extractor has no placement limits");
    const auto& limits = *fields.runtime_metadata;
    const auto fx = static_cast<int32_t>(spawn_types_[type].footprint_x);
    const auto fz = static_cast<int32_t>(spawn_types_[type].footprint_z);
    const auto& spatial = match_->spatial();
    const auto width = static_cast<int32_t>(spatial.terrain_width);
    const auto height = static_cast<int32_t>(spatial.terrain_height);
    const auto primary_count = [&] {
        std::size_t count = 0;
        for (const auto* order = match_->orders(builder).primary; order != nullptr;
             order = order->next)
            ++count;
        return count;
    };
    // Centres the view on the site, puts the pointer on its footprint centre
    // at the terrain height there, draws the frame (written to `snapshot`
    // when one is named) and clicks.
    const auto try_site = [&](int32_t x,
                              int32_t z,
                              bool expect_legal,
                              const char* what,
                              const char* snapshot = nullptr) {
        match_command_ = MatchCommand::build;
        pending_build_type_ = type;
        const auto centre_x = (x * 2 + fx) * 8;
        const auto centre_z = (z * 2 + fz) * 8;
        match_camera_x_ = centre_x - visible_map_width() / 2;
        match_camera_z_ = centre_z - visible_map_height() / 2;
        render_match_surface();
        const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
        const auto ground =
            std::max<int32_t>(terrain.sea_level(), terrain.height(centre_x << 16, centre_z << 16));
        const auto viewport = live_viewport(
            static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
        );
        const auto pointer = project_match_point(
            viewport,
            {static_cast<uint32_t>(centre_x) << 16,
             static_cast<uint32_t>(ground) << 16,
             static_cast<uint32_t>(centre_z) << 16}
        );
        const auto px = static_cast<float>(pointer.x);
        const auto py = static_cast<float>(pointer.y);
        update_pointer(px, py);
        const auto site = build_site_under(px, py);
        if (!site || site->cell_x != x || site->cell_z != z)
            throw std::runtime_error(
                std::string("build site check: the pointer missed the ") + what + " site " +
                std::to_string(x) + ',' + std::to_string(z) + " for " +
                (site ? std::to_string(site->cell_x) + ',' + std::to_string(site->cell_z)
                      : "none") +
                " at " + std::to_string(pointer.x) + ',' + std::to_string(pointer.y) + " height " +
                std::to_string(ground) + " camera " + std::to_string(match_camera_x_) + ',' +
                std::to_string(match_camera_z_)
            );
        if (site->legal != expect_legal)
            throw std::runtime_error(
                std::string("build site check: the ghost ") +
                (site->legal ? "accepted" : "refused") + " the " + what + " site " +
                std::to_string(x) + ',' + std::to_string(z)
            );
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        if (snapshot != nullptr) {
            const fs::path report_directory = "local/reports";
            fs::create_directories(report_directory);
            write_ppm(report_directory / snapshot, frame);
        }
        const auto top_left = project_match_point(
            viewport,
            {static_cast<uint32_t>(x * 16) << 16,
             static_cast<uint32_t>(site->world[1]),
             static_cast<uint32_t>(z * 16) << 16}
        );
        const auto top_right = project_match_point(
            viewport,
            {static_cast<uint32_t>((x + fx) * 16) << 16,
             static_cast<uint32_t>(site->world[1]),
             static_cast<uint32_t>(z * 16) << 16}
        );
        const auto edge_x = (top_left.x + top_right.x) / 2;
        const auto edge_y = top_left.y;
        if (edge_x < 0 || edge_y < 0 || edge_x >= static_cast<int>(frame.width) ||
            edge_y >= static_cast<int>(frame.height))
            throw std::runtime_error(
                std::string("build site check: the ") + what + " outline is off the frame"
            );
        const auto at =
            (static_cast<std::size_t>(edge_y) * frame.width + static_cast<std::size_t>(edge_x)) *
            3U;
        const std::array<uint8_t, 3> drawn{frame.rgb[at], frame.rgb[at + 1], frame.rgb[at + 2]};
        if (drawn != ui_color_rgb(expect_legal ? kBuildSiteClearColor : kBuildSiteRefusedColor))
            throw std::runtime_error(
                std::string("build site check: the ") + what + " site's outline is not the " +
                (expect_legal ? "clear" : "refused") + " colour"
            );
        const auto orders_before = primary_count();
        handle_match_left_click(px, py, 1);
        if (!expect_legal) {
            if (primary_count() != orders_before || match_command_ != MatchCommand::build ||
                pending_build_type_ != type)
                throw std::runtime_error(
                    std::string("build site check: a click on the ") + what +
                    " site placed the building"
                );
            return;
        }
        std::optional<oa::sim::match_runtime::Match::QueuedCommandView> first;
        match_->visit_primary_queue(builder, [&](const auto& queued) {
            if (!first)
                first = queued;
        });
        if (!first || first->kind != oa::sim::match_runtime::mobile_build_kind ||
            first->build_type != type || first->destination[0] != site->world[0] ||
            first->destination[2] != site->world[2])
            throw std::runtime_error(
                "build site check: a click on the clear site did not order MobileBuild there"
            );
        match_->stop_orders(builder);
    };

    std::string report;
    const auto note = [&](SiteFault fault, int32_t x, int32_t z) {
        report += std::string(report.empty() ? "" : ", ") + fault_name(fault) + ' ' +
                  std::to_string(x) + ',' + std::to_string(z);
    };
    // The builder's own plots, in its own sight, refuse the ghost: the ghost's
    // site test passes no unit to skip.
    const auto& builder_slot = match_->world().slots[builder];
    const auto builder_x = static_cast<int32_t>(builder_slot.unit->position[0] >> 20);
    const auto builder_z = static_cast<int32_t>(builder_slot.unit->position[2] >> 20);
    std::optional<std::pair<int32_t, int32_t>> occupied;
    for (int32_t dz = 1 - fz; dz <= 0 && !occupied; ++dz)
        for (int32_t dx = 1 - fx; dx <= 0 && !occupied; ++dx)
            if (site_fault(spatial, limits, builder_x + dx, builder_z + dz, fx, fz) ==
                SiteFault::occupied)
                occupied = std::pair{builder_x + dx, builder_z + dz};
    if (!occupied)
        throw std::runtime_error("build site check: the builder stands on no otherwise clear site");
    try_site(occupied->first, occupied->second, false, "occupied");
    note(SiteFault::occupied, occupied->first, occupied->second);

    // The rest of the map is mapped by the NowISee cheat, which also turns
    // line of sight off, so only the site's own rule can refuse it.
    chat_buffer_ = "+NowISee";
    submit_chat_line();
    // The first site, searching from the middle rows out so the view can
    // centre on it, that breaks `wanted` and whose metal `metal_fits`.
    const auto find_where = [&](
                                SiteFault wanted, const auto& metal_fits
                            ) -> std::optional<std::pair<int32_t, int32_t>> {
        for (int32_t step = 0; step < height; ++step) {
            const auto z = height / 2 + ((step & 1) != 0 ? -(step + 1) / 2 : step / 2);
            for (int32_t x = 0; x + fx <= width; ++x)
                if (site_fault(spatial, limits, x, z, fx, fz) == wanted &&
                    metal_fits(metal_under(spatial, x, z, fx, fz)))
                    return std::pair{x, z};
        }
        return std::nullopt;
    };
    const auto find = [&](SiteFault wanted) {
        return find_where(wanted, [](int32_t) { return true; });
    };
    for (const auto fault : {SiteFault::edge, SiteFault::water, SiteFault::slope}) {
        const auto site = find(fault);
        if (!site)
            throw std::runtime_error(
                std::string("build site check: the map has no ") + fault_name(fault) + " site"
            );
        try_site(site->first, site->second, false, fault_name(fault));
        note(fault, site->first, site->second);
    }
    const auto clear = find(SiteFault::none);
    if (!clear)
        throw std::runtime_error("build site check: the map has no clear site");
    try_site(clear->first, clear->second, true, "clear");
    // The site test has no metal rule: an extractor's ghost is green, and the
    // click orders it, on the clear site with the least metal under it (none,
    // off the deposits of a map without surface metal) as on the richest.
    auto poorest = std::numeric_limits<int32_t>::max();
    int32_t richest = 0;
    for (int32_t z = 0; z + fz <= height; ++z)
        for (int32_t x = 0; x + fx <= width; ++x)
            if (site_fault(spatial, limits, x, z, fx, fz) == SiteFault::none) {
                const auto metal = metal_under(spatial, x, z, fx, fz);
                poorest = std::min(poorest, metal);
                richest = std::max(richest, metal);
            }
    if (richest <= poorest)
        throw std::runtime_error("build site check: the map has no clear site on a deposit");
    const auto bare = find_where(SiteFault::none, [&](int32_t metal) { return metal == poorest; });
    const auto deposit =
        find_where(SiteFault::none, [&](int32_t metal) { return metal == richest; });
    try_site(bare->first, bare->second, true, "bare", "native-build-site-bare.ppm");
    try_site(deposit->first, deposit->second, true, "deposit", "native-build-site-deposit.ppm");
    std::cout << "build site check: ghost red and click refused over " << report
              << "; green and MobileBuild at " << clear->first << ',' << clear->second
              << ", on metal " << poorest << " at " << bare->first << ',' << bare->second
              << " and on metal " << richest << " at " << deposit->first << ',' << deposit->second
              << '\n';
}

} // namespace oa::app
