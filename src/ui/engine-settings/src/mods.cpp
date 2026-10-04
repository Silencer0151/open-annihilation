// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mods: the list of the mods the game can play, in the order it shows them,
// each row's texts and place, and the Switch Mod question's text.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

namespace {

/// Tells whether a byte continues a UTF-8 character rather than starting one.
///
/// @param byte the byte
/// @return true for 10xxxxxx
bool continuing_byte(char byte) noexcept {
    return (static_cast<unsigned char>(byte) & 0xC0U) == 0x80U;
}

/// Returns a text in lower case, ASCII letters only, for ordering titles.
///
/// @param text the text
/// @return the text with A to Z lowered
std::string folded(std::string_view text) {
    std::string lowered(text);
    for (char& character : lowered)
        if (character >= 'A' && character <= 'Z')
            character = static_cast<char>(character - 'A' + 'a');
    return lowered;
}

} // namespace

std::vector<ModRow> mod_rows(const Dialog& dialog) {
    const std::size_t offered = std::min(dialog.mod_names.size(), dialog.mod_folders.size());
    int32_t playing = no_mod_row;
    for (std::size_t index = 0; index < offered; ++index)
        if (!dialog.playing_mod_folder.empty() &&
            dialog.mod_folders[index] == dialog.playing_mod_folder)
            playing = static_cast<int32_t>(index);
    std::vector<ModRow> rows;
    rows.reserve(offered + 1);
    rows.push_back(ModRow{playing, true});
    if (playing != no_mod_row)
        rows.push_back(ModRow{no_mod_row, false});
    std::vector<int32_t> others;
    for (std::size_t index = 0; index < offered; ++index)
        if (static_cast<int32_t>(index) != playing)
            others.push_back(static_cast<int32_t>(index));
    std::stable_sort(others.begin(), others.end(), [&dialog](int32_t left, int32_t right) {
        return folded(dialog.mod_names[static_cast<std::size_t>(left)]) <
               folded(dialog.mod_names[static_cast<std::size_t>(right)]);
    });
    for (const int32_t index : others)
        rows.push_back(ModRow{index, false});
    return rows;
}

namespace geometry {

bool mods_page(const Dialog& dialog) noexcept {
    // A check's own section takes the place of the list.
    const bool own_section =
        dialog.section_hooks != nullptr && dialog.section_hooks->settings != nullptr;
    return dialog.kind == DialogKind::engine && dialog.page == Page::mods && !own_section;
}

int32_t mods_folder_control(const Rows& rows) noexcept {
    return first_row_control + static_cast<int32_t>(rows.rows.size());
}

Rows place_mod_rows(const Dialog& dialog, int32_t scroll) {
    Rows placed{};
    const ScrollArea area = mods_scroll(dialog.locks.mod != Lock::none);
    const auto rows = mod_rows(dialog);
    const Lock lock = dialog.locks.mod;
    int32_t top = area.view.y;
    placed.rows.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = Setting::mod;
        row.control = first_row_control + static_cast<int32_t>(index);
        // A locked page keeps the mod played: every row is inert.
        row.lock = lock;
        row.top = top;
        row.height = mod_row_height + mod_row_gap;
        row.control_area = {content_left, top, content_width, mod_row_height};
        const int32_t text_left = content_left + mod_row_inset + mod_badge_side + mod_row_inset;
        const int32_t text_width = content_right - mod_row_inset - text_left;
        row.label = {text_left, top + 1, text_width, label_line_height};
        row.value = {text_left, top + 3, text_width, hint_line_height};
        row.hints[0] = {text_left, top + 15, text_width, hint_line_height};
        row.hint_lines = 1;
        top += row.height;
    }
    placed.bottom = top - mod_row_gap;
    scroll_rows(placed, scroll);
    return placed;
}

ModRowText mod_row_text(const Dialog& dialog, const ModRow& row) {
    ModRowText text;
    if (row.offered < 0 || static_cast<std::size_t>(row.offered) >= dialog.mod_folders.size()) {
        text.title = std::string(shown_text(no_mod_text));
        text.version = std::string(no_mod_version_text);
        text.description = std::string(shown_text(no_mod_description_text));
        return text;
    }
    const auto index = static_cast<std::size_t>(row.offered);
    text.title = index < dialog.mod_names.size() ? dialog.mod_names[index] : std::string();
    if (index < dialog.mod_details.size()) {
        const ModDetails& details = dialog.mod_details[index];
        text.details = &details;
        text.has_profile = details.has_profile;
        text.version = details.version;
        text.description = details.description;
    }
    if (!text.has_profile) {
        if (text.version.empty())
            text.version = std::string(no_profile_version_text);
        if (text.description.empty())
            text.description = std::string(shown_text(no_profile_description_text));
    }
    return text;
}

std::string cut_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    if (text_width(text) <= width)
        return std::string(text);
    const std::string ellipsis(path_ellipsis);
    std::size_t end = text.size();
    while (end > 0) {
        --end;
        while (end > 0 && continuing_byte(text[end]))
            --end;
        std::string shown = std::string(text.substr(0, end)) + ellipsis;
        if (text_width(shown) <= width)
            return shown;
    }
    return ellipsis;
}

std::vector<std::string> wrap_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
) {
    std::vector<std::string> lines;
    std::string line;
    std::size_t at = 0;
    while (at < text.size()) {
        const std::size_t space = text.find(' ', at);
        const std::size_t end = space == std::string_view::npos ? text.size() : space;
        const std::string_view word = text.substr(at, end - at);
        at = end == text.size() ? end : end + 1;
        if (word.empty())
            continue;
        const std::string longer =
            line.empty() ? std::string(word) : line + ' ' + std::string(word);
        if (text_width(longer) <= width) {
            line = longer;
            continue;
        }
        if (!line.empty())
            lines.push_back(line);
        line = cut_text(word, width, text_width);
    }
    if (!line.empty())
        lines.push_back(line);
    return lines;
}

std::vector<std::string> question_text_lines(
    const Dialog& dialog, const std::function<int32_t(std::string_view)>& text_width
) {
    const ModRowText offered = mod_row_text(dialog, ModRow{dialog.switch_question, false});
    const std::string asked =
        std::string(switch_ask_before_text) + offered.title + std::string(switch_ask_after_text);
    std::vector<std::string> lines = wrap_text(asked, question_first_line.width, text_width);
    std::vector<std::string> note;
    if (!offered.has_profile)
        note = wrap_text(switch_no_profile_text, question_first_line.width, text_width);
    // The note keeps its lines; the question keeps at least one.
    if (note.size() > question_lines - 1)
        note.resize(question_lines - 1);
    const std::size_t room = question_lines - note.size();
    if (lines.size() > room) {
        lines.resize(room);
        lines.back() = cut_text(
            lines.back() + std::string(path_ellipsis), question_first_line.width, text_width
        );
    }
    lines.insert(lines.end(), note.begin(), note.end());
    return lines;
}

} // namespace geometry

} // namespace oa::ui::engine_settings
