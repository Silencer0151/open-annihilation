// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What a display reports, read through SDL, and the full-screen mode a
// window takes.
#include "oa/platform/display_modes/sdl.hpp"

#include <SDL3/SDL.h>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <tuple>

namespace oa::platform::display_modes {

namespace {

/// Frees the list SDL_GetFullscreenDisplayModes returns.
struct FreeModes {
    /// Frees the list.
    ///
    /// @param modes the list SDL returned; null does nothing
    void operator()(SDL_DisplayMode** modes) const noexcept { SDL_free(static_cast<void*>(modes)); }
};

/// A display's full-screen modes as SDL lists them.
using ModeList = std::unique_ptr<SDL_DisplayMode*, FreeModes>;

/// Returns a mode as the report keeps it.
///
/// @param mode SDL's mode
/// @return the mode
ReportedMode reported(const SDL_DisplayMode& mode) noexcept {
    ReportedMode result{};
    result.size = {mode.w, mode.h};
    result.pixel_density = mode.pixel_density > 0.0F ? mode.pixel_density : 1.0F;
    result.refresh_rate = mode.refresh_rate;
    return result;
}

/// Reads a display's modes and its report together.
///
/// @param display SDL's id of the display
/// @param[out] report what the display reports
/// @param[out] count the number of modes in the list
/// @return SDL's list, in the order the report keeps; null when it has none
ModeList read_modes(SDL_DisplayID display, DisplayReport& report, int& count) {
    report = {};
    count = 0;
    if (display == 0)
        return ModeList{};
    if (const SDL_DisplayMode* desktop = SDL_GetDesktopDisplayMode(display); desktop != nullptr)
        report.desktop = reported(*desktop);
    ModeList modes{SDL_GetFullscreenDisplayModes(display, &count)};
    if (!modes)
        count = 0;
    for (int index = 0; index < count; ++index)
        if (const SDL_DisplayMode* mode = modes.get()[index]; mode != nullptr)
            report.modes.push_back(reported(*mode));
        else
            report.modes.push_back({});
    return modes;
}

} // namespace

DisplayReport read_display(uint32_t display) {
    DisplayReport report;
    int count = 0;
    std::ignore = read_modes(static_cast<SDL_DisplayID>(display), report, count);
    return report;
}

bool take_full_screen_size(SDL_Window* window, Size size) {
    if (window == nullptr)
        return false;
    DisplayReport report;
    int count = 0;
    const ModeList modes = read_modes(SDL_GetDisplayForWindow(window), report, count);
    const auto chosen = mode_for(report, size);
    if (!chosen || *chosen >= static_cast<std::size_t>(count))
        return false;
    return SDL_SetWindowFullscreenMode(window, modes.get()[*chosen]);
}

} // namespace oa::platform::display_modes
