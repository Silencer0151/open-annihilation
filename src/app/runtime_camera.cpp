// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Radar interaction, zoom, camera panning and move/patrol orders.
#include "oa/app/runtime.hpp"
#include "oa/app/far_view.hpp"
#include "oa/core/map_plot.h"
#include "engine_settings_state.hpp"
#include "oa/sim/feature_runtime.hpp"
#include "oa/sim/selection.hpp"
#include "oa/platform/memory_status.hpp"
#include "oa/present/world_renderer/world_camera.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <optional>
#include <string>
#include <tuple>

namespace oa::app {

namespace {

// The longest step the zoom eases by, in seconds, however long since the
// last frame.
constexpr float kLongestZoomStep = 0.05F;

// How near the target's logarithm the eased zoom's must come for the zoom
// to take the target: a ten-thousandth of the zoom.
constexpr double kZoomArrival = 1.0e-4;

// The farthest the view's exact place goes from the map's corner, in map
// pixels, whatever moves it: far past any map, and well within a camera's
// whole map pixels.
constexpr double kFarthestViewPlace = 16'777'216.0;

// The wheel steps short of an end of the zoom's range that count as at
// it, well above the rounding of a target to float and below any step a
// trackpad sends.
constexpr double kZoomWheelEndSlack = 1.0e-4;

static_assert(
    oa::ui::engine_settings::closest_zoom(oa::ui::engine_settings::ZoomInLimit::four_times) ==
        kMaxBattlefieldZoom,
    "Maximum zoom in's closest choice is the closest any view zooms in"
);

} // namespace

bool Runtime::radar_contains(float x, float y) const {
    return radar_picture_.width > 0 && radar_picture_.height > 0 &&
           x >= static_cast<float>(radar_picture_.x) && y >= static_cast<float>(radar_picture_.y) &&
           x < static_cast<float>(radar_picture_.x + radar_picture_.width) &&
           y < static_cast<float>(radar_picture_.y + radar_picture_.height);
}

std::optional<oa::sim::ground_orders::Point> Runtime::radar_world_point(float x, float y) {
    if (!radar_contains(x, y) || !selected_tnt_ || radar_map_w_ <= 0 || radar_map_h_ <= 0)
        return std::nullopt;
    const auto rx = static_cast<int>(x) - radar_picture_.x;
    const auto ry = static_cast<int>(y) - radar_picture_.y;
    return map_world_point(
        rx * radar_map_w_ / radar_picture_.width, ry * radar_map_h_ / radar_picture_.height
    );
}

std::optional<oa::sim::ground_orders::Point>
Runtime::map_world_point(int32_t map_x, int32_t map_z) {
    if (!selected_tnt_)
        return std::nullopt;
    const oa::sim::unit_movement::Terrain terrain(*selected_tnt_);
    const auto target = oa::sim::gameplay_input::terrain_intersection(
        terrain,
        map_x,
        map_z,
        static_cast<int32_t>(selected_tnt_->attribute_width * 16U),
        static_cast<int32_t>(selected_tnt_->attribute_height * 16U)
    );
    return oa::sim::ground_orders::Point{target.x, target.y, target.z};
}

uint16_t Runtime::pick_radar_unit(float x, float y) {
    if (!match_ || !radar_contains(x, y) || radar_state_.built_for != &match_->state())
        return 0;
    auto& world = match_->state();
    const auto pointer = game_screen_point(x, y);
    world.game.pointer_state[0] = static_cast<uint32_t>(pointer.x);
    world.game.pointer_state[1] = static_cast<uint32_t>(pointer.y);
    return oa::sim::selection::unit_under_pointer(world, on_screen_lists(), selection_hooks());
}

void Runtime::bind_match_view() {
    namespace wr = oa::present::world_renderer;
    namespace layout = oa::ui::display_layout;
    auto& game = match_->state().game;
    game.camera_x = static_cast<uint32_t>(match_camera_x_);
    game.camera_y = static_cast<uint32_t>(match_camera_z_);
    game.view_cells_width = visible_map_width() / OA_MAP_CELL_PIXELS;
    game.view_cells_height = visible_map_height() / OA_MAP_CELL_PIXELS;
    // The off-screen surface is the screen: the visible battlefield with the
    // side column and bars around it (640x480 unzoomed).
    game.offscreen_width = static_cast<uint32_t>(layout::kSourceLeft + visible_map_width());
    game.offscreen_height =
        static_cast<uint32_t>(layout::kSourceTop + visible_map_height() + layout::kSourceBottom);
    // The game view on the game's screen (game_screen_point): the visible map
    // from (128, 32), which is the 640x480 screen's view unzoomed.
    game.battlefield_rect = oa::Rect32{
        layout::kSourceLeft,
        layout::kSourceTop,
        layout::kSourceLeft + visible_map_width() - 1,
        layout::kSourceTop + visible_map_height() - 1
    };
    oa::Rect32 view{};
    if (wr::radar_view_rect(game, view) &&
        std::memcmp(&view, &game.radar_view_rect, sizeof view) != 0) {
        game.radar_view_rect = view;
        game.radar_blink_flags =
            static_cast<uint16_t>(game.radar_blink_flags | wr::radar_flag_redraw);
    }
}

bool Runtime::issue_radar_orders(float x, float y) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || !radar_contains(x, y))
        return false;
    // The frame's pointer pass at the press: the unit whose dot is under the
    // pointer, the map point under it and the cursor shown there.
    update_pointer(x, y);
    const auto cursor = static_cast<input::OrderCursor>(pick_match_cursor());
    auto& world = match_->state();
    const auto command = input::pointer_command(world.game);
    if (command == input::OrderCommand::build)
        return place_pending_build_on_radar(x, y);
    switch (input::click_action(world, command, cursor)) {
    case input::ClickAction::none:
        return false;
    case input::ClickAction::select_unit:
        select_match_unit(x, y, 1);
        return true;
    case input::ClickAction::clear_selection:
        clear_local_selection();
        apply_match_hud_for_selection();
        return true;
    case input::ClickAction::issue_command:
        break;
    }
    const auto issued =
        issue_selection_orders(command, hovered_match_unit_, radar_world_point(x, y), queueing());
    if (!issued.empty())
        status_ = std::string(issued);
    // The command ends with the press whether or not a unit took an order.
    finish_issued_command();
    return true;
}

