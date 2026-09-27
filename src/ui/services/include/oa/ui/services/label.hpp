// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Centred single-line label with a one-pixel outline, drawn through the
// text-drawing boundary into a surface or the locked screen.

#include "oa/present/surface.h"

#include <cstdint>

namespace oa::ui::services {

// Text drawing state of the display context.
struct TextState {
    const void* font; // metrics passed to measure
    int32_t color;    // colour of the set glyph bits of the next draw
    // Colour of the clear glyph bits; each colour change sets it to transparent.
    int32_t background;
    int32_t transparent; // the colour value a text draw never writes
};

// Colour value meaning "leave unchanged".
inline constexpr int32_t text_color_keep = -1;
// max_width passed to draw_label: no limit.
inline constexpr int32_t label_unbounded = -1;

struct LabelDraw {
    void* context;
    int32_t (*measure)(void* context, const void* font, const char* text);
    bool (*lock_screen)(void* context, Surface* out);
    void (*unlock_screen)(void* context);
    void (*draw_label)(
        void* context,
        Surface* target,
        const TextState* text_state,
        const char* text,
        int32_t x,
        int32_t y,
        int32_t max_width
    );
};

/// Draws a line of text centred horizontally with a one-pixel outline.
///
/// Draws four copies offset by one pixel in outline_color, then the text in
/// text_color. The colour left selected is text_color's.
///
/// @param[in,out] text_state text drawing state; its colour is changed
/// @param draw text-drawing boundary
/// @param target surface to draw into, or null for the locked screen
/// @param text NUL-terminated text
/// @param outline_color outline colour, or text_color_keep
/// @param text_color text colour, or text_color_keep
/// @param y row of the text's top edge, in target pixels
void label_draw_outlined(
    TextState* text_state,
    const LabelDraw* draw,
    Surface* target,
    const char* text,
    int32_t outline_color,
    int32_t text_color,
    int32_t y
) noexcept;

} // namespace oa::ui::services
