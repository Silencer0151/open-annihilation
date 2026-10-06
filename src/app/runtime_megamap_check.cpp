// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The megamap's picture (ui.megamap), then its clicks against the
// battlefield's, in both interface types, through synthetic SDL input on a
// skirmish.
#include "engine_settings_state.hpp"
#include "oa/app/runtime.hpp"
#include "oa/sim/match_runtime/attack_orders.hpp"
#include "oa/sim/match_runtime/construction_orders.hpp"
#include "oa/ui/hud/megamap.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <functional>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

namespace fs = std::filesystem;
namespace input = oa::sim::gameplay_input;
namespace hud = oa::ui::hud;

/// Map pixels between the units the check places, which keeps their icons
/// apart on the megamap and the enemy in the mover's sight.
constexpr int32_t kApart = 192;
/// Ticks the match may run for the enemy to come into sight and onto the
/// radar.
constexpr int kSightTicks = 30;
/// Map pixels the units keep from the map's edges.
constexpr int32_t kEdgeMargin = 96;
/// Map pixels of a footprint cell.
constexpr int32_t cell_pixels = 16;
/// Picture pixels around a drawn feature's spot that its picture may cover
/// beyond its scaled size.
constexpr int32_t feature_reach_margin = 2;
/// Palette index of the bars beside the megamap's picture.
constexpr uint8_t megamap_bar_color = 95;

/// Fails the check, saying what went wrong.
[[noreturn]] void fail(std::string_view what) {
    throw std::runtime_error("megamap click check: " + std::string(what));
}

/// Fails the check, saying what went wrong, unless `ok` holds.
void require(bool ok, std::string_view what) {
    if (!ok)
        fail(what);
}

/// Names a step's frame: <stem>-<step>.ppm beside --snapshot.
fs::path step_snapshot(const fs::path& snapshot, std::string_view step) {
    return snapshot.parent_path() / (snapshot.stem().string() + '-' + std::string(step) + ".ppm");
}

/// What a click left behind: the selection, the armed command and the
/// orders of the mover and the solar collector.
struct ClickResult {
    bool mover_selected{};
    bool friend_selected{};
    bool any_selected{};
    MatchCommand command{};
    std::vector<uint8_t> kinds;                               ///< the mover's queue, head first
    std::optional<oa::sim::ground_orders::Point> destination; ///< its head's point
    std::vector<uint8_t> collector_kinds; ///< the solar collector's queue, head first

    /// Tells whether two clicks left the same selection, armed command and
    /// queues; the destinations differ by where each surface's pointer was.
    [[nodiscard]] bool same_as(const ClickResult& other) const {
        return mover_selected == other.mover_selected && friend_selected == other.friend_selected &&
               any_selected == other.any_selected && command == other.command &&
               kinds == other.kinds && collector_kinds == other.collector_kinds;
    }
};

/// Describes what a click left behind in one line of the check's output.
std::string describe(const ClickResult& result) {
    std::string text = std::string("mover ") + (result.mover_selected ? "selected" : "unselected") +
                       ", friend " + (result.friend_selected ? "selected" : "unselected") +
                       (result.any_selected ? "" : ", nothing selected") + ", command " +
                       std::to_string(static_cast<int>(result.command)) + ", orders [";
    for (std::size_t index = 0; index < result.kinds.size(); ++index)
        text += (index == 0 ? "" : " ") + std::to_string(result.kinds[index]);
    text += "]";
    if (result.collector_kinds.empty())
        return text;
    text += ", solar collector orders [";
    for (std::size_t index = 0; index < result.collector_kinds.size(); ++index)
        text += (index == 0 ? "" : " ") + std::to_string(result.collector_kinds[index]);
    return text + "]";
}

} // namespace