bool Runtime::issue_radar_default_order(float x, float y) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || !radar_contains(x, y) || selected_match_unit_ == 0)
        return false;
    update_pointer(x, y);
    // The pick is wanted for what it writes into the Game block; the cursor
    // it returns is not needed.
    std::ignore = pick_match_cursor();
    const auto issued = issue_selection_orders(
        input::OrderCommand::default_order, hovered_match_unit_, radar_world_point(x, y), queueing()
    );
    if (issued.empty())
        return false;
    status_ = std::string(issued);
    return true;
}

bool Runtime::issue_map_orders(
    const std::optional<oa::sim::ground_orders::Point>& world, uint16_t target
) {
    if (!match_ || selected_match_unit_ == 0 || match_command_ != MatchCommand::patrol || !world)
        return false;
    if (issue_selection_patrol(*world, target, queueing()))
        finish_issued_command();
    return true;
}

bool Runtime::pan_camera_from_radar(float x, float y) {
    if (!radar_contains(x, y) || radar_map_w_ <= 0 || radar_map_h_ <= 0)
        return false;
    center_camera_on_radar_point(x, y);
    status_ = "Radar";
    return true;
}

std::array<int32_t, 2> Runtime::shown_map_size() const noexcept {
    namespace features = oa::sim::feature_runtime;
    if (match_) {
        const auto& game = match_->state().game;
        if (game.map_pixel_width > 0 && game.map_pixel_height > 0)
            return {game.map_pixel_width, game.map_pixel_height};
    }
    if (!selected_tnt_)
        return {0, 0};
    const auto width = static_cast<int32_t>(selected_tnt_->tile_width * 32U);
    const auto height = static_cast<int32_t>(selected_tnt_->tile_height * 32U);
    return {
        std::max(0, width - features::hidden_right_edge),
        std::max(0, height - features::hidden_bottom_edge)
    };
}

