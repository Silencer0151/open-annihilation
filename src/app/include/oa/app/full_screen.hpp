// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Full screen and a window: the flags the game's window opens with, the
// Alt+Enter key that switches between them, the mode the player last asked
// for while the window system is still switching to it, the pointer kept on
// the game's screen in full screen, and the window brought back onto its
// display when it leaves full screen.
#pragma once

#include <SDL3/SDL.h>

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

/// What a keyboard event means to the full-screen switch.
enum class FullScreenKey {
    none,    ///< not Alt+Enter; the screens receive it
    toggle,  ///< a fresh Alt+Enter or Alt+keypad Enter: switch the mode
    repeat,  ///< the held key's repeats: nothing happens, and no screen sees them
    release, ///< the release of the Enter key that switched: no screen sees it
};

/// Returns the window flags the game's window is created with.
///
/// @param start_full_screen whether the run opens full screen
///        (Options::start_full_screen)
/// @return a resizable window, full screen on the desktop's mode when asked
[[nodiscard]] inline SDL_WindowFlags game_window_flags(bool start_full_screen) {
    return SDL_WINDOW_RESIZABLE | (start_full_screen ? SDL_WINDOW_FULLSCREEN : 0);
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
    /// full screen ends the check of the window's place as a window.
    ///
    /// @param shown true when the window entered full screen
    void note_shown(bool shown) {
        if (awaiting_shown && shown == requested)
            awaiting_shown = false;
        if (shown)
            placing_window = false;
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
/// resize. A window in full screen, minimised or hidden, or one whose
/// decorations the window system has yet to put back (an X11 window manager
/// still restoring it), is not yet placed. A maximised window, which the
/// window system fits to the display itself, is left as it is, and so is a
/// window on a window system that places windows itself (Wayland), which
/// refuses to move it.
///
/// @param window the game's window
/// @param display the display the window was full screen on; 0, or a display
///        no longer connected, takes the display the window is on now
/// @return true when the window's place is settled: it was on the display,
///         or it was moved or resized onto it, or it is left as it is; false
///         while the window system is still placing it
bool bring_window_on_display(SDL_Window* window, SDL_DisplayID display);

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
/// (bring_window_on_display) once the window system has placed it.
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
