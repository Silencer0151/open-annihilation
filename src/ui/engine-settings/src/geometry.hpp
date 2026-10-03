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
#include <span>
#include <string>
#include <string_view>
#include <vector>

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
/// The columns kept clear between a label and the control or lock beside it,
/// and between a lock and the switch it stands beside.
inline constexpr int32_t label_gap = 8;
/// An Off/On switch's width; each half is half of it, inside a 1-pixel border.
inline constexpr int32_t switch_width = 52;
/// Enhanced anti-aliasing's level strip's segment width, inside the strip's
/// 1-pixel border.
inline constexpr int32_t level_width = 23;
/// Hardware acceleration's level strip's segment width, inside the strip's
/// 1-pixel border: room for Basic, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t acceleration_level_width = 34;
/// A lock's width: the padlock and its text, right-aligned on the label line.
inline constexpr int32_t lock_width = 148;
/// The padlock's width.
inline constexpr int32_t padlock_width = 5;
/// The padlock's height.
inline constexpr int32_t padlock_height = 7;
/// The columns between the padlock and its text.
inline constexpr int32_t padlock_gap = 3;
/// The columns between a control and its keyboard focus outline.
inline constexpr int32_t focus_inset = 2;

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

using oa::ui::engine_settings::page_count;

/// The view the open section's rows scroll in, under its heading: from the
/// first row's line to the row above the footer's line.
inline constexpr SourceRect view{
    content_left, first_row_top, content_width, footer_rule_row - first_row_top
};
/// What the rows are drawn clipped to: the view, wider on each side by a
/// focus outline.
inline constexpr SourceRect view_clip{
    view.x - focus_inset, view.y, view.width + 2 * focus_inset, view.height
};
/// The clear rows under the last row's line at the end of a section, so
/// that at its end the line never meets the footer's.
inline constexpr int32_t end_gap = row_padding;
/// The scroll bar's thumb's width.
inline constexpr int32_t scroll_thumb_width = 5;
/// The scroll bar's well: in the margin right of the rows, one clear column
/// right of a focus outline, as high as the view; the thumb runs inside its
/// one-pixel border.
inline constexpr SourceRect scroll_well{
    content_right + focus_inset + 1, view.y, scroll_thumb_width + 2, view.height
};
/// Where a press holds the scroll bar: the whole margin right of the rows.
inline constexpr SourceRect scroll_hit{content_right, view.y, padding, view.height};
/// The scroll bar's thumb's least height.
inline constexpr int32_t least_thumb_height = 16;
/// The rows a notch of the mouse wheel scrolls: two hint lines.
inline constexpr int32_t wheel_step = 2 * hint_line_height;
/// The rows Page Up and Page Down scroll: the view less three hint lines,
/// so that what showed at one edge still shows at the other.
inline constexpr int32_t page_step = view.height - 3 * hint_line_height;

/// What a slider offers: its stops' count.
struct Slider {
    int32_t stops{}; ///< 2 or more
};

/// What a level strip offers: its levels' count and their width.
struct Strip {
    std::size_t levels{};  ///< 2 or more, left to right
    int32_t level_width{}; ///< each level's columns, inside the strip's 1-pixel border
};

/// One row of the open section, placed.
struct Row {
    Setting setting{};           ///< what it changes
    int32_t control{no_control}; ///< its control's number
    Lock lock{};                 ///< why it cannot be changed now
    bool hint_is_status{};       ///< its hint lines are its status, which a lock never fades
    int32_t top{};               ///< the row of its line
    int32_t height{};            ///< rows from its line to the next row's
    SourceRect label{};          ///< its label
    SourceRect lock_area{};      ///< its padlock and lock text; empty when unlocked
    std::array<SourceRect, most_hint_lines> hints{}; ///< its hint's lines
    std::size_t hint_lines{};                        ///< the lines its hint takes
    /// Its switch, level strip or slider track; empty for a locked row
    /// whose hint lines are its status, which shows its lock there.
    SourceRect control_area{};
    SourceRect value{}; ///< a slider's value; empty for the others
};

/// The open section's rows, placed.
struct Rows {
    std::vector<Row> rows; ///< one for each setting the section shows
    int32_t bottom{};      ///< the row of the line under the last row
};