std::array<uint32_t, 2> Runtime::shown_tile_grid() const noexcept {
    if (!selected_tnt_)
        return {0, 0};
    const auto shown = shown_map_size();
    const auto tiles = [](int32_t pixels, uint32_t most) {
        const auto whole = static_cast<uint32_t>((std::max(pixels, 0) + 31) / 32);
        return std::clamp<uint32_t>(whole, std::min<uint32_t>(1, most), most);
    };
    return {
        tiles(shown[0], selected_tnt_->tile_width), tiles(shown[1], selected_tnt_->tile_height)
    };
}

std::array<int32_t, 2> Runtime::view_camera() const {
    if (!selected_tnt_)
        return {match_camera_x_, match_camera_z_};
    const auto [map_width, map_height] = shown_map_size();
    const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
    const auto from = [this](double centre) {
        return view_hold_.held ? std::optional<double>(centre) : std::nullopt;
    };
    const double share = past_map_edge_share();
    return {
        held_camera(
            match_camera_x_,
            static_cast<double>(match_layout_.battlefield_width()) / zoom,
            static_cast<double>(map_width),
            share,
            from(view_hold_.centre_x)
        ),
        held_camera(
            match_camera_z_,
            static_cast<double>(match_layout_.battlefield_height()) / zoom,
            static_cast<double>(map_height),
            share,
            from(view_hold_.centre_z)
        )
    };
}

std::array<int32_t, 2> Runtime::on_map_camera() const {
    if (!selected_tnt_)
        return {match_camera_x_, match_camera_z_};
    const auto [map_width, map_height] = shown_map_size();
    return {
        std::clamp(match_camera_x_, 0, std::max(0, map_width - visible_map_width())),
        std::clamp(match_camera_z_, 0, std::max(0, map_height - visible_map_height()))
    };
}

std::array<double, 2> Runtime::match_view_place() const {
    if (exact_view_.held && exact_view_.camera_x == match_camera_x_ &&
        exact_view_.camera_z == match_camera_z_)
        return {exact_view_.x, exact_view_.z};
    return {static_cast<double>(match_camera_x_), static_cast<double>(match_camera_z_)};
}

void Runtime::place_match_view(double x, double z) {
    x = std::clamp(x, -kFarthestViewPlace, kFarthestViewPlace);
    z = std::clamp(z, -kFarthestViewPlace, kFarthestViewPlace);
    // A view drawn between map pixels lies past its camera's map pixel;
    // one drawn on whole map pixels is drawn at the nearest.
    const auto camera = [this](double place) {
        return static_cast<int32_t>(view_between_pixels_ ? std::floor(place) : std::round(place));
    };
    exact_view_ = {true, x, z, camera(x), camera(z)};
    match_camera_x_ = exact_view_.camera_x;
    match_camera_z_ = exact_view_.camera_z;
    camera_moved_ = true;
}

oa::present::world_renderer::ViewOffset Runtime::view_offset() const {
    if (!view_between_pixels_ || screen_ != Screen::match || !exact_view_.held)
        return {};
    const auto camera = view_camera();
    if (camera[0] != match_camera_x_ || camera[1] != match_camera_z_ ||
        camera[0] != exact_view_.camera_x || camera[1] != exact_view_.camera_z)
        return {};
    return {
        std::clamp(exact_view_.x - static_cast<double>(camera[0]), 0.0, 1.0),
        std::clamp(exact_view_.z - static_cast<double>(camera[1]), 0.0, 1.0)
    };
}

oa::present::world_renderer::ViewOffset Runtime::settle_view_offset(bool between) {
    // A reader's draw of the standard tier's picture leaves the view alone.
    if (accelerated_.suspended)
        return {};
    view_between_pixels_ = between;
    return view_offset();
}

