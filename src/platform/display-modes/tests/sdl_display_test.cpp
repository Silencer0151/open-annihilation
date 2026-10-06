// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The sizes offered over what SDL's dummy video driver reports: no
// full-screen mode and a 1024x768 desktop, so the fixed sizes that fit it.

#include "oa/platform/display_modes/sdl.hpp"
#include "oa/test/check.hpp"

#include <SDL3/SDL.h>

#include <iostream>
#include <vector>

namespace {

namespace dm = oa::platform::display_modes;

void test_dummy_display() {
    const SDL_DisplayID primary = SDL_GetPrimaryDisplay();
    OA_CHECK(primary != 0);
    const dm::DisplayReport report = dm::read_display(primary);
    std::cout << "dummy display: " << report.modes.size() << " modes, desktop "
              << dm::size_text(report.desktop.size) << '\n';
    OA_CHECK(report.modes.empty());
    OA_CHECK(report.desktop.size == (dm::Size{1024, 768}));
    const std::vector<dm::Size> fitting{{640, 480}, {800, 600}, {1024, 768}};
    OA_CHECK(dm::offered_sizes(report, dm::Use::full_screen) == fitting);
    OA_CHECK(dm::offered_sizes(report, dm::Use::window) == fitting);
    // A display that reports no mode cannot be judged: every stored size
    // stays.
    OA_CHECK(dm::can_show(report, {2560, 1440}, dm::Use::full_screen));
    // No display reads as nothing at all.
    const dm::DisplayReport none = dm::read_display(0);
    OA_CHECK(none.modes.empty() && none.desktop.size == dm::Size{});
    // With no mode of the size, the window keeps the mode it has.
    SDL_Window* window = SDL_CreateWindow("display modes", 640, 480, 0);
    OA_CHECK(window != nullptr);
    OA_CHECK(!dm::take_full_screen_size(window, {1024, 768}));
    OA_CHECK(!dm::take_full_screen_size(nullptr, {1024, 768}));
    if (window != nullptr)
        SDL_DestroyWindow(window);
}

} // namespace

int main() {
    if (!SDL_Init(SDL_INIT_VIDEO)) {
        std::cerr << "SDL_Init: " << SDL_GetError() << '\n';
        return 1;
    }
    test_dummy_display();
    SDL_Quit();
    return oa::test::check_exit_status();
}
