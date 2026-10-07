// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Full screen and a window: the flags the game's window opens with, among
// them the display's own pixel density, and what a window at that density
// changes; the Alt+Enter key that switches between full screen and a window,
// the mode the player last asked for while the window system is still
// switching to it, the pointer kept on the game's screen in full screen, the
// window brought back onto its display when it leaves full screen, and the
// window's title bar and borders hidden while a game is played.
#pragma once

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>

namespace oa::app {

/// How long a requested switch may take before the window's own report of its
/// mode is trusted again, in milliseconds.
///
/// A macOS full-screen space animates in and out for under a second; a window
/// manager that ignores the request never answers at all.
inline constexpr uint64_t full_screen_settle_ms = 2000;

/// How far inside its display a window that left full screen is put back, in
/// percent of the display's usable width (the left and right sides) or height
/// (the top and bottom); fractions of a pixel are dropped.
inline constexpr int window_margin_percent = 5;

/// Returns where a window goes so that it lies on its display.
///
/// Each side of the window outside the display's usable area moves to
/// window_margin_percent of the area's width or height inside it, and a
/// window with no side outside stays where it is; a side exactly on the
/// area's edge is inside. A window wider or taller than the room between two
/// margins keeps the left or top margin, so that its title bar and its
/// controls are on the display, and shrinks to fit between the margins when
/// it may be resized; one that may not keeps its size and the left or top
/// margin only.
///
/// @param window the window's frame (its contents with its title bar and
///        borders), in the desktop's coordinates, which are negative left of
///        and above the primary display
/// @param usable the display's usable area (SDL_GetDisplayUsableBounds), without
///        the menu bar, the dock or the taskbar
/// @param resizable whether the window may be resized
/// @return the window's frame on the display
[[nodiscard]] SDL_Rect
window_on_display(const SDL_Rect& window, const SDL_Rect& usable, bool resizable);

/// Returns where a window that keeps its size goes so that it lies on its
/// display.
///
/// On each axis a window that fits the display's usable area moves no more
/// than it must to lie within it, and one that lies within it stays where
/// it is; one longer than the area starts at the area's start, so that its
/// title bar and its controls are on the display.
///
/// @param window the window's frame (its contents with its title bar and
///        borders), in the desktop's coordinates, which are negative left of
///        and above the primary display
/// @param usable the display's usable area (SDL_GetDisplayUsableBounds), without
///        the menu bar, the dock or the taskbar
/// @return the window's frame on the display, of the window's size
[[nodiscard]] SDL_Rect window_at_size_on_display(const SDL_Rect& window, const SDL_Rect& usable);

/// What a keyboard event means to the full-screen switch.
enum class FullScreenKey {
    none,    ///< not Alt+Enter; the screens receive it
    toggle,  ///< a fresh Alt+Enter or Alt+keypad Enter: switch the mode
    repeat,  ///< the held key's repeats: nothing happens, and no screen sees them
    release, ///< the release of the Enter key that switched: no screen sees it
};

/// Returns the window flags the game's window is created with.
///
/// A window's pixel density is fixed when it opens: one opened at native
/// density keeps the display's own pixels for the run, and any other the
/// window system's density, which on a high-density display enlarges the
/// game's frame.
///
/// @param start_full_screen whether the run opens full screen
///        (Options::start_full_screen)
/// @param native_density whether the window opens at the display's own
///        pixel density (render_policy::decide_native_density)
/// @return a resizable window, full screen on the desktop's mode when asked,
///         at native density when asked
[[nodiscard]] inline SDL_WindowFlags
game_window_flags(bool start_full_screen, bool native_density) {
    return SDL_WINDOW_RESIZABLE | (start_full_screen ? SDL_WINDOW_FULLSCREEN : 0) |
           (native_density ? SDL_WINDOW_HIGH_PIXEL_DENSITY : 0);
}

/// Tells whether a window opened at native density, where a pixel of the
/// match's layout is a window point and the renderer enlarges what is drawn
/// to the display's pixels.
///
/// @param window_flags the window's flags, as SDL_GetWindowFlags reports them
/// @return true for a window game_window_flags opened at native density
[[nodiscard]] inline bool at_native_density(SDL_WindowFlags window_flags) {
    return (window_flags & SDL_WINDOW_HIGH_PIXEL_DENSITY) != 0;
}

/// Returns how deep the band along the screen's edges is in which the
/// pointer scrolls the battlefield: the layout pixels one window point
/// covers, since the pointer rests on the outermost point, not the outermost
/// pixel.
///
/// @param native_density the window opened at native density
///        (at_native_density), where a layout pixel is a window point
/// @param pixel_density the layout pixels a window point covers: the
///        window's pixels per window point (SDL_GetWindowPixelDensity), or
///        for a frame scaled to the screen that times the frame's pixels per
///        pixel it is presented at
/// @return 1 at native density; otherwise the pixel density rounded up
[[nodiscard]] inline int32_t edge_scroll_depth(bool native_density, float pixel_density) {
    return native_density ? 1 : static_cast<int32_t>(std::ceil(pixel_density));
}

/// Tells whether an event is the Alt+Enter that switches full screen.
///
/// Either Alt key counts (Option on macOS), with Return or keypad Enter.
///
/// @param event any SDL event
/// @return what the event means to the switch
[[nodiscard]] inline FullScreenKey full_screen_key(const SDL_Event& event) {
    if (event.type != SDL_EVENT_KEY_DOWN || (event.key.mod & SDL_KMOD_ALT) == 0 ||
        (event.key.key != SDLK_RETURN && event.key.key != SDLK_KP_ENTER))
        return FullScreenKey::none;
    return event.key.repeat ? FullScreenKey::repeat : FullScreenKey::toggle;
}

/// The window's full-screen mode as the player last asked for it, and the Enter key that asked.
///
/// On macOS, X11 and Wayland the window changes mode some time after it is
/// asked to, and until then it still reports its old mode; a second Alt+Enter
/// in that time switches from the mode the player asked for, not from the one
/// still shown. Windows switches at once.
///
/// The Enter key of an Alt+Enter belongs to the switch until it comes up:
/// letting go of Alt first does not hand its repeats to the screens as Enter.
///
/// A window that leaves full screen is brought back onto the display it was
/// full screen on (window_on_display) once the window system has given it its
/// place as a window, which an X11 window manager does some time after the
/// window has left (bring_window_on_display).
struct FullScreenSwitch {
    bool requested{};        ///< the mode last asked for: true for full screen
    bool awaiting_shown{};   ///< the window has not yet shown the requested mode
    uint64_t requested_ms{}; ///< when the mode was asked for, SDL_GetTicks milliseconds
    /// The Enter key of the last Alt+Enter while it is held; SDL_SCANCODE_UNKNOWN when none is.
    SDL_Scancode held_enter{SDL_SCANCODE_UNKNOWN};
    /// The display the window was last seen full screen on; 0 before it was.
    SDL_DisplayID full_screen_display{};
    /// The window left full screen, or was asked to, and its place as a window
    /// is still to be checked.
    bool placing_window{};
    /// When the window left full screen or was asked to, SDL_GetTicks milliseconds.
    uint64_t left_ms{};
    /// The screen size applied this run (apply_screen_size in
    /// screen_mode.hpp), in the window system's units: what full screen
    /// shows, through a display mode of the size or a frame drawn at it and
    /// scaled to the screen; 0 by 0 for the desktop's own.
    int32_t screen_width{};
    int32_t screen_height{}; ///< the screen size's height, as screen_width
    /// The size the window takes once it leaves full screen, in the window
    /// system's units, until it enters full screen again; 0 by 0 keeps the
    /// size it comes back at.
    int32_t window_width{};
    int32_t window_height{}; ///< that size's height, as window_width

