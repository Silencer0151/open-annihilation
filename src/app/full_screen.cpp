// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Alt+Enter: switching the game's window between full screen and a window,
// the pointer kept on the game's screen in full screen, and the window brought
// back onto its display when it leaves full screen.
#include "oa/app/full_screen.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <tuple>

namespace oa::app {

namespace {

/// One axis of a window's frame or of a display's usable area: where it
/// starts and how long it is, in the desktop's coordinates.
struct Span {
    int64_t start{};
    int64_t length{};
};

/// Places one axis of a window's frame on the same axis of a display's
/// usable area, as window_on_display says.
///
/// @param window the frame's start and length on the axis
/// @param usable the usable area's start and length on the axis
/// @param resizable whether the window may be resized
/// @return the frame's start and length on the display
Span span_on_display(Span window, Span usable, bool resizable) {
    const int64_t usable_end = usable.start + usable.length;
    if (window.start >= usable.start && window.start + window.length <= usable_end)
        return window;
    const int64_t margin = usable.length * window_margin_percent / 100;
    const int64_t room = usable.length - 2 * margin;
    if (window.length <= room) {
        // A window that fits between the margins has only one side outside.
        const int64_t start = window.start < usable.start ? usable.start + margin
                                                          : usable_end - margin - window.length;
        return {start, window.length};
    }
    return {usable.start + margin, resizable ? std::max<int64_t>(room, 1) : window.length};
}

/// Tells whether an event is one of a window's own events.
///
/// @param event any SDL event
/// @return true for the SDL_EVENT_WINDOW_ events
bool is_window_event(const SDL_Event& event) {
    return event.type >= SDL_EVENT_WINDOW_FIRST && event.type <= SDL_EVENT_WINDOW_LAST;
}

/// Notes the display a full-screen window is on, and brings a window that has
/// left full screen back onto that display once the window system has placed
/// it (bring_window_on_display).
///
/// @param window the game's window
/// @param[in,out] full_screen the display noted and the window's place still to
///        be checked
void follow_window_place(SDL_Window* window, FullScreenSwitch& full_screen) {
    if ((SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0) {
        full_screen.full_screen_display = SDL_GetDisplayForWindow(window);
        return;
    }
    if (full_screen.places_window(SDL_GetTicks()) &&
        bring_window_on_display(window, full_screen.full_screen_display))
        full_screen.placing_window = false;
}

} // namespace

SDL_Rect window_on_display(const SDL_Rect& window, const SDL_Rect& usable, bool resizable) {
    const Span across = span_on_display({window.x, window.w}, {usable.x, usable.w}, resizable);
    const Span down = span_on_display({window.y, window.h}, {usable.y, usable.h}, resizable);
    // Every start and length lies between the window's own and the usable
    // area's, so it fits an int.
    return {
        static_cast<int>(across.start),
        static_cast<int>(down.start),
        static_cast<int>(across.length),
        static_cast<int>(down.length)
    };
}

bool bring_window_on_display(SDL_Window* window, SDL_DisplayID display) {
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0)
        return false;
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0)
        return true;
    // The title bar and borders, where the window system reports them. An X11
    // window manager reports none while it puts a window's decorations back
    // after full screen, and the window's place as a window comes after them.
    int top = 0;
    int left = 0;
    int bottom = 0;
    int right = 0;
    if (SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) &&
        (flags & SDL_WINDOW_BORDERLESS) == 0 && top == 0 && left == 0 && bottom == 0 && right == 0)
        return false;
    SDL_Rect usable{};
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable)) {
        display = SDL_GetDisplayForWindow(window);
        if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable))
            return true;
    }
    SDL_Rect contents{};
    if (!SDL_GetWindowPosition(window, &contents.x, &contents.y) ||
        !SDL_GetWindowSize(window, &contents.w, &contents.h))
        return true;
    const SDL_Rect frame{
        contents.x - left, contents.y - top, contents.w + left + right, contents.h + top + bottom
    };
    const SDL_Rect placed = window_on_display(frame, usable, (flags & SDL_WINDOW_RESIZABLE) != 0);
    const int x = placed.x + left;
    const int y = placed.y + top;
    const int width = std::max(1, placed.w - left - right);
    const int height = std::max(1, placed.h - top - bottom);
    // A window system that places windows itself refuses the move, and the
    // window keeps its size as well.
    if ((x != contents.x || y != contents.y) && !SDL_SetWindowPosition(window, x, y))
        return true;
    // The window's new size reaches the game as any resize does, and the
    // screen is laid out again at it.
    // A window system that refuses the size leaves the window as it was,
    // which the game keeps drawing at.
    if (width != contents.w || height != contents.h)
        std::ignore = SDL_SetWindowSize(window, width, height);
    return true;
}

bool switch_full_screen(SDL_Window* window, FullScreenSwitch& full_screen) {
    const bool shown = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    const bool wanted = full_screen.next_mode(shown, SDL_GetTicks());
    if (!SDL_SetWindowFullscreen(window, wanted))
        return false;
    full_screen.note_request(wanted, SDL_GetTicks());
    return true;
}

void keep_pointer_on_screen(SDL_Window* window) {
    if (window == nullptr)
        return;
    // A window system that refuses leaves the pointer free, and the next
    // change of the window's state asks again.
    std::ignore =
        SDL_SetWindowMouseGrab(window, keeps_pointer_on_screen(SDL_GetWindowFlags(window)));
}

void release_pointer(SDL_Window* window) {
    // A grab the window system will not release ends when the window
    // closes.
    if (window != nullptr)
        std::ignore = SDL_SetWindowMouseGrab(window, false);
}

bool take_full_screen_event(
    SDL_Window* window, FullScreenSwitch& full_screen, const SDL_Event& event
) {
    // SDL has updated the window's flags by the time its event is read.
    if (window != nullptr && changes_pointer_bounds(event))
        keep_pointer_on_screen(window);
    if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
        event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        const bool entered = event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        full_screen.note_shown(entered);
        if (!entered)
            full_screen.note_left(SDL_GetTicks());
    }
    if (is_window_event(event)) {
        if (window != nullptr)
            follow_window_place(window, full_screen);
        return false;
    }
    switch (full_screen.take_key(event)) {
    case FullScreenKey::none:
        return false;
    case FullScreenKey::toggle:
        if (window == nullptr)
            return true;
        // The display is noted before the window leaves full screen; a
        // window system that switches at once has placed the window as a
        // window when the switch returns.
        follow_window_place(window, full_screen);
        if (switch_full_screen(window, full_screen))
            follow_window_place(window, full_screen);
        else
            std::cerr << "open-annihilation: switching full screen failed: " << SDL_GetError()
                      << '\n';
        return true;
    case FullScreenKey::repeat:
    case FullScreenKey::release:
        return true;
    }
    return false;
}

} // namespace oa::app