void Runtime::EngineSettingsState::ease_zoom_about_centre(Runtime& runtime, float target) {
    if (!runtime.match_ || !runtime.selected_tnt_)
        return;
    // The point at the battlefield's centre stays there while the zoom
    // eases; a followed unit, which the follow keeps there, goes on being
    // followed.
    const auto& layout = runtime.match_layout_;
    runtime.zoom_focus_ = {
        false,
        static_cast<float>(layout.left) + static_cast<float>(layout.battlefield_width()) / 2.0F,
        static_cast<float>(layout.top) + static_cast<float>(layout.battlefield_height()) / 2.0F
    };
    runtime.match_zoom_target_ =
        std::clamp(target, runtime.least_match_zoom(), runtime.most_match_zoom());
}

float Runtime::least_match_zoom() const noexcept {
    // Settings not yet read hold their defaults, Automatic among them.
    const auto limit = engine_settings_ ? engine_settings_->current.max_zoom_out
                                        : oa::ui::engine_settings::ZoomOutLimit::automatic;
    const auto [map_width, map_height] = shown_map_size();
    return least_battlefield_zoom(
        limit,
        detail_zoom_floor(),
        map_width,
        map_height,
        match_layout_.battlefield_width(),
        match_layout_.battlefield_height()
    );
}

float Runtime::detail_zoom_floor() const noexcept {
    return full_presentation() ? kMinFullBattlefieldZoom : kMinBattlefieldZoom;
}

bool Runtime::far_view_frame() const noexcept {
    return screen_ == Screen::match && !director_mode() &&
           far_view_zoom(match_zoom_, detail_zoom_floor());
}

bool Runtime::dots_frame() const noexcept {
    namespace settings = oa::ui::engine_settings;
    if (screen_ != Screen::match || director_mode() || !(match_zoom_ > 0.0F))
        return false;
    const settings::EngineSettings chosen =
        engine_settings_ ? engine_settings_->current : settings::EngineSettings{};
    if (chosen.zoomed_out_units == settings::ZoomedOutUnits::dots &&
        match_zoom_ < settings::zoomed_out_zoom(chosen.zoomed_out_after))
        return true;
    // Past the drawing's floor, models whose drawing would take more of the
    // machine's memory than it can spare, or a view whose units drawn reach
    // farther from the camera than the models' places do, are drawn as
    // dots.
    if (!far_view_frame())
        return false;
    const auto zoom = static_cast<double>(match_zoom_);
    const double wide = static_cast<double>(match_layout_.battlefield_width()) / zoom;
    const double high = static_cast<double>(match_layout_.battlefield_height()) / zoom;
    return wide * high * static_cast<double>(kModelBridgeBytesPerMapPixel) >
               static_cast<double>(rendered_units_budget()) ||
           std::max(wide, high) + kFarCullMarginMapPixels > kMostModelOffset;
}

uint64_t Runtime::rendered_units_budget() const noexcept {
    // The machine's physical memory, read once.
    static const uint64_t physical = [] {
        oa::platform::SystemMemorySample sample{};
        return oa::platform::sample_system_memory(&sample) ? sample.physical : uint64_t{0};
    }();
    return physical != 0 ? physical / kRenderedUnitsMemoryShare : kRenderedUnitsUnknownBudget;
}

float Runtime::most_match_zoom() const noexcept {
    namespace settings = oa::ui::engine_settings;
    return settings::closest_zoom(
        engine_settings_ ? engine_settings_->current.max_zoom_in : settings::ZoomInLimit::four_times
    );
}

