// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The two interface types' pointer buttons, the queued-order cancel and the
// factory queue's right click, then the on-screen unit list, the pointer's
// pick and what they drive, through synthetic SDL input.
#include "oa/app/runtime.hpp"
#include "oa/sim/messages.hpp"
#include "oa/ui/hud/chat_panel.hpp"
#include "oa/ui/hud/order_overlays.hpp"
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
#include <variant>
#include <vector>

namespace oa::app {
namespace {

namespace input = oa::sim::gameplay_input;

// Canvas pixels from the Peewee to the open-ground points it is sent to, and
// the nudge a second click is given to stay within the 16-pixel cancel reach.
constexpr float kPointOffset = 96.0F;
constexpr float kCancelNudge = 4.0F;
// Pixels of the game's screen the pointer travels during mouse look: four to
// a cell, and clear of a canvas rounding either way.
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
        update_pointer(x, y);
        if (!point || hovered_match_unit_ != 0)
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
    const auto travel = game_screen_canvas(anchor_x + kLookTravel, anchor_y);
    move_to(static_cast<float>(travel.x), static_cast<float>(travel.y));
    const auto looked = (kLookTravel / 4 + look_from / 16) * 16;
    require(
        match_camera_x_ == looked && static_cast<int32_t>(world.game.camera_x) == looked,
        "mouse look did not move the view by the pointer's travel"
    );
    // The pointer was warped back to the anchor; it comes up there.
    const auto anchor = game_screen_canvas(anchor_x, anchor_y);
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
    check_pointer_picks();
}

} // namespace oa::app

