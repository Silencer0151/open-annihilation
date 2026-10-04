// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A notice of Open Annihilation's own: its text wrapped and placed, and what
// pointer and key events do to it.

#include "oa/ui/engine_settings/notice.hpp"

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

namespace notice_geometry {

Placed place_notice(
    const Notice& notice,
    const std::function<int32_t(std::string_view)>& regular_width,
    const std::function<int32_t(std::string_view)>& small_width
) {
    Placed placed{};
    const int32_t title_left = icon.x + icon.width + title_gap;
    placed.title = {title_left, edge, notice_width - padding - title_left, header_height};
    // The lines of the text and the failure, before the height is known.
    std::vector<Line> lines;
    int32_t row = text_top;
    bool first = true;
    const auto add = [&](const std::vector<std::string>& wrapped, bool path, bool failure) {
        if (!first)
            row += paragraph_gap;
        first = false;
        const int32_t line_height = path ? path_line_height : small_line_height;
        for (const auto& text : wrapped) {
            lines.push_back(Line{text, path, failure, {text_left, row, text_width, line_height}});
            row += line_height;
        }
    };
    for (const auto& paragraph : notice.paragraphs)
        add(paragraph.path ? wrap_path(paragraph.text, text_width, regular_width)
                           : wrap_text(paragraph.text, text_width, small_width),
            paragraph.path,
            false);
    if (!notice.failure.empty())
        add(wrap_text(notice.failure, text_width, small_width), false, true);
    placed.height = std::clamp(
        row + text_bottom_gap + 1 + footer_height + edge,
        least_notice_height,
        greatest_notice_height
    );
    placed.footer_rule = placed.height - edge - footer_height - 1;
    // A line the height cuts is left out.
    for (auto& line : lines)
        if (line.rect.y + line.rect.height <= placed.footer_rule - text_bottom_gap / 2)
            placed.lines.push_back(std::move(line));
    const int32_t button_top = placed.footer_rule + 1 + (footer_height - button_height) / 2;
    placed.ok_button = {notice_width - padding - ok_width, button_top, ok_width, button_height};
    placed.open_button = {
        placed.ok_button.x - button_gap - open_width, button_top, open_width, button_height
    };
    return placed;
}

} // namespace notice_geometry

