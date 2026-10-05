// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A prompt of Open Annihilation's own: its text and buttons placed, and what
// pointer and key events do to it.

#include "oa/ui/engine_settings/prompt.hpp"

#include "geometry.hpp"
#include "notice_geometry.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

namespace {

/// Returns a text's estimated width: estimated_character_width a character.
///
/// @param text the text, UTF-8
/// @return the width, in source pixels
int32_t estimated_width(std::string_view text) {
    int32_t characters = 0;
    for (const char byte : text)
        if ((static_cast<unsigned char>(byte) & 0xC0U) != 0x80U)
            ++characters;
    return characters * estimated_character_width;
}

} // namespace

namespace notice_geometry {

std::vector<SourceRect> prompt_button_rects(const Prompt& prompt, int32_t height) {
    const int32_t top = footer_rule_of(height) + 1 + (footer_height - button_height) / 2;
    const std::size_t count = std::min(prompt.buttons.size(), most_prompt_buttons);
    std::vector<SourceRect> rects(count);
    int32_t right = notice_width - padding;
    for (std::size_t index = count; index-- > 0;) {
        const int32_t width = std::max(
            least_prompt_button_width,
            estimated_width(prompt.buttons[index].caption) + prompt_button_padding
        );
        rects[index] = {right - width, top, width, button_height};
        right -= width + button_gap;
    }
    return rects;
}

PlacedPrompt place_prompt(
    const Prompt& prompt,
    const std::function<int32_t(std::string_view)>& regular_width,
    const std::function<int32_t(std::string_view)>& small_width
) {
    PlacedPrompt placed{};
    const int32_t title_left = icon.x + icon.width + title_gap;
    placed.title = {title_left, edge, notice_width - padding - title_left, header_height};
    PlacedText text = place_text(prompt.paragraphs, prompt.failure, regular_width, small_width);
    int32_t bottom = text.bottom;
    if (prompt.progress >= 0) {
        bottom += paragraph_gap;
        placed.bar = {text_left, bottom, text_width, progress_bar_height};
        bottom += progress_bar_height;
    }
    placed.height = height_for(bottom);
    placed.footer_rule = footer_rule_of(placed.height);
    const std::size_t all_lines = text.lines.size();
    placed.lines = lines_that_fit(std::move(text.lines), placed.footer_rule);
    placed.cut = placed.lines.size() != all_lines ||
                 (prompt.progress >= 0 &&
                  placed.bar.y + placed.bar.height > placed.footer_rule - text_bottom_gap / 2);
    if (placed.cut && prompt.progress >= 0 &&
        placed.bar.y + placed.bar.height > placed.footer_rule - text_bottom_gap / 2)
        placed.bar = {};
    placed.buttons = prompt_button_rects(prompt, placed.height);
    return placed;
}

} // namespace notice_geometry

