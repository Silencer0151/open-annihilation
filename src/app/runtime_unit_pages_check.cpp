// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The pages of a few unit types on windows of several sizes, driven through
// synthetic SDL input: the side column is drawn at one scale, as wide as its
// pages, each page is drawn as its file places it, each control lies inside
// the side column and takes a click there, and the radar and the
// battlefield's edge take the pointer where they are drawn.
#include "oa/app/runtime.hpp"
#include "oa/core/unit.h"
#include "oa/core/unit_def.h"
#include "oa/core/world.h"
#include "oa/formats/png.hpp"
#include "oa/sim/gameplay_input/order_cursor.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/hud/build_page_fit.hpp"
#include "oa/ui/hud/order_panel.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <variant>
#include <vector>

namespace oa::app {
namespace {

// Window sizes, smallest first: columns of 480 rows at two scales, then of
// 540 and 720 rows.
constexpr std::array<std::pair<int, int>, 4> kWindowSizes{{
    {640, 480},
    {1280, 720},
    {1920, 1080},
    {2560, 1440},
}};

// Pages NEXT may step through for one unit before the check stops it.
constexpr int kMostPageSteps = 16;

// Failures reported in full; the rest are counted.
constexpr std::size_t kReportedFailures = 40;

// Columns a build button's picture may reach past the side column: some of
// the game's own pictures are one column wider than their 64-column slot.
constexpr int kPictureColumnsPastSlot = 1;

// The check's modes, as --check-unit-pages names them: every page fits the
// column at the chrome's scale, or the pages taller than it narrow the whole
// column to the scale that fits the tallest.
constexpr std::string_view kWholeMode = "whole";
constexpr std::string_view kScaledMode = "scaled";

// Window sizes whose whole frame is written as a PNG, for the first unit
// type named's first build page.
constexpr std::array<std::string_view, 2> kFrameShotSizes{"1920x1080", "1280x720"};

// Returns whether a control's name holds `part`, as the order panel tells
// its page controls apart.
bool names(const std::string& name, std::string_view part) {
    return name.find(part) != std::string::npos;
}

// Returns whether a click on the control turns the page: PREV, NEXT, ORDERS
// or BUILD.
bool turns_page(const std::string& name) {
    return names(name, "PREV") || names(name, "NEXT") || names(name, "ORDERS") ||
           names(name, "BUILD");
}

// Splits "a,b,c" at its commas.
std::vector<std::string> split_names(std::string_view list) {
    std::vector<std::string> parts;
    while (!list.empty()) {
        const auto comma = list.find(',');
        parts.emplace_back(list.substr(0, comma));
        if (comma == std::string_view::npos)
            break;
        list.remove_prefix(comma + 1);
    }
    return parts;
}

} // namespace

void Runtime::check_unit_pages() {
    namespace hud = oa::ui::hud;
    namespace layout = oa::ui::gui_layout;
    // MODE:TYPE,TYPE,...
    const std::string_view argument = options_.check_unit_pages;
    const auto colon = argument.find(':');
    const auto mode = argument.substr(0, colon);
    const auto type_names =
        split_names(colon == std::string_view::npos ? "" : argument.substr(colon + 1));
    if ((mode != kWholeMode && mode != kScaledMode) || type_names.empty())
        throw std::runtime_error(
            "unit pages check: --check-unit-pages takes whole:TYPE,... or scaled:TYPE,..., not " +
            std::string(argument)
        );
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        throw std::runtime_error("unit pages check: needs the SDL renderer");
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    std::vector<std::string> failures;
    std::size_t failure_count = 0;
    const auto failed = [&](const std::string& what) {
        if (failures.size() < kReportedFailures)
            failures.push_back(what);
        ++failure_count;
    };
    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!frame_to_window(sdl_.renderer, x, y, &window_x, &window_y))
            throw std::runtime_error(std::string("unit pages check: ") + SDL_GetError());
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
    if (commander == 0)
        throw std::runtime_error("unit pages check: found no local commander");