void Runtime::step_match_zoom() {
    // Each frame of a match: a match that turns out to be shared or a replay
    // goes back to the base path credit (Common Tweaks' pathfinding setting).
    EngineSettingsState::hold_path_credit(*this, current_extension_state());
    // The director sets the zoom of every frame itself, off the wall clock.
    if (director_mode())
        return;
    // The limits move with the tier, the window, the map and the zoom's
    // settings: the target keeps within them, and a view zoomed past them
    // eases back within them about the battlefield's centre.
    const float least = least_match_zoom();
    const float most = most_match_zoom();
    match_zoom_target_ = std::clamp(match_zoom_target_, least, most);
    const auto bf_w = static_cast<float>(match_layout_.battlefield_width());
    const auto bf_h = static_cast<float>(match_layout_.battlefield_height());
    if (match_zoom_ < least || match_zoom_ > most)
        zoom_focus_ = {
            false,
            static_cast<float>(match_layout_.left) + bf_w / 2.0F,
            static_cast<float>(match_layout_.top) + bf_h / 2.0F
        };
    // Seconds since the zoom last eased, from the frames' times; the first
    // frame takes a frame at the full rate, and a long gap counts as
    // kLongestZoomStep.
    float dt = 1.0F / static_cast<float>(kDefaultMaxFramesPerSecond);
    if (zoom_clock_valid_ && frame_time_ns_ >= zoom_clock_) {
        dt = static_cast<float>(
            static_cast<double>(frame_time_ns_ - zoom_clock_) /
            static_cast<double>(frame_pacing::kNanosecondsPerSecond)
        );
        dt = std::clamp(dt, 0.0F, kLongestZoomStep);
    }
    zoom_clock_ = frame_time_ns_;
    zoom_clock_valid_ = true;
    if (match_zoom_ == match_zoom_target_ || !selected_tnt_)
        return;
    // The zoom eases by its logarithm, so that each frame zooms by the same
    // ratio whichever way it goes and however far out it is.
    const double from = std::log(static_cast<double>(match_zoom_ > 0.0F ? match_zoom_ : 1.0F));
    const double to = std::log(static_cast<double>(match_zoom_target_));
    const double eased =
        from + (to - from) * (1.0 - std::exp(-static_cast<double>(kZoomLerpHz * dt)));
    const float zoom = std::abs(to - eased) < kZoomArrival ? match_zoom_target_
                                                           : static_cast<float>(std::exp(eased));
    // The focus where it is this frame: the pointer as it moves, within
    // the battlefield.
    const double focus_x = std::clamp(
        static_cast<double>(zoom_focus_.x) - static_cast<double>(match_layout_.left),
        0.0,
        static_cast<double>(bf_w)
    );
    const double focus_y = std::clamp(
        static_cast<double>(zoom_focus_.y) - static_cast<double>(match_layout_.top),
        0.0,
        static_cast<double>(bf_h)
    );
    zoom_view_about(zoom, focus_x, focus_y);
    // A followed unit, which only the settings dialog's ease and a change
    // of the limits leave followed, stays at the centre as the zoom eases,
    // while a menu holds the match too.
    if (match_tracking_ && match_unit_present(tracked_match_unit_))
        center_camera_on_unit(tracked_match_unit_);
}

void Runtime::zoom_view_about(float zoom, double focus_x, double focus_y) {
    if (!selected_tnt_ || !(zoom > 0.0F))
        return;
    const auto before = static_cast<double>(match_zoom_ > 0.0F ? match_zoom_ : 1.0F);
    const auto after = static_cast<double>(zoom);
    const auto view = match_view_place();
    // The exact map point under the focus, which stays under it.
    const double point_x = view[0] + focus_x / before;
    const double point_z = view[1] + focus_y / before;
    match_zoom_ = zoom;
    const auto [map_width, map_height] = shown_map_size();
    const double visible_x = static_cast<double>(match_layout_.battlefield_width()) / after;
    const double visible_z = static_cast<double>(match_layout_.battlefield_height()) / after;
    const double share = past_map_edge_share();
    // At half the battlefield past the map's edges, as the view always
    // went, the zoom keeps the point under the pointer wherever the view
    // goes; less keeps the view within the limits wherever the point goes.
    namespace settings = oa::ui::engine_settings;
    const bool zoom_past_limits =
        share == settings::past_map_edge_share(settings::ViewPastMapEdge::one_half);
    const auto along = [&](double point, double focus, double visible, int32_t map, double centre) {
        const double place = point - focus / after;
        // A point of the map keeps it in view, wherever the view goes.
        if (zoom_past_limits && point >= 0.0 && point <= static_cast<double>(map))
            return place;
        return held_view(
            place,
            visible,
            static_cast<double>(map),
            share,
            zoom_past_limits && view_hold_.held ? std::optional<double>(centre) : std::nullopt
        );
    };
    const double x = along(point_x, focus_x, visible_x, map_width, view_hold_.centre_x);
    const double z = along(point_z, focus_y, visible_z, map_height, view_hold_.centre_z);
    place_match_view(x, z);
    view_hold_ = {true, x + visible_x / 2.0, z + visible_z / 2.0};
}

