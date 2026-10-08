// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A text field's place, converted from canvas pixels to the window's
// coordinates and handed to the caller. The game's own fields and an
// extension's share this conversion.
#include "text_input_area.hpp"

#include "oa/app/extension.hpp"

#include <algorithm>
#include <cmath>

namespace oa::app {

bool set_text_input_area_with(
    const TextField* field, SDL_Renderer* renderer, const TextInputAreaTarget& target
) {
    if (target.set_area == nullptr)
        return false;
    if (field == nullptr || field->width <= 0 || field->height <= 0)
        return target.set_area(target.context, nullptr);
    float left = static_cast<float>(field->x);
    float top = static_cast<float>(field->y);
    float right = static_cast<float>(field->x + field->width);
    float bottom = static_cast<float>(field->y + field->height);
    float caret = static_cast<float>(field->x + field->cursor);
    float caret_row = top;
    if (renderer != nullptr &&
        (!SDL_RenderCoordinatesToWindow(renderer, left, top, &left, &top) ||
         !SDL_RenderCoordinatesToWindow(renderer, right, bottom, &right, &bottom) ||
         !SDL_RenderCoordinatesToWindow(renderer, caret, caret_row, &caret, &caret_row)))
        return target.set_area(target.context, nullptr);
    const int x = static_cast<int>(std::floor(left));
    const int y = static_cast<int>(std::floor(top));
    const TextInputArea area{
        x,
        y,
        std::max(static_cast<int>(std::ceil(right)) - x, 1),
        std::max(static_cast<int>(std::ceil(bottom)) - y, 1),
        static_cast<int>(std::floor(caret)) - x,
    };
    return target.set_area(target.context, &area);
}

} // namespace oa::app
