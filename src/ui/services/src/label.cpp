// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/services/label.hpp"

#include <cstdint>

namespace oa::ui::services {
namespace {

/// Selects the text colour and makes the clear glyph bits transparent.
///
/// @param[in,out] text_state text drawing state; background takes transparent unless that is
///        text_color_keep
/// @param color new text colour, or text_color_keep
void select_color(TextState* text_state, int32_t color) noexcept {
    const int32_t transparent = text_state->transparent;
    if (color != text_color_keep) {
        text_state->color = color;
    }
    if (transparent != text_color_keep) {
        text_state->background = transparent;
    }
}

void draw_outlined(
    TextState* text_state,
    const LabelDraw* draw,
    Surface* target,
    const char* text,
    int32_t x,
    int32_t outline_color,
    int32_t text_color,
    int32_t y
) noexcept {
    select_color(text_state, outline_color);
    draw->draw_label(draw->context, target, text_state, text, x - 1, y, label_unbounded);
    draw->draw_label(draw->context, target, text_state, text, x + 1, y, label_unbounded);
    draw->draw_label(draw->context, target, text_state, text, x, y - 1, label_unbounded);
    draw->draw_label(draw->context, target, text_state, text, x, y + 1, label_unbounded);
    select_color(text_state, text_color);
    draw->draw_label(draw->context, target, text_state, text, x, y, label_unbounded);
}

} // namespace

void label_draw_outlined(
    TextState* text_state,
    const LabelDraw* draw,
    Surface* target,
    const char* text,
    int32_t outline_color,
    int32_t text_color,
    int32_t y
) noexcept {
    const int32_t width = draw->measure(draw->context, text_state->font, text);
    if (target != nullptr) {
        draw_outlined(
            text_state,
            draw,
            target,
            text,
            (target->width - width) >> 1,
            outline_color,
            text_color,
            y
        );
        return;
    }
    Surface screen{};
    if (!draw->lock_screen(draw->context, &screen)) {
        return;
    }
    draw_outlined(
        text_state, draw, &screen, text, (screen.width - width) >> 1, outline_color, text_color, y
    );
    draw->unlock_screen(draw->context);
}

} // namespace oa::ui::services
