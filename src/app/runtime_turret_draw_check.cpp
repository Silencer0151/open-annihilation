// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Headless check that a turret built during the match, then turned over
// several frames, is drawn with exactly its current pieces.
#include "oa/app/runtime.hpp"
#include "oa/present/model/model_draw.hpp"
#include "match_fault.hpp"
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <utility>

namespace oa::app {
namespace {

// Rings of 16-pixel cells searched around the commander for the turret's
// site: far enough that the commander stays out of the frames compared.
constexpr int32_t kSiteNearest = 12;
constexpr int32_t kSiteFarthest = 60;
// Cells around the site that must hold no feature.
constexpr int32_t kFeatureClearance = 6;
// Map pixels kept around the turret's model in the frames compared, for its
// ground shadow.
constexpr int32_t kCropMargin = 16;
// Frames drawn while the turret is still being built.
constexpr int kUnfinishedFrames = 4;
// Ticks a turn is given to end, one frame drawn after each.
constexpr int kTurnTickLimit = 90;

// Headings the turret is aimed at in turn, the zoom each turn is drawn at,
// and whether the camera looks away and back before the frames compared.
struct TurretTurn {
    uint16_t heading{};
    float zoom{};
    bool camera_moves{};
};

constexpr std::array<TurretTurn, 3> kTurns{{
    {0x8000, 1.0F, false},
    {0x4000, 2.0F, false},
    {0xc000, 1.0F, true},
}};

[[noreturn]] void fail(const std::string& what) {
    throw std::runtime_error("turret draw check: " + what);
}

} // namespace

void Runtime::check_turret_draws(const fs::path& report_directory) {
    exercise_click(skirmish::resource_name(skirmish::Button::start));
    if (screen_ != Screen::match || !match_ || !selected_tnt_)
        fail("Start did not enter a match");
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots) {
        if (slot.unit_index == 0 || slot.unit == nullptr || slot.record.type_index == 0 ||
            slot.record.owner_index != match_local_player_)
            continue;
        if (const auto* def = definition_for(slot.unit_index); def != nullptr && def->can_dgun) {
            commander = slot.unit_index;
            break;
        }
    }
    if (commander == 0)
        fail("found no local commander");
    const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMHLT");
    if (type == 0 || type >= spawn_types_.size())
        fail("the game lacks ARMHLT");

    // The Sentinel is placed as a nanoframe, as a builder starts one, on a
    // site with no feature near enough to stand over it in the frames.
    auto& world = match_->state();
    const auto clear_of_features = [&](int32_t x, int32_t z) {
        for (int32_t dz = -kFeatureClearance; dz <= kFeatureClearance; ++dz)
            for (int32_t dx = -kFeatureClearance; dx <= kFeatureClearance; ++dx) {
                const auto* plot = oa::world_plot(&world, x + dx, z + dz);
                if (plot != nullptr && plot->feature < world.feature_def_count)
                    return false;
            }
        return true;
    };
    const auto cell_x = static_cast<int32_t>(slots[commander].unit->position[0] >> 20);
    const auto cell_z = static_cast<int32_t>(slots[commander].unit->position[2] >> 20);
    std::optional<std::pair<int32_t, int32_t>> site;
    for (int32_t ring = kSiteNearest; ring < kSiteFarthest && !site; ++ring)
        for (int32_t dz = -ring; dz <= ring && !site; dz += 2)
            for (int32_t dx = -ring; dx <= ring && !site; dx += 2)
                if ((dx == -ring || dx == ring || dz == -ring || dz == ring) &&
                    clear_of_features(cell_x + dx, cell_z + dz) &&
                    match_->building_site(type, cell_x + dx, cell_z + dz, 0))
                    site = std::pair{cell_x + dx, cell_z + dz};
    if (!site)
        fail("found no site for ARMHLT near the commander");
    const auto& placed_type = spawn_types_[type];
    const auto x = static_cast<uint32_t>((site->first * 2 + placed_type.footprint_x) * 8);
    const auto z = static_cast<uint32_t>((site->second * 2 + placed_type.footprint_z) * 8);
    oa::sim::unit_spawn::Request request;
    request.player = match_local_player_;
    request.type = type;
    request.finished = false;
    request.state = kGroundOccupancyState;
    request.position = {
        x << 16, static_cast<uint32_t>(match_->map_height(x << 16, z << 16)) << 16, z << 16
    };
    auto* placed = match_->create(request);
    if (placed == nullptr || placed->unit == nullptr || placed->record.build_remaining == 0.0F)
        fail("could not place an unfinished ARMHLT");
    const auto turret = placed->unit_index;
    auto* instance = match_->instance(turret);
    if (instance == nullptr || instance->script() == nullptr)
        fail("the ARMHLT has no script");
    const auto& piece_names = instance->script()->program().piece_names;
    const auto named = std::find(piece_names.begin(), piece_names.end(), "turret");
    if (named == piece_names.end())
        fail("the ARMHLT script has no turret piece");
    const auto turret_piece = static_cast<uint32_t>(named - piece_names.begin());