    /// Tells what a keyboard event means to the switch, following the Enter key an Alt+Enter holds.
    ///
    /// A fresh Alt+Enter holds its Enter key. Until that key comes up, its
    /// repeats are repeats and its release is taken, with Alt held or not; a
    /// fresh press of it without Alt (its release was lost) lets it go and
    /// reaches the screens.
    ///
    /// @param event any SDL event
    /// @return what the event means to the switch
    [[nodiscard]] FullScreenKey take_key(const SDL_Event& event) {
        const bool key_event = event.type == SDL_EVENT_KEY_DOWN || event.type == SDL_EVENT_KEY_UP;
        const bool held =
            key_event && held_enter != SDL_SCANCODE_UNKNOWN && event.key.scancode == held_enter;
        if (held && event.type == SDL_EVENT_KEY_UP) {
            held_enter = SDL_SCANCODE_UNKNOWN;
            return FullScreenKey::release;
        }
        if (held && event.key.repeat)
            return FullScreenKey::repeat;
        const auto key = full_screen_key(event);
        if (key == FullScreenKey::toggle)
            held_enter = event.key.scancode;
        else if (held)
            held_enter = SDL_SCANCODE_UNKNOWN;
        return key;
    }

    /// Chooses the mode an Alt+Enter asks for.
    ///
    /// @param shown whether the window reports full screen now
    /// @param now_ms the time of the key, SDL_GetTicks milliseconds
    /// @return true to ask for full screen, false for a window
    [[nodiscard]] bool next_mode(bool shown, uint64_t now_ms) const {
        const bool settling = awaiting_shown && now_ms - requested_ms < full_screen_settle_ms;
        return !(settling ? requested : shown);
    }

