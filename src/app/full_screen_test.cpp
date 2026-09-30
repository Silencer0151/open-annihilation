// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Alt+Enter: which keys switch full screen, the Enter key it holds until that
// key comes up, the mode a press asks for while the window is still switching
// (as a macOS full-screen space, an X11 window manager or a Wayland
// compositor leaves it for a while), and a window of SDL's dummy video driver
// switched to full screen and back.
#include "oa/app/full_screen.hpp"

#include <SDL3/SDL.h>

#include <cstdint>
#include <cstdio>

namespace {

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using oa::app::FullScreenKey;
using oa::app::FullScreenSwitch;

// A press of Return, keypad Enter or A as SDL reports it.
SDL_Event key_down(SDL_Keycode key, SDL_Keymod mod, bool repeat) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = key;
    event.key.scancode = key == SDLK_KP_ENTER ? SDL_SCANCODE_KP_ENTER
                         : key == SDLK_RETURN ? SDL_SCANCODE_RETURN
                                              : SDL_SCANCODE_A;
    event.key.mod = mod;
    event.key.repeat = repeat;
    event.key.down = true;
    return event;
}

void test_keys() {
    using oa::app::full_screen_key;
    CHECK(full_screen_key(key_down(SDLK_RETURN, SDL_KMOD_LALT, false)) == FullScreenKey::toggle);
    CHECK(full_screen_key(key_down(SDLK_RETURN, SDL_KMOD_RALT, false)) == FullScreenKey::toggle);
    CHECK(full_screen_key(key_down(SDLK_KP_ENTER, SDL_KMOD_LALT, false)) == FullScreenKey::toggle);
    // Alt with Shift or Num Lock held still switches.
    CHECK(
        full_screen_key(key_down(SDLK_RETURN, SDL_KMOD_LALT | SDL_KMOD_LSHIFT, false)) ==
        FullScreenKey::toggle
    );
    CHECK(
        full_screen_key(key_down(SDLK_KP_ENTER, SDL_KMOD_RALT | SDL_KMOD_NUM, false)) ==
        FullScreenKey::toggle
    );
    // The held key's repeats are taken, but switch nothing.
    CHECK(full_screen_key(key_down(SDLK_RETURN, SDL_KMOD_LALT, true)) == FullScreenKey::repeat);
    CHECK(full_screen_key(key_down(SDLK_KP_ENTER, SDL_KMOD_RALT, true)) == FullScreenKey::repeat);
    // Enter alone, Alt with another key, and the release are the screens'.
    CHECK(full_screen_key(key_down(SDLK_RETURN, SDL_KMOD_NONE, false)) == FullScreenKey::none);
    CHECK(full_screen_key(key_down(SDLK_RETURN, SDL_KMOD_LCTRL, false)) == FullScreenKey::none);
    CHECK(full_screen_key(key_down(SDLK_A, SDL_KMOD_LALT, false)) == FullScreenKey::none);
    SDL_Event release = key_down(SDLK_RETURN, SDL_KMOD_LALT, false);
    release.type = SDL_EVENT_KEY_UP;
    release.key.down = false;
    CHECK(full_screen_key(release) == FullScreenKey::none);
}

// A key release as SDL reports it.
SDL_Event key_up(SDL_Keycode key, SDL_Keymod mod) {
    SDL_Event event = key_down(key, mod, false);
    event.type = SDL_EVENT_KEY_UP;
    event.key.down = false;
    return event;
}

