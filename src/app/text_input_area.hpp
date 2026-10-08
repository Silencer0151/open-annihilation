// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where a focused text field is handed to the system: the canvas rectangle
// converted to the window's coordinates, then an area the caller gives. An
// extension does not see this. It calls set_focused_text_field in
// extension.hpp, and that hands the rectangle to SDL.
#pragma once

#include <SDL3/SDL.h>

namespace oa::app {

struct TextField;

/// The field's place in the window's own coordinates, and the caret's
/// distance from its left.
struct TextInputArea {
    int x{};
    int y{};
    int width{};
    int height{};
    /// the caret's distance from the field's left, in the window's pixels
    int cursor{};
};

/// Where a field's place is handed.
struct TextInputAreaTarget {
    void* context{};
    /// Puts the area, or clears it when area is null. It must not throw.
    ///
    /// @param context TextInputAreaTarget::context
    /// @param area the area; null clears it
    /// @return false when the area could not be put
    bool (*set_area)(void* context, const TextInputArea* area){};
};

/// Puts a field's place through target, in the window's coordinates.
///
/// A null field, or one whose width or height is not positive, clears the
/// area. With a null renderer the canvas's pixels are the window's. The
/// caret is the field's cursor pixels from the field's left in the canvas,
/// and that distance in the window after the canvas is converted. A corner
/// the renderer cannot convert clears the area.
///
/// @param field the field, in canvas pixels; null clears the area
/// @param renderer the renderer whose output the canvas is; null takes the
///        canvas as the window's coordinates
/// @param target where the area is put; a null set_area puts nothing
/// @return false when set_area is null or returns false
[[nodiscard]] bool set_text_input_area_with(
    const TextField* field, SDL_Renderer* renderer, const TextInputAreaTarget& target
);

} // namespace oa::app
