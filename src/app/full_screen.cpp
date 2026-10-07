// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Alt+Enter: switching the game's window between full screen and a window,
// the pointer kept on the game's screen in full screen, the window brought
// back onto its display when it leaves full screen, and its title bar and
// borders shown or hidden.
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
    if (full_screen.places_window(SDL_GetTicks()) && bring_window_on_display(
                                                         window,
                                                         full_screen.full_screen_display,
                                                         full_screen.window_width,
                                                         full_screen.window_height
                                                     ))
        full_screen.placing_window = false;
}

/// Places one axis of a window's frame that keeps its size on the same
/// axis of a display's usable area, as window_at_size_on_display says.
///
/// @param window the frame's start and length on the axis
/// @param usable the usable area's start and length on the axis
/// @return the frame's start and length on the display
Span span_at_size_on_display(Span window, Span usable) {
    if (window.length > usable.length)
        return {usable.start, window.length};
    return {
        std::clamp(window.start, usable.start, usable.start + usable.length - window.length),
        window.length
    };
}

/// Reads a window's frame: its contents with its title bar and borders,
/// where the window system reports them, at a size asked for.
///
/// @param window the window
/// @param width the contents' width asked for; 0 for the window's own
/// @param height the contents' height asked for; 0 for the window's own
/// @param[out] frame the frame, in the desktop's coordinates
/// @param[out] left the left border's width
/// @param[out] top the title bar's and top border's height
/// @return false when the window system reports no place or size, or the
///     window's decorations are still to come back
bool read_window_frame(
    SDL_Window* window, int width, int height, SDL_Rect& frame, int& left, int& top
) {
    // The title bar and borders, where the window system reports them. An X11
    // window manager reports none while it puts a window's decorations back
    // after full screen, and the window's place as a window comes after them.
    top = 0;
    left = 0;
    int bottom = 0;
    int right = 0;
    if (SDL_GetWindowBordersSize(window, &top, &left, &bottom, &right) &&
        (SDL_GetWindowFlags(window) & SDL_WINDOW_BORDERLESS) == 0 && top == 0 && left == 0 &&
        bottom == 0 && right == 0)
        return false;
    SDL_Rect contents{};
    if (!SDL_GetWindowPosition(window, &contents.x, &contents.y) ||
        !SDL_GetWindowSize(window, &contents.w, &contents.h))
        return false;
    if (width > 0 && height > 0) {
        contents.w = width;
        contents.h = height;
    }
    frame = {
        contents.x - left, contents.y - top, contents.w + left + right, contents.h + top + bottom
    };
    return true;
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

SDL_Rect window_at_size_on_display(const SDL_Rect& window, const SDL_Rect& usable) {
    const Span across = span_at_size_on_display({window.x, window.w}, {usable.x, usable.w});
    const Span down = span_at_size_on_display({window.y, window.h}, {usable.y, usable.h});
    // Every start lies between the window's own and the usable area's, so
    // it fits an int.
    return {static_cast<int>(across.start), static_cast<int>(down.start), window.w, window.h};
}

void keep_window_on_display(SDL_Window* window, int width, int height) {
    if (window == nullptr || width <= 0 || height <= 0)
        return;
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0)
        return;
    SDL_Rect usable{};
    const SDL_DisplayID display = SDL_GetDisplayForWindow(window);
    SDL_Rect frame{};
    int left = 0;
    int top = 0;
    if (display == 0 || !SDL_GetDisplayUsableBounds(display, &usable) ||
        !read_window_frame(window, width, height, frame, left, top))
        return;
    const SDL_Rect placed = window_at_size_on_display(frame, usable);
    // A window system that places windows itself refuses the move.
    if (placed.x != frame.x || placed.y != frame.y)
        std::ignore = SDL_SetWindowPosition(window, placed.x + left, placed.y + top);
}

bool bring_window_on_display(SDL_Window* window, SDL_DisplayID display, int width, int height) {
    const SDL_WindowFlags flags = SDL_GetWindowFlags(window);
    if ((flags & (SDL_WINDOW_FULLSCREEN | SDL_WINDOW_MINIMIZED | SDL_WINDOW_HIDDEN)) != 0)
        return false;
    const bool sized = width > 0 && height > 0;
    if ((flags & SDL_WINDOW_MAXIMIZED) != 0) {
        if (!sized)
            return true;
        // A size asked for is taken once the window is back to a size of
        // its own; a window system that refuses leaves it maximised.
        return !SDL_RestoreWindow(window);
    }
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
    // The size asked for reaches the game as any resize does, and the screen
    // is laid out again at it; a window system that refuses it leaves the
    // window as it was, which the game keeps drawing at.
    if (sized)
        std::ignore = SDL_SetWindowSize(window, width, height);
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
    if (sized) {
        contents.w = width;
        contents.h = height;
    }
    const SDL_Rect frame{
        contents.x - left, contents.y - top, contents.w + left + right, contents.h + top + bottom
    };
    const SDL_Rect placed =
        sized ? window_at_size_on_display(frame, usable)
              : window_on_display(frame, usable, (flags & SDL_WINDOW_RESIZABLE) != 0);
    const int x = placed.x + left;
    const int y = placed.y + top;
    const int placed_width = std::max(1, placed.w - left - right);
    const int placed_height = std::max(1, placed.h - top - bottom);
    // A window system that places windows itself refuses the move, and the
    // window keeps its size as well.
    if ((x != contents.x || y != contents.y) && !SDL_SetWindowPosition(window, x, y))
        return true;
    // The window's new size reaches the game as any resize does, and the
    // screen is laid out again at it.
    // A window system that refuses the size leaves the window as it was,
    // which the game keeps drawing at.
    if (placed_width != contents.w || placed_height != contents.h)
        std::ignore = SDL_SetWindowSize(window, placed_width, placed_height);
    return true;
}

bool set_window_frame(SDL_Window* window, bool shown) {
    if (window == nullptr)
        return true;
    SDL_Rect before{};
    const bool read = SDL_GetWindowPosition(window, &before.x, &before.y) &&
                      SDL_GetWindowSize(window, &before.w, &before.h);
    if (!SDL_SetWindowBordered(window, shown))
        return false;
    SDL_Rect after{};
    if (!read || (SDL_GetWindowFlags(window) & SDL_WINDOW_MAXIMIZED) != 0 ||
        !SDL_GetWindowSize(window, &after.w, &after.h) ||
        (after.w == before.w && after.h == before.h))
        return true;
    // The contents go back where they were, then to their size, which a
    // window system that places windows itself refuses; the window is then
    // drawn at the size it is given, as after any resize.
    std::ignore = SDL_SetWindowPosition(window, before.x, before.y);
    std::ignore = SDL_SetWindowSize(window, before.w, before.h);
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