    const auto saved_zoom = match_zoom_;
    const auto saved_zoom_target = match_zoom_target_;
    const auto tick = [&] {
        ++match_timing_.tick;
        match_->simulation().tick = match_timing_.tick;
        tick_or_raise(*match_);
    };
    const auto set_zoom = [&](float zoom) {
        match_zoom_ = zoom;
        match_zoom_target_ = zoom;
    };
    const auto draw = [&] {
        center_camera_on_unit(turret);
        render_match_surface();
    };
    // The battlefield around the turret, from the frame drawn last.
    const auto crop = [&] {
        const auto& battlefield = match_world_cpu_;
        auto view = live_viewport(
            static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
        );
        view.destination_x = 0;
        view.destination_y = 0;
        view.surface_width = battlefield.width;
        view.surface_height = battlefield.height;
        const auto at = project_match_point(view, slots[turret].unit->position);
        const auto bounds = oa::present::model::measure_model_bounds(instance->model(), nullptr);
        const auto reach = static_cast<int32_t>(
            static_cast<float>(std::max(bounds.width, bounds.height) + kCropMargin) * view.scale
        );
        const auto left = std::max(0, at.x - reach);
        const auto top = std::max(0, at.y - reach);
        const auto right = std::min(static_cast<int32_t>(battlefield.width), at.x + reach);
        const auto bottom = std::min(static_cast<int32_t>(battlefield.height), at.y + reach);
        if (right - left < reach || bottom - top < reach)
            fail("the ARMHLT is off the battlefield");
        renderer::Surface out{
            static_cast<uint32_t>(right - left), static_cast<uint32_t>(bottom - top), {}
        };
        out.rgb.reserve(static_cast<std::size_t>(out.width) * out.height * 3U);
        for (int32_t row = top; row < bottom; ++row) {
            const auto* first =
                battlefield.rgb.data() + (static_cast<std::size_t>(row) * battlefield.width +
                                          static_cast<std::size_t>(left)) *
                                             3U;
            out.rgb.insert(out.rgb.end(), first, first + static_cast<std::size_t>(out.width) * 3U);
        }
        return out;
    };

    // The nanoframe is drawn over a few frames, then finished.
    set_zoom(1.0F);
    for (int frame = 0; frame < kUnfinishedFrames; ++frame) {
        tick();
        draw();
    }
    write_ppm(report_directory / "native-turret-unfinished.ppm", crop());
    match_->finish_unit(turret, commander);
    if (slots[turret].record.build_remaining != 0.0F)
        fail("the ARMHLT did not finish");

    std::optional<renderer::Surface> first_heading;
    std::string report;
    for (std::size_t index = 0; index < kTurns.size(); ++index) {
        const auto& turn = kTurns[index];
        const std::array<int32_t, 2> aim{turn.heading, 0};
        if (!instance->script()->call("AimPrimary", std::span<const int32_t>(aim), false))
            fail("could not start AimPrimary");
        set_zoom(turn.zoom);
        // Each tick of the turn is drawn, as the game draws every frame.
        int ticks = 0;
        const auto& piece = instance->model().piece_for_script_index(turret_piece);
        while (static_cast<uint16_t>(piece.rotation.xz) != turn.heading) {
            if (++ticks > kTurnTickLimit)
                fail("the turret did not reach heading " + std::to_string(turn.heading));
            tick();
            draw();
        }
        if (turn.camera_moves) {
            center_camera_on_unit(commander);
            render_match_surface();
        }
        draw();
        const auto drawn = crop();
        // The same state drawn afresh: every cached image is dropped first.
        release_model_images();
        draw();
        const auto fresh = crop();
        const auto name = std::to_string(index + 1);
        write_ppm(report_directory / ("native-turret-drawn-" + name + ".ppm"), drawn);
        write_ppm(report_directory / ("native-turret-fresh-" + name + ".ppm"), fresh);
        std::size_t differing = 0;
        if (drawn.width == fresh.width && drawn.height == fresh.height)
            for (std::size_t at = 0; at < drawn.rgb.size(); at += 3)
                differing += std::memcmp(&drawn.rgb[at], &fresh.rgb[at], 3) != 0 ? 1 : 0;
        if (drawn.width != fresh.width || drawn.height != fresh.height || differing != 0)
            fail(
                "after turn " + name + " the drawn ARMHLT differs from a fresh draw of it in " +
                std::to_string(differing) + " pixels"
            );
        // The frames compared show the turret itself: it looks different
        // when it has turned.
        if (turn.zoom == 1.0F) {
            if (first_heading && first_heading->rgb == fresh.rgb)
                fail("the frames compared do not show the turret turn");
            if (!first_heading)
                first_heading = fresh;
        }
        report += std::string(report.empty() ? "" : ", ") + std::to_string(ticks) +
                  " frames to heading " + std::to_string(turn.heading) + " at zoom " +
                  std::to_string(static_cast<int>(turn.zoom));
    }
    set_zoom(saved_zoom);
    match_zoom_target_ = saved_zoom_target;
    std::cout << "turret draw check: a Sentinel drawn while built, finished and turned (" << report
              << ") draws as a fresh draw of it\n";
    return_to_skirmish_menu();
}

} // namespace oa::app