    /// Records a mode the window was asked for.
    ///
    /// Asking for a window starts the check of the window's place as a
    /// window; asking for full screen ends it.
    ///
    /// @param full_screen true for full screen, false for a window
    /// @param now_ms the time of the request, SDL_GetTicks milliseconds
    void note_request(bool full_screen, uint64_t now_ms) {
        requested = full_screen;
        awaiting_shown = true;
        requested_ms = now_ms;
        placing_window = !full_screen;
        left_ms = now_ms;
    }

    /// Notes that the window entered or left full screen.
    ///
    /// A change to the requested mode ends the wait; a change on the way to it
    /// (entering a space that a later request leaves again) does not. Entering
    /// full screen ends the check of the window's place as a window, and
    /// forgets the size asked for as the window leaves it (window_width): the
    /// window comes back at the size it went in at.
    ///
    /// @param shown true when the window entered full screen
    void note_shown(bool shown) {
        if (awaiting_shown && shown == requested)
            awaiting_shown = false;
        if (shown) {
            placing_window = false;
            window_width = 0;
            window_height = 0;
        }
    }

    /// Notes that the window left full screen, after note_shown: its place as
    /// a window is checked from now on, unless it is leaving on its way back
    /// to the full screen a later request asked for.
    ///
    /// @param now_ms the time the window left, SDL_GetTicks milliseconds
    void note_left(uint64_t now_ms) {
        placing_window = !(awaiting_shown && requested);
        left_ms = now_ms;
    }

