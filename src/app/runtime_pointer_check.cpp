// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The two interface types' pointer buttons, the queued-order cancel and the
// factory queue's right click, driven through synthetic SDL input.
#include "oa/app/runtime.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

namespace input = oa::sim::gameplay_input;

// Canvas pixels from the Peewee to the open-ground points it is sent to, and
// the nudge a second click is given to stay within the 16-pixel cancel reach.
constexpr float kPointOffset = 96.0F;
constexpr float kCancelNudge = 4.0F;
// Source pixels the pointer travels during mouse look: four to a cell, and
// clear of a canvas rounding either way.
constexpr int32_t kLookTravel = 66;
// Rings of 16-pixel cells searched around the commander for a solar site and
// for the lab's site.
constexpr int32_t kSiteNearest = 6;
constexpr int32_t kSiteFarthest = 40;

[[noreturn]] void fail(std::string_view what) {
    throw std::runtime_error("pointer interface check: " + std::string(what));
}

void require(bool ok, std::string_view what) {
    if (!ok)
        fail(what);
}

} // namespace

void Runtime::check_pointer_interfaces() {
    namespace orders = oa::sim::match_runtime;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    start_benchmark_skirmish();
    apply_output_mode();
    auto& world = match_->state();
    auto& slots = match_->world().slots;
    uint16_t commander = 0;
    for (const auto& slot : slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            break;
        }
    require(commander != 0, "found no local commander");
    const auto spawn = [&](std::string_view name, int32_t dx, int32_t dz) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0)
            fail("lacks " + std::string(name));
        const auto x = static_cast<int32_t>(slots[commander].unit->position[0] >> 16) + dx;
        const auto z = static_cast<int32_t>(slots[commander].unit->position[2] >> 16) + dz;
        oa::sim::unit_spawn::Request request;
        request.player = match_local_player_;
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
        return slot->unit_index;
    };
    const auto peewee = spawn("ARMPW", 64, 0);

    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y, SDL_Keymod mods) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &window_x, &window_y))
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
        SDL_SetModState(mods);
        dispatch_event(event, running);
        SDL_SetModState(SDL_KMOD_NONE);
    };
    const auto move_to = [&](float x, float y, SDL_Keymod mods = SDL_KMOD_NONE) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y, mods);
    };
    const auto press = [&](uint8_t button, float x, float y, SDL_Keymod mods = SDL_KMOD_NONE) {
        move_to(x, y, mods);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, button, x, y, mods);
    };
    const auto release = [&](uint8_t button, float x, float y, SDL_Keymod mods = SDL_KMOD_NONE) {
        send(SDL_EVENT_MOUSE_BUTTON_UP, button, x, y, mods);
    };
    const auto click = [&](uint8_t button, float x, float y, SDL_Keymod mods = SDL_KMOD_NONE) {
        press(button, x, y, mods);
        release(button, x, y, mods);
    };
    const auto set_interface = [&](int32_t type) { world.game.interface_type = type; };
    const auto viewport = [&] {
        return live_viewport(
            static_cast<uint32_t>(std::max(0, match_camera_x_)),
            static_cast<uint32_t>(std::max(0, match_camera_z_))
        );
    };
    const auto screen_of = [&](uint16_t id) {
        const auto point = project_match_point(viewport(), slots[id].unit->position);
        return std::pair{static_cast<float>(point.x), static_cast<float>(point.y)};
    };
    const auto selected = [&](uint16_t id) {
        return (slots[id].unit->flags & OA_UNIT_FLAG_SELECTED) != 0;
    };
    const auto queue_of = [&](uint16_t id) {
        std::vector<orders::Match::QueuedCommandView> queue;
        match_->visit_primary_queue(id, [&](const auto& view) { queue.push_back(view); });
        return queue;
    };
    const auto open_ground = [&](float x, float y) {
        const auto point = match_world_point(x, y);
        if (!point || !pick_match_units(x, y).empty())
            fail("found no open ground at a test point");
        return *point;
    };
    const auto moves_to = [&](uint16_t id, std::vector<oa::sim::ground_orders::Point> points) {
        const auto queue = queue_of(id);
        if (queue.size() != points.size())
            return false;
        for (std::size_t index = 0; index < queue.size(); ++index)
            if (queue[index].kind != oa::sim::ground_orders::move_ground_kind ||
                queue[index].destination != points[index])
                return false;
        return true;
    };
    // A frame clamps the view to the map and binds it to the Game block.
    const auto centre_on = [&](uint16_t id) {
        center_camera_on_unit(id);
        render_match_surface();
    };
    const auto select_only = [&](uint16_t id) {
        centre_on(id);
        const auto [x, y] = screen_of(id);
        click(SDL_BUTTON_LEFT, x, y);
        require(selected(id) && selected_match_unit_ == id, "a left click did not select the unit");
    };
    const auto radar_centre = [&] {
        return std::pair{
            static_cast<float>(radar_picture_.x + radar_picture_.width / 2),
            static_cast<float>(radar_picture_.y + radar_picture_.height / 2)
        };
    };

    // Left-click interface.
    set_interface(input::interface_left_click);
    match_->stop_orders(peewee);
    select_only(peewee);
    auto [px, py] = screen_of(peewee);
    const auto a = open_ground(px + kPointOffset, py);
    const auto b = open_ground(px, py + kPointOffset);
    click(SDL_BUTTON_LEFT, px + kPointOffset, py);
    require(
        moves_to(peewee, {a}) && selected(peewee),
        "left-click interface: a left click on open ground did not move the Peewee there"
    );
    click(SDL_BUTTON_LEFT, px, py + kPointOffset, SDL_KMOD_LSHIFT);
    require(moves_to(peewee, {a, b}), "left-click interface: a shift click did not queue a move");
    click(SDL_BUTTON_LEFT, px + kCancelNudge, py + kPointOffset + kCancelNudge, SDL_KMOD_LSHIFT);
    require(
        moves_to(peewee, {a}),
        "left-click interface: a shift click on the queued point did not take it back"
    );
    click(SDL_BUTTON_RIGHT, px, py + kPointOffset);
    require(
        !selected(peewee) && !has_local_selection() && moves_to(peewee, {a}),
        "left-click interface: a right press did not deselect without an order"
    );

    select_only(peewee);
    std::tie(px, py) = screen_of(peewee);
    match_command_ = MatchCommand::patrol;
    click(SDL_BUTTON_RIGHT, px, py + kPointOffset);
    require(
        match_command_ == MatchCommand::none && selected(peewee) && moves_to(peewee, {a}),
        "a right press did not cancel the armed PATROL alone"
    );

    const auto [rx, ry] = radar_centre();
    press(SDL_BUTTON_RIGHT, rx, ry);
    require(
        (input::pointer_flags(world.game) & input::pointer_radar_scroll) != 0,
        "left-click interface: a right press over the radar did not scroll with it"
    );
    move_to(rx, ry);
    const auto scrolled_x = match_camera_x_;
    move_to(rx + static_cast<float>(radar_picture_.width) / 4.0F, ry);
    require(match_camera_x_ > scrolled_x, "the radar scroll did not follow the pointer");
    release(SDL_BUTTON_LEFT, rx, ry);
    require(
        (input::pointer_flags(world.game) & input::pointer_radar_scroll) != 0,
        "the left release ended the right button's radar scroll"
    );
    release(SDL_BUTTON_RIGHT, rx, ry);
    require(
        (input::pointer_flags(world.game) & input::pointer_radar_scroll) == 0 && selected(peewee) &&
            moves_to(peewee, {a}),
        "the right release did not end the radar scroll alone"
    );

    centre_on(peewee);
    std::tie(px, py) = screen_of(peewee);
    press(SDL_BUTTON_RIGHT, px, py + kPointOffset, SDL_KMOD_LCTRL);
    require(
        world.game.mouse_look_active != 0 && selected(peewee),
        "left-click interface: control and a right press did not start mouse look"
    );
    const auto anchor_x = world.game.mouse_look_anchor_x;
    const auto anchor_y = world.game.mouse_look_anchor_y;
    const auto look_from = static_cast<int32_t>(world.game.camera_x);
    const auto travel =
        oa::ui::display_layout::source_to_canvas(match_layout_, anchor_x + kLookTravel, anchor_y);
    move_to(static_cast<float>(travel.x), static_cast<float>(travel.y));
    const auto looked = (kLookTravel / 4 + look_from / 16) * 16;
    require(
        match_camera_x_ == looked && static_cast<int32_t>(world.game.camera_x) == looked,
        "mouse look did not move the view by the pointer's travel"
    );
    // The pointer was warped back to the anchor; it comes up there.
    const auto anchor = oa::ui::display_layout::source_to_canvas(match_layout_, anchor_x, anchor_y);
    release(SDL_BUTTON_RIGHT, static_cast<float>(anchor.x), static_cast<float>(anchor.y));
    require(
        world.game.mouse_look_active == 0 && match_camera_x_ == looked && selected(peewee),
        "the right release did not end mouse look where it was"
    );

    // The commander's MobileBuild: a shift click on its queued site takes it back.
    const auto solar = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMSOLAR");
    require(solar != 0 && solar < spawn_types_.size(), "lacks ARMSOLAR");
    clear_local_selection();
    select_only(commander);
    match_->stop_orders(commander);
    std::optional<PendingBuildSite> site;
    const auto cell_x = static_cast<int32_t>(slots[commander].unit->position[0] >> 20);
    const auto cell_z = static_cast<int32_t>(slots[commander].unit->position[2] >> 20);
    for (int32_t ring = kSiteNearest; ring < kSiteFarthest && !site; ++ring)
        for (int32_t dz = -ring; dz <= ring && !site; dz += 2)
            for (int32_t dx = -ring; dx <= ring && !site; dx += 2) {
                if (dx != -ring && dx != ring && dz != -ring && dz != ring)
                    continue;
                const auto& type = spawn_types_[solar];
                const oa::sim::ground_orders::Point centre{
                    ((cell_x + dx) * 16 + type.footprint_x * 8) << 16,
                    0,
                    ((cell_z + dz) * 16 + type.footprint_z * 8) << 16
                };
                pending_build_type_ = solar;
                if (auto candidate = pending_build_site(centre); candidate && candidate->legal)
                    site = candidate;
            }
    require(site.has_value(), "found no ARMSOLAR site near the commander");
    match_command_ = MatchCommand::build;
    pending_build_type_ = solar;
    centre_on(commander);
    const auto site_screen = project_match_point(
        viewport(),
        {static_cast<uint32_t>(site->world[0]),
         static_cast<uint32_t>(site->world[1]),
         static_cast<uint32_t>(site->world[2])}
    );
    const auto sx = static_cast<float>(site_screen.x);
    const auto sy = static_cast<float>(site_screen.y);
    const auto builds = [&] {
        std::size_t count = 0;
        for (const auto& view : queue_of(commander))
            if (view.kind == orders::mobile_build_kind && view.build_type == solar)
                ++count;
        return count;
    };
    click(SDL_BUTTON_LEFT, sx, sy, SDL_KMOD_LSHIFT);
    require(
        builds() == 1 && match_command_ == MatchCommand::build,
        "a shift click on the site did not queue the commander's MobileBuild"
    );
    click(SDL_BUTTON_LEFT, sx, sy, SDL_KMOD_LSHIFT);
    require(
        builds() == 0 && queue_of(commander).empty(),
        "a shift click on the queued site did not take the MobileBuild back"
    );
    click(SDL_BUTTON_RIGHT, sx, sy);
    require(match_command_ == MatchCommand::none, "a right press did not drop the build command");

    // Right-click interface.
    set_interface(input::interface_right_click);
    clear_local_selection();
    match_->stop_orders(peewee);
    select_only(peewee);
    std::tie(px, py) = screen_of(peewee);
    const auto c = open_ground(px - kPointOffset, py);
    const auto d = open_ground(px, py - kPointOffset);
    click(SDL_BUTTON_LEFT, px - kPointOffset, py);
    require(
        !selected(peewee) && queue_of(peewee).empty(),
        "right-click interface: a left click on open ground did not only deselect"
    );
    select_only(peewee);
    std::tie(px, py) = screen_of(peewee);
    click(SDL_BUTTON_RIGHT, px - kPointOffset, py);
    require(
        moves_to(peewee, {c}) && selected(peewee),
        "right-click interface: a right press on open ground did not move the Peewee there"
    );
    click(SDL_BUTTON_RIGHT, px, py - kPointOffset, SDL_KMOD_LSHIFT);
    require(moves_to(peewee, {c, d}), "right-click interface: a shift right press did not queue");
    click(SDL_BUTTON_RIGHT, px - kCancelNudge, py - kPointOffset + kCancelNudge, SDL_KMOD_LSHIFT);
    require(
        moves_to(peewee, {c}),
        "right-click interface: a shift right press on the queued point did not take it back"
    );
    const auto [cx, cy] = screen_of(commander);
    click(SDL_BUTTON_RIGHT, cx, cy);
    const auto guards = [&] {
        const auto queue = queue_of(peewee);
        return queue.size() == 1 && queue.front().kind == orders::follow_ground_kind;
    };
    require(
        guards() && selected(peewee) && !selected(commander),
        "right-click interface: a right press on the commander did not make the Peewee "
        "guard it"
    );
    click(SDL_BUTTON_RIGHT, cx, cy, SDL_KMOD_LSHIFT);
    require(
        queue_of(peewee).empty(),
        "right-click interface: a shift right press on the guarded commander did not take the "
        "guard back"
    );

    const auto [lx, ly] = radar_centre();
    press(SDL_BUTTON_LEFT, lx, ly);
    require(
        (input::pointer_flags(world.game) & input::pointer_radar_scroll) != 0,
        "right-click interface: a left press over the radar did not scroll with it"
    );
    release(SDL_BUTTON_LEFT, lx, ly);
    require(
        (input::pointer_flags(world.game) & input::pointer_radar_scroll) == 0 && selected(peewee) &&
            queue_of(peewee).empty(),
        "the left release did not end the radar scroll alone"
    );
    const auto radar_point = radar_world_point(lx, ly);
    require(radar_point.has_value(), "found no map point under the radar");
    click(SDL_BUTTON_RIGHT, lx, ly);
    require(
        moves_to(peewee, {*radar_point}),
        "right-click interface: a right press on the radar did not move the Peewee there"
    );
    set_interface(input::interface_left_click);

    // A right click on a factory's build button takes that type off its queue.
    const auto lab = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMLAB");
    const auto builder = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMCK");
    const auto kbot = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "ARMPW");
    require(lab != 0 && builder != 0 && kbot != 0, "lacks ARMLAB, ARMCK or ARMPW");
    std::optional<std::pair<int32_t, int32_t>> lab_site;
    for (int32_t ring = kSiteNearest; ring < kSiteFarthest && !lab_site; ++ring)
        for (int32_t dz = -ring; dz <= ring && !lab_site; dz += 2)
            for (int32_t dx = -ring; dx <= ring && !lab_site; dx += 2)
                if ((dx == -ring || dx == ring || dz == -ring || dz == ring) &&
                    match_->building_site(lab, cell_x + dx, cell_z + dz, 0))
                    lab_site = std::pair{cell_x + dx, cell_z + dz};
    require(lab_site.has_value(), "found no site for ARMLAB");
    const auto& lab_type = spawn_types_[lab];
    const auto factory = spawn(
        "ARMLAB",
        (lab_site->first * 2 + lab_type.footprint_x) * 8 -
            static_cast<int32_t>(slots[commander].unit->position[0] >> 16),
        (lab_site->second * 2 + lab_type.footprint_z) * 8 -
            static_cast<int32_t>(slots[commander].unit->position[2] >> 16)
    );
    clear_local_selection();
    centre_on(factory);
    const auto [fx, fy] = screen_of(factory);
    click(SDL_BUTTON_LEFT, fx, fy);
    require(selected(factory), "a left click did not select the lab");
    const auto click_button = [&](std::string_view name, uint8_t button) {
        require(match_hud_.has_value(), "has no build panel");
        for (const auto& gadget : match_hud_->layout.gadgets) {
            if (gadget.common.name != name)
                continue;
            const auto point = oa::ui::display_layout::source_to_canvas(
                match_layout_,
                gadget.common.x + gadget.common.width / 2,
                gadget.common.y + gadget.common.height / 2
            );
            click(button, static_cast<float>(point.x), static_cast<float>(point.y));
            return;
        }
        fail("found no lab button " + std::string(name));
    };
    click_button("ARMPW", SDL_BUTTON_LEFT);
    click_button("ARMCK", SDL_BUTTON_LEFT);
    require(
        match_->queued_build_count(factory, kbot) == 1 &&
            match_->queued_build_count(factory, builder) == 1,
        "the lab's build buttons did not queue ARMPW then ARMCK"
    );
    click_button("ARMPW", SDL_BUTTON_RIGHT);
    require(
        match_->queued_build_count(factory, kbot) == 0 &&
            match_->queued_build_count(factory, builder) == 1,
        "a right click on ARMPW did not take it off the queue ahead of ARMCK"
    );
    std::cout << "pointer interface check: left-click interface clicks, shift cancels, right "
                 "press deselect/cancel/radar scroll/mouse look and build-site cancel; "
                 "right-click interface deselect, default orders, guard, cancels and radar; "
                 "factory right click took ARMPW off ahead of ARMCK\n";
}

} // namespace oa::app
