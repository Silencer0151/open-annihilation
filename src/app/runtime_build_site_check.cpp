// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless check of the build ghost and the placing click over the sites the
// game's building-site test refuses, and over bare ground and a metal deposit,
// which it accepts alike; and of the ghost's outline over the map's edges and
// at several zooms.
#include "oa/app/runtime.hpp"
#include "oa/core/map_plot.h"
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <iostream>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    // A solar collector's wide footprint hangs well over the edges.
    const auto solar = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMSOLAR");
    check_build_site_edges(solar != 0 && solar < spawn_types_.size() ? solar : type);
}

void Runtime::check_build_site_edges(uint16_t type) {
    // Map pixels across a terrain tile and a footprint cell.
    constexpr uint32_t tile_pixels = 32;
    // Map pixels the pointer stands inside an edge of the battlefield.
    constexpr int pointer_inset = 2;
    const auto map_width = static_cast<int32_t>(selected_tnt_->tile_width * tile_pixels);
    const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * tile_pixels);
    const auto saved_camera_x = match_camera_x_;
    const auto saved_camera_z = match_camera_z_;
    const auto saved_zoom = match_zoom_;
    const auto saved_zoom_target = match_zoom_target_;
    const auto saved_zoom_anchored = zoom_anchored_;
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    std::vector<std::string> failures;
    std::string report;
    const auto set_zoom = [&](float zoom) {
        match_zoom_ = zoom;
        match_zoom_target_ = zoom;
        zoom_anchored_ = false;
    };
    // Draws the frame with and without the ghost for the pointer at
    // (`pointer_x`, `pointer_y`) on the battlefield, the camera at (`camera_x`,
    // `camera_z`) and `zoom`, and compares them. The site must start left of
    // the map when `over_left` is set and above it when `over_top` is.
    const auto try_view = [&](const std::string& where,
                              int32_t camera_x,
                              int32_t camera_z,
                              int pointer_x,
                              int pointer_y,
                              float zoom,
                              bool over_left,
                              bool over_top) {
        // The zoom in tenths where it is not whole: 1, 1.5, 2.
        const auto tenths = static_cast<int>(std::lround(zoom * 10.0F));
        const auto zoom_name = std::to_string(tenths / 10) +
                               (tenths % 10 != 0 ? '.' + std::to_string(tenths % 10) : "");
        const auto name = where + " at zoom " + zoom_name;
        set_zoom(zoom);
        match_camera_x_ = camera_x;
        match_camera_z_ = camera_z;
        const auto canvas_x = static_cast<float>(match_layout_.left + pointer_x);
        const auto canvas_y = static_cast<float>(match_layout_.top + pointer_y);
        update_pointer(canvas_x, canvas_y);
        match_command_ = MatchCommand::none;
        render_match_surface();
        const auto without = match_world_cpu_;
        match_command_ = MatchCommand::build;
        pending_build_type_ = type;
        render_match_surface();
        const auto with = match_world_cpu_;
        const auto site = build_site_under(canvas_x, canvas_y);
        if (!site) {
            failures.push_back("no site under the pointer " + name);
            return;
        }
        std::string snapshot = "native-build-ghost-" + where + "-zoom-" + zoom_name + ".ppm";
        for (auto& letter : snapshot)
            if (letter == ' ')
                letter = '-';
        renderer::Surface frame;
        compose_match_frame(frame);
        write_ppm(report_directory / snapshot, frame);
        report += std::string(report.empty() ? "" : ", ") + name + " site " +
                  std::to_string(site->cell_x) + ',' + std::to_string(site->cell_z);
        if ((over_left && site->cell_x >= 0) || (over_top && site->cell_z >= 0))
            failures.push_back(
                "the site " + name + " starts at " + std::to_string(site->cell_x) + ',' +
                std::to_string(site->cell_z) + ", not over the edge of the map"
            );
        // The footprint's rectangle on the battlefield frame, from the camera
        // and the zoom alone.
        const auto scale = static_cast<double>(zoom);
        const auto raise = std::lround(static_cast<double>(site->world[1] >> 16) * 0.5 * scale);
        const auto frame_x = [&](int32_t cell) {
            return std::llround((cell * OA_MAP_CELL_PIXELS - match_camera_x_) * scale);
        };
        const auto frame_y = [&](int32_t cell) {
            return std::llround((cell * OA_MAP_CELL_PIXELS - match_camera_z_) * scale) - raise;
        };
        const auto left = frame_x(site->cell_x);
        const auto right = frame_x(site->cell_x + site->footprint_x);
        const auto top = frame_y(site->cell_z);
        const auto bottom = frame_y(site->cell_z + site->footprint_z);
        const auto band = kBuildSiteOutlineCount * build_site_outline_width(zoom);
        const auto colour =
            ui_color_rgb(site->legal ? kBuildSiteClearColor : kBuildSiteRefusedColor);
        const auto changed = [&](int64_t x, int64_t y) {
            if (x < 0 || y < 0 || x >= static_cast<int64_t>(with.width) ||
                y >= static_cast<int64_t>(with.height))
                return false;
            const auto at =
                (static_cast<std::size_t>(y) * with.width + static_cast<std::size_t>(x)) * 3U;
            return with.rgb[at] != without.rgb[at] || with.rgb[at + 1] != without.rgb[at + 1] ||
                   with.rgb[at + 2] != without.rgb[at + 2];
        };
        std::size_t drawn = 0;
        std::optional<std::pair<int64_t, int64_t>> stray;
        for (int64_t y = 0; y < static_cast<int64_t>(with.height); ++y)
            for (int64_t x = 0; x < static_cast<int64_t>(with.width); ++x) {
                if (!changed(x, y))
                    continue;
                ++drawn;
                const auto at =
                    (static_cast<std::size_t>(y) * with.width + static_cast<std::size_t>(x)) * 3U;
                const std::array<uint8_t, 3> pixel{
                    with.rgb[at], with.rgb[at + 1], with.rgb[at + 2]
                };
                // How far inside the rectangle the pixel lies, with one pixel
                // of slack for rounding at each side of the band.
                const auto depth = std::min({x - left, right - x, y - top, bottom - y});
                if (!stray && (pixel != colour || depth < -1 || depth > band))
                    stray = std::pair{x, y};
            }
        // A refused site stands at height 0, so over the bottom edge of high
        // ground its rectangle can fall wholly below the battlefield.
        const bool on_battlefield = right >= 0 && left < static_cast<int64_t>(with.width) &&
                                    bottom >= 0 && top < static_cast<int64_t>(with.height);
        if (on_battlefield && drawn == 0)
            failures.push_back("no outline drawn " + name);
        if (stray)
            failures.push_back(
                "the ghost " + name + " changed " + std::to_string(stray->first) + ',' +
                std::to_string(stray->second) + ", outside its outline " + std::to_string(left) +
                ',' + std::to_string(top) + " to " + std::to_string(right) + ',' +
                std::to_string(bottom)
            );
        if (left < 0 || top < 0 || right >= static_cast<int64_t>(with.width) ||
            bottom >= static_cast<int64_t>(with.height))
            return;
        // A whole rectangle shows the whole band: count it from outside the
        // top edge's middle down to the centre, and from outside the left
        // edge's middle across to the centre.
        const auto count_changed = [&](int64_t x, int64_t y, int64_t step_x, int64_t step_y) {
            int count = 0;
            for (; x <= (left + right) / 2 && y <= (top + bottom) / 2; x += step_x, y += step_y)
                if (changed(x, y))
                    ++count;
            return count;
        };
        const auto top_depth = count_changed((left + right) / 2, top - 2, 0, 1);
        const auto left_depth = count_changed(left - 2, (top + bottom) / 2, 1, 0);
        if (top_depth != band || left_depth != band)
            failures.push_back(
                "the outline " + name + " is " + std::to_string(top_depth) +
                " pixels deep at the top and " + std::to_string(left_depth) + " at the left, not " +
                std::to_string(band)
            );
    };
    // The view at `column` and `row` of the map (0 its left or top edge, 1 its
    // middle, 2 its right or bottom edge), the pointer as far into the
    // battlefield.
    const auto try_grid = [&](const char* where, int column, int row, float zoom) {
        set_zoom(zoom);
        const auto inset = static_cast<int>(std::lround(pointer_inset * zoom));
        const auto along = [&](int step, int extent) {
            return step == 0 ? inset : step == 1 ? extent / 2 : extent - 1 - inset;
        };
        try_view(
            where,
            column * std::max(0, map_width - visible_map_width()) / 2,
            row * std::max(0, map_height - visible_map_height()) / 2,
            along(column, match_layout_.battlefield_width()),
            along(row, match_layout_.battlefield_height()),
            zoom,
            column == 0,
            false
        );
    };

    // Over the top left corner the footprint starts left of and above the
    // map whatever the ground there, and its rectangle is placed left of and
    // above the battlefield.
    set_zoom(1.0F);
    match_command_ = MatchCommand::build;
    pending_build_type_ = type;
    const auto corner = pending_build_site({pointer_inset << 16, 0, pointer_inset << 16});
    // The first cell of a footprint `size` cells wide centred on the pointer;
    // a footprint of 0 counts as 2.
    const auto corner_cell = [&](int16_t size) {
        const auto cells = static_cast<double>(size > 0 ? size : 2);
        return static_cast<int32_t>(std::floor(
            (pointer_inset - (cells - 1.0) * OA_MAP_CELL_PIXELS / 2.0) / OA_MAP_CELL_PIXELS
        ));
    };
    const auto corner_x = corner_cell(spawn_types_[type].footprint_x);
    const auto corner_z = corner_cell(spawn_types_[type].footprint_z);
    if (!corner || corner->legal || corner->cell_x != corner_x || corner->cell_z != corner_z)
        failures.push_back(
            "the site at the top left corner is " +
            (corner ? std::to_string(corner->cell_x) + ',' + std::to_string(corner->cell_z)
                    : std::string("none")) +
            ", not a refused " + std::to_string(corner_x) + ',' + std::to_string(corner_z)
        );
    const auto corner_view = live_viewport(0, 0);
    const auto above_left = project_match_point(
        corner_view,
        {static_cast<uint32_t>(corner_x * OA_MAP_CELL_PIXELS) << 16,
         0,
         static_cast<uint32_t>(corner_z * OA_MAP_CELL_PIXELS) << 16}
    );
    if (above_left.x != corner_view.destination_x + corner_x * OA_MAP_CELL_PIXELS ||
        above_left.y != corner_view.destination_y + corner_z * OA_MAP_CELL_PIXELS)
        failures.push_back(
            "the site at the top left corner is placed at " + std::to_string(above_left.x) + ',' +
            std::to_string(above_left.y) + " on the frame"
        );

    try_grid("top left", 0, 0, 1.0F);
    try_grid("top", 1, 0, 1.0F);
    try_grid("top right", 2, 0, 1.0F);
    try_grid("left", 0, 1, 1.0F);
    try_grid("right", 2, 1, 1.0F);
    try_grid("bottom left", 0, 2, 1.0F);
    try_grid("bottom", 1, 2, 1.0F);
    try_grid("bottom right", 2, 2, 1.0F);
    for (const auto zoom : {1.0F, 1.5F, 2.0F, 3.0F})
        try_grid("middle", 1, 1, zoom);
    // Each outline is one pixel wide up to zoom 1 and as wide as the zoom,
    // rounded to the nearest pixel, zoomed in.
    for (const auto& [zoom, width] :
         {std::pair{0.5F, 1},
          std::pair{1.0F, 1},
          std::pair{1.3F, 1},
          std::pair{1.5F, 2},
          std::pair{2.0F, 2},
          std::pair{2.5F, 3},
          std::pair{3.0F, 3}})
        if (build_site_outline_width(zoom) != width)
            failures.push_back(
                "each outline is " + std::to_string(build_site_outline_width(zoom)) +
                " pixels wide at zoom " + std::to_string(zoom) + ", not " + std::to_string(width)
            );

    // The pointer reaches a site over the top edge only where the ground
    // along it is low enough; the first such place, a cell apart, is tried
    // when the map has one.
    set_zoom(1.0F);
    const auto top_inset = pointer_inset;
    std::optional<std::pair<int32_t, int>> low_top;
    const auto camera_step = std::max(1, visible_map_width() / 2);
    for (int32_t camera_x = 0; !low_top && camera_x + visible_map_width() <= map_width;
         camera_x += camera_step)
        for (int pointer_x = top_inset;
             !low_top && pointer_x < match_layout_.battlefield_width() - top_inset;
             pointer_x += OA_MAP_CELL_PIXELS) {
            match_camera_x_ = camera_x;
            match_camera_z_ = 0;
            const auto site = build_site_under(
                static_cast<float>(match_layout_.left + pointer_x),
                static_cast<float>(match_layout_.top + top_inset)
            );
            if (site && site->cell_z < 0)
                low_top = std::pair{camera_x, pointer_x};
        }
    if (low_top)
        try_view(
            "top low ground", low_top->first, 0, low_top->second, top_inset, 1.0F, false, true
        );
    else
        report += ", no site over the top edge within the pointer's reach";

    set_zoom(saved_zoom);
    match_zoom_target_ = saved_zoom_target;
    zoom_anchored_ = saved_zoom_anchored;
    match_camera_x_ = saved_camera_x;
    match_camera_z_ = saved_camera_z;
    render_match_surface();
    if (!failures.empty()) {
        std::string message = "build ghost edge check:";
        for (const auto& failure : failures)
            message += "\n  " + failure;
        throw std::runtime_error(message);
    }
    std::cout << "build ghost edge check: " << spawn_type_names_.at(type)
              << " outline stays inside its footprint over " << report << '\n';
}

} // namespace oa::app