// The Enter key of an Alt+Enter stays the switch's until it comes up, with
// Alt held or not.
void test_held_enter() {
    FullScreenSwitch full_screen{};
    CHECK(
        full_screen.take_key(key_down(SDLK_RETURN, SDL_KMOD_LALT, false)) == FullScreenKey::toggle
    );
    CHECK(full_screen.held_enter == SDL_SCANCODE_RETURN);
    // Alt let go of first: Enter's repeats and its release are still taken.
    CHECK(
        full_screen.take_key(key_down(SDLK_RETURN, SDL_KMOD_NONE, true)) == FullScreenKey::repeat
    );
    CHECK(
        full_screen.take_key(key_down(SDLK_RETURN, SDL_KMOD_LALT, true)) == FullScreenKey::repeat
    );
    // Other keys meanwhile are the screens'.
    CHECK(full_screen.take_key(key_down(SDLK_A, SDL_KMOD_NONE, false)) == FullScreenKey::none);
    CHECK(
        full_screen.take_key(key_down(SDLK_KP_ENTER, SDL_KMOD_NONE, true)) == FullScreenKey::none
    );
    CHECK(full_screen.take_key(key_up(SDLK_RETURN, SDL_KMOD_NONE)) == FullScreenKey::release);
    CHECK(full_screen.held_enter == SDL_SCANCODE_UNKNOWN);
    // Once Enter is up, Enter alone, its repeats and its release are the screens'.
    CHECK(full_screen.take_key(key_down(SDLK_RETURN, SDL_KMOD_NONE, false)) == FullScreenKey::none);
    CHECK(full_screen.take_key(key_down(SDLK_RETURN, SDL_KMOD_NONE, true)) == FullScreenKey::none);
    CHECK(full_screen.take_key(key_up(SDLK_RETURN, SDL_KMOD_NONE)) == FullScreenKey::none);
    // A fresh press without Alt after a lost release lets the key go.
    CHECK(
        full_screen.take_key(key_down(SDLK_KP_ENTER, SDL_KMOD_RALT, false)) == FullScreenKey::toggle
    );
    CHECK(full_screen.held_enter == SDL_SCANCODE_KP_ENTER);
    CHECK(
        full_screen.take_key(key_down(SDLK_KP_ENTER, SDL_KMOD_NONE, false)) == FullScreenKey::none
    );
    CHECK(full_screen.held_enter == SDL_SCANCODE_UNKNOWN);
    CHECK(
        full_screen.take_key(key_down(SDLK_KP_ENTER, SDL_KMOD_NONE, true)) == FullScreenKey::none
    );
}

void test_window_flags() {
    using oa::app::game_window_flags;
    CHECK(game_window_flags(false) == SDL_WINDOW_RESIZABLE);
    CHECK(game_window_flags(true) == (SDL_WINDOW_RESIZABLE | SDL_WINDOW_FULLSCREEN));
}

// A window system that switches at once: every press switches from the mode
// the window shows.
void test_switch_at_once() {
    FullScreenSwitch full_screen{};
    CHECK(full_screen.next_mode(false, 0));
    full_screen.note_request(true, 0);
    full_screen.note_shown(true);
    CHECK(!full_screen.awaiting_shown);
    CHECK(!full_screen.next_mode(true, 1));
    full_screen.note_request(false, 1);
    full_screen.note_shown(false);
    CHECK(full_screen.next_mode(false, 2));
}

// A window that is still switching reports its old mode: presses in that time
// switch from the mode last asked for.
void test_switch_while_changing() {
    FullScreenSwitch full_screen{};
    constexpr uint64_t pressed_ms = 10000;
    // The first press asks for full screen; the window still reports a window.
    CHECK(full_screen.next_mode(false, pressed_ms));
    full_screen.note_request(true, pressed_ms);
    // A second press during the change asks for a window, not full screen
    // again, though the window still reports a window.
    CHECK(!full_screen.next_mode(false, pressed_ms + 100));
    full_screen.note_request(false, pressed_ms + 100);
    // The window reaches full screen on the way back to a window: the wait
    // goes on, and a third press asks for full screen again.
    full_screen.note_shown(true);
    CHECK(full_screen.awaiting_shown);
    CHECK(full_screen.next_mode(true, pressed_ms + 300));
    // Leaving full screen then ends the wait for the window asked for.
    full_screen.note_shown(false);
    CHECK(!full_screen.awaiting_shown);
    CHECK(full_screen.next_mode(false, pressed_ms + 400));
}

// A request the window never answers (a window manager without full screen)
// is forgotten after full_screen_settle_ms, and the window's own report
// counts again.
void test_unanswered_request() {
    FullScreenSwitch full_screen{};
    full_screen.note_request(true, 0);
    CHECK(!full_screen.next_mode(false, oa::app::full_screen_settle_ms - 1));
    CHECK(full_screen.next_mode(false, oa::app::full_screen_settle_ms));
}