/// The open section's rows placed at its scroll offset, and the offset's range.
struct ScrolledRows {
    Rows rows;                ///< placed `scroll` rows higher than at the section's top
    int32_t scroll{};         ///< the offset, 0 to `limit`
    int32_t limit{};          ///< the most the section scrolls; 0 when its rows fit the view
    int32_t content_height{}; ///< rows from the first row's line to the end gap under the last
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

/// Tells whether a setting is a strip of levels: Enhanced anti-aliasing and
/// Hardware acceleration.
///
/// @param setting the setting
/// @return true for a level strip, false for a slider or a switch
[[nodiscard]] bool is_strip(Setting setting) noexcept;

/// Returns what a strip setting offers.
///
/// @param setting a strip setting
/// @return its levels and their width; no levels for any other setting
[[nodiscard]] Strip strip_of(Setting setting) noexcept;

/// Returns the level a strip setting shows.
///
/// @param settings the settings
/// @param setting a strip setting
/// @return its level's index, from 0 at the strip's left
[[nodiscard]] std::size_t strip_level(const EngineSettings& settings, Setting setting) noexcept;

/// Sets a strip setting to a level.
///
/// @param[in,out] settings the settings
/// @param setting a strip setting; any other is left alone
/// @param level the level's index, clamped to the strip's
void set_strip_level(EngineSettings& settings, Setting setting, std::size_t level) noexcept;

/// Returns a level's caption in a strip.
///
/// @param setting a strip setting
/// @param level the level's index
/// @return "Off", "2x", "Basic" and so on; empty past the strip's last
[[nodiscard]] std::string_view strip_caption(Setting setting, std::size_t level) noexcept;

/// Tells whether a setting is an Off/On switch.
///
/// @param setting the setting
/// @return true for a switch, false for a slider or a level strip
[[nodiscard]] bool is_switch(Setting setting) noexcept;

/// Tells whether a switch setting is On. Every switch is read and set
/// through one table from the setting to its value, so a new switch is
/// added in one place.
///
/// @param settings the settings
/// @param setting a switch setting
/// @return true for On; false for a setting that is not a switch
[[nodiscard]] bool switch_on(const EngineSettings& settings, Setting setting) noexcept;

/// Sets a switch setting, through the same table as switch_on.
///
/// @param[in,out] settings the settings
/// @param setting a switch setting; any other is left alone
/// @param on true for On
void set_switch(EngineSettings& settings, Setting setting, bool on) noexcept;

/// Returns the lock a setting has.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @return why it cannot be changed now
[[nodiscard]] Lock lock_of(const Locks& locks, Setting setting) noexcept;

/// Tells whether a setting's hint lines are its status: such a row, locked,
/// shows its lock where its control was, and only its label line fades.
///
/// @param setting the setting
/// @return true for Hardware acceleration, whose hint lines are its status
[[nodiscard]] bool hint_is_status(Setting setting) noexcept;

/// Returns the settings a section shows, a check's own section's when given.
///
/// @param page the section
/// @param section a check's own section; null for the dialog's
/// @return its settings, top to bottom
[[nodiscard]] std::span<const Setting> section_settings(Page page, const SectionHooks* section);

/// Places the rows of a section.
///
/// A locked switch keeps its switch, faded, with its lock left of it, so
/// that its value shows; a locked switch or strip whose hint lines are its
/// status shows its lock where its control was.
///
/// @param page the section
/// @param locks the dialog's locks
/// @param scroll the rows the section is scrolled by from its top; not clamped
/// @param section a check's own section; null for the dialog's
/// @return its rows
[[nodiscard]] Rows place_rows(
    Page page, const Locks& locks, int32_t scroll = 0, const SectionHooks* section = nullptr
);

/// Moves placed rows up: each row's line and every part it has, and the
/// line under the last row. An empty part stays empty.
///
/// @param[in,out] rows the rows
/// @param by the rows they move up; negative moves them down
void scroll_rows(Rows& rows, int32_t by) noexcept;

/// Returns a section's content height: from its first row's line to the
/// end gap under its last row's.
///
/// @param rows its rows, placed at any offset
/// @param scroll the offset they are placed at
/// @return the height, in rows
[[nodiscard]] int32_t content_height(const Rows& rows, int32_t scroll) noexcept;

/// Returns the most a section scrolls.
///
/// @param content_height its content height (content_height)
/// @return the rows its content is taller than the view; 0 when it fits
[[nodiscard]] int32_t scroll_limit(int32_t content_height) noexcept;

/// Places the open section's rows once, at its offset clamped to its limit.
///
/// @param dialog the dialog
/// @return its rows, offset, limit and content height
[[nodiscard]] ScrolledRows open_rows(const Dialog& dialog);

/// Returns the offset nearest the open one that shows a row whole: from its
/// line to the line under it, or for the last row the section's end.
///
/// @param open the open section's rows (open_rows)
/// @param index the row, from 0
/// @return the offset, 0 to the section's limit; the open one for no such row
[[nodiscard]] int32_t scroll_showing(const ScrolledRows& open, std::size_t index) noexcept;

/// Returns the scroll bar's thumb: inside the well's border, as tall as the
/// view's share of the content and never under least_thumb_height, and as
/// far down its travel as the offset is down the limit, to the nearest row.
///
/// @param scroll the offset, 0 to `limit`
/// @param limit the section's limit, above 0
/// @param content_height the section's content height
/// @return the thumb
[[nodiscard]] SourceRect
scroll_thumb(int32_t scroll, int32_t limit, int32_t content_height) noexcept;

/// Returns the offset that puts the scroll bar's thumb's top at a row, to
/// the nearest row.
///
/// @param thumb_top the thumb's top row, clamped to its travel
/// @param limit the section's limit, above 0
/// @param content_height the section's content height
/// @return the offset, 0 to `limit`
[[nodiscard]] int32_t scroll_at(int32_t thumb_top, int32_t limit, int32_t content_height) noexcept;

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

/// Returns the level of a strip under a column.
///
/// @param area the strip's control area
/// @param strip what the strip offers
/// @param column the column
/// @return the level's index, from 0 at the strip's left to its last; the
///     nearest end for a column outside it
[[nodiscard]] std::size_t
level_at(const SourceRect& area, const Strip& strip, int32_t column) noexcept;

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
/// @param acceleration Hardware acceleration's status, which is its hint
/// @param line the line, from 0
/// @return the line; empty past the hint's last
[[nodiscard]] std::string_view hint_line(
    Setting setting,
    const EngineSettings& settings,
    const AccelerationStatus& acceleration,
    std::size_t line
) noexcept;

/// Returns a line of Hardware acceleration's status: the first says what
/// runs, or why not; the second what draws the view, what the player can
/// do, or, while it is in use, what it does on this machine.
///
/// @param acceleration the status
/// @param line the line, 0 or 1
/// @return the line; empty past the second
[[nodiscard]] std::string_view
status_line(const AccelerationStatus& acceleration, std::size_t line) noexcept;

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
