// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check_host_input.hpp"

#include <algorithm>
#include <cstring>
#include <stdexcept>
#include <string>

namespace oa::app::check_host_input {

namespace {

/// Tells whether two names are equal ignoring ASCII case.
///
/// @param left a name
/// @param right another
/// @return true when they differ at most in the case of ASCII letters
bool same_name(std::string_view left, std::string_view right) {
    const auto upper = [](char c) {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    };
    return std::equal(left.begin(), left.end(), right.begin(), right.end(), [&](char a, char b) {
        return upper(a) == upper(b);
    });
}

/// A point of the window, in its pixels.
struct WindowPoint {
    float x{}; ///< column
    float y{}; ///< row
};

/// Places a canvas point in the window as a renderer shows the canvas there.
///
/// Throws std::runtime_error when the renderer cannot place it.
///
/// @param renderer the game's renderer; null leaves the canvas point as it is
/// @param x canvas column
/// @param y canvas row
/// @return the window point
WindowPoint window_point(SDL_Renderer* renderer, int32_t x, int32_t y) {
    WindowPoint point{static_cast<float>(x), static_cast<float>(y)};
    if (renderer != nullptr &&
        !SDL_RenderCoordinatesToWindow(
            renderer, static_cast<float>(x), static_cast<float>(y), &point.x, &point.y
        ))
        throw std::runtime_error(std::string("check host: ") + SDL_GetError());
    return point;
}

/// Returns the id an event names a window by.
///
/// @param window the game's window; null in a run without a window
/// @return its id; 0 without one
SDL_WindowID window_id(SDL_Window* window) {
    return window != nullptr ? SDL_GetWindowID(window) : 0;
}

} // namespace

SDL_Event pointer_event(
    SDL_Renderer* renderer, SDL_Window* window, uint32_t type, int32_t x, int32_t y, uint8_t clicks
) {
    if (type != SDL_EVENT_MOUSE_MOTION && type != SDL_EVENT_MOUSE_BUTTON_DOWN &&
        type != SDL_EVENT_MOUSE_BUTTON_UP)
        throw std::runtime_error(
            "check host: a pointer event is a motion, a press or a release, not event type " +
            std::to_string(type)
        );
    const WindowPoint at = window_point(renderer, x, y);
    SDL_Event event{};
    event.type = type;
    if (type == SDL_EVENT_MOUSE_MOTION) {
        event.motion.windowID = window_id(window);
        event.motion.x = at.x;
        event.motion.y = at.y;
    } else {
        event.button.windowID = window_id(window);
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = type == SDL_EVENT_MOUSE_BUTTON_DOWN;
        event.button.clicks = clicks;
        event.button.x = at.x;
        event.button.y = at.y;
    }
    return event;
}

SDL_Event
wheel_event(SDL_Renderer* renderer, SDL_Window* window, int32_t x, int32_t y, float notches) {
    const WindowPoint at = window_point(renderer, x, y);
    SDL_Event event{};
    event.type = SDL_EVENT_MOUSE_WHEEL;
    event.wheel.windowID = window_id(window);
    event.wheel.direction = SDL_MOUSEWHEEL_NORMAL;
    event.wheel.y = notches;
    event.wheel.mouse_x = at.x;
    event.wheel.mouse_y = at.y;
    return event;
}

const oa::ui::gui_layout::Gadget*
find_gadget(const oa::ui::gui_layout::Layout& layout, std::string_view name) {
    const auto found =
        std::find_if(layout.gadgets.begin(), layout.gadgets.end(), [&](const auto& gadget) {
            return same_name(gadget.common.name, name);
        });
    return found != layout.gadgets.end() ? &*found : nullptr;
}

bool copy_text(std::string_view text, char* out, std::size_t size) {
    if (out == nullptr || text.size() >= size)
        return false;
    std::memcpy(out, text.data(), text.size());
    out[text.size()] = '\0';
    return true;
}

} // namespace oa::app::check_host_input
