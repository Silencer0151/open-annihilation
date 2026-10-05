// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Text input with the field's place given to the system, so that an
// on-screen keyboard (Steam's in Game Mode, the system's on a desktop)
// opens clear of it, and an input method's candidates stand beside it.
#include "oa/app/runtime.hpp"
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <optional>

namespace oa::app {

namespace {

/// The character the cursor stands after in the field, as SDL_SetTextInputArea
/// counts from the field's left: the field's start, since the game keeps no
/// cursor inside a typed line.
constexpr int text_cursor_offset = 0;

/// Returns a field's rectangle in the window's own coordinates, from its
/// rectangle in canvas pixels.
///
/// @param renderer the renderer whose output the canvas is; null takes the
///     canvas as the window's coordinates
/// @param field the field, in canvas pixels
/// @return the rectangle, at least one point across and down; nothing when
///     the renderer cannot convert the field's corners
std::optional<SDL_Rect>
window_area(SDL_Renderer* renderer, const oa::ui::display_layout::Rect& field) {
    float left = static_cast<float>(field.x);
    float top = static_cast<float>(field.y);
    float right = static_cast<float>(field.x + field.width);
    float bottom = static_cast<float>(field.y + field.height);
    if (renderer != nullptr &&
        (!SDL_RenderCoordinatesToWindow(renderer, left, top, &left, &top) ||
         !SDL_RenderCoordinatesToWindow(renderer, right, bottom, &right, &bottom)))
        return std::nullopt;
    const int x = static_cast<int>(std::floor(left));
    const int y = static_cast<int>(std::floor(top));
    return SDL_Rect{
        x,
        y,
        std::max(static_cast<int>(std::ceil(right)) - x, 1),
        std::max(static_cast<int>(std::ceil(bottom)) - y, 1),
    };
}

} // namespace

void Runtime::start_text_input(std::optional<oa::ui::display_layout::Rect> field) {
    if (sdl_.window == nullptr)
        return;
    // The field's place first, so that a keyboard opening with the input
    // already knows where not to stand; without a field, or where the
    // system takes no place, it places the keyboard as it would.
    std::optional<SDL_Rect> area;
    if (field && field->width > 0 && field->height > 0)
        area = window_area(sdl_.renderer, *field);
    static_cast<void>(
        SDL_SetTextInputArea(sdl_.window, area ? &*area : nullptr, text_cursor_offset)
    );
    SDL_StartTextInput(sdl_.window);
}

void Runtime::stop_text_input() {
    if (sdl_.window != nullptr)
        SDL_StopTextInput(sdl_.window);
}

} // namespace oa::app