namespace oa::app {
namespace {

[[noreturn]] void fail_pick(std::string_view what) {
    throw std::runtime_error("pointer pick check: " + std::string(what));
}

void require_pick(bool ok, std::string_view what) {
    if (!ok)
        fail_pick(what);
}

// Mission kinds the pick check reads back from the order queues.
constexpr uint8_t kMoveGroundKind = 26;
constexpr uint8_t kVtolLandingKind = 53;
constexpr uint8_t kVtolUnloadKind = 65;
// A zoom that leaves room to put a unit off screen on any map.
constexpr float kOffScreenZoom = 4.0F;
// Map pixels a test unit keeps from the map's edges and from another.
constexpr int32_t kEdgeMargin = 96;
constexpr int32_t kNearby = 96;
// Game-screen pixels searched around a unit for the pick's test points.
constexpr int32_t kPickReach = 48;
// Fewest pixels a line of text changes in a panel rectangle.
constexpr std::size_t kPanelTextPixels = 8;

} // namespace

void Runtime::check_pointer_picks() {
    namespace selection = oa::sim::selection;
    namespace input = oa::sim::gameplay_input;
    namespace hud = oa::ui::hud;
    auto& world = match_->state();
    auto& game = world.game;
    auto& slots = match_->world().slots;
    const auto local = match_local_player_;
    const auto map_width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto map_height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
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
    require_pick(commander != 0 && enemy_player != local, "found no commander or enemy player");
    const auto type_of = [&](std::string_view name) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, name);
        if (type == 0 || type >= spawn_types_.size())
            fail_pick("lacks " + std::string(name));
        return type;
    };
    const auto spawn = [&](std::string_view name, uint8_t owner, int32_t x, int32_t z) {
        oa::sim::unit_spawn::Request request;
        request.player = owner;
        request.type = type_of(name);
        request.finished = true;
        request.state = kGroundOccupancyState;
        x = std::clamp(x, kEdgeMargin, map_width - kEdgeMargin);
        z = std::clamp(z, kEdgeMargin, map_height - kEdgeMargin);
        request.position = {
            static_cast<uint32_t>(x) << 16,
            static_cast<uint32_t>(
                match_->map_height(static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(z) << 16)
            ) << 16,
            static_cast<uint32_t>(z) << 16
        };
        auto* slot = match_->create(request);
        if (slot == nullptr || slot->unit == nullptr)
            fail_pick("could not spawn " + std::string(name));
        slot->unit->object_present = true;
        // Held fire, so the units the check places do not fight while it
        // steps the match.
        slot->record.flags &= ~OA_UNIT_FLAG_FIRE_ORDER_MASK;
        return slot->unit_index;
    };
    const auto map_x = [&](uint16_t id) {
        return static_cast<int32_t>(slots[id].unit->position[0] >> 16);
    };
    const auto map_z = [&](uint16_t id) {
        return static_cast<int32_t>(slots[id].unit->position[2] >> 16);
    };
    const auto sees = [&](uint16_t id) { return match_->unit_visible(local, id); };
    const auto listed = [&](uint16_t id) {
        return selection::unit_listed(world, on_screen_lists(), id);
    };

    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y, SDL_Keymod mods) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &window_x, &window_y))
            fail_pick(SDL_GetError());
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
    const auto move_to = [&](float x, float y) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y, SDL_KMOD_NONE);
    };
    const auto click = [&](uint8_t button, float x, float y, SDL_Keymod mods = SDL_KMOD_NONE) {
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y, mods);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, button, x, y, mods);
        send(SDL_EVENT_MOUSE_BUTTON_UP, button, x, y, mods);
    };
    const auto key = [&](SDL_Keycode code, SDL_Scancode scancode, SDL_Keymod mods = SDL_KMOD_NONE) {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.key = code;
        event.key.scancode = scancode;
        event.key.mod = mods;
        SDL_SetModState(mods);
        handle_sdl_event(event, running);
        SDL_SetModState(SDL_KMOD_NONE);
    };
    const auto set_zoom = [&](float zoom) {
        match_zoom_ = zoom;
        match_zoom_target_ = zoom;
        zoom_anchored_ = false;
    };
    // A frame: the view held on the map, drawn, and the list it builds.
    const auto frame = [&] { render_match_surface(); };
    const auto look_at = [&](int32_t x, int32_t z) {
        set_camera_position(x - visible_map_width() / 2, z - visible_map_height() / 2, 0);
        frame();
    };
    const auto centre_on = [&](uint16_t id) { look_at(map_x(id), map_z(id)); };
    const auto canvas_of = [&](uint16_t id) {
        const auto viewport = live_viewport(
            static_cast<uint32_t>(std::max(0, match_camera_x_)),
            static_cast<uint32_t>(std::max(0, match_camera_z_))
        );
        const auto point = project_match_point(viewport, slots[id].unit->position);
        return std::pair{static_cast<float>(point.x), static_cast<float>(point.y)};
    };
    // The view away from a unit: zoomed in and on the far side of the map.
    const auto look_away_from = [&](uint16_t id) {
        set_zoom(kOffScreenZoom);
        look_at(
            map_x(id) < map_width / 2 ? map_width : 0, map_z(id) < map_height / 2 ? map_height : 0
        );
    };
    const auto selected = [&](uint16_t id) {
        return (slots[id].unit->flags & OA_UNIT_FLAG_SELECTED) != 0;
    };
    const auto queue_kinds = [&](uint16_t id) {
        std::vector<uint8_t> kinds;
        match_->visit_primary_queue(id, [&](const auto& view) { kinds.push_back(view.kind); });
        return kinds;
    };
    const auto clear_panels = [&] {
        clear_local_selection();
        reset_match_command();
        pending_build_type_ = 0;
        apply_match_hud_for_selection();
    };
    const auto commander_x = map_x(commander);
    const auto commander_z = map_z(commander);
    // Frames of the unit panel and the unit info panel go to the reports.
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    const auto snapshot = [&](const char* name) {
        renderer::Surface composed;
        render_match_surface();
        compose_match_frame(composed);
        write_ppm(report_directory / name, composed);
    };
    // Open ground beside the commander: east, west, south and north of it.
    const auto beside = [&](int32_t dx, int32_t dz) {
        return std::pair{commander_x + dx, commander_z + dz};
    };
    clear_panels();
    set_zoom(kDefaultBattlefieldZoom);
    game.interface_type = input::interface_left_click;

    // The on-screen list: a unit on screen is listed and leaves it as the view
    // scrolls away; an enemy out of sight or cloaked is never listed.
    const auto [runner_x, runner_z] = beside(3 * kNearby, 0);
    const auto runner = spawn("ARMPW", local, runner_x, runner_z);
    centre_on(runner);
    require_pick(listed(runner), "a local unit centred on screen is not listed");
    look_away_from(runner);
    require_pick(!listed(runner), "a unit scrolled off screen stays listed");
    set_zoom(kDefaultBattlefieldZoom);
    const auto far_x = commander_x < map_width / 2 ? map_width - kEdgeMargin : kEdgeMargin;
    const auto far_z = commander_z < map_height / 2 ? map_height - kEdgeMargin : kEdgeMargin;
    const auto hidden = spawn("CORAK", enemy_player, far_x, far_z);
    require_pick(!sees(hidden), "the far enemy is in sight");
    centre_on(hidden);
    require_pick(!listed(hidden), "an enemy out of sight is listed");
    const auto [hidden_x, hidden_y] = canvas_of(hidden);
    move_to(hidden_x, hidden_y);
    require_pick(
        hovered_match_unit_ == 0 && game.cursor_unit_id == 0,
        "the pointer picked an enemy out of sight"
    );
    // A click over it with a unit selected moves there rather than attacks.
    match_->stop_orders(runner);
    clear_local_selection();
    adopt_selection(runner);
    selected_match_unit_ = runner;
    click(SDL_BUTTON_LEFT, hidden_x, hidden_y);
    const auto kinds = queue_kinds(runner);
    require_pick(
        !kinds.empty() && kinds.front() == kMoveGroundKind,
        "a click over an enemy out of sight did not move the selection"
    );
    match_->stop_orders(runner);
    clear_panels();
    const auto [seen_x, seen_z] = beside(kNearby, kNearby);
    const auto seen = spawn("CORAK", enemy_player, seen_x, seen_z);
    const auto [cloaked_x, cloaked_z] = beside(-kNearby, kNearby);
    const auto cloaked = spawn("CORAK", enemy_player, cloaked_x, cloaked_z);
    slots[cloaked].record.state_flags =
        static_cast<uint8_t>(slots[cloaked].record.state_flags | hud::kUnitStateCloaked);
    centre_on(commander);
    require_pick(sees(seen) && listed(seen), "an enemy in sight is not listed");
    require_pick(!listed(cloaked), "a cloaked enemy is listed");
    // A box that overlaps the view's left edge lists its unit, though the
    // unit's centre is off the view.
    const auto lab_type = type_of("ARMLAB");
    const auto* lab_slot = place_finished_structure(lab_type, commander);
    require_pick(lab_slot != nullptr, "found no site for ARMLAB");
    const auto lab = lab_slot->unit_index;
    const auto lab_reach = static_cast<int32_t>(
        static_cast<int16_t>(static_cast<uint32_t>(world.unit_defs[lab_type].bounds_max_x) >> 16)
    );
    require_pick(lab_reach > 2, "ARMLAB's box is too narrow");
    set_camera_position(map_x(lab) + lab_reach / 2, map_z(lab) - visible_map_height() / 2, 0);
    frame();
    require_pick(
        match_camera_x_ == map_x(lab) + lab_reach / 2 && canvas_of(lab).first < match_layout_.left,
        "the view could not be put right of the lab's centre"
    );
    require_pick(listed(lab), "a unit whose box overlaps the view's edge is not listed");

    // The pick turns the root box by the heading: a Goliath across the view
    // is hit where only the turned box lies and missed where only the box
    // unturned, as a pick ignoring the heading took it, lies.
    const auto [tank_x, tank_z] = beside(-3 * kNearby, -2 * kNearby);
    const auto tank = spawn("CORGOL", local, tank_x, tank_z);
    slots[tank].record.heading = 0x4000;
    centre_on(tank);
    auto* tank_instance = match_->instance(tank);
    require_pick(tank_instance != nullptr, "the Goliath has no model");
    input::PickUnit turned;
    turned.id = tank;
    turned.position = {
        slots[tank].record.position.x, slots[tank].record.position.y, slots[tank].record.position.z
    };
    turned.rotation = {0, 0x4000, 0};
    turned.model = &tank_instance->model().model();
    auto unturned = turned;
    unturned.rotation = {};
    const input::Camera camera{
        static_cast<int32_t>(game.camera_x), static_cast<int32_t>(game.camera_y)
    };
    const auto centre = input::project({}, turned.position, camera);
    std::optional<std::pair<int32_t, int32_t>> turned_only;
    std::optional<std::pair<int32_t, int32_t>> unturned_only;
    for (int32_t dy = -kPickReach; dy <= kPickReach; ++dy)
        for (int32_t dx = -kPickReach; dx <= kPickReach; ++dx) {
            const input::ScreenPoint point{centre.x + dx, centre.y + dy};
            const bool hit = input::hits_root_bounds(turned, camera, point);
            const bool plain = input::hits_root_bounds(unturned, camera, point);
            if (hit && !plain && !turned_only)
                turned_only = std::pair{point.x, point.y};
            if (plain && !hit && !unturned_only)
                unturned_only = std::pair{point.x, point.y};
        }
    require_pick(turned_only && unturned_only, "the turned and unturned boxes do not differ");
    const auto pick_at = [&](int32_t gx, int32_t gy) {
        const auto point = game_screen_canvas(gx, gy);
        move_to(static_cast<float>(point.x), static_cast<float>(point.y));
        return hovered_match_unit_;
    };
    require_pick(
        pick_at(turned_only->first, turned_only->second) == tank,
        "a point in the turned box did not pick the Goliath"
    );
    require_pick(
        pick_at(unturned_only->first, unturned_only->second) != tank,
        "a point only in the unturned box picked the Goliath"
    );
    // A Peewee on the Goliath: the smaller wins, whoever owns it.
    const auto small = spawn("ARMPW", enemy_player, tank_x, tank_z);
    centre_on(tank);
    require_pick(sees(small), "the enemy Peewee is out of sight");
    const auto [both_x, both_y] = canvas_of(small);
    move_to(both_x, both_y);
    require_pick(hovered_match_unit_ == small, "the larger unit won the pick");
    // Two of a size: the earlier listed wins.
    const auto twin = spawn("ARMPW", enemy_player, tank_x, tank_z);
    frame();
    move_to(both_x, both_y);
    require_pick(
        hovered_match_unit_ == std::min(small, twin), "a tie did not keep the earlier listed unit"
    );
    // With the pointer still, a unit that walks under it is picked next frame.
    const auto [open_x, open_z] = beside(-3 * kNearby, 2 * kNearby);
    look_at(open_x, open_z);
    const auto open_canvas = game_screen_canvas(
        oa::ui::display_layout::kSourceLeft + open_x - static_cast<int32_t>(game.camera_x),
        oa::ui::display_layout::kSourceTop + open_z - static_cast<int32_t>(game.camera_y) -
            (match_->map_height(
                 static_cast<uint32_t>(open_x) << 16, static_cast<uint32_t>(open_z) << 16
             ) >>
             1)
    );
    move_to(static_cast<float>(open_canvas.x), static_cast<float>(open_canvas.y));
    require_pick(hovered_match_unit_ == 0, "the open ground has a unit on it");
    auto& walker = slots[runner];
    walker.record.position.x = static_cast<int32_t>(static_cast<uint32_t>(open_x) << 16);
    walker.record.position.z = static_cast<int32_t>(static_cast<uint32_t>(open_z) << 16);
    walker.record.position.y =
        match_->map_height(static_cast<uint32_t>(open_x) << 16, static_cast<uint32_t>(open_z) << 16)
        << 16;
    frame();
    pick_cursor_unit(false);
    require_pick(
        hovered_match_unit_ == runner && game.cursor_unit_id == runner,
        "a unit that walked under the still pointer was not picked"
    );

    // Under attack: a unit on screen says nothing, one off screen says it once.
    const auto* runner_def = definition_for(runner);
    const std::string under_attack =
        (runner_def != nullptr ? runner_def->display_name : std::string()) + ": Under Attack";
    const auto notices = [&] {
        for (std::size_t pump = 0; pump < oa::audio::game_audio::AnnouncementQueue::capacity;
             ++pump)
            present_unit_announcements();
        std::size_t count = 0;
        for (const auto& line : match_message_lines())
            count += line == under_attack ? 1 : 0;
        return count;
    };
    match_->stop_orders(runner);
    oa::sim::messages::clear_messages(game);
    (void)notices();
    oa::sim::messages::clear_messages(game);
    centre_on(runner);
    require_pick(listed(runner), "the Peewee is not on screen");
    match_->apply_damage_event(slots[runner], nullptr, 1, 1, 0);
    require_pick(notices() == 0, "a unit on screen said it was under attack");
    look_away_from(runner);
    require_pick(!listed(runner), "the Peewee did not leave the screen");
    match_->apply_damage_event(slots[runner], nullptr, 1, 1, 0);
    require_pick(notices() == 1, "a unit off screen did not say once it was under attack");
    set_zoom(kDefaultBattlefieldZoom);

    // The unit panel: the commander building a solar collector shows its
    // status and the collector; a build button shows its cost line; a blip
    // of an enemy out of sight shows the unidentified line and no bar.
    // The HUD pixels of a panel rectangle and the text reaching out of it:
    // `left` and `right` columns beyond its sides, nine rows of text down.
    const auto hud_pixels = [&](const HudRect& area, int left, int right) {
        frame();
        std::vector<uint8_t> pixels;
        const auto width = static_cast<int>(match_hud_cpu_.width);
        const auto height = static_cast<int>(match_hud_cpu_.height);
        for (int y = area.y; y < area.y + std::max(area.height, 9); ++y)
            for (int x = area.x - left; x < area.x + std::max(area.width, 1) + right; ++x) {
                if (x < 0 || y < 0 || x >= width || y >= height)
                    continue;
                const auto* pixel = match_hud_cpu_.rgb.data() +
                                    (static_cast<std::size_t>(y) * match_hud_cpu_.width +
                                     static_cast<std::size_t>(x)) *
                                        3U;
                pixels.insert(pixels.end(), pixel, pixel + 3);
            }
        return pixels;
    };
    const auto changed = [](const std::vector<uint8_t>& a, const std::vector<uint8_t>& b) {
        std::size_t count = 0;
        for (std::size_t index = 0; index + 2 < a.size() && index + 2 < b.size(); index += 3)
            count +=
                a[index] != b[index] || a[index + 1] != b[index + 1] || a[index + 2] != b[index + 2]
                    ? 1
                    : 0;
        return count;
    };
    constexpr int kTextReach = 40;
    constexpr int kLineReach = 200;
    clear_panels();
    match_->stop_orders(commander);
    const auto solar = type_of("ARMSOLAR");
    const oa::sim::ground_orders::Point site{
        static_cast<int32_t>(static_cast<uint32_t>(commander_x + 2 * kNearby) << 16),
        0,
        static_cast<int32_t>(static_cast<uint32_t>(commander_z - kNearby) << 16)
    };
    (void)match_->issue_mobile_build(commander, solar, site, false);
    for (int step = 0; step < 300; ++step) {
        auto& player = match_->world().players[local];
        player.metal = player.metal_cap;
        player.energy = player.energy_cap;
        step_match_simulation();
        std::array<oa::sim::match_runtime::Match::OrderRecordView, 1> head{};
        if (match_->queue_records(commander, false, head.data(), head.size()) == 1 &&
            head[0].target != 0)
            break;
    }
    centre_on(commander);
    const auto [ground_x, ground_y] = canvas_of(commander);
    move_to(ground_x, ground_y - static_cast<float>(4 * kNearby));
    const auto no_status = hud_pixels(side_hud_.mission_text, kTextReach, kTextReach);
    const auto no_target = hud_pixels(side_hud_.unit_name2, kTextReach, kTextReach);
    const auto [commander_canvas_x, commander_canvas_y] = canvas_of(commander);
    move_to(commander_canvas_x, commander_canvas_y);
    require_pick(hovered_match_unit_ == commander, "the pointer did not pick the commander");
    require_pick(
        changed(no_status, hud_pixels(side_hud_.mission_text, kTextReach, kTextReach)) >=
            kPanelTextPixels,
        "the commander's status was not drawn at MISSIONTEXT"
    );
    require_pick(
        changed(no_target, hud_pixels(side_hud_.unit_name2, kTextReach, kTextReach)) >=
            kPanelTextPixels,
        "the unit the commander builds was not drawn at UNITNAME2"
    );
    snapshot("native-pointer-unit-panel.ppm");
    // A build button on the commander's build page.
    adopt_selection(commander);
    selected_match_unit_ = commander;
    apply_match_hud_for_selection();
    require_pick(match_hud_.has_value(), "has no build page");
    const auto gadget_centre = [&](const oa::ui::gui_layout::Gadget& gadget) {
        const auto point = oa::ui::display_layout::source_to_canvas(
            match_layout_,
            gadget.common.x + gadget.common.width / 2,
            gadget.common.y + gadget.common.height / 2
        );
        return std::pair{static_cast<float>(point.x), static_cast<float>(point.y)};
    };
    std::optional<std::pair<float, float>> build_button;
    std::optional<std::pair<float, float>> other_button;
    for (const auto& gadget : match_hud_->layout.gadgets) {
        if (gadget.common.width <= 0 || gadget.common.height <= 0 || gadget.common.name.empty())
            continue;
        const auto names_unit =
            oa::sim::unit_spawn::find_type_index(spawn_type_names_, gadget.common.name) != 0;
        if (names_unit && !build_button && gadget.common.name != "CORBUILD")
            build_button = gadget_centre(gadget);
        else if (
            !names_unit && !other_button &&
            std::holds_alternative<oa::ui::gui_layout::ButtonFields>(gadget.fields)
        )
            other_button = gadget_centre(gadget);
    }
    require_pick(build_button && other_button, "the build page has no unit and other button");
    move_to(other_button->first, other_button->second);
    require_pick(hovered_.has_value(), "the pointer is not over the other button");
    const auto no_name = hud_pixels(side_hud_.name, 0, kLineReach);
    const auto no_description = hud_pixels(side_hud_.description, 0, kLineReach);
    move_to(build_button->first, build_button->second);
    require_pick(hovered_gadget_name() != nullptr, "the pointer is not over the build button");
    require_pick(
        changed(no_name, hud_pixels(side_hud_.name, 0, kLineReach)) >= kPanelTextPixels,
        "the build button's cost line was not drawn at NAME"
    );
    require_pick(
        changed(no_description, hud_pixels(side_hud_.description, 0, kLineReach)) >=
            kPanelTextPixels,
        "the build button's description was not drawn at DESCRIPTION"
    );
    clear_panels();
    // An enemy blip on the radar out of sight.
    const auto blip_unit = spawn("CORAK", enemy_player, far_x, far_z - kNearby);
    require_pick(!sees(blip_unit), "the enemy for the blip is in sight");
    centre_on(commander);
    slots[blip_unit].record.flags |= OA_UNIT_FLAG_RADAR_CONTACT;
    compose_radar_final();
    std::optional<std::pair<float, float>> blip;
    for (int32_t index = 0; index < game.hot_radar_unit_count; ++index)
        if (radar_state_.hot_units[static_cast<std::size_t>(index)].unit_id == blip_unit) {
            const auto& hot = radar_state_.hot_units[static_cast<std::size_t>(index)];
            const auto point =
                oa::ui::display_layout::source_to_canvas(match_layout_, hot.x, hot.y);
            blip = std::pair{static_cast<float>(point.x), static_cast<float>(point.y)};
        }
    require_pick(blip.has_value(), "the enemy has no radar blip");
    const auto [open_ground_x, open_ground_y] = canvas_of(commander);
    move_to(open_ground_x, open_ground_y - static_cast<float>(4 * kNearby));
    const auto no_unit_name = hud_pixels(side_hud_.unit_name, kTextReach, kTextReach);
    const auto no_bar = hud_pixels(side_hud_.damage_bar, 0, 0);
    move_to(blip->first, blip->second);
    require_pick(hovered_match_unit_ == blip_unit, "the radar pick missed the enemy's blip");
    require_pick(
        changed(no_unit_name, hud_pixels(side_hud_.unit_name, kTextReach, kTextReach)) >=
            kPanelTextPixels,
        "the blip drew no unidentified line at UNITNAME"
    );
    require_pick(
        changed(no_bar, hud_pixels(side_hud_.damage_bar, 0, 0)) == 0,
        "the unidentified blip drew a damage bar"
    );

    // The feature line: with no unit under the cursor, the feature on the
    // ground under the pointer shows its description and metal at NAME.
    clear_panels();
    constexpr int32_t kCellPixels = 16;
    const auto cell_centre = [&](int32_t cell) { return cell * kCellPixels + kCellPixels / 2; };
    const auto ground_canvas = [&](int32_t x, int32_t z) {
        const auto point = game_screen_canvas(
            oa::ui::display_layout::kSourceLeft + x - static_cast<int32_t>(game.camera_x),
            oa::ui::display_layout::kSourceTop + z - static_cast<int32_t>(game.camera_y) -
                (match_->map_height(
                     static_cast<uint32_t>(x) << 16, static_cast<uint32_t>(z) << 16
                 ) >>
                 1)
        );
        return std::pair{static_cast<float>(point.x), static_cast<float>(point.y)};
    };
    std::optional<std::pair<int32_t, int32_t>> feature_cell;
    uint16_t feature_type = 0;
    const auto first_cell_x =
        std::clamp(commander_x - 3 * kNearby, 3 * kEdgeMargin, map_width - kEdgeMargin) /
        kCellPixels;
    const auto first_cell_z =
        std::clamp(commander_z + 3 * kNearby, kEdgeMargin, map_height - 3 * kEdgeMargin) /
        kCellPixels;
    const int32_t game_feature_defs = game.feature_def_count;
    const auto shown_types = std::min<uint32_t>(
        world.feature_def_count, static_cast<uint32_t>(std::max(0, game_feature_defs))
    );
    for (uint32_t type = 0; type < shown_types && !feature_cell; ++type) {
        const auto& def = world.feature_defs[type];
        constexpr uint16_t kNameOnlyOrHidden =
            OA_FEATURE_FLAG_INDESTRUCTIBLE | OA_FEATURE_FLAG_NO_DISPLAY_INFO;
        if (def.description[0] == '\0' || def.metal < 1.0F ||
            (def.flags & kNameOnlyOrHidden) != 0 || def.footprint_x != 1 || def.footprint_z != 1)
            continue;
        const std::string name(def.name, strnlen(def.name, sizeof def.name));
        for (int32_t step = 0; step < 8 && !feature_cell; ++step)
            if (console_place_feature(name.c_str(), first_cell_x - 2 * step, first_cell_z)) {
                feature_cell = std::pair{first_cell_x - 2 * step, first_cell_z};
                feature_type = static_cast<uint16_t>(type);
            }
    }
    require_pick(feature_cell.has_value(), "found no site for a feature with metal");
    const auto feature_x = cell_centre(feature_cell->first);
    const auto feature_z = cell_centre(feature_cell->second);
    look_at(feature_x, feature_z);
    // Bare ground a few cells away, and the name line there.
    std::optional<std::pair<float, float>> bare;
    for (int32_t offset = 3; offset < 12 && !bare; ++offset) {
        const auto [bare_x, bare_y] = ground_canvas(feature_x, feature_z + offset * kCellPixels);
        move_to(bare_x, bare_y);
        if (game.cursor_unit_id == 0 && game.cursor_feature >= OA_PLOT_FEATURE_RESERVED)
            bare = std::pair{bare_x, bare_y};
    }
    require_pick(bare.has_value(), "found no bare ground beside the feature");
    const auto no_feature_line = hud_pixels(side_hud_.name, 0, kLineReach);
    const auto [feature_canvas_x, feature_canvas_y] = ground_canvas(feature_x, feature_z);
    move_to(feature_canvas_x, feature_canvas_y);
    require_pick(
        game.cursor_unit_id == 0 && game.cursor_feature == feature_type,
        "the pointer over the feature did not make it the cursor feature"
    );
    require_pick(
        changed(no_feature_line, hud_pixels(side_hud_.name, 0, kLineReach)) >= kPanelTextPixels,
        "the feature under the cursor was not named at NAME"
    );
    (void)console_burn_feature(feature_cell->first, feature_cell->second, true);

    // F1: the unit info panel of the unit under the cursor, with its picture.
    centre_on(commander);
    const auto [info_x, info_y] = canvas_of(commander);
    move_to(info_x, info_y - static_cast<float>(4 * kNearby));
    key(SDLK_F1, SDL_SCANCODE_F1);
    require_pick(!unit_info_panel_, "F1 over nothing opened the unit info panel");
    move_to(info_x, info_y);
    key(SDLK_F1, SDL_SCANCODE_F1, SDL_KMOD_LSHIFT);
    require_pick(
        !unit_info_panel_ && game.pinned_unit_a_valid != 0 && game.pinned_unit_a == commander,
        "Shift+F1 did not pin the commander alone"
    );
    key(SDLK_F1, SDL_SCANCODE_F1);
    require_pick(
        unit_info_panel_.has_value() && (game.frame_flags & hud::kFrameUnitInfoOpen) != 0,
        "F1 over the commander did not open the unit info panel"
    );
    const auto* opened = &*unit_info_panel_;
    key(SDLK_F1, SDL_SCANCODE_F1);
    require_pick(&*unit_info_panel_ == opened, "a second F1 opened the panel again");
    const auto* commander_def = oa::world_unit_def_of(&world, &slots[commander].record);
    require_pick(commander_def != nullptr, "the commander has no type");
    const auto picture = oa::decode_pcx(
        assets_
            .read(
                "unitpics/" +
                std::string(commander_def->unit_name, strnlen(commander_def->unit_name, 32)) +
                ".PCX"
            )
            .bytes
    );
    const oa::ui::gui_layout::Gadget* hotr = nullptr;
    const oa::ui::gui_layout::Gadget* done = nullptr;
    for (const auto& gadget : unit_info_panel_->screen->layout.gadgets) {
        if (gadget.common.name == "HOTR")
            hotr = &gadget;
        if (gadget.common.name == "DONE")
            done = &gadget;
    }
    require_pick(hotr != nullptr && done != nullptr, "the panel has no HOTR or DONE");
    std::size_t matching = 0;
    const auto& drawn = unit_info_panel_->frame;
    for (uint32_t row = 0; row < picture.height; ++row)
        for (uint32_t column = 0; column < picture.width; ++column) {
            const auto x = static_cast<uint32_t>(hotr->common.x) + column;
            const auto y = static_cast<uint32_t>(hotr->common.y) + row;
            const auto at = static_cast<std::size_t>(row) * picture.width + column;
            if (x >= drawn.width || y >= drawn.height || at >= picture.indices.size())
                continue;
            const auto* pixel =
                drawn.rgb.data() + (static_cast<std::size_t>(y) * drawn.width + x) * 3U;
            const auto entry = static_cast<std::size_t>(picture.indices[at]) * 4U;
            matching += pixel[0] == match_palette_[entry] &&
                                pixel[1] == match_palette_[entry + 1] &&
                                pixel[2] == match_palette_[entry + 2]
                            ? 1
                            : 0;
        }
    require_pick(
        picture.width > 0 && matching == static_cast<std::size_t>(picture.width) * picture.height,
        "the HOTR pixels are not the unit's picture"
    );
    snapshot("native-pointer-unit-info.ppm");
    const auto done_canvas = oa::ui::display_layout::source_to_canvas(
        match_layout_,
        done->common.x + done->common.width / 2,
        done->common.y + done->common.height / 2
    );
    click(SDL_BUTTON_LEFT, static_cast<float>(done_canvas.x), static_cast<float>(done_canvas.y));
    require_pick(
        !unit_info_panel_ && (game.frame_flags & hud::kFrameUnitInfoOpen) == 0,
        "DONE did not close the unit info panel"
    );
    move_to(info_x, info_y);
    key(SDLK_F1, SDL_SCANCODE_F1);
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require_pick(!unit_info_panel_, "Escape did not close the unit info panel");

    // 'n' centres on the next unvisited local unit and selects nothing; the
    // units then on screen are visited; with every unit visited the cycle
    // starts again. Zoomed in, the local units do not share one view.
    clear_panels();
    set_zoom(kOffScreenZoom);
    selection::clear_cycle_marks(world);
    std::size_t live_local = 0;
    for (const auto& slot : slots)
        live_local +=
            slot.unit != nullptr && slot.record.type_index != 0 && slot.record.owner_index == local
                ? 1
                : 0;
    adopt_selection(commander);
    selected_match_unit_ = commander;
    std::vector<uint16_t> cycle;
    for (std::size_t press = 0; press <= live_local + 1; ++press) {
        key(SDLK_N, SDL_SCANCODE_N);
        const auto next = game.cycle_unit_id;
        require_pick(next != 0, "'n' reached no unit");
        require_pick(
            selected(commander) && selected_match_unit_ == commander, "'n' changed the selection"
        );
        frame();
        require_pick(listed(next), "'n' did not centre on the unit it reached");
        if (std::find(cycle.begin(), cycle.end(), next) != cycle.end()) {
            require_pick(next == cycle.front(), "'n' came back to a visited unit");
            break;
        }
        cycle.push_back(next);
    }
    require_pick(cycle.size() >= 2, "'n' did not step through the units");
    set_zoom(kDefaultBattlefieldZoom);

    // Ctrl+S selects the local units on screen, the lab whose centre is off
    // the view among them, and resets an armed command; with none on screen
    // it leaves the command armed.
    clear_panels();
    adopt_selection(runner);
    set_camera_position(map_x(lab) + lab_reach / 2, map_z(lab) - visible_map_height() / 2, 0);
    frame();
    match_command_ = MatchCommand::patrol;
    key(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_LCTRL);
    require_pick(selected(lab), "Ctrl+S did not select the lab at the view's edge");
    require_pick(!selected(runner) || listed(runner), "Ctrl+S kept a unit off screen selected");
    require_pick(!selected(seen), "Ctrl+S selected an enemy");
    require_pick(match_command_ == MatchCommand::none, "Ctrl+S kept the armed command");
    look_away_from(runner);
    bool local_on_screen = false;
    for (const auto& slot : slots)
        local_on_screen |= slot.unit != nullptr && slot.record.type_index != 0 &&
                           slot.record.owner_index == local && listed(slot.unit_index);
    if (!local_on_screen) {
        match_command_ = MatchCommand::patrol;
        key(SDLK_S, SDL_SCANCODE_S, SDL_KMOD_LCTRL);
        require_pick(!has_local_selection(), "Ctrl+S kept units off screen selected");
        require_pick(
            match_command_ == MatchCommand::patrol,
            "Ctrl+S with nothing on screen reset the command"
        );
        reset_match_command();
    }
    set_zoom(kDefaultBattlefieldZoom);

    // A click selects and visits the local units on screen for 'n'; shift
    // toggles.
    clear_panels();
    set_zoom(kOffScreenZoom);
    selection::clear_cycle_marks(world);
    centre_on(commander);
    const auto [select_x, select_y] = canvas_of(commander);
    // The speech queued so far is presented first, so that the click's line
    // is the one read back.
    for (std::size_t pump = 0; pump < oa::audio::game_audio::AnnouncementQueue::capacity; ++pump)
        (void)offline_services_.pump_announcements();
    click(SDL_BUTTON_LEFT, select_x, select_y);
    require_pick(selected(commander), "a click did not select the commander");
    // The unit selected alone speaks its selection line once.
    std::size_t select_lines = 0;
    for (std::size_t pump = 0; pump < oa::audio::game_audio::AnnouncementQueue::capacity; ++pump)
        for (const auto& event : offline_services_.pump_announcements())
            select_lines +=
                event.unit_index == commander &&
                        event.category == oa::audio::game_audio::UnitAnnouncementCategory::select
                    ? 1
                    : 0;
    require_pick(select_lines == 1, "a click-selected unit did not speak its selection line once");
    std::vector<uint16_t> visited;
    for (const auto& slot : slots)
        if (slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == local && listed(slot.unit_index)) {
            require_pick(
                (slot.record.flags & OA_UNIT_FLAG_CYCLE_VISITED) != 0,
                "a click did not visit a local unit on screen"
            );
            visited.push_back(slot.unit_index);
        }
    if (visited.size() < live_local) {
        key(SDLK_N, SDL_SCANCODE_N);
        const uint16_t cycled = game.cycle_unit_id;
        require_pick(
            std::find(visited.begin(), visited.end(), cycled) == visited.end(),
            "'n' after a click reached a unit that was on screen"
        );
    }
    set_zoom(kDefaultBattlefieldZoom);
    centre_on(commander);
    const auto [shift_x, shift_y] = canvas_of(lab);
    click(SDL_BUTTON_LEFT, shift_x, shift_y, SDL_KMOD_LSHIFT);
    require_pick(selected(lab) && selected(commander), "a shift click did not add the lab");
    click(SDL_BUTTON_LEFT, shift_x, shift_y, SDL_KMOD_LSHIFT);
    require_pick(!selected(lab) && selected(commander), "a second shift click kept the lab");

    // Escape takes back an armed build and keeps the builder, then drops the
    // selection, then opens nothing; with the menu open it closes it.
    clear_panels();
    adopt_selection(commander);
    selected_match_unit_ = commander;
    apply_match_hud_for_selection();
    match_command_ = MatchCommand::build;
    pending_build_type_ = solar;
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require_pick(
        match_command_ == MatchCommand::none && pending_build_type_ == 0 && selected(commander),
        "Escape did not take back the armed build alone"
    );
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require_pick(!has_local_selection(), "a second Escape kept the selection");
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require_pick(!match_paused_, "a third Escape opened the menu");
    key(SDLK_F2, SDL_SCANCODE_F2);
    require_pick(match_paused_, "F2 did not open the menu");
    key(SDLK_ESCAPE, SDL_SCANCODE_ESCAPE);
    require_pick(!match_paused_, "Escape did not close the menu");

    // LOAD, UNLOAD and a pad through the order table.
    clear_panels();
    const auto [atlas_x, atlas_z] = beside(2 * kNearby, 2 * kNearby);
    const auto atlas = spawn("ARMATLAS", local, atlas_x, atlas_z);
    const auto stump = spawn("ARMSTUMP", local, atlas_x + kNearby / 2, atlas_z);
    const auto big_name = oa::sim::unit_spawn::find_type_index(spawn_type_names_, "CORKROG") != 0
                              ? "CORKROG"
                              : "CORGOL";
    const auto big = spawn(big_name, local, atlas_x, atlas_z + kNearby);
    match_->stop_orders(atlas);
    const oa::sim::ground_orders::Point away{
        static_cast<int32_t>(static_cast<uint32_t>(atlas_x - kNearby) << 16),
        0,
        static_cast<int32_t>(static_cast<uint32_t>(atlas_z) << 16)
    };
    match_->issue_ground_move(stump, away, false);
    centre_on(big);
    adopt_selection(atlas);
    adopt_selection(stump);
    selected_match_unit_ = atlas;
    match_command_ = MatchCommand::load;
    const auto [big_x, big_y] = canvas_of(big);
    click(SDL_BUTTON_LEFT, big_x, big_y);
    require_pick(hovered_match_unit_ == big, "the pointer did not pick the large unit");
    require_pick(queue_kinds(atlas).empty(), "LOAD over a unit too large queued an order");
    const auto stump_kinds = queue_kinds(stump);
    require_pick(
        !stump_kinds.empty() && stump_kinds.front() == kMoveGroundKind,
        "LOAD replaced the tank's move"
    );
    require_pick(match_command_ == MatchCommand::load, "LOAD over nothing it applies to disarmed");
    match_command_ = MatchCommand::unload;
    click(SDL_BUTTON_LEFT, big_x, big_y + static_cast<float>(kNearby));
    const auto atlas_kinds = queue_kinds(atlas);
    require_pick(
        !atlas_kinds.empty() && atlas_kinds.front() == kVtolUnloadKind,
        "UNLOAD did not give the Atlas VTOL_Unload"
    );
    require_pick(queue_kinds(stump).front() == kMoveGroundKind, "UNLOAD gave the tank an order");
    reset_match_command();
    match_->stop_orders(atlas);
    // A right press on an allied air pad in the right-click interface lands.
    const auto* pad_slot = place_finished_structure(type_of("ARMASP"), commander);
    require_pick(pad_slot != nullptr, "found no site for ARMASP");
    const auto pad = pad_slot->unit_index;
    clear_panels();
    adopt_selection(atlas);
    selected_match_unit_ = atlas;
    game.interface_type = input::interface_right_click;
    centre_on(pad);
    const auto [pad_x, pad_y] = canvas_of(pad);
    click(SDL_BUTTON_RIGHT, pad_x, pad_y);
    game.interface_type = input::interface_left_click;
    const auto landing = queue_kinds(atlas);
    require_pick(
        !landing.empty() && landing.front() == kVtolLandingKind,
        "a right press on an allied pad did not land the Atlas"
    );
    match_->stop_orders(atlas);
    clear_panels();
    std::cout << "pointer pick check: on-screen list (sight, cloak, box at the edge), turned root "
                 "box, smaller unit and tie, still pointer, under-attack only off screen, unit "
                 "panel status, target, build button and unidentified blip, F1 panel and its "
                 "picture, 'n', Ctrl+S, click visits, Escape, LOAD/UNLOAD and pad landing\n";
}

} // namespace oa::app