// The player's own switch outside the game (the green button or Control+
// Command+F on macOS, a window manager's key) is followed.
void test_switch_outside_the_game() {
    FullScreenSwitch full_screen{};
    full_screen.note_request(true, 0);
    full_screen.note_shown(true);
    full_screen.note_shown(false);
    CHECK(full_screen.next_mode(false, 1));
}

// Pumps the window's events through the switch, as the game's loop does.
void pump(FullScreenSwitch& full_screen, bool& entered, bool& left) {
    SDL_Event event{};
    while (SDL_PollEvent(&event)) {
        entered = entered || event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN;
        left = left || event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN;
        CHECK(!oa::app::take_full_screen_event(nullptr, full_screen, event));
    }
}

bool shows_full_screen(SDL_Window* window) {
    return (SDL_GetWindowFlags(window) & SDL_WINDOW_FULLSCREEN) != 0;
}

// A window of SDL's video driver (the dummy driver under ctest), switched by
// Alt+Enter, its repeats and keypad Enter.
void test_window() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        ++failures;
        return;
    }
    SDL_Window* window =
        SDL_CreateWindow("full screen test", 640, 480, oa::app::game_window_flags(false));
    CHECK(window != nullptr);
    if (window == nullptr) {
        SDL_Quit();
        return;
    }
    FullScreenSwitch full_screen{};
    bool entered = false;
    bool left = false;
    pump(full_screen, entered, left);
    CHECK(!shows_full_screen(window));

    CHECK(
        oa::app::take_full_screen_event(
            window, full_screen, key_down(SDLK_RETURN, SDL_KMOD_LALT, false)
        )
    );
    (void)SDL_SyncWindow(window);
    pump(full_screen, entered, left);
    CHECK(shows_full_screen(window));
    CHECK(entered);
    CHECK(!full_screen.awaiting_shown);
    int width = 0;
    int height = 0;
    SDL_Rect display{};
    CHECK(SDL_GetWindowSizeInPixels(window, &width, &height));
    CHECK(SDL_GetDisplayBounds(SDL_GetDisplayForWindow(window), &display));
    CHECK(width == display.w && height == display.h);

    // A held Alt+Enter's repeats are taken and change nothing.
    for (int repeat = 0; repeat < 3; ++repeat)
        CHECK(
            oa::app::take_full_screen_event(
                window, full_screen, key_down(SDLK_RETURN, SDL_KMOD_LALT, true)
            )
        );
    (void)SDL_SyncWindow(window);
    pump(full_screen, entered, left);
    CHECK(shows_full_screen(window));
    CHECK(!left);

    CHECK(
        oa::app::take_full_screen_event(
            window, full_screen, key_down(SDLK_KP_ENTER, SDL_KMOD_RALT, false)
        )
    );
    (void)SDL_SyncWindow(window);
    pump(full_screen, entered, left);
    CHECK(!shows_full_screen(window));
    CHECK(left);
    CHECK(SDL_GetWindowSizeInPixels(window, &width, &height));
    CHECK(width == 640 && height == 480);
    SDL_DestroyWindow(window);

    // A window created full screen starts in it, and Alt+Enter leaves it.
    window = SDL_CreateWindow("full screen test", 640, 480, oa::app::game_window_flags(true));
    CHECK(window != nullptr);
    if (window != nullptr) {
        FullScreenSwitch started{};
        CHECK(shows_full_screen(window));
        CHECK(
            oa::app::take_full_screen_event(
                window, started, key_down(SDLK_RETURN, SDL_KMOD_LALT, false)
            )
        );
        (void)SDL_SyncWindow(window);
        CHECK(!shows_full_screen(window));
        SDL_DestroyWindow(window);
    }
    SDL_Quit();
}

} // namespace

int main() {
    test_keys();
    test_held_enter();
    test_window_flags();
    test_switch_at_once();
    test_switch_while_changing();
    test_unanswered_request();
    test_switch_outside_the_game();
    test_window();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("full screen: Alt+Enter switches and the mode asked for is kept");
    return 0;
}
