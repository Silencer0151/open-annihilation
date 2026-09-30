// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Alt+Enter: switching the game's window between full screen and a window.
#include "oa/app/full_screen.hpp"

#include <SDL3/SDL.h>

#include <iostream>

namespace oa::app {

bool switch_full_screen(SDL_Window* window, FullScreenSwitch& full_screen) {
    const bool shown = (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
    const bool wanted = full_screen.next_mode(shown, SDL_GetTicks());
    if (!SDL_SetWindowFullscreen(window, wanted))
        return false;
    full_screen.note_request(wanted, SDL_GetTicks());
    return true;
}

bool take_full_screen_event(
    SDL_Window* window, FullScreenSwitch& full_screen, const SDL_Event& event
) {
    if (event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
        event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        full_screen.note_shown(event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN);
        return false;
    }
    switch (full_screen.take_key(event)) {
    case FullScreenKey::none:
        return false;
    case FullScreenKey::toggle:
        if (window != nullptr && !switch_full_screen(window, full_screen))
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
