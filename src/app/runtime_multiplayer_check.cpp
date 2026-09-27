// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The main menu's Multiplayer path, clicked through the SDL presenter as a
// player does: the box MULTI opens without an extension that offers
// multiplayer, or the notice over game data with no multiplayer map; that
// extension checks its own screens.
#include "oa/app/runtime.hpp"

#include "oa/ui/frontend_dialogs.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <variant>
#include <vector>

namespace oa::app {

namespace {

// MSGBOX.GUI holds its root and OK; the message lines follow as labels.
constexpr std::size_t kFirstMessageLabel = 2;
constexpr int kSettleFrames = 4;

[[noreturn]] void fail(std::string_view what) {
    throw std::runtime_error("multiplayer menu check: " + std::string(what));
}

void require(bool condition, std::string_view what) {
    if (!condition)
        fail(what);
}

// A left-button pointer event at canvas point (x, y), placed in the window
// as the presenter shows the canvas.
SDL_Event pointer_event(
    SDL_Renderer* renderer, SDL_Window* window, SDL_EventType type, int32_t x, int32_t y
) {
    float window_x = 0.0F;
    float window_y = 0.0F;
    require(
        SDL_RenderCoordinatesToWindow(
            renderer, static_cast<float>(x), static_cast<float>(y), &window_x, &window_y
        ),
        SDL_GetError()
    );
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = SDL_GetWindowID(window);
        event.motion.x = window_x;
        event.motion.y = window_y;
    } else {
        event.button.windowID = SDL_GetWindowID(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = 1;
        event.button.x = window_x;
        event.button.y = window_y;
    }
    return event;
}

struct CanvasPoint {
    int32_t x{};
    int32_t y{};
};

// The centre of the main menu's MULTI gadget.
CanvasPoint multi_centre(const oa::ui::gui_layout::Layout& layout) {
    for (const auto& gadget : layout.gadgets)
        if (gadget.common.name == menu::resource_name(menu::Button::multiplayer))
            return {
                gadget.common.x + gadget.common.width / 2,
                gadget.common.y + gadget.common.height / 2
            };
    fail("the main menu has no MULTI gadget");
}

// <stem>-<step>.ppm beside --snapshot.
fs::path step_snapshot(const fs::path& snapshot, std::string_view step) {
    return snapshot.parent_path() / (snapshot.stem().string() + '-' + std::string(step) + ".ppm");
}

} // namespace

void Runtime::multiplayer_check_pointer(SDL_EventType type, int32_t x, int32_t y) {
    bool running = true;
    SDL_Event event = pointer_event(sdl_.renderer, sdl_.window, type, x, y);
    dispatch_event(event, running);
    idle_tick();
}

void Runtime::multiplayer_check_click(int32_t x, int32_t y) {
    multiplayer_check_pointer(SDL_EVENT_MOUSE_MOTION, x, y);
    multiplayer_check_pointer(SDL_EVENT_MOUSE_BUTTON_DOWN, x, y);
    multiplayer_check_pointer(SDL_EVENT_MOUSE_BUTTON_UP, x, y);
    multiplayer_check_settle();
}

void Runtime::multiplayer_check_settle() {
    for (int frame = 0; frame < kSettleFrames; ++frame)
        idle_tick();
}

void Runtime::check_multiplayer_menu() {
    if (extension_.check_multiplayer_menu != nullptr) {
        extension_.check_multiplayer_menu(extension_.context, *this);
        return;
    }
    check_multiplayer_unavailable();
}

void Runtime::check_multiplayer_unavailable() {
    namespace dialogs = oa::ui::frontend_dialogs;
    if (sdl_.renderer == nullptr || sdl_.window == nullptr)
        fail("needs the SDL renderer");
    bool running = true;
    const auto press_enter = [&] {
        SDL_Event event{};
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.windowID = SDL_GetWindowID(sdl_.window);
        event.key.scancode = SDL_SCANCODE_RETURN;
        event.key.key = SDLK_RETURN;
        event.key.down = true;
        dispatch_event(event, running);
        event.type = SDL_EVENT_KEY_UP;
        event.key.down = false;
        dispatch_event(event, running);
        multiplayer_check_settle();
    };
    const auto click_multi = [&] {
        const auto centre = multi_centre(resources_.layout);
        multiplayer_check_click(centre.x, centre.y);
    };
    const auto snapshot = [&](std::string_view step) {
        if (!options_.snapshot.empty())
            write_ppm(step_snapshot(options_.snapshot, step), surface_);
    };
    // OK's records are relative to the box's root, which holds its place on
    // the canvas.
    const auto click_ok = [&] {
        const auto* box = dialogs::dialog_resources();
        require(box != nullptr, "no message box to close");
        const auto& gadgets = box->layout.gadgets;
        const auto& root = gadgets.front().common;
        for (std::size_t index = 1; index < gadgets.size(); ++index) {
            const auto& ok = gadgets[index].common;
            if (ok.name != "OK")
                continue;
            require(ok.active != 0, "the message box's OK is hidden");
            multiplayer_check_click(root.x + ok.x + ok.width / 2, root.y + ok.y + ok.height / 2);
            return;
        }
        fail("the message box has no OK");
    };
    const auto message_lines = [] {
        std::vector<std::string> lines;
        const auto* box = dialogs::dialog_resources();
        if (box == nullptr)
            return lines;
        const auto& gadgets = box->layout.gadgets;
        for (std::size_t index = kFirstMessageLabel; index < gadgets.size(); ++index) {
            const auto& fields = gadgets[index].fields;
            if (const auto* label = std::get_if<oa::ui::gui_layout::LabelFields>(&fields))
                lines.push_back(label->text);
        }
        return lines;
    };
    const auto require_main_menu = [&](std::string_view when) {
        require(
            screen_ == Screen::main_menu && state_.state == frontend::state_id::main_menu &&
                state_.pending_signal != frontend::signal_id::multiplayer,
            std::string(when) + " left the main menu"
        );
    };
    // Game data with no multiplayer map shows its notice instead.
    const bool no_maps = eligible_map_names_.empty();
    for (const bool by_enter : {false, true}) {
        multiplayer_check_settle();
        require_main_menu("settling");
        require(dialogs::dialog_kind() == dialogs::DialogKind::none, "a dialog is already open");
        click_multi();
        if (no_maps) {
            require(
                dialogs::dialog_kind() == dialogs::DialogKind::notice ||
                    dialogs::dialog_kind() == dialogs::DialogKind::message_box,
                "MULTI opened no notice"
            );
        } else {
            require(
                dialogs::dialog_kind() == dialogs::DialogKind::message_box,
                "MULTI opened no message box"
            );
            const auto lines = message_lines();
            require(
                lines.size() == 1 && lines.front() == kMultiplayerUnavailable,
                "the message box does not say \"" + std::string(kMultiplayerUnavailable) +
                    "\" on one line"
            );
        }
        require_main_menu("MULTI");
        if (by_enter) {
            press_enter();
        } else {
            snapshot("box");
            click_ok();
        }
        const std::string_view closer = by_enter ? "Enter" : "OK";
        require(
            dialogs::dialog_kind() == dialogs::DialogKind::none,
            std::string(closer) + " did not close the message box"
        );
        require_main_menu(closer);
        require(status_.find("unimplemented service") == std::string::npos, status_);
        if (!by_enter)
            snapshot("closed");
    }
    if (no_maps)
        std::cout << "multiplayer menu check: MULTI shows the notice for data with no "
                     "multiplayer map over the main menu; OK and Enter close it, twice\n";
    else
        std::cout << "multiplayer menu check: MULTI says \"" << kMultiplayerUnavailable
                  << "\" over the main menu; OK and Enter close it, twice\n";
}

} // namespace oa::app