    /// Tells whether the window's place as a window is still to be checked.
    ///
    /// The check lasts full_screen_settle_ms from the time the window left
    /// full screen or was asked to; a window the window system has not placed
    /// by then is left where it is.
    ///
    /// @param now_ms the time now, SDL_GetTicks milliseconds
    /// @return true while the window's place is still to be checked
    [[nodiscard]] bool places_window(uint64_t now_ms) const {
        return placing_window && now_ms - left_ms < full_screen_settle_ms;
    }
};

/// Asks a window for the mode Alt+Enter switches to.
///
/// Switches from the mode last asked for while the window is still changing
/// to it, and from the mode the window shows otherwise.
///
/// @param window the game's window
/// @param[in,out] full_screen the mode last asked for; left as it was when
///        SDL refuses
/// @return true when SDL accepted the request; false leaves the window as it
///         was, with SDL_GetError saying why
[[nodiscard]] bool switch_full_screen(SDL_Window* window, FullScreenSwitch& full_screen);

/// Tells whether the pointer is kept on the game's window.
///
/// In full screen with the input focus the pointer stops at the edges of the
/// game's screen, as in 3.1c: it can rest against an edge, on its last row or
/// column of pixels, and never strays onto another monitor. A window, and full
/// screen without the focus (another program switched to, or a dialog of the
/// system's in front), leave it free.
///
/// @param window_flags the window's flags, as SDL_GetWindowFlags reports them
/// @return true to keep the pointer on the window
[[nodiscard]] inline bool keeps_pointer_on_screen(SDL_WindowFlags window_flags) {
    return (window_flags & SDL_WINDOW_FULLSCREEN) != 0 &&
           (window_flags & SDL_WINDOW_INPUT_FOCUS) != 0;
}

/// Tells whether an event may change what keeps_pointer_on_screen says.
///
/// These are a window's entering or leaving full screen, a change of its size
/// or of the display it is on, its being shown, hidden, minimised or restored,
/// and its gaining or losing the input focus.
///
/// @param event any SDL event
/// @return true for those window events
[[nodiscard]] inline bool changes_pointer_bounds(const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_WINDOW_ENTER_FULLSCREEN:
    case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN:
    case SDL_EVENT_WINDOW_RESIZED:
    case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
    case SDL_EVENT_WINDOW_DISPLAY_CHANGED:
    case SDL_EVENT_WINDOW_SHOWN:
    case SDL_EVENT_WINDOW_HIDDEN:
    case SDL_EVENT_WINDOW_MINIMIZED:
    case SDL_EVENT_WINDOW_RESTORED:
    case SDL_EVENT_WINDOW_FOCUS_GAINED:
    case SDL_EVENT_WINDOW_FOCUS_LOST:
        return true;
    default:
        return false;
    }
}

/// Keeps the pointer on the game's window, or lets it go, as
/// keeps_pointer_on_screen says for the window's flags now.
///
/// The window system holds the pointer inside the whole window and follows
/// the window's size and place. While the window is without the focus the
/// pointer is free, whatever was asked last. A window system that cannot hold
/// the pointer leaves it free, as in a window.
///
/// @param window the game's window; null does nothing
void keep_pointer_on_screen(SDL_Window* window);

/// Lets the pointer leave the game's window, before the window closes or
/// before a dialog of the system's opens over it.
///
/// keep_pointer_on_screen holds it again.
///
/// @param window the game's window; null does nothing
void release_pointer(SDL_Window* window);

/// Brings a window that has left full screen back onto a display, once the
/// window system has given it its place as a window.
///
/// The window's frame, its title bar and borders included where the window
/// system reports them, is placed on the display's usable area as
/// window_on_display says; a window that may be resized and is too large is
/// resized, and the screen is laid out again at its new size as after any
/// resize. A size asked for is taken first, and kept where the window is
/// placed (window_at_size_on_display); a maximised window is brought back
/// to a size of its own for it. A window in full screen, minimised or
/// hidden, or one whose decorations the window system has yet to put back
/// (an X11 window manager still restoring it), is not yet placed. A
/// maximised window, which the window system fits to the display itself,
/// is left as it is when no size is asked for, and so is a window on a
/// window system that places windows itself (Wayland), which refuses to
/// move it.
///
/// @param window the game's window
/// @param display the display the window was full screen on; 0, or a display
///        no longer connected, takes the display the window is on now
/// @param width the width the window takes, in the window system's units;
///        0 keeps its own
/// @param height the height the window takes; 0 keeps its own
/// @return true when the window's place is settled: it was on the display,
///         or it was moved or resized onto it, or it is left as it is; false
///         while the window system is still placing it
bool bring_window_on_display(
    SDL_Window* window, SDL_DisplayID display, int width = 0, int height = 0
);