void Runtime::handle_match_zoom(
    float wheel_y, float pointer_x, float pointer_y, bool follow_pointer
) {
    if (wheel_y == 0.0F || !selected_tnt_)
        return;
    if (pointer_x < static_cast<float>(match_layout_.left) ||
        pointer_y < static_cast<float>(match_layout_.top) ||
        pointer_x >= static_cast<float>(match_layout_.left + match_layout_.battlefield_width()) ||
        pointer_y >= static_cast<float>(match_layout_.top + match_layout_.battlefield_height()))
        return;
    // A zoom ends a follow, as a scroll does: the view zooms about the
    // point, wherever the followed unit is.
    stop_match_tracking();
    zoom_focus_ = {follow_pointer, pointer_x, pointer_y};
    match_zoom_target_ = wheel_zoom_target(wheel_y);
}

float Runtime::wheel_zoom_target(float wheel_y) {
    // A target something else set since the wheel's last step begins the
    // count again.
    const float least = least_match_zoom();
    const float most = most_match_zoom();
    if (zoom_wheel_.target != match_zoom_target_)
        zoom_wheel_ = {std::clamp(match_zoom_target_, least, most), 0.0, match_zoom_target_};
    const auto from = static_cast<double>(zoom_wheel_.from);
    const auto factor = static_cast<double>(kZoomWheelFactor);
    // The steps in all, so that as many back return the target exactly. A
    // step toward an end counts in whole, though the target stops at the
    // end, but no further than the first whole step at or past it, and a
    // step toward an end the steps are already at counts nothing: the
    // first step back leaves the end, and as many back as reached it
    // return the target.
    const double way = wheel_y > 0.0F ? 1.0 : -1.0;
    const double end =
        std::log(static_cast<double>(wheel_y > 0.0F ? most : least) / from) / std::log(factor);
    const double short_of_end = way * (end - zoom_wheel_.steps);
    if (short_of_end > kZoomWheelEndSlack) {
        const double counted = std::min(
            std::abs(static_cast<double>(wheel_y)), std::ceil(short_of_end - kZoomWheelEndSlack)
        );
        zoom_wheel_.steps += way * counted;
    }
    zoom_wheel_.target =
        std::clamp(static_cast<float>(from * std::pow(factor, zoom_wheel_.steps)), least, most);
    return zoom_wheel_.target;
}

