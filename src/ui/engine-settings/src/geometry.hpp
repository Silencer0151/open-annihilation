// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where the settings dialog puts each of its parts, in source pixels from
// its top left corner, and the texts it shows. The events (dialog.cpp) and
// the drawing (dialog_draw.cpp) both place things through these functions,
// so a control is pressed where it is drawn.
#pragma once

#include "oa/ui/engine_settings/dialog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>

namespace oa::ui::engine_settings::geometry {

using oa::ui::frontend_renderer::SourceRect;

/// The width of the dialog's raised edge.
inline constexpr int32_t edge = 1;
/// The header's height, under the top edge.
inline constexpr int32_t header_height = 26;
/// The footer's height, over the bottom edge.
inline constexpr int32_t footer_height = 32;
/// The header's first row.
inline constexpr int32_t header_top = edge;
/// The row of the line between the header and the body.
inline constexpr int32_t header_rule_row = header_top + header_height;
/// The row of the line between the body and the footer.
inline constexpr int32_t footer_rule_row = dialog_height - edge - footer_height - 1;
/// The footer's first row.
inline constexpr int32_t footer_top = footer_rule_row + 1;
/// The body's first row, under the header's line.
inline constexpr int32_t body_top = header_rule_row + 1;
/// The section list's width, from the left edge.
inline constexpr int32_t list_width = 144;
/// The column of the line between the section list and the open section.
inline constexpr int32_t list_rule_column = edge + list_width;
/// The space between a panel's edge and what it holds.
inline constexpr int32_t padding = 12;
/// The open section's first column.
inline constexpr int32_t content_left = list_rule_column + 1 + padding;
/// The column just right of the open section and the header's version.
inline constexpr int32_t content_right = dialog_width - edge - padding;
/// The open section's width.
inline constexpr int32_t content_width = content_right - content_left;

/// The header's OA mark: an outlined square with the letters inside.
inline constexpr SourceRect header_mark{padding, 7, 13, 13};
/// The space between the header's mark and the title, and between the
/// title's two words.
inline constexpr int32_t header_gap = 6;
/// Extra columns after each glyph of the title and the section heading.
inline constexpr int32_t heading_tracking = 1;

/// A section's entry in the list: its left column and width.
inline constexpr int32_t list_item_left = edge + 6;
/// A section's entry's width.
inline constexpr int32_t list_item_width = list_width - 12;
/// A section's entry's height.
inline constexpr int32_t list_item_height = 20;
/// The first entry's top row.
inline constexpr int32_t list_first_top = body_top + 8;
/// The rows between two entries.
inline constexpr int32_t list_item_gap = 1;
/// The rows above and below the line before the Developer section.
inline constexpr int32_t list_divider_margin = 5;
/// The columns between the list's sides and the line before Developer.
inline constexpr int32_t list_divider_inset = 4;
/// The selected entry's marker: its column within the entry, width and height.
inline constexpr int32_t list_marker_offset = 5;
/// The selected entry's marker's width.
inline constexpr int32_t list_marker_width = 2;
/// The selected entry's marker's height.
inline constexpr int32_t list_marker_height = 8;
/// An entry's text column within the entry.
inline constexpr int32_t list_text_offset = 12;
/// The columns an entry keeps clear right of its text.
inline constexpr int32_t list_text_margin = 4;

/// The open section's heading.
inline constexpr SourceRect heading{content_left, body_top + 10, content_width, 12};
/// The first row's top, where its line is drawn.
inline constexpr int32_t first_row_top = heading.y + heading.height + 4;
/// The rows between a row's line and its label, and under its last part.
inline constexpr int32_t row_padding = 8;
/// A row's label line: the label, and a switch, a level strip or a lock.
inline constexpr int32_t label_line_height = 16;
/// The rows between the label line and the first hint line.
inline constexpr int32_t hint_gap = 2;
/// A hint line's height.
inline constexpr int32_t hint_line_height = 12;
/// The most lines a hint takes.
inline constexpr std::size_t most_hint_lines = 2;
/// The rows between the last hint line and a slider.
inline constexpr int32_t slider_gap = 4;
/// A slider line's height: the track with its knob and stops, and the value.
inline constexpr int32_t slider_line_height = 14;
/// The width of a slider's value, right of its track.
inline constexpr int32_t slider_value_width = 110;
/// The columns between a slider's track and its value.
inline constexpr int32_t slider_value_gap = 10;
/// The columns kept clear between a label and the control or lock beside it.
inline constexpr int32_t label_gap = 8;
/// An Off/On switch's width; each half is half of it, inside a 1-pixel border.
inline constexpr int32_t switch_width = 52;
/// A level strip's segment width, inside the strip's 1-pixel border.
inline constexpr int32_t level_width = 23;
/// A lock's width: the padlock and its text, right-aligned on the label line.
inline constexpr int32_t lock_width = 148;
/// The padlock's width.
inline constexpr int32_t padlock_width = 5;
/// The padlock's height.
inline constexpr int32_t padlock_height = 7;
/// The columns between the padlock and its text.
inline constexpr int32_t padlock_gap = 3;

/// A slider's knob: its width and height.
inline constexpr int32_t knob_width = 7;
/// A slider knob's height.
inline constexpr int32_t knob_height = 12;
/// A slider track's height and its top within the slider line.
inline constexpr int32_t track_height = 4;
/// A slider track's top row within the slider line.
inline constexpr int32_t track_offset = 4;
/// A stop mark's height and its top within the slider line.
inline constexpr int32_t stop_height = 2;
/// A stop mark's top row within the slider line.
inline constexpr int32_t stop_offset = 12;
/// Stop marks are drawn only this many columns or more apart.
inline constexpr int32_t least_stop_spacing = 4;

/// The footer's buttons' height and top row.
inline constexpr int32_t button_height = 17;
/// The footer's buttons' top row.
inline constexpr int32_t button_top = footer_top + (footer_height - button_height) / 2;
/// Restore defaults, at the footer's left.
inline constexpr SourceRect restore_button{padding, button_top, 110, button_height};
/// OK, at the footer's right.
inline constexpr SourceRect ok_button{content_right - 52, button_top, 52, button_height};
/// Cancel, left of OK.
inline constexpr SourceRect cancel_button{ok_button.x - 5 - 52, button_top, 52, button_height};

/// The number of sections.
inline constexpr std::size_t page_count = 5;
/// The most rows a section holds.
inline constexpr std::size_t most_rows = 3;

/// What a slider offers: its stops' count.
struct Slider {
    int32_t stops{}; ///< 2 or more
};

/// One row of the open section, placed.
struct Row {
    Setting setting{};           ///< what it changes
    int32_t control{no_control}; ///< its control's number
    Lock lock{};                 ///< why it cannot be changed now
    int32_t top{};               ///< the row of its line
    int32_t height{};            ///< rows from its line to the next row's
    SourceRect label{};          ///< its label
    SourceRect lock_area{};      ///< its padlock and lock text; empty when unlocked
    std::array<SourceRect, most_hint_lines> hints{}; ///< its hint's lines
    std::size_t hint_lines{};                        ///< the lines its hint takes
    SourceRect control_area{};                       ///< its switch, level strip or slider track
    SourceRect value{};                              ///< a slider's value; empty for the others
};

/// The open section's rows, placed.
struct Rows {
    std::array<Row, most_rows> rows{}; ///< the first `count` hold rows
    std::size_t count{};               ///< 1 to most_rows
    int32_t bottom{};                  ///< the row of the line under the last row
};

/// Returns how a setting is changed.
///
/// @param setting the setting
/// @return true for a slider, false for a switch or the level strip
[[nodiscard]] bool is_slider(Setting setting) noexcept;

/// Returns what a slider setting offers.
///
/// @param setting a slider setting
/// @return its stops
[[nodiscard]] Slider slider_of(Setting setting) noexcept;

/// Returns the stop nearest a setting's value.
///
/// @param settings the settings
/// @param setting a slider setting
/// @return 0 for the lowest value to stops - 1 for the highest
[[nodiscard]] int32_t stop_of(const EngineSettings& settings, Setting setting) noexcept;

/// Sets a slider setting to a stop's value.
///
/// @param[in,out] settings the settings
/// @param setting a slider setting
/// @param stop the stop, clamped to the slider's
void set_stop(EngineSettings& settings, Setting setting, int32_t stop) noexcept;

/// Returns the lock a setting has.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @return why it cannot be changed now
[[nodiscard]] Lock lock_of(const Locks& locks, Setting setting) noexcept;

/// Places the rows of a section.
///
/// @param page the section
/// @param locks the dialog's locks
/// @return its rows
[[nodiscard]] Rows place_rows(Page page, const Locks& locks) noexcept;

/// Returns a section's entry in the list.
///
/// @param page the section
/// @return its rectangle
[[nodiscard]] SourceRect list_item(Page page) noexcept;

/// Returns the line before the Developer section in the list.
///
/// @return its rectangle, one row high
[[nodiscard]] SourceRect list_divider() noexcept;

/// Returns a footer button's rectangle.
///
/// @param control restore_control, cancel_control or ok_control
/// @return its rectangle
[[nodiscard]] SourceRect footer_button(int32_t control) noexcept;

/// Returns the column a slider's knob is centred on.
///
/// @param track the slider's track area
/// @param stop the stop
/// @param stops the slider's stops
/// @return the column
[[nodiscard]] int32_t knob_column(const SourceRect& track, int32_t stop, int32_t stops) noexcept;

/// Returns the stop nearest a column on a slider.
///
/// @param track the slider's track area
/// @param column the column
/// @param stops the slider's stops
/// @return the stop, 0 to stops - 1
[[nodiscard]] int32_t stop_at(const SourceRect& track, int32_t column, int32_t stops) noexcept;

/// Returns the segment of the level strip under a column.
///
/// @param strip the strip
/// @param column the column
/// @return the index in anti_aliasing_levels
[[nodiscard]] std::size_t level_at(const SourceRect& strip, int32_t column) noexcept;

/// Returns the index of a level in anti_aliasing_levels.
///
/// @param level the level
/// @return its index; 0 for a level that is not offered
[[nodiscard]] std::size_t level_index(AntiAliasing level) noexcept;

/// Returns a section's name, as its list entry shows it.
///
/// @param page the section
/// @return the name
[[nodiscard]] std::string_view page_name(Page page) noexcept;

/// Returns a section's heading, as the open section shows it.
///
/// @param page the section
/// @return the heading, in capitals
[[nodiscard]] std::string_view page_heading(Page page) noexcept;

/// Returns a setting's label.
///
/// @param setting the setting
/// @return the label
[[nodiscard]] std::string_view label_of(Setting setting) noexcept;

/// Returns a hint's line.
///
/// @param setting the setting
/// @param settings the settings shown; the anti-aliasing hint depends on its level
/// @param line the line, from 0
/// @return the line; empty past the hint's last
[[nodiscard]] std::string_view
hint_line(Setting setting, const EngineSettings& settings, std::size_t line) noexcept;

/// Returns the lines a setting's hint takes.
///
/// @param setting the setting
/// @return 1 or 2
[[nodiscard]] std::size_t hint_line_count(Setting setting) noexcept;

/// Returns a slider's value as it is shown.
///
/// @param setting a slider setting
/// @param settings the settings shown
/// @return the text
[[nodiscard]] std::string value_text(Setting setting, const EngineSettings& settings);

/// Returns a lock's text.
///
/// @param lock the lock
/// @return the text; empty for Lock::none
[[nodiscard]] std::string_view lock_text(Lock lock) noexcept;

/// Returns a level's caption in the strip.
///
/// @param level the level
/// @return "Off", "2x" and so on
[[nodiscard]] std::string_view level_caption(AntiAliasing level) noexcept;

/// The title's first words.
inline constexpr std::string_view title_text = "OPEN ANNIHILATION";
/// The title's last word, drawn muted.
inline constexpr std::string_view title_suffix_text = "SETTINGS";
/// What the header says while a shared game keeps running.
inline constexpr std::string_view shared_game_text = "Shared game - still running";
/// Restore defaults' caption.
inline constexpr std::string_view restore_text = "RESTORE DEFAULTS";
/// Cancel's caption.
inline constexpr std::string_view cancel_text = "CANCEL";
/// OK's caption.
inline constexpr std::string_view ok_text = "OK";
/// A switch's Off caption.
inline constexpr std::string_view off_text = "OFF";
/// A switch's On caption.
inline constexpr std::string_view on_text = "ON";
/// The columns the header keeps for the title's first words.
inline constexpr int32_t title_width = 138;
/// The columns the header keeps for the title's last word.
inline constexpr int32_t title_suffix_width = 68;
/// The widest version text the header has room for, in the small font's columns.
inline constexpr int32_t version_width = 44;
/// The columns between the shared game's text and the version.
inline constexpr int32_t version_gap = 8;

} // namespace oa::ui::engine_settings::geometry