namespace {

/// Returns the bytes of the UTF-8 character a text starts with.
///
/// @param text the text, not empty
/// @return 1 to 4; 1 for a byte that starts no sequence
std::size_t character_bytes(std::string_view text) noexcept {
    std::size_t bytes = 1;
    while (bytes < text.size() && bytes < 4 &&
           (static_cast<unsigned char>(text[bytes]) & 0xC0U) == 0x80U)
        ++bytes;
    return bytes;
}

/// Breaks a text that is wider than a line between its characters.
///
/// @param text the text
/// @param width the room
/// @param text_width a text's width
/// @param[in,out] lines receives every line but the last
/// @return the last line, which may take more after it
std::string break_characters(
    std::string_view text,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width,
    std::vector<std::string>& lines
) {
    std::string line;
    for (std::size_t at = 0; at < text.size();) {
        const std::size_t bytes = character_bytes(text.substr(at));
        const std::string_view character = text.substr(at, bytes);
        if (!line.empty() && text_width(line + std::string(character)) > width) {
            lines.push_back(line);
            line.clear();
        }
        line += character;
        at += bytes;
    }
    return line;
}

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

/// Places a notice in its fonts, or at estimated widths without them.
///
/// @param notice the notice
/// @param fonts the fonts; null to estimate
/// @return the placed notice
notice_geometry::Placed placed_in(const Notice& notice, const DialogFonts* fonts) {
    if (fonts == nullptr)
        return notice_geometry::place_notice(notice, estimated_width, estimated_width);
    return notice_geometry::place_notice(
        notice,
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

/// Returns a button's place, which depends on the notice's height alone.
///
/// @param height the notice's height
/// @param control notice_ok_control or notice_open_control
/// @return the button's rectangle, in source pixels from the notice's top left corner
notice_geometry::SourceRect button_rect(int32_t height, int32_t control) noexcept {
    const int32_t footer_rule = height - notice_geometry::edge - notice_geometry::footer_height - 1;
    const int32_t top =
        footer_rule + 1 + (notice_geometry::footer_height - notice_geometry::button_height) / 2;
    const notice_geometry::SourceRect ok_rect{
        notice_width - notice_geometry::padding - notice_geometry::ok_width,
        top,
        notice_geometry::ok_width,
        notice_geometry::button_height
    };
    if (control == notice_ok_control)
        return ok_rect;
    return {
        ok_rect.x - notice_geometry::button_gap - notice_geometry::open_width,
        top,
        notice_geometry::open_width,
        notice_geometry::button_height
    };
}

/// Returns the button under a point.
///
/// @param height the notice's height
/// @param x the point's column
/// @param y the point's row
/// @return notice_ok_control, notice_open_control or no_control
int32_t button_at(int32_t height, int32_t x, int32_t y) noexcept {
    for (const int32_t control : {notice_ok_control, notice_open_control})
        if (inside(button_rect(height, control), x, y))
            return control;
    return no_control;
}

/// Returns what pressing a button asks.
///
/// @param control the button
/// @return NoticeAction::closed for OK, open_folder for the open button
NoticeAction press(int32_t control) noexcept {
    return control == notice_open_control ? NoticeAction::open_folder : NoticeAction::closed;
}

} // namespace

std::vector<std::string> wrap_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        if (text[at] == ' ') {
            ++at;
            continue;
        }
        const auto end = std::min(text.find(' ', at), text.size());
        const std::string_view word = text.substr(at, end - at);
        at = end;
        const std::string joined =
            line.empty() ? std::string(word) : line + " " + std::string(word);
        if (text_width(joined) <= width) {
            line = joined;
            continue;
        }
        if (!line.empty())
            lines.push_back(line);
        line = text_width(word) <= width ? std::string(word)
                                         : break_characters(word, width, text_width, lines);
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

std::vector<std::string> wrap_path(
    std::string_view path, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < path.size()) {
        // A component and the separator after it.
        const auto separator = path.find_first_of("/\\", at);
        const auto end = separator == std::string_view::npos ? path.size() : separator + 1;
        const std::string_view piece = path.substr(at, end - at);
        at = end;
        if (text_width(line + std::string(piece)) <= width) {
            line += piece;
            continue;
        }
        if (!line.empty())
            lines.push_back(line);
        line = text_width(piece) <= width ? std::string(piece)
                                          : break_characters(piece, width, text_width, lines);
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

int32_t notice_height(const Notice& notice, const DialogFonts* fonts) {
    return placed_in(notice, fonts).height;
}

std::vector<LayoutPart> notice_layout(const Notice& notice, const DialogFonts* fonts) {
    const auto placed = placed_in(notice, fonts);
    std::vector<LayoutPart> parts;
    parts.push_back(LayoutPart{notice_geometry::icon, {}, DialogFont::regular, 0, no_control});
    parts.push_back(
        LayoutPart{
            placed.title,
            std::string(geometry::shown_text(notice.title)),
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
    parts.push_back(LayoutPart{placed.open_button, {}, DialogFont::small, 0, notice_open_control});
    parts.push_back(LayoutPart{placed.ok_button, {}, DialogFont::small, 0, notice_ok_control});
    return parts;
}

NoticeAction notice_pointer_move(Notice& notice, int32_t x, int32_t y, int32_t height) {
    // A finger's held press moves as it was moved to the button it took.
    if (notice.pressed != no_control) {
        x += notice.finger_shift_x;
        y += notice.finger_shift_y;
    }
    const int32_t hovered = button_at(height, x, y);
    if (hovered == notice.hovered)
        return NoticeAction::none;
    notice.hovered = hovered;
    return NoticeAction::redraw;
}

NoticeAction notice_pointer_down(Notice& notice, int32_t x, int32_t y, int32_t height) {
    notice.finger_shift_x = 0;
    notice.finger_shift_y = 0;
    notice.hovered = button_at(height, x, y);
    if (notice.hovered == no_control)
        return NoticeAction::none;
    notice.pressed = notice.hovered;
    return NoticeAction::redraw;
}

NoticeAction
notice_finger_down(Notice& notice, int32_t x, int32_t y, int32_t height, int32_t reach) {
    int32_t target_x = x;
    int32_t target_y = y;
    if (button_at(height, x, y) == no_control && reach > 0) {
        // The nearer button within reach, at its point nearest the finger.
        const int64_t within = int64_t{reach} * reach;
        int64_t best = within + 1;
        for (const int32_t control : {notice_ok_control, notice_open_control}) {
            const auto rect = button_rect(height, control);
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
    const NoticeAction action = notice_pointer_down(notice, target_x, target_y, height);
    if (action == NoticeAction::redraw) {
        notice.finger_shift_x = target_x - x;
        notice.finger_shift_y = target_y - y;
    }
    return action;
}

NoticeAction notice_pointer_up(Notice& notice, int32_t x, int32_t y, int32_t height) {
    // A finger's release lands as its press was moved; the next press
    // starts afresh.
    if (notice.pressed != no_control) {
        x += notice.finger_shift_x;
        y += notice.finger_shift_y;
    }
    notice.finger_shift_x = 0;
    notice.finger_shift_y = 0;
    notice.hovered = button_at(height, x, y);
    const int32_t held = notice.pressed;
    notice.pressed = no_control;
    if (held == no_control)
        return NoticeAction::none;
    if (held != notice.hovered)
        return NoticeAction::redraw;
    return press(held);
}

NoticeAction notice_key(Notice& notice, DialogKey key) {
    switch (key) {
    case DialogKey::enter:
    case DialogKey::escape:
        return NoticeAction::closed;
    case DialogKey::space:
        return press(notice.marked);
    case DialogKey::left:
    case DialogKey::right:
    case DialogKey::up:
    case DialogKey::down:
    case DialogKey::tab:
    case DialogKey::back_tab:
        notice.marked =
            notice.marked == notice_ok_control ? notice_open_control : notice_ok_control;
        return NoticeAction::redraw;
    default:
        return NoticeAction::none;
    }
}

} // namespace oa::ui::engine_settings