void Runtime::pan_match_camera() {
    // Neither keys nor the pointer move the director's camera.
    if (screen_ != Screen::match || !selected_tnt_ || director_mode())
        return;
    // The map pixels moved in a frame are the scroll speed, pixels per 30 Hz
    // clock unit, times the clock units the frame's real time is worth, so a
    // second of scrolling covers the same ground at any frame rate, at the
    // match record's speed (the preference, or the console's ScrollSpeed),
    // and the camera moves a steady amount each frame.
    const uint64_t elapsed_ns =
        scroll_clock_ != 0 && frame_time_ns_ > scroll_clock_ ? frame_time_ns_ - scroll_clock_ : 0;
    scroll_clock_ = frame_time_ns_;
    const auto speed =
        match_ != nullptr ? match_->state().game.scroll_speed : preferences_.scroll_speed;
    const double step = frame_pacing::scroll_distance(speed, elapsed_ns);
    const bool* keys = SDL_GetKeyboardState(nullptr);
    int dx = 0, dz = 0;
    // A --frame-rate run's held scroll is an arrow key.
    if (frame_run_clock_ns_)
        dx += frame_run_scroll_;
    // Only the arrow keys scroll, as in 3.1c: the letter keys stay the
    // game's own commands (A attack, S stop and so on).
    if (keys[SDL_SCANCODE_LEFT])
        --dx;
    if (keys[SDL_SCANCODE_RIGHT])
        ++dx;
    if (keys[SDL_SCANCODE_UP])
        --dz;
    if (keys[SDL_SCANCODE_DOWN])
        ++dz;
    // The pointer on the screen's outermost pixels scrolls toward that edge,
    // and in a corner both ways, whatever panel lies under it, as in 3.1c: in
    // full screen and in a window alike, while the pointer is in the window
    // at a place SDL has reported on this screen.
    const auto flags = sdl_.window != nullptr ? SDL_GetWindowFlags(sdl_.window) : 0u;
    if (match_pointer_known_ && (flags & SDL_WINDOW_MOUSE_FOCUS) != 0) {
        // A window point covers several pixels on a high-density display, and
        // the pointer rests on the outermost point, not the outermost pixel;
        // on a window at native density the layout is in window points. A
        // scaled frame's pixels to a window point are its width over the
        // points it is presented across.
        float per_point = SDL_GetWindowPixelDensity(sdl_.window);
        SDL_FRect presented{};
        const bool scaled = scaled_frame_width_ > 0 &&
                            SDL_GetRenderLogicalPresentationRect(sdl_.renderer, &presented) &&
                            presented.w > 0.0F;
        if (scaled)
            per_point = per_point * static_cast<float>(match_layout_.width) / presented.w;
        const auto edge = edge_scroll_depth(at_native_density(flags) && !scaled, per_point);
        const auto way = oa::ui::hud::edge_scroll(
            static_cast<int32_t>(std::floor(match_pointer_x_)),
            static_cast<int32_t>(std::floor(match_pointer_y_)),
            match_layout_.width,
            match_layout_.height,
            edge
        );
        dx += way.x;
        dz += way.y;
    }
    if (dx == 0 && dz == 0)
        return;
    scroll_match_view(dx, dz, step);
}

void Runtime::scroll_match_view(int32_t way_x, int32_t way_z, double step) {
    way_x = std::clamp(way_x, -1, 1);
    way_z = std::clamp(way_z, -1, 1);
    if (!selected_tnt_ || !(step > 0.0) || (way_x == 0 && way_z == 0))
        return;
    // Zoom keeps the on-screen rate constant; the view's exact place keeps
    // the world rate exact rather than rounding it every frame.
    const auto zoom = static_cast<double>(match_zoom() <= 0.0F ? 1.0F : match_zoom());
    const double travel = step / zoom;
    const auto view = match_view_place();
    const auto [map_width, map_height] = shown_map_size();
    const auto along =
        [&](double place, int32_t way, int32_t battlefield, int32_t map, double centre) {
            if (way == 0)
                return place;
            return held_view(
                place + static_cast<double>(way) * travel,
                static_cast<double>(battlefield) / zoom,
                static_cast<double>(map),
                past_map_edge_share(),
                view_hold_.held ? std::optional<double>(centre) : std::nullopt
            );
        };
    const double x =
        along(view[0], way_x, match_layout_.battlefield_width(), map_width, view_hold_.centre_x);
    const double z =
        along(view[1], way_z, match_layout_.battlefield_height(), map_height, view_hold_.centre_z);
    // Held at the limits, the view stays as it is.
    if (x == view[0] && z == view[1])
        return;
    stop_match_tracking();
    place_match_view(x, z);
}

void Runtime::issue_resume_or_repair(uint16_t id) {
    issue_resume_or_repair_from(selected_match_unit_, id, queueing());
}