void Runtime::check_megamap_clicks() {
    namespace orders = oa::sim::match_runtime;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    start_benchmark_skirmish();
    apply_output_mode();
    if (!ui_rules().megamap.enabled) {
        std::cout << "megamap click check: ui.megamap is off, so nothing was checked\n";
        return;
    }
    auto& world = match_->state();
    auto& slots = match_->world().slots;
    const auto local = match_local_player_;
    uint16_t commander = 0;
    uint8_t enemy_player = local;
    for (const auto& slot : slots) {
        if (slot.unit == nullptr || slot.record.type_index == 0)
            continue;
        if (commander == 0 && slot.record.owner_index == local)
            commander = slot.unit_index;
        if (enemy_player == local && slot.record.owner_index != local)
            enemy_player = slot.record.owner_index;
    }
    require(commander != 0 && enemy_player != local, "found no commander or enemy player");
    const auto map_width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
    const auto map_x = [&](uint16_t id) {
        return static_cast<int32_t>(slots[id].unit->position[0] >> 16);
    };
    const auto map_z = [&](uint16_t id) {
        return static_cast<int32_t>(slots[id].unit->position[2] >> 16);
    };
    const auto spawn = [&](std::string_view name, uint8_t owner, int32_t x, int32_t z) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0)
            fail("lacks " + std::string(name));
        x = std::clamp(x, kEdgeMargin, map_width - kEdgeMargin);
        z = std::clamp(z, kEdgeMargin, map_height - kEdgeMargin);
        oa::sim::unit_spawn::Request request;
        request.player = owner;
        request.type = type;
        request.finished = true;
        request.state = kGroundOccupancyState;
        request.position = {
            static_cast<uint32_t>(x) << 16,
            static_cast<uint32_t>(
                match_->map_height(static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(z) << 16)
            ) << 16,
            static_cast<uint32_t>(z) << 16
        };
        auto* slot = match_->create(request);
        if (slot == nullptr || slot->unit == nullptr)
            fail("could not spawn " + std::string(name));
        slot->unit->object_present = true;
        // Held fire, so that the units do not fight while the check runs.
        slot->record.flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
        return slot->unit_index;
    };
    // The mover, a friend beside it and an enemy in its sight, placed from
    // the commander toward the map's middle; the ground clicked lies
    // between them. A solar collector, which cannot patrol, stands between
    // the commander and the mover.
    const int32_t way_x = map_x(commander) < map_width / 2 ? 1 : -1;
    const int32_t way_z = map_z(commander) < map_height / 2 ? 1 : -1;
    const auto mover =
        spawn("ARMPW", local, map_x(commander) + way_x * kApart, map_z(commander) + way_z * kApart);
    const auto friend_unit = spawn("ARMPW", local, map_x(mover) + way_x * kApart, map_z(mover));
    const auto enemy = spawn("CORAK", enemy_player, map_x(mover), map_z(mover) + way_z * kApart);
    const auto collector = spawn(
        "ARMSOLAR", local, map_x(mover) - way_x * kApart / 2, map_z(mover) - way_z * kApart / 2
    );
    const std::array<int32_t, 2> open_ground{
        map_x(mover) + way_x * kApart / 2, map_z(mover) + way_z * kApart / 2
    };

    // The megamap acts only with the Mouse wheel zoom setting off.
    auto& wheel_zoom = engine_settings_state().current.wheel_zoom;
    const bool kept_wheel_zoom = std::exchange(wheel_zoom, false);
    megamap_wheel_zoom_changed();
    set_megamap_open(false);
    // The match runs until the enemy is in sight and on the radar, as the
    // minimap and the megamap show it; then the enemy stands where it is.
    for (int step = 0;
         step < kSightTicks && !((slots[enemy].unit->flags & OA_UNIT_FLAG_RADAR_CONTACT) != 0 &&
                                 match_->unit_visible(local, enemy));
         ++step)
        step_match_simulation();
    match_->stop_orders(enemy);
    render_match_surface();
    require(
        match_->unit_visible(local, enemy) &&
            (slots[enemy].unit->flags & OA_UNIT_FLAG_RADAR_CONTACT) != 0,
        "the enemy is not in the mover's sight and on the radar"
    );
    check_megamap_picture();

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
    const auto click = [&](uint8_t button, std::pair<float, float> at) {
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, button, at.first, at.second);
        send(SDL_EVENT_MOUSE_BUTTON_UP, button, at.first, at.second);
    };
    // The point clicked on the battlefield: a unit's, or the open ground's,
    // with the view centred there. The pointer rests on it for a frame, as
    // a player's does before clicking.
    const auto battlefield_at = [&](uint16_t id) {
        const std::array<int32_t, 2> on =
            id != 0 ? std::array<int32_t, 2>{map_x(id), map_z(id)} : open_ground;
        set_camera_position(on[0] - visible_map_width() / 2, on[1] - visible_map_height() / 2, 0);
        render_match_surface();
        const auto viewport = live_viewport(match_camera_x_, match_camera_z_);
        std::array<uint32_t, 3> position{};
        if (id != 0) {
            position = slots[id].unit->position;
        } else {
            const auto ground = map_world_point(on[0], on[1]);
            require(ground.has_value(), "found no ground at the open ground's point");
            position = {
                static_cast<uint32_t>((*ground)[0]),
                static_cast<uint32_t>((*ground)[1]),
                static_cast<uint32_t>((*ground)[2])
            };
        }
        const auto screen = project_match_point(viewport, position);
        const std::pair<float, float> at{
            static_cast<float>(screen.x), static_cast<float>(screen.y)
        };
        send(SDL_EVENT_MOUSE_MOTION, 0, at.first, at.second);
        render_match_surface();
        require(
            hovered_match_unit_ == id,
            id == 0 ? "a unit lies under the open ground on the battlefield"
                    : "the battlefield's pointer did not pick the unit"
        );
        return at;
    };
    // The point clicked on the megamap: a unit's icon, or the open ground's
    // pixel.
    const auto megamap_at = [&](uint16_t id) {
        const std::array<int32_t, 2> on =
            id != 0 ? std::array<int32_t, 2>{map_x(id), map_z(id)} : open_ground;
        const auto pixel = hud::megamap_point(megamap_.layout, on[0], on[1]);
        const std::pair<float, float> at{
            static_cast<float>(match_layout_.left + pixel[0]),
            static_cast<float>(match_layout_.top + pixel[1])
        };
        send(SDL_EVENT_MOUSE_MOTION, 0, at.first, at.second);
        render_match_surface();
        require(
            megamap_.hovered == id,
            id == 0 ? "a unit's icon lies under the open ground's pixel"
                    : "the megamap's pointer did not pick the unit's icon"
        );
        return at;
    };
    const auto result = [&] {
        ClickResult out;
        out.mover_selected = (slots[mover].unit->flags & OA_UNIT_FLAG_SELECTED) != 0;
        out.friend_selected = (slots[friend_unit].unit->flags & OA_UNIT_FLAG_SELECTED) != 0;
        out.any_selected = has_local_selection();
        out.command = match_command_;
        match_->visit_primary_queue(mover, [&](const auto& view) {
            if (out.kinds.empty())
                out.destination = view.destination;
            out.kinds.push_back(view.kind);
        });
        match_->visit_primary_queue(collector, [&](const auto& view) {
            out.collector_kinds.push_back(view.kind);
        });
        return out;
    };

    struct Case {
        const char* name{};
        int32_t interface_type{};
        bool mover_selected{};  ///< the mover selected before the click
        MatchCommand command{}; ///< the command armed before it
        uint8_t button{};       ///< the button clicked
        uint16_t on{};          ///< the unit clicked on, or 0 for the open ground
        std::function<bool(const ClickResult&)> expected; ///< what both must leave
        bool collector_selected{}; ///< the solar collector selected beside the mover
    };

    const auto moved = [&](const ClickResult& r) {
        return r.mover_selected && r.kinds.size() == 1 &&
               r.kinds.front() == oa::sim::ground_orders::move_ground_kind;
    };
    const auto attacked = [&](const ClickResult& r) {
        return r.mover_selected && r.kinds.size() == 1 &&
               r.kinds.front() == orders::attack_chase_kind;
    };
    const auto deselected = [](const ClickResult& r) { return !r.any_selected && r.kinds.empty(); };
    const std::vector<Case> cases{
        {"left-click interface: a left click on open ground moves the selection",
         input::interface_left_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_LEFT,
         0,
         [&](const ClickResult& r) { return moved(r) && r.command == MatchCommand::none; },
         false},
        {"left-click interface: a left click on an enemy attacks it",
         input::interface_left_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_LEFT,
         enemy,
         [&](const ClickResult& r) { return attacked(r); },
         false},
        {"left-click interface: a left click on an own unit selects it alone",
         input::interface_left_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_LEFT,
         friend_unit,
         [](const ClickResult& r) {
             return !r.mover_selected && r.friend_selected && r.kinds.empty();
         },
         false},
        {"left-click interface: a right press on open ground deselects without an order",
         input::interface_left_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_RIGHT,
         0,
         deselected,
         false},
        {"left-click interface: a right press cancels an armed PATROL alone",
         input::interface_left_click,
         true,
         MatchCommand::patrol,
         SDL_BUTTON_RIGHT,
         0,
         [](const ClickResult& r) {
             return r.mover_selected && r.command == MatchCommand::none && r.kinds.empty();
         },
         false},
        {"left-click interface: a left click with PATROL armed sends the mover on patrol and "
         "gives the solar collector selected beside it no order",
         input::interface_left_click,
         true,
         MatchCommand::patrol,
         SDL_BUTTON_LEFT,
         0,
         [](const ClickResult& r) {
             return r.mover_selected && r.command == MatchCommand::none && r.kinds.size() == 1 &&
                    r.kinds.front() == orders::patrol_kind && r.collector_kinds.empty();
         },
         true},
        {"right-click interface: a left click on open ground deselects without an order",
         input::interface_right_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_LEFT,
         0,
         deselected,
         false},
        {"right-click interface: a left click on an own unit selects it",
         input::interface_right_click,
         false,
         MatchCommand::none,
         SDL_BUTTON_LEFT,
         friend_unit,
         [](const ClickResult& r) {
             return !r.mover_selected && r.friend_selected && r.kinds.empty();
         },
         false},
        {"right-click interface: a right press on open ground moves the selection",
         input::interface_right_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_RIGHT,
         0,
         [&](const ClickResult& r) { return moved(r); },
         false},
        {"right-click interface: a right press on an own unit guards it",
         input::interface_right_click,
         true,
         MatchCommand::none,
         SDL_BUTTON_RIGHT,
         friend_unit,
         [](const ClickResult& r) {
             return r.mover_selected && !r.friend_selected && r.kinds.size() == 1 &&
                    r.kinds.front() == orders::follow_ground_kind;
         },
         false},
        {"right-click interface: a left click with ATTACK armed attacks the enemy",
         input::interface_right_click,
         true,
         MatchCommand::attack,
         SDL_BUTTON_LEFT,
         enemy,
         [&](const ClickResult& r) { return attacked(r) && r.command == MatchCommand::none; },
         false},
        {"left-click interface: a left click with MOVE armed on an enemy moves to it",
         input::interface_left_click,
         true,
         MatchCommand::move,
         SDL_BUTTON_LEFT,
         enemy,
         [&](const ClickResult& r) { return moved(r) && r.command == MatchCommand::none; },
         false},
        {"right-click interface: a left click with MOVE armed on an own unit guards it",
         input::interface_right_click,
         true,
         MatchCommand::move,
         SDL_BUTTON_LEFT,
         friend_unit,
         [](const ClickResult& r) {
             return r.mover_selected && !r.friend_selected && r.kinds.size() == 1 &&
                    r.kinds.front() == orders::follow_ground_kind &&
                    r.command == MatchCommand::none;
         },
         false},
    };

    // Each case from the same start on the battlefield, then on the megamap.
    const auto prepare = [&](const Case& one) {
        world.game.interface_type = one.interface_type;
        match_->stop_orders(mover);
        match_->stop_orders(collector);
        clear_local_selection();
        reset_match_command();
        pending_build_type_ = 0;
        if (one.mover_selected) {
            adopt_selection(mover);
            selected_match_unit_ = mover;
        }
        if (one.collector_selected)
            adopt_selection(collector);
        apply_match_hud_for_selection();
        match_command_ = one.command;
    };
    std::vector<std::string> failures;
    // With --snapshot, each surface's frame just before and just after the
    // click: case<number>-<battlefield|megamap>-<before|after>.
    const auto snapshot = [&](std::size_t number, std::string_view surface, bool after) {
        if (options_.snapshot.empty())
            return;
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        write_ppm(
            step_snapshot(
                options_.snapshot,
                "case" + std::to_string(number) + "-" + std::string(surface) +
                    (after ? "-after" : "-before")
            ),
            frame
        );
    };
    for (std::size_t index = 0; index < cases.size(); ++index) {
        const auto& one = cases[index];
        set_megamap_open(false);
        prepare(one);
        const auto battlefield_click = battlefield_at(one.on);
        snapshot(index + 1, "battlefield", false);
        click(one.button, battlefield_click);
        snapshot(index + 1, "battlefield", true);
        const auto battlefield = result();
        const auto battlefield_ground =
            one.on == 0 ? match_world_point(pointer_x_, pointer_y_) : std::nullopt;

        prepare(one);
        set_megamap_open(true);
        render_match_surface();
        require(megamap_shown(), "the megamap did not open");
        const auto at = megamap_at(one.on);
        std::optional<oa::sim::ground_orders::Point> megamap_ground;
        if (one.on == 0)
            if (const auto point = hud::megamap_map_point(
                    megamap_.layout,
                    static_cast<int32_t>(at.first) - match_layout_.left,
                    static_cast<int32_t>(at.second) - match_layout_.top
                ))
                megamap_ground = map_world_point((*point)[0], (*point)[1]);
        snapshot(index + 1, "megamap", false);
        click(one.button, at);
        snapshot(index + 1, "megamap", true);
        const auto megamap = result();
        set_megamap_open(false);

        std::cout << "megamap click check: case " << index + 1 << ": " << one.name
                  << "\n  battlefield: " << describe(battlefield) << " (clicked at "
                  << battlefield_click.first << "," << battlefield_click.second << ")"
                  << "\n  megamap:     " << describe(megamap) << " (clicked at " << at.first << ","
                  << at.second << ")\n";
        // A move goes to the ground under the pointer on each.
        const bool battlefield_point =
            battlefield.kinds.empty() ||
            battlefield.kinds.front() != oa::sim::ground_orders::move_ground_kind ||
            !battlefield_ground || battlefield.destination == battlefield_ground;
        const bool megamap_point =
            megamap.kinds.empty() ||
            megamap.kinds.front() != oa::sim::ground_orders::move_ground_kind || !megamap_ground ||
            megamap.destination == megamap_ground;
        if (!one.expected(battlefield) || !battlefield_point)
            failures.push_back(
                std::string(one.name) + " (battlefield: " + describe(battlefield) + ")"
            );
        if (!megamap.same_as(battlefield) || !one.expected(megamap) || !megamap_point)
            failures.push_back(std::string(one.name) + " (megamap: " + describe(megamap) + ")");
    }
    match_->stop_orders(mover);
    match_->stop_orders(collector);
    clear_local_selection();
    wheel_zoom = kept_wheel_zoom;
    megamap_wheel_zoom_changed();
    if (!failures.empty()) {
        std::string text;
        for (const auto& failure : failures)
            text += "\n  " + failure;
        fail("the megamap's clicks differ from the battlefield's:" + text);
    }
    std::cout << "megamap click check: the megamap's clicks give the battlefield's results in "
                 "both interface types: orders, selection, deselection, guard, the armed "
                 "command's order and cancel, and no patrol for a solar collector\n";
}