    const auto select = [&](uint16_t unit) {
        for (auto& slot : slots)
            if (slot.unit != nullptr && slot.owner_index == match_local_player_)
                slot.unit->flags &= ~OA_UNIT_FLAG_SELECTED;
        if (unit != 0)
            slots[unit].unit->flags |= OA_UNIT_FLAG_SELECTED;
        selected_match_unit_ = unit;
        adopt_selected_units();
    };
    const auto click = [&](std::size_t index) {
        const auto point = hud_gadget_centre(index);
        const auto x = static_cast<float>(point.x);
        const auto y = static_cast<float>(point.y);
        send(SDL_EVENT_MOUSE_MOTION, 0, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
        send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
    };
    // Whether two of the page's gadgets have one rectangle.
    const auto same_place = [&](std::size_t first, std::size_t second) {
        const auto& gadgets = match_hud_->layout.gadgets;
        if (first >= gadgets.size() || second >= gadgets.size())
            return false;
        const auto& a = gadgets[first].common;
        const auto& b = gadgets[second].common;
        return a.x == b.x && a.y == b.y && a.width == b.width && a.height == b.height;
    };

    // Whether the runtime draws a button, and whether it takes the pointer,
    // as the HUD's drawing and update_pointer tell.
    struct Shown {
        bool drawn = false;
        bool takes_pointer = false;
    };

    const auto shown = [&](const layout::Gadget& gadget) {
        const auto* state = match_gadget_state(gadget);
        const auto* fields = std::get_if<layout::ButtonFields>(&gadget.fields);
        const bool grayed = fields != nullptr && fields->grayed_out;
        const bool hidden =
            (match_hud_action(gadget.common.name) == "MISSION" && !campaign_mission_) ||
            !gadget_command_available(gadget);
        const bool idle_pager = is_build_page_nav(gadget.common.name) && !build_page_nav_shown();
        return Shown{
            state != nullptr || grayed || !hidden, state != nullptr || (!hidden && !idle_pager)
        };
    };
    // The first control the pointer can take whose name ends with `suffix`.
    const auto control_ending = [&](std::string_view suffix) -> std::size_t {
        const auto& gadgets = match_hud_->layout.gadgets;
        for (std::size_t index = 1; index < gadgets.size(); ++index) {
            const auto& common = gadgets[index].common;
            if (common.active != 0 && common.width > 0 &&
                std::string_view(common.name).ends_with(suffix) &&
                shown(gadgets[index]).takes_pointer)
                return index;
        }
        return 0;
    };
    std::size_t pages_checked = 0;
    std::size_t narrowed_windows = 0;
    std::size_t clicks = 0;
    std::set<std::string> snapshots_written;
    // Writes the side column as the window shows it, and, named by `frame`,
    // the whole window as a PNG.
    const auto snapshot = [&](const std::string& name, const std::string& frame_name) {
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        renderer::Surface column;
        column.width = static_cast<uint32_t>(std::min<int>(match_layout_.left, frame.width));
        column.height = frame.height;
        column.rgb.resize(static_cast<std::size_t>(column.width) * column.height * 3U);
        for (uint32_t row = 0; row < column.height; ++row)
            std::copy_n(
                frame.rgb.begin() + static_cast<std::ptrdiff_t>(row) * frame.width * 3,
                static_cast<std::ptrdiff_t>(column.width) * 3,
                column.rgb.begin() + static_cast<std::ptrdiff_t>(row) * column.width * 3
            );
        write_ppm(report_directory / name, column);
        if (frame_name.empty())
            return;
        std::vector<uint8_t> file;
        const oa::formats::png::Header header{
            frame.width, frame.height, 8, oa::formats::png::ColorType::rgb, {}
        };
        std::ofstream output(report_directory / frame_name, std::ios::binary | std::ios::trunc);
        if (!oa::formats::png::write(oa::formats::png::Image{header, {}, frame.rgb}, &file) ||
            !output.write(
                reinterpret_cast<const char*>(file.data()),
                static_cast<std::streamsize>(file.size())
            ))
            failed("could not write " + frame_name);
    };
    // Checks the page the HUD shows: it is drawn as its file places it,
    // whole, at the side column's scale and no other; every control the
    // runtime draws lies inside the rows the side column shows and its 128
    // columns; and every one the pointer can take is under the pointer at
    // its centre and holds a press there; a click on each that does not turn
    // the page leaves the page as it is.
    const auto check_page = [&](const std::string& label,
                                const std::string& snapshot_name,
                                const std::string& frame_name = {}) {
        ++pages_checked;
        const auto& gadgets = match_hud_->layout.gadgets;
        if (gadgets.size() != match_hud_authored_.size())
            failed(label + ": has controls its file does not place");
        for (std::size_t index = 0; index < gadgets.size() && index < match_hud_authored_.size();
             ++index) {
            const auto& common = gadgets[index].common;
            const auto& authored = match_hud_authored_[index];
            if (common.x != authored.x || common.y != authored.y ||
                common.height != authored.height || common.active != authored.active)
                failed(label + ": " + common.name + " is not where its file places it");
        }
        // The page is drawn at the column's one scale, with the radar and
        // the panel above it: never scaled apart from them.
        if (match_side_page_scale().scaled() || match_hud_strips()[3].w > 0)
            failed(label + ": scaled apart from the side column, not drawn at its scale");
        const int rows = match_column_rows();
        const auto panel = match_hud_panel_;
        const auto page = match_build_page_;
        if (!snapshot_name.empty() && snapshots_written.insert(snapshot_name).second)
            snapshot(snapshot_name, frame_name);
        for (std::size_t index = 1; index < gadgets.size(); ++index) {
            const auto gadget = gadgets[index];
            const auto& common = gadget.common;
            if (common.active == 0 || common.width <= 0 || common.height <= 0 ||
                common.type == layout::GadgetType::image)
                continue;
            const bool button = common.type == layout::GadgetType::button;
            const auto how = button ? shown(gadget) : Shown{true, false};
            if (!how.drawn)
                continue;
            const auto place = common.name + " at " + std::to_string(common.x) + "," +
                               std::to_string(common.y) + " size " + std::to_string(common.width) +
                               "x" + std::to_string(common.height);
            if (common.y < 0 || common.y + common.height > rows) {
                failed(
                    label + ": " + place + " lies outside the side column's " +
                    std::to_string(rows) + " rows"
                );
                continue;
            }
            if (common.x < 0 ||
                common.x + common.width > kBattlefieldLeft + kPictureColumnsPastSlot) {
                failed(
                    label + ": " + place + " lies outside the side column's " +
                    std::to_string(kBattlefieldLeft) + " columns"
                );
                continue;
            }
            if (!how.takes_pointer || turns_page(common.name))
                continue;
            const auto point = hud_gadget_centre(index);
            const auto x = static_cast<float>(point.x);
            const auto y = static_cast<float>(point.y);
            send(SDL_EVENT_MOUSE_MOTION, 0, x, y);
            if (hovered_ != index) {
                // A page may place two buttons in one spot, such as LOAD and
                // BLAST, of which a unit has one; the pointer takes either.
                if (!hovered_ || !same_place(*hovered_, index))
                    failed(
                        label + ": the pointer at the centre of " + common.name + " is not over it"
                    );
                continue;
            }
            send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, x, y);
            if (match_hud_held_ != index)
                failed(label + ": a press on " + common.name + " did not hold it");
            send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, x, y);
            ++clicks;
            reset_match_command();
            match_command_ = MatchCommand::none;
            pending_build_type_ = 0;
            if (match_hud_panel_ != panel || match_build_page_ != page) {
                failed(label + ": a click on " + common.name + " turned the page");
                return;
            }
        }
    };
    // Opens build page `page` of the selected unit, which keeps that page in
    // its flags as the player's page turns leave it.
    const auto open_page = [&](int page) {
        if (auto* panel_unit = oa::world_unit_at(&world, selected_match_unit_))
            panel_unit->flags = hud::build_menu_select(
                panel_unit->flags,
                world.unit_defs[panel_unit->type_index].gui_page_count,
                static_cast<uint32_t>(page)
            );
        open_match_build_page(page);
        return match_build_page_ == page;
    };
    // Clicks a page-turning control and tells whether it took the click.
    const auto click_control = [&](std::size_t index, const std::string& label) {
        const auto point = hud_gadget_centre(index);
        send(SDL_EVENT_MOUSE_MOTION, 0, static_cast<float>(point.x), static_cast<float>(point.y));
        if (hovered_ != index) {
            failed(
                label + ": the pointer at the centre of " +
                match_hud_->layout.gadgets[index].common.name + " is not over it"
            );
            return false;
        }
        click(index);
        ++clicks;
        return true;
    };

    const auto resize = [&](int width, int height) {
        const auto size = std::to_string(width) + 'x' + std::to_string(height);
        if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
            throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
        apply_output_mode();
        if (match_layout_.width != width || match_layout_.height != height)
            throw std::runtime_error("unit pages check: could not size the match to " + size);
        return size;
    };
    // The side column on the window of `size`, as the mode expects it:
    // whole, it keeps the chrome's width and scale; scaled, the whole column
    // is drawn at the one scale at which the game's tallest unit page ends on
    // the window's last row, and is 128 columns at that scale wide. Either
    // way its 128 columns fill its width, with no blank strip beside them;
    // the bars and the battlefield start at its edge, and the bars reach the
    // window's right edge; the radar is drawn in it at its scale and scrolls
    // the view; and the battlefield takes a press from the column's edge on,
    // the column up to it.
    std::set<std::string> windows_checked;
    const auto check_window = [&](const std::string& size) {
        if (!windows_checked.insert(size).second)
            return;
        select(0);
        namespace display = oa::ui::display_layout;
        namespace input = oa::sim::gameplay_input;
        const auto chrome = display::make_match_layout(match_layout_.width, match_layout_.height);
        const int page_rows = side_column_page_rows();
        if (mode == kWholeMode) {
            if (match_layout_.column_narrowed() || match_layout_.left != chrome.left ||
                match_layout_.hud_width != chrome.hud_width ||
                match_layout_.column_scale != chrome.scale)
                failed(size + ": the side column is not the chrome's width and scale");
        } else if (!match_layout_.column_narrowed()) {
            failed(
                size + ": the side column is not narrowed to its tallest page's " +
                std::to_string(page_rows) + " rows"
            );
        } else {
            ++narrowed_windows;
            const auto width = std::lround(kBattlefieldLeft * match_layout_.column_scale);
            if (match_layout_.left != width)
                failed(
                    size + ": the side column is " + std::to_string(match_layout_.left) +
                    " pixels wide, not its 128 columns' " + std::to_string(width)
                );
            if (std::lround(page_rows * match_layout_.column_scale) != match_layout_.height)
                failed(
                    size + ": the tallest page's " + std::to_string(page_rows) +
                    " rows do not end on the window's last row"
                );
        }
        const auto strips = match_hud_strips();
        const auto& column = strips[0];
        if (column.x != 0 || column.y != 0 || column.w != match_layout_.left ||
            column.source_x != 0 || column.source_w != kBattlefieldLeft)
            failed(size + ": the side column's 128 columns do not fill its width");
        if (strips[1].x != match_layout_.left || strips[2].x != match_layout_.left ||
            match_layout_.battlefield_x() != match_layout_.left ||
            match_layout_.battlefield_width() != match_layout_.width - match_layout_.left)
            failed(size + ": the bars and the battlefield do not start at the side column's edge");
        render_match_surface();
        // The bars take the width a narrowed column gives up: at the
        // chrome's scale, each reaches the window's right edge, and its last
        // column there shows its art, not a blank strip.
        {
            renderer::Surface frame;
            compose_match_frame(frame);
            const auto lit = [&frame](int x, int top, int rows) {
                for (int y = top; y < top + rows; ++y) {
                    if (x < 0 || y < 0 || x >= static_cast<int>(frame.width) ||
                        y >= static_cast<int>(frame.height))
                        continue;
                    const auto* pixel =
                        frame.rgb.data() +
                        (static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) *
                            3U;
                    if (pixel[0] != 0 || pixel[1] != 0 || pixel[2] != 0)
                        return true;
                }
                return false;
            };
            for (const auto* bar : {&strips[1], &strips[2]}) {
                const bool top_bar = bar == &strips[1];
                const auto drawn = static_cast<double>(bar->source_w) * match_layout_.scale;
                // The window's edge may cut the bar's last source column.
                const auto last_column =
                    bar->x + static_cast<double>(bar->source_w - 1) * match_layout_.scale;
                if (bar->x + bar->w < match_layout_.width || last_column >= match_layout_.width ||
                    std::abs(drawn - bar->w) > 1.0 || !lit(match_layout_.width - 1, bar->y, bar->h))
                    failed(
                        size + ": the " + (top_bar ? "top" : "bottom") + " bar ends at " +
                        std::to_string(bar->x + bar->w) + ", not at the window's right edge " +
                        std::to_string(match_layout_.width) + " with its art at the bars' scale"
                    );
            }
        }
        auto& game = match_->state().game;
        const auto radar = display::source_rect_to_canvas(
            match_layout_,
            game.radar_offset_x,
            game.radar_offset_y,
            game.radar_width,
            game.radar_height
        );
        if (radar_picture_.width <= 0 || radar_picture_.height <= 0) {
            failed(size + ": the radar is not drawn");
        } else {
            if (radar_picture_.x != radar.x || radar_picture_.y != radar.y ||
                radar_picture_.width != radar.width || radar_picture_.height != radar.height ||
                radar_picture_.x + radar_picture_.width > match_layout_.left)
                failed(size + ": the radar is not drawn in the side column at its scale");
            const auto radar_x =
                static_cast<float>(radar_picture_.x) + static_cast<float>(radar_picture_.width) / 2;
            const auto radar_y = static_cast<float>(radar_picture_.y) +
                                 static_cast<float>(radar_picture_.height) / 2;
            const auto on_radar = hud_source_point(radar_x, radar_y);
            if (on_radar.x < game.radar_offset_x ||
                on_radar.x >= game.radar_offset_x + game.radar_width ||
                on_radar.y < game.radar_offset_y ||
                on_radar.y >= game.radar_offset_y + game.radar_height)
                failed(size + ": the radar's middle does not map onto its picture");
            // A right press over the radar scrolls the view with the pointer,
            // down and right, as far as the view does not already show the
            // whole map that way.
            send(SDL_EVENT_MOUSE_MOTION, 0, radar_x, radar_y);
            send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_RIGHT, radar_x, radar_y);
            if ((input::pointer_flags(game) & input::pointer_radar_scroll) == 0)
                failed(size + ": a press on the radar did not scroll with it");
            send(SDL_EVENT_MOUSE_MOTION, 0, radar_x, radar_y);
            const auto scrolled_x = match_camera_x_;
            const auto scrolled_z = match_camera_z_;
            send(
                SDL_EVENT_MOUSE_MOTION,
                0,
                radar_x + static_cast<float>(radar_picture_.width) / 4,
                radar_y + static_cast<float>(radar_picture_.height) / 4
            );
            const bool across = visible_map_width() < radar_map_w_;
            const bool down = visible_map_height() < radar_map_h_;
            if ((across && match_camera_x_ <= scrolled_x) ||
                (down && match_camera_z_ <= scrolled_z))
                failed(size + ": the radar scroll did not follow the pointer");
            send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_RIGHT, radar_x, radar_y);
            ++clicks;
        }
        // The battlefield's first column takes a press as the battlefield's,
        // and the column's last does not.
        const auto edge_y = static_cast<float>(
            match_layout_.battlefield_y() + match_layout_.battlefield_height() / 2
        );
        for (const int x : {match_layout_.left, match_layout_.left - 1}) {
            const bool battlefield = x >= match_layout_.left;
            const auto edge_x = static_cast<float>(x);
            match_drag_.reset();
            send(SDL_EVENT_MOUSE_MOTION, 0, edge_x, edge_y);
            const bool over_view = (input::pointer_flags(game) & input::pointer_over_view) != 0;
            send(SDL_EVENT_MOUSE_BUTTON_DOWN, SDL_BUTTON_LEFT, edge_x, edge_y);
            const bool boxed = match_drag_.has_value();
            send(SDL_EVENT_MOUSE_BUTTON_UP, SDL_BUTTON_LEFT, edge_x, edge_y);
            ++clicks;
            match_drag_.reset();
            if (over_view != battlefield || boxed != battlefield ||
                (battlefield && game_screen_point(edge_x, edge_y).x != kBattlefieldLeft))
                failed(
                    size + ": a press at column " + std::to_string(x) +
                    (battlefield ? " did not reach the battlefield"
                                 : " reached the battlefield, not the side column")
                );
        }
        reset_match_command();
        match_command_ = MatchCommand::none;
        pending_build_type_ = 0;
    };
    // Every page of one unit on the window of `size`.
    const auto check_unit = [&](uint16_t unit, const std::string& size) {
        check_window(size);
        const auto type = slots[unit].record.type_index;
        const auto& name = spawn_type_names_.at(type);
        const auto& definition = world.unit_defs[type];
        const auto shot = [&](const std::string& page) {
            return "unit-pages-" + size + "-" + name + "-" + page + ".ppm";
        };
        // The whole window on the first build page, at the sizes kept.
        const auto frame_shot = [&](int page) -> std::string {
            if (page != 1 || std::find(kFrameShotSizes.begin(), kFrameShotSizes.end(), size) ==
                                 kFrameShotSizes.end())
                return {};
            return "unit-pages-" + size + "-" + name + "-page1.png";
        };
        select(unit);
        if (selected_match_unit_ != unit) {
            failed(size + " " + name + ": was not selected");
            return;
        }
        // A unit without build pages shows the general page.
        if (match_build_page_ == 0)
            check_page(size + " " + name + " general page", shot("general"));
        // Page 0, which a type may have of its own, as the order panel
        // loads it.
        if ((definition.flags & hud::kDefFlagBuildMenuDefault) != 0) {
            char file[64];
            hud::format_build_page_name(file, sizeof file, definition, 0);
            auto state = hud::order_panel_load(world.game);
            summarize_order_panel(state);
            state.unit_id = 0;
            match_build_page_ = 0;
            const auto prefix = match_side_name_prefix();
            hud::load_build_page(
                state,
                slots[unit].record,
                definition,
                file,
                0,
                prefix.c_str(),
                order_panel_controls(),
                order_panel_loader()
            );
            if (state.unit_id != unit) {
                failed(size + " " + name + ": page 0 " + file + " did not load");
            } else {
                hud::order_panel_store(world.game, state);
                render_match_surface();
                check_page(size + " " + name + " page 0", shot("page0"));
            }
        }
        if (definition.gui_page_count < 2)
            return;
        // Every page NEXT reaches from the first, back to the first.
        std::vector<int> steps;
        if (!open_page(1)) {
            failed(size + " " + name + ": did not open its first build page");
            return;
        }
        bool tabs_checked = false;
        for (int step = 0; step < kMostPageSteps; ++step) {
            const int here = match_build_page_;
            if (!steps.empty() && here == steps.front())
                break;
            if (std::find(steps.begin(), steps.end(), here) != steps.end()) {
                failed(size + " " + name + ": NEXT came back to a page before the first");
                break;
            }
            steps.push_back(here);
            const auto label = size + " " + name + " page " + std::to_string(here);
            check_page(label, shot("page" + std::to_string(here)), frame_shot(here));
            // ORDERS opens the general page, whose BUILD comes back to the
            // page the unit kept.
            if (const auto orders = control_ending("ORDERS"); orders != 0 && !tabs_checked) {
                tabs_checked = true;
                if (click_control(orders, label)) {
                    if (match_build_page_ != 0) {
                        failed(label + ": ORDERS did not open the general page");
                    } else {
                        check_page(size + " " + name + " general page", shot("general"));
                        const auto build = control_ending("BUILD");
                        if (build == 0)
                            failed(size + " " + name + ": the general page has no BUILD tab");
                        else if (
                            click_control(build, size + " " + name + " general page") &&
                            match_build_page_ != here
                        )
                            failed(
                                size + " " + name +
                                ": BUILD did not come back to the page the unit kept"
                            );
                    }
                }
                if (!open_page(here)) {
                    failed(label + ": could not open it again");
                    break;
                }
            }
            const auto next = control_ending("NEXT");
            if (next == 0)
                break;
            if (!click_control(next, label))
                break;
        }
        // PREV steps back through the same pages.
        for (std::size_t step = 1; step < steps.size(); ++step) {
            const auto label = size + " " + name + " page " + std::to_string(steps[step]);
            if (!open_page(steps[step])) {
                failed(label + ": could not open it again");
                break;
            }
            const auto previous = control_ending("PREV");
            if (previous == 0) {
                failed(label + ": has no PREV");
                continue;
            }
            if (click_control(previous, label) && match_build_page_ != steps[step - 1])
                failed(label + ": PREV opened page " + std::to_string(match_build_page_));
        }
    };

    // A unit of each type named: the player's own, or one made beside the
    // commander and removed once it is checked. The check runs no tick, so
    // it stands where it is made.
    for (const auto& type_name : type_names) {
        const auto type = oa::sim::unit_spawn::find_type_index(spawn_type_names_, type_name);
        if (type == 0 || type >= world.unit_def_count) {
            failed("no unit type " + type_name);
            continue;
        }
        uint16_t unit = 0;
        for (const auto& slot : slots)
            if (slot.unit != nullptr && slot.record.type_index == type &&
                slot.record.owner_index == match_local_player_) {
                unit = slot.unit_index;
                break;
            }
        const bool made = unit == 0;
        if (made) {
            oa::sim::unit_spawn::Request request;
            request.player = match_local_player_;
            request.type = static_cast<uint16_t>(type);
            request.finished = true;
            request.state = kGroundOccupancyState;
            request.position = slots[commander].unit->position;
            if (auto* created = match_->create(request); created != nullptr && created->unit)
                unit = created->unit_index;
        }
        if (unit == 0) {
            failed("could not make a " + type_name);
            continue;
        }
        std::cout << "unit pages check: " << type_name << " has "
                  << std::max(0, static_cast<int>(world.unit_defs[type].gui_page_count) - 1)
                  << " build page(s)\n";
        for (const auto& [width, height] : kWindowSizes)
            check_unit(unit, resize(width, height));
        if (made) {
            select(0);
            match_->kill_unit(
                unit, static_cast<uint8_t>(oa::sim::match_runtime::DeathKind::cancelled)
            );
        }
    }
    select(0);
    for (const auto& failure : failures)
        std::cerr << "unit pages check: " << failure << '\n';
    if (failure_count != 0)
        throw std::runtime_error(
            "unit pages check: " + std::to_string(failure_count) + " failure(s)"
        );
    std::cout << "unit pages check: " << type_names.size() << " unit types, " << pages_checked
              << " pages on " << kWindowSizes.size() << " window sizes, the side column "
              << (mode == kWholeMode ? "at the chrome's width for its tallest page of "
                                     : "narrowed to its tallest page of ")
              << side_column_page_rows() << " rows"
              << " on " << (mode == kWholeMode ? windows_checked.size() : narrowed_windows) << "; "
              << clicks
              << " clicks, every page drawn as authored at the column's scale, every control "
                 "inside the column and taking its click, the radar and the battlefield's edge "
                 "taking theirs\n";
}

} // namespace oa::app