namespace {

/// Places a prompt in its fonts, or at estimated widths without them.
///
/// @param prompt the prompt
/// @param fonts the fonts; null to estimate
/// @return the placed prompt
notice_geometry::PlacedPrompt placed_in(const Prompt& prompt, const DialogFonts* fonts) {
    if (fonts == nullptr)
        return notice_geometry::place_prompt(prompt, estimated_width, estimated_width);
    return notice_geometry::place_prompt(
        prompt,
        [fonts](std::string_view text) {
            return dialog_text_width(*fonts, DialogFont::regular, text);
        },
        [fonts](std::string_view text) {
            return dialog_text_width(*fonts, DialogFont::small, text);
        }
    );
}

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the point's column
/// @param y the point's row
/// @return true inside it
bool inside(const notice_geometry::SourceRect& rect, int32_t x, int32_t y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Returns the button under a point.
///
/// @param prompt the prompt
/// @param height its height
/// @param x the point's column
/// @param y the point's row
/// @return the button's number; no_control for none
int32_t button_at(const Prompt& prompt, int32_t height, int32_t x, int32_t y) {
    const auto rects = notice_geometry::prompt_button_rects(prompt, height);
    for (std::size_t index = 0; index < rects.size(); ++index)
        if (inside(rects[index], x, y))
            return static_cast<int32_t>(index);
    return no_control;
}

/// Returns an answer.
///
/// @param prompt the prompt
/// @param button the button pressed
/// @return the answer; nothing for a button the prompt lacks
PromptAnswer answer(const Prompt& prompt, int32_t button) {
    if (button < 0 || static_cast<std::size_t>(button) >= prompt.buttons.size())
        return {};
    return {PromptAction::answered, button};
}

/// Returns a redraw, or nothing.
///
/// @param redraw whether the look changed
/// @return the answer
PromptAnswer look_changed(bool redraw) {
    return {redraw ? PromptAction::redraw : PromptAction::none, -1};
}

} // namespace

int32_t prompt_height(const Prompt& prompt, const DialogFonts* fonts) {
    return placed_in(prompt, fonts).height;
}

bool prompt_fits(const Prompt& prompt, const DialogFonts* fonts) {
    return !placed_in(prompt, fonts).cut;
}

std::vector<LayoutPart> prompt_layout(const Prompt& prompt, const DialogFonts* fonts) {
    const auto placed = placed_in(prompt, fonts);
    std::vector<LayoutPart> parts;
    parts.push_back(LayoutPart{notice_geometry::icon, {}, DialogFont::regular, 0, no_control});
    parts.push_back(
        LayoutPart{
            placed.title,
            prompt.title,
            DialogFont::regular,
            notice_geometry::title_tracking,
            no_control
        }
    );
    for (const auto& line : placed.lines)
        parts.push_back(
            LayoutPart{
                line.rect,
                line.text,
                line.path ? DialogFont::regular : DialogFont::small,
                0,
                no_control
            }
        );
    if (placed.bar.width > 0)
        parts.push_back(LayoutPart{placed.bar, {}, DialogFont::small, 0, prompt_bar_control});
    const auto rects = notice_geometry::prompt_button_rects(prompt, placed.height);
    for (std::size_t index = 0; index < rects.size(); ++index)
        parts.push_back(
            LayoutPart{
                rects[index],
                prompt.buttons[index].caption,
                DialogFont::small,
                0,
                static_cast<int32_t>(index)
            }
        );
    return parts;
}

PromptAnswer prompt_pointer_move(Prompt& prompt, int32_t x, int32_t y, int32_t height) {
    if (prompt.pressed != no_control) {
        x += prompt.finger_shift_x;
        y += prompt.finger_shift_y;
    }
    const int32_t hovered = button_at(prompt, height, x, y);
    if (hovered == prompt.hovered)
        return {};
    prompt.hovered = hovered;
    return look_changed(true);
}

PromptAnswer prompt_pointer_down(Prompt& prompt, int32_t x, int32_t y, int32_t height) {
    prompt.finger_shift_x = 0;
    prompt.finger_shift_y = 0;
    prompt.hovered = button_at(prompt, height, x, y);
    if (prompt.hovered == no_control)
        return {};
    prompt.pressed = prompt.hovered;
    return look_changed(true);
}

PromptAnswer
prompt_finger_down(Prompt& prompt, int32_t x, int32_t y, int32_t height, int32_t reach) {
    int32_t target_x = x;
    int32_t target_y = y;
    if (button_at(prompt, height, x, y) == no_control && reach > 0) {
        // The nearest button within reach, at its point nearest the finger.
        const int64_t within = int64_t{reach} * reach;
        int64_t best = within + 1;
        for (const auto& rect : notice_geometry::prompt_button_rects(prompt, height)) {
            const int32_t near_x = std::clamp(x, rect.x, rect.x + rect.width - 1);
            const int32_t near_y = std::clamp(y, rect.y, rect.y + rect.height - 1);
            const int64_t across = int64_t{near_x} - x;
            const int64_t down = int64_t{near_y} - y;
            const int64_t distance = across * across + down * down;
            if (distance < best) {
                best = distance;
                target_x = near_x;
                target_y = near_y;
            }
        }
    }
    const PromptAnswer pressed = prompt_pointer_down(prompt, target_x, target_y, height);
    if (pressed.action == PromptAction::redraw) {
        prompt.finger_shift_x = target_x - x;
        prompt.finger_shift_y = target_y - y;
    }
    return pressed;
}

PromptAnswer prompt_pointer_up(Prompt& prompt, int32_t x, int32_t y, int32_t height) {
    if (prompt.pressed != no_control) {
        x += prompt.finger_shift_x;
        y += prompt.finger_shift_y;
    }
    prompt.finger_shift_x = 0;
    prompt.finger_shift_y = 0;
    prompt.hovered = button_at(prompt, height, x, y);
    const int32_t held = prompt.pressed;
    prompt.pressed = no_control;
    if (held == no_control)
        return {};
    if (held != prompt.hovered)
        return look_changed(true);
    return answer(prompt, held);
}

PromptAnswer prompt_key(Prompt& prompt, DialogKey key) {
    const auto count = static_cast<int32_t>(std::min(prompt.buttons.size(), most_prompt_buttons));
    if (count == 0)
        return {};
    switch (key) {
    case DialogKey::enter:
    case DialogKey::space:
        return answer(prompt, prompt.marked);
    case DialogKey::escape:
    case DialogKey::no:
        return answer(prompt, prompt.cancel_button);
    case DialogKey::yes:
        return answer(prompt, prompt.primary_button);
    case DialogKey::left:
    case DialogKey::up:
    case DialogKey::back_tab:
        prompt.marked = (prompt.marked + count - 1) % count;
        return look_changed(count > 1);
    case DialogKey::right:
    case DialogKey::down:
    case DialogKey::tab:
        prompt.marked = (prompt.marked + 1) % count;
        return look_changed(count > 1);
    default:
        return {};
    }
}

} // namespace oa::ui::engine_settings