void Runtime::issue_resume_or_repair_from(uint16_t source, uint16_t id, bool queue) {
    if (source == 0 || id == 0 || source == id)
        return;
    auto& slot = match_->world().slots[id];
    const auto unfinished = slot.unit && slot.build_remaining != 0.0F;
    if (unfinished) {
        match_->issue_help_build(source, id, queue);
        status_ = "Resume construction";
    } else {
        match_->issue_repair(source, id, queue);
        status_ = "Repair";
    }
}

void Runtime::issue_match_move(float x, float y, bool queue) {
    if (!match_ || !selected_tnt_ || selected_match_unit_ == 0)
        return;
    if (radar_contains(x, y)) {
        // A press on the radar is the radar's whether or not it gave orders.
        std::ignore = issue_radar_orders(x, y);
        return;
    }
    // The ground drawn under the point, a whole map pixel as orders take.
    const auto target = ground_point_under(x, y);
    if (!target)
        return;
    issue_selection_move(*target, hovered_match_unit_, queue);
}

void Runtime::issue_selection_move(
    const oa::sim::ground_orders::Point& point, uint16_t pointer_unit, bool queue
) {
    if (!match_ || selected_match_unit_ == 0)
        return;
    try {
        // Each unit moves to its own point, keeping its place in the
        // selection around the ground under the pointer. The unit under the
        // pointer is not counted in the centre and takes no order.
        const auto command = oa::sim::gameplay_input::OrderCommand::move;
        const auto bound = group_order_bound_unit(command, pointer_unit);
        const auto centre = local_selection_centre(bound);
        for_each_selected([&](uint16_t id) {
            if (id == bound)
                return;
            const auto at = group_order_destination(centre, command, id, 0, point);
            if (!cancels_queued_command(id, command, 0, at, queue) && match_->takes_move_order(id))
                match_->issue_ground_move(id, at, queue);
        });
    } catch (const std::exception& error) {
        status_ = std::string("move command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
    }
}

void Runtime::issue_match_patrol(float x, float y, bool queue) {
    if (!match_ || !selected_tnt_ || selected_match_unit_ == 0)
        return;
    // The ground drawn under the point, a whole map pixel as orders take.
    const auto target = ground_point_under(x, y);
    if (!target)
        return;
    std::ignore = issue_selection_patrol(*target, hovered_match_unit_, queue);
}

bool Runtime::issue_selection_patrol(
    const oa::sim::ground_orders::Point& point, uint16_t pointer_unit, bool queue
) {
    namespace input = oa::sim::gameplay_input;
    if (!match_ || selected_match_unit_ == 0)
        return false;
    try {
        // Each unit patrols to its own point, keeping its place in the
        // selection around the point. The unit under the pointer is not
        // counted in the centre and takes no order. A unit the order table
        // gives no patrol, one that cannot patrol, is counted in the centre
        // but takes no order either.
        const auto command = input::OrderCommand::patrol;
        const auto bound = group_order_bound_unit(command, pointer_unit);
        const auto centre = local_selection_centre(bound);
        const auto& world = match_->state();
        const auto* aimed = bound != 0 ? oa::world_unit_at(&world, bound) : nullptr;
        const oa::FixedVec3 position{point[0], point[1], point[2]};
        const auto hooks = order_cursor_hooks();
        for_each_selected([&](uint16_t id) {
            const auto* actor = oa::world_unit_at(&world, id);
            if (id == bound || actor == nullptr ||
                input::unit_order(world, command, *actor, aimed, &position, hooks) ==
                    input::UnitOrder::none)
                return;
            const auto at = group_order_destination(centre, command, id, 0, point);
            if (!cancels_queued_command(id, command, 0, at, queue))
                match_->issue_patrol(id, at, queue);
        });
    } catch (const std::exception& error) {
        status_ = std::string("patrol command: ") + error.what();
        std::cerr << "unsupported operation: " << status_ << '\n';
        return false;
    }
    status_ = "Patrol";
    return true;
}

} // namespace oa::app
