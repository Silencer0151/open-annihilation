// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The commander's build pages on windows of several sizes, driven through
// synthetic SDL input: every page fits the side column, every build button
// stays reachable and the blank strip under the column builds nothing.
#include "oa/app/runtime.hpp"
#include "oa/ui/hud/order_panel.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {
namespace {

// NEXT presses that walk every build page of a commander and back to the first.
constexpr int kMaxPageSteps = 16;

// Window sizes, smallest first.
constexpr std::array<std::pair<int, int>, 8> kWindowSizes{{
    {640, 480},
    {1024, 768},
    {1280, 720},
    {1920, 1080},
    {1920, 1200},
    {2560, 1440},
    {3840, 2160},
    {5120, 2880},
}};

} // namespace

void Runtime::check_side_column() {
    namespace hud = oa::ui::hud;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        throw std::runtime_error("side column check: needs the SDL renderer");
    const fs::path report_directory = "local/reports";
    fs::create_directories(report_directory);
    std::vector<std::string> failures;
    // Unit buttons any loaded page holds, shown or not.
    std::set<std::string> on_pages;
    std::vector<std::pair<std::string, std::set<std::string>>> reachable_by_size;
    bool running = true;
    const auto send = [&](SDL_EventType type, uint8_t button, float x, float y) {
        float window_x = 0.0F;
        float window_y = 0.0F;
        if (!SDL_RenderCoordinatesToWindow(sdl_.renderer, x, y, &window_x, &window_y))
            throw std::runtime_error(std::string("side column check: ") + SDL_GetError());
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
    const auto snapshot = [&](const std::string& name) {
        render_match_surface();
        renderer::Surface frame;
        compose_match_frame(frame);
        write_ppm(report_directory / name, frame);
    };

    for (const auto& [width, height] : kWindowSizes) {
        const auto size = std::to_string(width) + 'x' + std::to_string(height);
        if (match_)
            leave_match();
        if (!SDL_SetWindowSize(sdl_.window, width, height) || !SDL_SyncWindow(sdl_.window))
            throw std::runtime_error(std::string("SDL_SetWindowSize: ") + SDL_GetError());
        load(Screen::main_menu);
        start_benchmark_skirmish();
        apply_output_mode();
        if (match_layout_.width != width || match_layout_.height != height)
            throw std::runtime_error("side column check: could not size the match to " + size);
        auto& slots = match_->world().slots;
        uint16_t commander = 0;
        for (const auto& slot : slots)
            if (slot.unit != nullptr && slot.record.type_index != 0 &&
                slot.record.owner_index == match_local_player_) {
                commander = slot.unit_index;
                break;
            }
        if (commander == 0)
            throw std::runtime_error("side column check: found no local commander");
        center_camera_on_unit(commander);
        const auto viewport = live_viewport(
            static_cast<uint32_t>(std::max(0, match_camera_x_)),
            static_cast<uint32_t>(std::max(0, match_camera_z_))
        );
        const auto on_screen = project_match_point(viewport, slots[commander].unit->position);
        click(static_cast<float>(on_screen.x), static_cast<float>(on_screen.y));
        if (selected_match_unit_ != commander)
            throw std::runtime_error("side column check: did not select the commander on " + size);
        show_match_build_page(1);
        render_match_surface();

        // The side column shows source rows [0, drawn_rows) of the HUD, or
        // the rows above a page taller than it and the page scaled under them.
        const auto strips = match_hud_strips();
        const int drawn_height = strips[3].h > 0 ? strips[3].y + strips[3].h : strips[0].h;
        std::set<std::string> reachable;
        std::vector<std::string> first_page;
        for (int step = 0; step < kMaxPageSteps; ++step) {
            const auto& gadgets = match_hud_->layout.gadgets;
            const auto scale = match_side_page_scale();
            const int drawn_rows =
                scale.scaled() ? scale.top + scale.shown_rows : match_hud_strips()[0].source_h;
            // The row of the column a page's row is drawn on.
            const auto column_row = [&](int row) {
                return scale.scaled() && row > scale.top
                           ? scale.top + scale.to_column(row - scale.top)
                           : row;
            };
            std::vector<std::string> shown;
            for (std::size_t index = 1; index < gadgets.size(); ++index) {
                const auto& common = gadgets[index].common;
                if (common.active == 0 || common.width <= 0 || common.height <= 0)
                    continue;
                if ((static_cast<uint8_t>(common.common_attributes) & hud::kCommonUnitButton) != 0)
                    on_pages.insert(common.name);
                if (column_row(common.y + common.height) > drawn_rows)
                    failures.push_back(
                        size + " page " + std::to_string(step) + ": " + common.name +
                        " ends at row " + std::to_string(common.y + common.height) +
                        ", below the column's " + std::to_string(drawn_rows)
                    );
                else if (
                    (static_cast<uint8_t>(common.common_attributes) & hud::kCommonUnitButton) != 0
                )
                    shown.push_back(common.name);
            }
            snapshot("side-column-" + size + "-" + std::to_string(step) + ".ppm");
            if (step == 0)
                first_page = shown;
            else if (shown == first_page && match_build_page_ == 1)
                break;
            reachable.insert(shown.begin(), shown.end());
            const auto previous_panel = match_hud_panel_;
            const auto previous_page = match_build_page_;
            show_match_build_page(match_build_page_ + 1);
            render_match_surface();
            if (match_hud_panel_ == previous_panel && match_build_page_ == previous_page &&
                step > 0) {
                std::vector<std::string> now;
                for (const auto& gadget : match_hud_->layout.gadgets)
                    if (gadget.common.active != 0 &&
                        (static_cast<uint8_t>(gadget.common.common_attributes) &
                         hud::kCommonUnitButton) != 0)
                        now.push_back(gadget.common.name);
                if (now == shown)
                    break;
            }
        }
        reachable_by_size.emplace_back(size, reachable);

        // The strip under the side column, where there is one, builds nothing.
        show_match_build_page(1);
        render_match_surface();
        if (drawn_height < match_layout_.height) {
            match_command_ = MatchCommand::none;
            pending_build_type_ = 0;
            click(
                static_cast<float>(match_layout_.left / 2),
                static_cast<float>((drawn_height + match_layout_.height) / 2)
            );
            if (pending_build_type_ != 0 || match_command_ == MatchCommand::build)
                failures.push_back(
                    size + ": a click under the side column armed a build of type " +
                    std::to_string(pending_build_type_)
                );
            match_command_ = MatchCommand::none;
            pending_build_type_ = 0;
        }

        // The commander's general page, which the ORDERS tab opens.
        show_match_orders_page();
        snapshot("side-column-" + size + "-orders.ppm");
        show_match_build_page(1);
        render_match_surface();

        // A page under ORDERS and BUILD tabs opens the general page from
        // ORDERS and comes back to its first page from BUILD.
        const auto shown_gadget = [&](std::string_view suffix) -> const ui::gui_layout::Gadget* {
            for (const auto& gadget : match_hud_->layout.gadgets)
                if (gadget.common.active != 0 && gadget.common.width > 0 &&
                    std::string_view(gadget.common.name).ends_with(suffix))
                    return &gadget;
            return nullptr;
        };
        const auto click_gadget = [&](const ui::gui_layout::Gadget& gadget) {
            const auto point = oa::ui::display_layout::source_to_canvas(
                match_layout_,
                gadget.common.x + gadget.common.width / 2,
                gadget.common.y + gadget.common.height / 2
            );
            click(static_cast<float>(point.x), static_cast<float>(point.y));
        };
        if (const auto* orders = shown_gadget("ORDERS")) {
            click_gadget(*orders);
            if (match_build_page_ != 0)
                failures.push_back(size + ": ORDERS did not open the general page");
            else if (const auto* build = shown_gadget("BUILD")) {
                click_gadget(*build);
                if (match_build_page_ != 1)
                    failures.push_back(size + ": BUILD did not come back to the first page");
            } else {
                failures.push_back(size + ": the general page has no BUILD tab");
            }
        }
    }
    for (const auto& [size, reachable] : reachable_by_size)
        for (const auto& name : on_pages)
            if (reachable.count(name) == 0)
                failures.push_back(size + ": " + name + " is on no build page the column shows");
    if (match_)
        leave_match();
    for (const auto& failure : failures)
        std::cerr << "side column check: " << failure << '\n';
    if (!failures.empty())
        throw std::runtime_error(
            "side column check: " + std::to_string(failures.size()) + " failure(s)"
        );
    std::cout << "side column check: passed on " << kWindowSizes.size() << " window sizes\n";
}

} // namespace oa::app
