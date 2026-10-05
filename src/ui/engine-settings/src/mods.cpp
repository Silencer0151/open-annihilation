// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mods: the list of the mods the game can play, in the order it shows them,
// each row's texts and place, and the Switch Mod question's text.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"
#include "oa/base/text/line_break.hpp"
#include "oa/data/languages/interface_text.hpp"

#include <algorithm>
#include <cstddef>
#include <functional>
#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
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

/// Returns the details of the row the question asks about.
///
/// @param dialog the dialog, its question showing
/// @return the details; null for No Mod
const ModDetails* question_details(const Dialog& dialog) noexcept {
    const int32_t offered = dialog.switch_question;
    if (offered < 0 || static_cast<std::size_t>(offered) >= dialog.mod_details.size())
        return nullptr;
    return &dialog.mod_details[static_cast<std::size_t>(offered)];
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

std::string filled(
    std::string_view english,
    std::initializer_list<std::pair<std::string_view, std::string_view>> places
) {
    const std::string pattern(geometry::shown_text(english));
    std::string text;
    std::size_t at = 0;
    while (at < pattern.size()) {
        const std::size_t open = pattern.find('{', at);
        const std::size_t close = open == std::string::npos ? open : pattern.find('}', open);
        if (close == std::string::npos) {
            text.append(pattern, at, std::string::npos);
            break;
        }
        text.append(pattern, at, open - at);
        const std::string_view name(pattern.data() + open + 1, close - open - 1);
        bool found = false;
        for (const auto& [place, value] : places)
            if (place == name) {
                text += value;
                found = true;
                break;
            }
        if (!found)
            text.append(pattern, open, close - open + 1);
        at = close + 1;
    }
    return text;
}

bool mods_page(const Dialog& dialog) noexcept {
    // A check's own section takes the place of the list.
    const bool own_section =
        dialog.section_hooks != nullptr && dialog.section_hooks->settings != nullptr;
    return dialog.kind == DialogKind::engine && dialog.page == Page::mods && !own_section;
}

int32_t mods_folder_control(const Rows& rows) noexcept {
    return first_row_control + static_cast<int32_t>(rows.rows.size());
}

int32_t roll_back_control(const Rows& rows, std::size_t index) noexcept {
    return mods_folder_control(rows) + 1 + static_cast<int32_t>(index);
}

int32_t roll_back_row(const Rows& rows, int32_t control) noexcept {
    const int32_t first = mods_folder_control(rows) + 1;
    if (control < first || control - first >= static_cast<int32_t>(rows.rows.size()))
        return -1;
    return control - first;
}

SourceRect roll_back_button(const Row& row) noexcept {
    const SourceRect& box = row.control_area;
    return {
        box.x + box.width - mod_row_inset - mod_roll_back_width,
        box.y + box.height - mod_row_inset + 1 - mod_roll_back_height,
        mod_roll_back_width,
        mod_roll_back_height
    };
}

bool offers_roll_back(const Dialog& dialog, const ModRow& row) noexcept {
    return row.offered >= 0 && static_cast<std::size_t>(row.offered) < dialog.mod_details.size() &&
           !dialog.mod_details[static_cast<std::size_t>(row.offered)].roll_back_from.empty();
}

SourceRect question_yes_rect(const Dialog& dialog) noexcept {
    if (dialog.mod_question != ModQuestion::roll_back)
        return question_yes_button;
    return {
        question_box.x + question_box.width - padding - question_roll_back_width,
        question_yes_button.y,
        question_roll_back_width,
        question_yes_button.height
    };
}

SourceRect question_no_rect(const Dialog& dialog) noexcept {
    const SourceRect yes = question_yes_rect(dialog);
    return {
        yes.x - (question_yes_button.x - question_no_button.x - question_no_button.width) -
            question_no_button.width,
        yes.y,
        question_no_button.width,
        question_no_button.height
    };
}

std::string question_version_text(const Dialog& dialog, const ModRowText& offered) {
    const ModDetails* details = question_details(dialog);
    if (dialog.mod_question != ModQuestion::roll_back || details == nullptr)
        return offered.version;
    return filled(
        roll_back_versions_text, {{"from", details->roll_back_from}, {"to", details->roll_back_to}}
    );
}

Rows place_mod_rows(const Dialog& dialog, int32_t scroll) {
    Rows placed{};
    const ScrollArea area = mods_scroll(dialog.locks.mod != Lock::none);
    const auto rows = mod_rows(dialog);
    const Lock lock = dialog.locks.mod;
    int32_t top = area.view.y;
    // While the dialog's words are drawn in the modern fonts a row is
    // taller, and its description lower.
    const bool tall = oa::data::languages::interface_language().needs !=
                      oa::data::languages::TextNeeds::game_fonts;
    const int32_t height = mod_row_height + (tall ? tall_mod_row_growth : 0);
    const int32_t description_top = mod_description_top + (tall ? tall_mod_description_drop : 0);
    placed.rows.reserve(rows.size());
    for (std::size_t index = 0; index < rows.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = Setting::mod;
        row.control = first_row_control + static_cast<int32_t>(index);
        // A locked page keeps the mod played: every row is inert.
        row.lock = lock;
        row.top = top;
        row.height = height + mod_row_gap;
        row.control_area = {content_left, top, content_width, height};
        const int32_t text_left = content_left + mod_row_inset + mod_badge_side + mod_row_inset;
        const int32_t text_width = content_right - mod_row_inset - text_left;
        row.label = {text_left, top + 1, text_width, label_line_height};
        row.value = {text_left, top + 3, text_width, hint_line_height};
        row.hints[0] = {text_left, top + description_top, text_width, hint_line_height};
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
    if (oa::base::text::has_wide_script(text)) {
        // Chinese, Japanese and Korean are written without spaces: a line
        // also breaks between their characters, as the line breaker allows.
        const auto fits = [&](std::string_view start) { return text_width(start) <= width; };
        for (;;) {
            while (!text.empty() && text.front() == ' ')
                text.remove_prefix(1);
            if (text.empty())
                break;
            const auto row = oa::base::text::first_row(text, fits);
            lines.emplace_back(text.substr(0, row.bytes));
            text.remove_prefix(row.next);
        }
        return lines;
    }
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
    if (const ModDetails* details = question_details(dialog);
        dialog.mod_question == ModQuestion::roll_back && details != nullptr) {
        std::string asked = filled(
            roll_back_ask_text,
            {{"title", offered.title},
             {"from", details->roll_back_from},
             {"to", details->roll_back_to}}
        );
        const bool playing =
            !dialog.playing_mod_folder.empty() &&
            static_cast<std::size_t>(dialog.switch_question) < dialog.mod_folders.size() &&
            dialog.mod_folders[static_cast<std::size_t>(dialog.switch_question)] ==
                dialog.playing_mod_folder;
        if (playing)
            asked += " " + std::string(shown_text(roll_back_reload_text));
        std::vector<std::string> lines = wrap_text(asked, question_first_line.width, text_width);
        if (lines.size() > question_lines) {
            lines.resize(question_lines);
            lines.back() = cut_text(
                lines.back() + std::string(path_ellipsis), question_first_line.width, text_width
            );
        }
        return lines;
    }
    const std::string asked = filled(switch_ask_text, {{"title", offered.title}});
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