/// Moves a window that has just been asked for a size so that, at that
/// size, it lies on the display it is on (window_at_size_on_display), its
/// title bar and borders included where the window system reports them. A
/// window in full screen, minimised or hidden is left where it is, and so
/// is a window on a window system that places windows itself (Wayland).
///
/// @param window the game's window; null does nothing
/// @param width the window's width asked for, in the window system's units
/// @param height the window's height asked for
void keep_window_on_display(SDL_Window* window, int width, int height);

/// What the game's window is asked to do with its frame: its title bar and
/// borders.
enum class WindowFrameRequest : uint8_t {
    none, ///< nothing: the window already shows what it should, or has no frame to change
    show, ///< show the title bar and borders
    hide, ///< hide them
};

/// Where the game is, as the window's frame follows it.
struct WindowFramePlace {
    /// The game has a window that is neither in full screen nor still
    /// switching to or from it (FullScreenSwitch::awaiting_shown within
    /// full_screen_settle_ms); a run without a window has none.
    bool windowed{};
    /// A game is played: its battlefield shows, unfinished, with neither the
    /// game menu nor a panel it opens, a team panel or the exit confirmation
    /// over it.
    bool playing{};
    /// The window shows its title bar and borders now (no
    /// SDL_WINDOW_BORDERLESS among its flags).
    bool bordered{};
};

/// Returns what to ask of the game's window's frame (the Window frame
/// setting).
///
/// With Window frame at Hidden in play, the window hides its title bar and
/// borders while a game is played, and shows them on every other screen
/// and while the game menu or a panel it opens is over the game, so that
/// the window can be moved and closed there; at Always shown it shows them
/// everywhere. Full screen, a window still switching to or from it, and a
/// run without a window, are left as they are.
///
/// @param hidden_in_play Window frame is Hidden in play
/// @param place where the game is, and what the window shows now
/// @return show or hide where the window shows otherwise; none where it
///         already shows what it should, or is not windowed
[[nodiscard]] constexpr WindowFrameRequest
window_frame_request(bool hidden_in_play, const WindowFramePlace& place) noexcept {
    if (!place.windowed)
        return WindowFrameRequest::none;
    const bool shown = !(hidden_in_play && place.playing);
    if (shown == place.bordered)
        return WindowFrameRequest::none;
    return shown ? WindowFrameRequest::show : WindowFrameRequest::hide;
}

/// Shows or hides a window's title bar and borders, keeping the window's
/// contents at their size and place.
///
/// A window system that changes the contents' size with the frame (macOS
/// keeps the frame's own) has the contents put back at the size and place
/// they had, so that the change reaches the game as no resize; a
/// maximised window, which the window system fits to the display itself,
/// is left at the size it is given.
///
/// @param window the game's window; null does nothing
/// @param shown true to show the title bar and borders, false to hide them
/// @return false when SDL refused, with SDL_GetError saying why
bool set_window_frame(SDL_Window* window, bool shown);

/// Handles an event of the full-screen switch before anything else sees it.
///
/// Alt+Enter switches the mode, and a switch SDL refuses is reported on
/// standard error; its repeats and its Enter key's release do nothing
/// (FullScreenSwitch::take_key). The window's entering or leaving full screen
/// is noted, and the caller still handles it. After an event that may change
/// it (changes_pointer_bounds), the pointer is kept on the window or let go
/// (keep_pointer_on_screen). The display a full-screen window is on is noted,
/// and after the window leaves full screen, its own events (its leaving,
/// moving and resizing among them) bring it back onto that display
/// (bring_window_on_display) once the window system has placed it, at the
/// size FullScreenSwitch::window_width and window_height ask for, if any.
///
/// @param window the game's window; null ignores Alt+Enter and leaves the
///        pointer and the window's place as they are
/// @param[in,out] full_screen the mode last asked for, the Enter key held and
///        the window's place still to be checked
/// @param event any SDL event
/// @return true when the event was Alt+Enter, one of its repeats or its
///         Enter key's release, which nothing else may see
bool take_full_screen_event(
    SDL_Window* window, FullScreenSwitch& full_screen, const SDL_Event& event
);

} // namespace oa::app