void Runtime::check_megamap_picture() {
    set_megamap_open(true);
    render_match_surface();
    require(megamap_shown(), "the megamap did not open");
    const auto& world = match_->state();
    const auto layout = megamap_.layout;
    require(layout.width > 0 && layout.height > 0, "the megamap has no picture");

    // Every difference from what the megamap should draw, reported together.
    std::vector<std::string> failures;

    // The terrain keeps to the colours of the map's tiles.
    const auto plain = downscale_megamap_terrain(layout);
    require(
        !plain.empty() && plain.size() == megamap_.terrain.size(),
        "the terrain picture and the megamap's picture differ in size"
    );
    std::array<bool, 256> map_colours{};
    for (const uint8_t pixel : selected_tnt_->tile_palette_indices)
        map_colours[pixel] = true;
    std::size_t foreign = 0;
    for (const uint8_t pixel : plain)
        foreign += map_colours[pixel] ? 0U : 1U;
    if (foreign != 0)
        failures.push_back(
            "the terrain picture takes " + std::to_string(foreign) +
            " pixels of colours the map's tiles do not use"
        );

    // The features: those the megamap draws are the indestructible ones
    // that cannot be reclaimed; each may change the picture only near its
    // spot, raised by half the ground's height.
    struct Drawn {
        int32_t x{};
        int32_t y{};
        int32_t reach{};
        uint8_t mark{};
        bool picture{};
        std::size_t changed{};
        std::size_t unlike_mark{};
    };

    std::vector<Drawn> drawn;
    std::size_t left_alone = 0;
    const auto width = static_cast<int32_t>(world.game.map_width);
    const auto cells = static_cast<std::size_t>(world.game.map_width) * world.game.map_height;
    for (std::size_t index = 0; index < cells && width > 0; ++index) {
        const auto& plot = world.plots[index];
        if (plot.feature >= OA_PLOT_FEATURE_RESERVED || plot.feature >= world.feature_def_count)
            continue;
        const auto& def = world.feature_defs[plot.feature];
        if ((def.flags & OA_FEATURE_FLAG_INDESTRUCTIBLE) == 0 ||
            (def.flags & OA_FEATURE_FLAG_RECLAIMABLE) != 0) {
            ++left_alone;
            continue;
        }
        const auto cell_x = static_cast<int32_t>(index % static_cast<std::size_t>(width));
        const auto cell_z = static_cast<int32_t>(index / static_cast<std::size_t>(width));
        const auto* frame = feature_sequence_image(def.seq_name, 0);
        const int32_t frame_side =
            frame != nullptr ? std::max<int32_t>(frame->width, frame->height) : 0;
        const auto spot = hud::megamap_point(
            layout,
            cell_x * cell_pixels + std::max<int32_t>(1, def.footprint_x) * cell_pixels / 2,
            cell_z * cell_pixels + std::max<int32_t>(1, def.footprint_z) * cell_pixels / 2 -
                plot.height / 2
        );
        const int32_t scaled = static_cast<int32_t>(
            static_cast<int64_t>(frame_side) * layout.width / layout.map_width
        );
        drawn.push_back(
            {spot[0] - layout.left,
             spot[1] - layout.top,
             std::max(scaled, hud::feature_mark_size) + feature_reach_margin,
             hud::feature_mark_color(def),
             frame != nullptr}
        );
    }
    require(left_alone != 0, "the map places no feature the megamap leaves out");
    require(
        std::any_of(drawn.begin(), drawn.end(), [](const Drawn& one) { return one.picture; }),
        "the map places no feature with a picture the megamap draws"
    );
    std::size_t stray = 0;
    for (int32_t y = 0; y < layout.height; ++y)
        for (int32_t x = 0; x < layout.width; ++x) {
            const auto at = static_cast<std::size_t>(y) * static_cast<std::size_t>(layout.width) +
                            static_cast<std::size_t>(x);
            if (megamap_.terrain[at] == plain[at])
                continue;
            bool near = false;
            for (auto& one : drawn)
                if (std::abs(x - one.x) <= one.reach && std::abs(y - one.y) <= one.reach) {
                    near = true;
                    ++one.changed;
                    one.unlike_mark += megamap_.terrain[at] != one.mark ? 1U : 0U;
                }
            stray += near ? 0U : 1U;
        }
    if (stray != 0)
        failures.push_back(
            std::to_string(stray) +
            " pixels of the megamap's picture changed away from the features it draws: "
            "reclaimable or destructible features are drawn"
        );
    std::size_t pictured = 0;
    std::size_t flat = 0;
    for (const auto& one : drawn) {
        if (!one.picture || one.changed == 0)
            continue;
        ++pictured;
        flat += one.unlike_mark == 0 ? 1U : 0U;
    }
    if (pictured == 0)
        failures.push_back("no feature with a picture changed the megamap's picture");
    if (flat != 0)
        failures.push_back(
            std::to_string(flat) + " of " + std::to_string(pictured) +
            " features with a picture are drawn as flat marks of one colour"
        );

    // Moving the main view leaves the megamap as it was: it has no
    // rectangle for the view. The bars beside the map are a dark grey.
    const auto view_at = [&](int32_t x, int32_t z) {
        set_camera_position(x, z, 0);
        render_match_surface();
        return match_world_cpu_;
    };
    const auto first = view_at(0, 0);
    const auto second = view_at(
        world.game.map_pixel_width - visible_map_width(),
        world.game.map_pixel_height - visible_map_height()
    );
    require(
        first.width == second.width && first.height == second.height,
        "the battlefield layer changed size"
    );
    const auto pixel_at = [](const renderer::Surface& layer, int32_t x, int32_t y) {
        const auto at =
            (static_cast<std::size_t>(y) * layer.width + static_cast<std::size_t>(x)) * 3U;
        return std::array<uint8_t, 3>{layer.rgb[at], layer.rgb[at + 1], layer.rgb[at + 2]};
    };
    std::size_t moved = 0;
    for (int32_t y = std::max(0, layout.top);
         y < std::min(layout.top + layout.height, static_cast<int32_t>(first.height));
         ++y)
        for (int32_t x = std::max(0, layout.left);
             x < std::min(layout.left + layout.width, static_cast<int32_t>(first.width));
             ++x)
            moved += pixel_at(first, x, y) != pixel_at(second, x, y) ? 1U : 0U;
    if (moved != 0)
        failures.push_back(
            std::to_string(moved) +
            " pixels of the megamap changed with the main view: it draws the view's rectangle"
        );
    const auto area = overlay_area();
    const int32_t area_left = area.x - match_layout_.battlefield_x();
    const int32_t area_top = area.y - match_layout_.battlefield_y();
    std::optional<std::array<int32_t, 2>> bar;
    if (layout.left > area_left)
        bar = std::array<int32_t, 2>{(area_left + layout.left) / 2, layout.top + layout.height / 2};
    else if (layout.top + layout.height < area_top + area.height)
        bar = std::array<int32_t, 2>{
            layout.left + layout.width / 2,
            (layout.top + layout.height + area_top + area.height) / 2
        };
    if (bar) {
        const auto pal = static_cast<std::size_t>(megamap_bar_color) * 4U;
        const std::array<uint8_t, 3> grey{
            match_palette_[pal], match_palette_[pal + 1], match_palette_[pal + 2]
        };
        if (pixel_at(second, (*bar)[0], (*bar)[1]) != grey)
            failures.push_back("the bars beside the megamap's picture are not the dark grey");
    }
    set_megamap_open(false);
    if (!failures.empty()) {
        std::string text;
        for (const auto& failure : failures)
            text += "\n  " + failure;
        fail("the megamap's picture differs from what it should draw:" + text);
    }
    std::cout << "megamap click check: the picture keeps to the map's colours, draws " << pictured
              << " of the map's indestructible features by their pictures and " << left_alone
              << " others not at all, and draws no rectangle for the main view"
              << (bar ? ", beside dark grey bars\n" : "\n");
}

} // namespace oa::app
