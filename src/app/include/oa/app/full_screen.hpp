// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Full screen and a window: the flags the game's window opens with, the
// Alt+Enter key that switches between them, and the mode the player last
// asked for while the window system is still switching to it.
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
struct FullScreenSwitch {
    bool requested{};        ///< the mode last asked for: true for full screen
    bool awaiting_shown{};   ///< the window has not yet shown the requested mode
    uint64_t requested_ms{}; ///< when the mode was asked for, SDL_GetTicks milliseconds
    /// The Enter key of the last Alt+Enter while it is held; SDL_SCANCODE_UNKNOWN when none is.
    SDL_Scancode held_enter{SDL_SCANCODE_UNKNOWN};

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
    /// @param full_screen true for full screen, false for a window
    /// @param now_ms the time of the request, SDL_GetTicks milliseconds
    void note_request(bool full_screen, uint64_t now_ms) {
        requested = full_screen;
        awaiting_shown = true;
        requested_ms = now_ms;
    }

    /// Notes that the window entered or left full screen.
    ///
    /// A change to the requested mode ends the wait; a change on the way to it
    /// (entering a space that a later request leaves again) does not.
    ///
    /// @param shown true when the window entered full screen
    void note_shown(bool shown) {
        if (awaiting_shown && shown == requested)
            awaiting_shown = false;
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

/// Handles an event of the full-screen switch before anything else sees it.
///
/// Alt+Enter switches the mode, and a switch SDL refuses is reported on
/// standard error; its repeats and its Enter key's release do nothing
/// (FullScreenSwitch::take_key). The window's entering or leaving full screen
/// is noted, and the caller still handles it.
///
/// @param window the game's window; null ignores Alt+Enter
/// @param[in,out] full_screen the mode last asked for and the Enter key held
/// @param event any SDL event
/// @return true when the event was Alt+Enter, one of its repeats or its
///         Enter key's release, which nothing else may see
bool take_full_screen_event(
    SDL_Window* window, FullScreenSwitch& full_screen, const SDL_Event& event
);

} // namespace oa::app
