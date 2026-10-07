// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Where the settings dialog puts each of its parts, in source pixels from
// its top left corner, and the texts it shows. The events (dialog.cpp) and
// the drawing (dialog_draw.cpp) both place things through these functions,
// so a control is pressed where it is drawn.
#pragma once

#include "oa/data/mod_profile/value.hpp"
#include "oa/ui/engine_settings/dialog.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::engine_settings::geometry {

using oa::data::mod_profile::Value;
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

/// The header's Open Annihilation icon, in the header's middle rows; or,
/// without the icon, the OA mark's outlined square in its middle.
inline constexpr SourceRect header_mark{padding, 4, 20, 20};
/// The side of the OA mark's outlined square, which stands in for the icon.
inline constexpr int32_t header_mark_square = 13;
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
/// The rows added between two lines of a hint, or of a notice, while the
/// dialog's words are drawn in the modern fonts, whose ideographs stand as
/// tall as a hint line: one line's letters, outline and shadow then keep
/// clear of the next's.
inline constexpr int32_t tall_hint_line_gap = 3;
/// The most lines a hint takes.
inline constexpr std::size_t most_hint_lines = 2;
/// The most lines a notice row's text takes under its label: Controller's
/// Steam Input notice.
inline constexpr std::size_t most_notice_lines = 4;
/// The most lines under any row's label: a hint's, or a notice's.
inline constexpr std::size_t most_row_lines =
    most_notice_lines > most_hint_lines ? most_notice_lines : most_hint_lines;
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
/// One-finger drag's level strip's segment width, inside the strip's
/// 1-pixel border: room for Automatic, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t touch_drag_level_width = 59;
/// QUEUE and ADD's level strip's segment width, inside the strip's 1-pixel
/// border: room for One action, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t touch_latches_level_width = 64;
/// Control size's level strip's segment width, inside the strip's 1-pixel
/// border: room for Standard, its widest caption, with three clear columns
/// each side.
inline constexpr int32_t control_size_level_width = 53;
/// Scheme's level strip's segment width: room for Trackpads, its widest
/// caption, with three clear columns each side.
inline constexpr int32_t scheme_level_width = 60;
/// Right trackpad's level strip's segment width: room for Absolute, its
/// widest caption, with three clear columns each side.
inline constexpr int32_t right_trackpad_level_width = 51;
/// Pointer acceleration's level strip's segment width: Hardware
/// acceleration's, so that the two strips of three levels from Off look
/// alike; High, its widest caption, fits with room to spare.
inline constexpr int32_t pad_acceleration_level_width = acceleration_level_width;
/// View past the map's edge's level strip's segment width: Hardware
/// acceleration's, so that the strips of three levels from Off look alike;
/// 25% and 50%, its widest captions, fit with room to spare.
inline constexpr int32_t view_past_map_edge_level_width = acceleration_level_width;
/// Right stick's level strip's segment width: room for Nothing, its widest
/// caption, with three clear columns each side.
inline constexpr int32_t right_stick_level_width = 46;
/// The Controller section's Haptics' level strip's segment width: room for
/// Strong, its widest caption, with three clear columns each side.
inline constexpr int32_t pad_haptics_level_width = 41;
/// Menu scaling's level strip's segment width, inside the strip's 1-pixel
/// border: room for Whole steps, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t menu_scaling_level_width = 71;
/// Explosion flash's level strip's segment width, inside the strip's
/// 1-pixel border: room for Reduced, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t explosion_flash_level_width = 50;
/// Zoomed out units' level strip's segment width, inside the strip's
/// 1-pixel border: room for Rendered, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t zoomed_out_units_level_width = 56;
/// Window frame's level strip's segment width, inside the strip's 1-pixel
/// border: room for Hidden in play, its widest caption, with three clear
/// columns each side.
inline constexpr int32_t window_frame_level_width = 85;
/// MANAGE…'s width, Game files' button on its summary row's label line:
/// room for its caption in the small font with clear columns each side.
inline constexpr int32_t manage_button_width = 76;
/// The most characters a line of a host's text under a row holds, broken
/// between words: what fits the section's width in the small font.
inline constexpr std::size_t hint_line_characters = 50;
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

/// The Switch Mod question (Dialog::switch_question): a box over the middle
/// of the dialog's body.
inline constexpr SourceRect question_box{
    (dialog_width - 300) / 2, body_top + (footer_rule_row - body_top - 150) / 2, 300, 150
};
/// The question's heading, along its top.
inline constexpr SourceRect question_heading{
    question_box.x + padding, question_box.y + 8, question_box.width - 2 * padding, 12
};
/// The badge of the mod the question offers, under the heading.
inline constexpr SourceRect question_badge{question_heading.x, question_heading.y + 16, 20, 20};
/// The mod's title, right of its badge.
inline constexpr SourceRect question_title{
    question_badge.x + question_badge.width + 6,
    question_badge.y - 2,
    question_heading.width - question_badge.width - 6,
    16
};
/// The mod's version, under its title.
inline constexpr SourceRect question_version{
    question_title.x, question_title.y + 14, question_title.width, 12
};
/// The question's first text line, under the badge; the others follow it.
inline constexpr SourceRect question_first_line{
    question_heading.x, question_badge.y + question_badge.height + 6, question_heading.width, 12
};
/// The most lines the question's text takes, its note's among them.
inline constexpr std::size_t question_lines = 5;
/// SWITCH, at the question's bottom right.
inline constexpr SourceRect question_yes_button{
    question_box.x + question_box.width - padding - 52,
    question_box.y + question_box.height - 8 - button_height,
    52,
    button_height
};
/// CANCEL, left of SWITCH.
inline constexpr SourceRect question_no_button{
    question_yes_button.x - 5 - 52, question_yes_button.y, 52, button_height
};

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

/// Where a section's rows scroll: the view, the scroll bar's well, the
/// margin a press holds the bar in, and the rows Page Up and Page Down move.
struct ScrollArea {
    SourceRect view{};   ///< what the rows are seen through
    SourceRect well{};   ///< the scroll bar's well, as high as the view
    SourceRect hit{};    ///< where a press holds the scroll bar
    int32_t page_step{}; ///< the rows Page Up and Page Down scroll
};

/// Where a section's rows scroll; Developer's list scrolls in
/// developer_scroll.
inline constexpr ScrollArea section_scroll{view, scroll_well, scroll_hit, page_step};

// Developer: its rows under the heading, Enable Developer Mode and Show
// performance statistics, placed closer than a section's so that the list
// under them has room; Developer Mode's list of the standard hacks in a view
// of its own; and a footer of its own with Show Active Only and Restore
// profile values. Only the list scrolls.

/// The rows between the line of each of Developer's rows and its label, and
/// under its last part: half a section's.
inline constexpr int32_t developer_row_padding = row_padding / 2;
/// The height of each of Developer's rows, from its line to the next row's:
/// its label line and one hint line.
inline constexpr int32_t developer_row_height = 1 + developer_row_padding + label_line_height +
                                                hint_gap + hint_line_height + developer_row_padding;
/// The row of the line over the list, under Developer's rows.
inline constexpr int32_t developer_list_rule =
    first_row_top + developer_row_count * developer_row_height;
/// The rows between the footer's line, its two rows and the dialog's footer.
inline constexpr int32_t developer_footer_gap = 4;
/// The row of the line over the footer under Developer's list.
inline constexpr int32_t developer_footer_rule = footer_rule_row - 2 - button_height -
                                                 developer_footer_gap - label_line_height -
                                                 developer_footer_gap;
/// The view the list scrolls in, between the two lines.
inline constexpr SourceRect developer_view{
    content_left,
    developer_list_rule + 1,
    content_width,
    developer_footer_rule - developer_list_rule - 1
};
/// What the list is drawn clipped to: the view, wider on each side by a
/// focus outline.
inline constexpr SourceRect developer_view_clip{
    developer_view.x - focus_inset,
    developer_view.y,
    developer_view.width + 2 * focus_inset,
    developer_view.height
};
/// Show Active Only's label, in the footer.
inline constexpr SourceRect active_only_label{
    content_left,
    developer_footer_rule + developer_footer_gap,
    content_width - switch_width - label_gap,
    label_line_height
};
/// Show Active Only's switch, at the footer's right.
inline constexpr SourceRect active_only_switch{
    content_right - switch_width,
    developer_footer_rule + developer_footer_gap,
    switch_width,
    label_line_height
};
/// Restore profile values, under Show Active Only.
inline constexpr SourceRect restore_profile_button{
    content_left, active_only_label.y + label_line_height + developer_footer_gap, 142, button_height
};
/// Where Developer's list scrolls.
inline constexpr ScrollArea developer_scroll{
    developer_view,
    {scroll_well.x, developer_view.y, scroll_well.width, developer_view.height},
    {scroll_hit.x, developer_view.y, scroll_hit.width, developer_view.height},
    developer_view.height - 3 * hint_line_height,
};

// Mods: the list of the mods, one row each, in a view of its own under the
// heading, under a lock line while the page is locked; under the list, OPEN
// MODS FOLDER and two lines naming the folders listed, which stay put. Only
// the list scrolls.

/// The lines under the list naming the folders it lists: the first.
inline constexpr SourceRect mods_note_first{
    content_left, footer_rule_row - 4 - 2 * hint_line_height, content_width, hint_line_height
};
/// The second of those lines.
inline constexpr SourceRect mods_note_second{
    content_left, mods_note_first.y + hint_line_height, content_width, hint_line_height
};
/// OPEN MODS FOLDER, over those lines.
inline constexpr SourceRect mods_folder_button{
    content_left, mods_note_first.y - 4 - button_height, 104, button_height
};
/// The two lines over the list that say why the page is locked, the
/// padlock at the first's left.
inline constexpr SourceRect mods_lock_line{
    content_left, first_row_top, content_width, 2 * hint_line_height
};
/// The first of those lines' text, right of the padlock; the second lies
/// under it.
inline constexpr SourceRect mods_lock_text{
    mods_lock_line.x + padlock_width + padlock_gap,
    mods_lock_line.y,
    mods_lock_line.width - padlock_width - padlock_gap,
    hint_line_height
};
/// A mod row's height, its border included.
inline constexpr int32_t mod_row_height = 28;
/// The rows from a mod row's top to its description line's.
inline constexpr int32_t mod_description_top = 15;
/// The rows a mod row grows by while the dialog's words are drawn in the
/// modern fonts, whose ideographs stand taller than the game's fonts, and
/// the rows its description moves down then: its title and its description
/// each keep clear of the other and of its border.
inline constexpr int32_t tall_mod_row_growth = 6;
inline constexpr int32_t tall_mod_description_drop = 4;
/// The rows between two mod rows.
inline constexpr int32_t mod_row_gap = 3;
/// A mod row's badge's side, in source pixels.
inline constexpr int32_t mod_badge_side = 20;
/// The columns between a mod row's edge and its badge, and between the
/// badge and the text.
inline constexpr int32_t mod_row_inset = 4;

/// A mod row's ROLL BACK button's width, at the right of its description line.
inline constexpr int32_t mod_roll_back_width = 60;
/// Its height.
inline constexpr int32_t mod_roll_back_height = 12;
/// The columns between the description and the ROLL BACK button.
inline constexpr int32_t mod_roll_back_gap = 4;
/// The Roll Back Mod question's ROLL BACK button's width.
inline constexpr int32_t question_roll_back_width = 64;

/// Returns where Mods' list scrolls: under the heading, or under the lock
/// line while the page is locked, down to OPEN MODS FOLDER.
///
/// @param locked the page is locked
/// @return the view, the scroll bar's well and hit area, and Page Up's step
[[nodiscard]] constexpr ScrollArea mods_scroll(bool locked) noexcept {
    const int32_t top = locked ? mods_lock_line.y + mods_lock_line.height + 2 : first_row_top;
    const SourceRect list_view{content_left, top, content_width, mods_folder_button.y - 6 - top};
    return {
        list_view,
        {scroll_well.x, list_view.y, scroll_well.width, list_view.height},
        {scroll_hit.x, list_view.y, scroll_hit.width, list_view.height},
        list_view.height - (mod_row_height + mod_row_gap),
    };
}

/// An area's or a hack's header in the list: its height.
inline constexpr int32_t list_header_height = 20;
/// A line of text under a hack (its summary, its scope, a note): its height.
inline constexpr int32_t list_text_height = hint_line_height;
/// A parameter's switch row: its height.
inline constexpr int32_t list_toggle_height = 20;
/// A parameter's slider row, its name and value over the slider: its height.
inline constexpr int32_t list_slider_height = 2 + hint_line_height + 2 + slider_line_height + 2;
/// The line naming a set or a list over its items: its height.
inline constexpr int32_t list_heading_height = hint_line_height + 2;
/// The rows over a hack's first line of text, and under its last row.
inline constexpr int32_t list_body_gap = 2;
/// The column of an area's arrow, from content_left.
inline constexpr int32_t area_arrow_offset = 1;
/// The column of an area's name, from content_left.
inline constexpr int32_t area_text_offset = 10;
/// The column of a hack's arrow, from content_left.
inline constexpr int32_t hack_arrow_offset = 9;
/// The column of a hack's id and of what is under it, from content_left.
inline constexpr int32_t hack_text_offset = 18;
/// The column of a set's values and a list's items, from content_left.
inline constexpr int32_t item_text_offset = 28;
/// An arrow's side: closed it points right, open it points down.
inline constexpr int32_t arrow_side = 5;
/// The columns an area's count of hacks that are on keeps, at the row's right.
inline constexpr int32_t area_count_width = 80;
/// The most characters a line of a hack's summary holds, broken between
/// words: what fits the columns under a hack in the small font.
inline constexpr std::size_t summary_line_characters = 45;

/// A drop-down's field: its width. It stands on its own line under the
/// hint, as a slider's track does.
inline constexpr int32_t choice_width = 200;
/// A wide drop-down's field: room for Gyro pointer's longest choice, While
/// the right stick is touched, in the regular font with the field's inset
/// and arrow.
inline constexpr int32_t wide_choice_width = 248;
/// A drop-down field's height.
inline constexpr int32_t choice_line_height = label_line_height;
/// The columns between a drop-down field's left edge and its text.
inline constexpr int32_t choice_text_inset = 6;
/// The columns a drop-down field keeps at its right for its arrow.
inline constexpr int32_t choice_arrow_room = 16;
/// The drop-down's arrow's width.
inline constexpr int32_t choice_arrow_width = 7;
/// The drop-down's arrow's height.
inline constexpr int32_t choice_arrow_height = 4;
/// An open drop-down list's item's height.
inline constexpr int32_t choice_item_height = 16;
/// The most items an open drop-down list shows at once; a longer list
/// scrolls.
inline constexpr int32_t most_shown_choices = 8;
/// The columns between an open list item's left edge and its text, right of
/// the marker the chosen item shows.
inline constexpr int32_t choice_item_text_inset = 12;
/// The columns a drop-down field gives its choice's text.
inline constexpr int32_t choice_field_text_room =
    choice_width - choice_text_inset - choice_arrow_room;
/// The columns an open list's item gives its text.
inline constexpr int32_t choice_item_text_room =
    choice_width - 2 - choice_item_text_inset - list_text_margin;

/// What a slider offers: its stops' count.
struct Slider {
    int32_t stops{}; ///< 2 or more
};

/// What a level strip offers: its levels' count and their width.
struct Strip {
    std::size_t levels{};  ///< 2 or more, left to right
    int32_t level_width{}; ///< each level's columns, inside the strip's 1-pixel border
    /// The levels a player may choose, counted from the left; the others are
    /// shown faded and choosing them changes nothing. 0 offers every level.
    std::size_t offered{};
};

/// Returns the levels of a strip a player may choose, from its left.
///
/// @param strip the strip
/// @return Strip::offered, or every level where it is 0
[[nodiscard]] constexpr std::size_t offered_levels(const Strip& strip) noexcept {
    return strip.offered == 0 ? strip.levels : std::min(strip.offered, strip.levels);
}

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
    std::array<SourceRect, most_row_lines> hints{}; ///< its hint's or notice's lines
    std::size_t hint_lines{};                       ///< the lines its hint takes
    /// Its switch, level strip or slider track; empty for a locked row
    /// whose hint lines are its status, which shows its lock there.
    SourceRect control_area{};
    SourceRect value{}; ///< a slider's value; empty for the others
};

/// The text the dialog shows for one of its own words: the interface
/// catalogue's translation into the language shown
/// (oa/data/languages/interface_text.hpp), or the word itself.
///
/// @param english the word, in English as the source writes it
/// @return the text to show
[[nodiscard]] std::string_view shown_text(std::string_view english);

/// Returns a text the dialog shows, looked up whole in the language shown
/// (shown_text), with its places ({name}) filled; the values are not looked
/// up, and a place no value names stays as it is written.
///
/// @param english the text, in English
/// @param places each place's name and value
/// @return the text
[[nodiscard]] std::string filled(
    std::string_view english,
    std::initializer_list<std::pair<std::string_view, std::string_view>> places
);

/// The open section's rows, placed.
struct Rows {
    std::vector<Row> rows; ///< one for each setting the section shows
    int32_t bottom{};      ///< the row of the line under the last row
};

/// What a row of Developer's list is.
enum class ListRowKind : uint8_t {
    area,    ///< an area's header: its title and how many of its hacks are on
    hack,    ///< a hack's header: its title and its switch
    id,      ///< the first line under an open hack: its id, for reference
    text,    ///< a line under a hack: its summary or a note
    scope,   ///< "Applies at next match", under a rule (sim-scope) hack
    toggle,  ///< a parameter's switch, or one value of a set's
    slider,  ///< a parameter's slider: a number, a choice, a list's length or one of its items
    heading, ///< the name of a set or a list, over its items
};

/// ListRow::item for a row that changes its whole parameter.
inline constexpr int32_t whole_parameter = -1;
/// ListRow::item for a list's length.
inline constexpr int32_t list_length = -2;

/// One row of Developer's list, placed.
struct ListRow {
    ListRowKind kind{};
    int32_t control{no_control};   ///< its control; no_control for a line or a heading
    std::size_t area{};            ///< its area's place among developer_areas
    std::size_t hack{};            ///< its hack's place among the standard hacks; an area's first
    int32_t parameter{-1};         ///< its parameter's index within its hack; -1 for none
    int32_t item{whole_parameter}; ///< a list's item or a set's value, or list_length
    int32_t top{};                 ///< its first row
    int32_t height{};              ///< its rows
    std::string text;              ///< its title, id, name or line
    SourceRect label{};            ///< where its text is drawn
    /// Its switch or slider track; a header's whole row, which a press opens
    /// or closes, its switch apart.
    SourceRect control_area{};
    SourceRect toggle{}; ///< a hack's switch, inside its header
    SourceRect value{};  ///< a slider's value, or an area's count of hacks that are on
    SourceRect arrow{};  ///< a header's open or closed mark
    std::string shown;   ///< a slider's value, or an area's count, as the row shows it
    bool open{};         ///< a header's part is open
    bool on{};           ///< a hack's or a switch's state
    int32_t stops{};     ///< a slider's stops, 1 or more
    int32_t stop{};      ///< the stop a slider's knob is on
    /// It shows but takes no change: Developer Mode is off, or the row's hack
    /// is off or not implemented. A header still opens and closes.
    bool locked{};
};

/// Developer's list, placed.
struct List {
    std::vector<ListRow> rows; ///< the rows the open areas and hacks show, top to bottom
    int32_t bottom{};          ///< the row under the last row
};

/// The open section's rows placed at its scroll offset, and the offset's range.
struct ScrolledRows {
    /// Placed `scroll` rows higher than at the section's top; Developer's
    /// stay at its top, over its list.
    Rows rows;
    List list;                       ///< Developer's list, placed `scroll` rows higher
    ScrollArea area{section_scroll}; ///< where they scroll
    int32_t scroll{};                ///< the offset, 0 to `limit`
    int32_t limit{};                 ///< the most the section scrolls; 0 when its rows fit the view
    int32_t content_height{}; ///< rows from the first row's line to the end gap under the last
};

/// Tells whether Mods shows: its rows are the dialog's own, not a check's.
///
/// @param dialog the dialog
/// @return true on Mods
[[nodiscard]] bool mods_page(const Dialog& dialog) noexcept;

/// Returns OPEN MODS FOLDER's control: the one after the last mod row's.
///
/// @param rows Mods' rows
/// @return its control
[[nodiscard]] int32_t mods_folder_control(const Rows& rows) noexcept;

/// Returns the control of a row's ROLL BACK button: OPEN MODS FOLDER's
/// control and one more for each row before it and itself.
///
/// @param rows Mods' rows
/// @param index the row's place among them
/// @return its control
[[nodiscard]] int32_t roll_back_control(const Rows& rows, std::size_t index) noexcept;

/// Returns the row a ROLL BACK control belongs to.
///
/// @param rows Mods' rows
/// @param control the control
/// @return the row's place; -1 when the control is no row's ROLL BACK
[[nodiscard]] int32_t roll_back_row(const Rows& rows, int32_t control) noexcept;

/// Returns a mod row's ROLL BACK button: at the right of its description
/// line.
///
/// @param row the row, placed
/// @return the button's rectangle
[[nodiscard]] SourceRect roll_back_button(const Row& row) noexcept;

/// Tells whether a row of Mods offers ROLL BACK: its folder keeps an
/// earlier version (ModDetails::roll_back_from).
///
/// @param dialog the dialog
/// @param row the row
/// @return true when it does
[[nodiscard]] bool offers_roll_back(const Dialog& dialog, const ModRow& row) noexcept;

/// Returns the question's answering button: SWITCH, or ROLL BACK, which is
/// wider.
///
/// @param dialog the dialog, its question showing
/// @return the button's rectangle
[[nodiscard]] SourceRect question_yes_rect(const Dialog& dialog) noexcept;

/// Returns the question's CANCEL button, left of its answering button.
///
/// @param dialog the dialog, its question showing
/// @return the button's rectangle
[[nodiscard]] SourceRect question_no_rect(const Dialog& dialog) noexcept;

/// Returns Mods' rows placed in its list at a scroll offset: one for each of
/// mod_rows, whose control_area is the whole row, label its title, value
/// its version and hints[0] its description.
///
/// @param dialog the dialog
/// @param scroll the offset
/// @return the rows
[[nodiscard]] Rows place_mod_rows(const Dialog& dialog, int32_t scroll);

/// Returns a mod row's title, version, description and badge.
struct ModRowText {
    std::string title;           ///< the title
    std::string version;         ///< the version
    std::string description;     ///< the description
    bool has_profile{true};      ///< the folder holds an oamod.yaml
    const ModDetails* details{}; ///< what the host read of it; null for No Mod or none
};

/// Returns what a row of Mods shows.
///
/// @param dialog the dialog
/// @param row the row
/// @return its texts
[[nodiscard]] ModRowText mod_row_text(const Dialog& dialog, const ModRow& row);

/// Returns the question's version line: the mod's version, or for a roll
/// back the version it rolls back from and the one it rolls back to.
///
/// @param dialog the dialog, its question showing
/// @param offered what the question's row shows
/// @return the line
[[nodiscard]] std::string question_version_text(const Dialog& dialog, const ModRowText& offered);

/// Cuts a text to a width, ending it with "..." when it is cut.
///
/// @param text the text, in UTF-8
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the text, or as much of it as fits before "..."
[[nodiscard]] std::string cut_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
);

/// Breaks a text into lines of a width, at its spaces. A text with
/// Chinese, Japanese or Korean characters also breaks between them, and
/// never starts a line with a closing mark or ends one with an opening mark
/// (oa::base::text::first_row).
///
/// @param text the text, in UTF-8
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the lines; a word wider than the room is cut with "..."
[[nodiscard]] std::vector<std::string> wrap_text(
    std::string_view text, int32_t width, const std::function<int32_t(std::string_view)>& text_width
);

/// Returns the Switch Mod question's text lines: its question, then, for a
/// folder without an oamod.yaml, its note, each broken into the lines that
/// fit; a question too long for the lines the note leaves it ends in "...".
///
/// @param dialog the dialog, its question showing
/// @param text_width a small text's width
/// @return at most question_lines lines; the note's are the last, when there is one
[[nodiscard]] std::vector<std::string> question_text_lines(
    const Dialog& dialog, const std::function<int32_t(std::string_view)>& text_width
);

/// Returns how a setting is changed.
///
/// @param setting the setting
/// @return true for a slider, false for a switch, a level strip or a drop-down
[[nodiscard]] bool is_slider(Setting setting) noexcept;

/// Returns what a slider setting offers.
///
/// @param setting a slider setting
/// @param highest_offered_unit the unit limit slider's highest value, in
///     units per player
/// @param offered_sizes the Screen size slider's stops, in order
///     (Dialog::offered_screen_sizes); never empty
/// @return its stops
[[nodiscard]] Slider slider_of(
    Setting setting,
    uint16_t highest_offered_unit = highest_unit_limit,
    std::span<const ScreenSize> offered_sizes = screen_sizes
) noexcept;

/// Returns the stops a slider setting offers for the settings shown: a
/// snap radius runs from 0 to the mod's most (at least two stops), the
/// others as slider_of gives them.
///
/// @param settings the settings shown
/// @param setting a slider setting
/// @param highest_offered_unit the unit limit slider's highest value, in
///     units per player
/// @param offered_sizes the Screen size slider's stops, in order
///     (Dialog::offered_screen_sizes); never empty
/// @return the stops, 1 or more; 2 or more but for a Screen size slider of
///     one stop
[[nodiscard]] int32_t stops_of(
    const EngineSettings& settings,
    Setting setting,
    uint16_t highest_offered_unit = highest_unit_limit,
    std::span<const ScreenSize> offered_sizes = screen_sizes
) noexcept;

/// Returns the stop nearest a setting's value.
///
/// @param settings the settings
/// @param setting a slider setting
/// @param highest_offered_unit the unit limit slider's highest value, in
///     units per player
/// @param offered_sizes the Screen size slider's stops, in order
///     (Dialog::offered_screen_sizes); a screen size not among them is on
///     the first
/// @return 0 for the lowest value to stops - 1 for the highest
[[nodiscard]] int32_t stop_of(
    const EngineSettings& settings,
    Setting setting,
    uint16_t highest_offered_unit = highest_unit_limit,
    std::span<const ScreenSize> offered_sizes = screen_sizes
) noexcept;

/// Sets a slider setting to a stop's value.
///
/// @param[in,out] settings the settings
/// @param setting a slider setting
/// @param stop the stop, clamped to the slider's
/// @param highest_offered_unit the unit limit slider's highest value, in
///     units per player
/// @param offered_sizes the Screen size slider's stops, in order
///     (Dialog::offered_screen_sizes); never empty
void set_stop(
    EngineSettings& settings,
    Setting setting,
    int32_t stop,
    uint16_t highest_offered_unit = highest_unit_limit,
    std::span<const ScreenSize> offered_sizes = screen_sizes
) noexcept;

/// Tells whether a setting is a strip of levels: Enhanced anti-aliasing,
/// Hardware acceleration, One-finger drag and QUEUE and ADD.
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
/// @return "Off", "2x", "Basic", "Automatic", "Stay on" and so on; empty
///     past the strip's last
[[nodiscard]] std::string_view strip_caption(Setting setting, std::size_t level) noexcept;

/// Tells whether a setting is a drop-down: a field that shows the choice
/// and opens a list of the choices.
///
/// @param setting the setting
/// @return true for Language and Mod
[[nodiscard]] bool is_choice(Setting setting) noexcept;

/// Returns the languages the Language drop-down offers after System
/// default: the known languages this build draws, in the registry's order.
///
/// @return the languages
[[nodiscard]] std::span<const oa::data::languages::Language* const> offered_languages();

/// Returns how many choices a drop-down offers.
///
/// @param dialog the dialog
/// @param setting a drop-down setting
/// @return for Language, 1 and the offered languages; 0 for any other
///     setting
[[nodiscard]] std::size_t choice_count(const Dialog& dialog, Setting setting);

/// Returns a drop-down's choice as the drop-down names it: Language's first
/// is System default with the operating system's language named in itself,
/// "System default (Deutsch)", and the others each language named in itself.
///
/// @param dialog the dialog, whose mods and system language the choices name
/// @param setting a drop-down setting
/// @param index the choice, from 0
/// @return the text, in UTF-8; empty past the last choice
[[nodiscard]] std::string choice_text(const Dialog& dialog, Setting setting, std::size_t index);

/// Returns a drop-down's choice as it shows in a room: choice_text, but a
/// picked folder's path as its tail that fits (path_tail).
///
/// @param dialog the dialog
/// @param setting a drop-down setting
/// @param index the choice, from 0
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the text, in UTF-8
[[nodiscard]] std::string shown_choice_text(
    const Dialog& dialog,
    Setting setting,
    std::size_t index,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width
);

/// Returns what a drop-down's closed field shows: its choice, as
/// shown_choice_text gives it; but on the Mod row a game locks
/// (Lock::in_game), the mod the game plays (Dialog::playing_mod_folder):
/// No Mod, an offered mod's name, or another folder's path as its tail that
/// fits (path_tail).
///
/// @param dialog the dialog
/// @param row the drop-down's row
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the text, in UTF-8
[[nodiscard]] std::string field_text(
    const Dialog& dialog,
    const Row& row,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width
);

/// Returns the choice a drop-down shows.
///
/// @param dialog the dialog, whose chosen settings count
/// @param setting a drop-down setting
/// @return the choice, from 0; 0, System default or No Mod, for a value not
///     offered
[[nodiscard]] std::size_t choice_index(const Dialog& dialog, Setting setting);

/// Sets a drop-down setting to a choice.
///
/// @param[in,out] dialog the dialog, whose chosen settings change
/// @param setting a drop-down setting; any other is left alone
/// @param index the choice, clamped to the drop-down's
void set_choice(Dialog& dialog, Setting setting, std::size_t index);

/// Returns where a drop-down's open list lies: under its field, its left
/// edge with the field's, as wide as the field and as tall as the items it
/// shows, at most most_shown_choices, with a one-pixel border; over the
/// field when it would reach below the footer's line.
///
/// @param field the drop-down's field
/// @param choices the choices it offers
/// @return the list's rectangle
[[nodiscard]] SourceRect choice_list(const SourceRect& field, std::size_t choices) noexcept;

/// Returns how many items an open list shows at once.
///
/// @param choices the choices it offers
/// @return at most most_shown_choices
[[nodiscard]] int32_t shown_choices(std::size_t choices) noexcept;

/// Returns one shown item's rectangle in an open list.
///
/// @param list the list (choice_list)
/// @param shown the item's place among those shown, from 0
/// @return the item's rectangle, inside the list's border
[[nodiscard]] SourceRect choice_item(const SourceRect& list, int32_t shown) noexcept;

/// Tells whether a setting's row has a button on its label line that asks
/// the host to act: Game files' summary row and its MANAGE….
///
/// @param setting the setting
/// @return true for Setting::game_files_summary
[[nodiscard]] bool is_button(Setting setting) noexcept;

/// Tells whether a setting's row only shows text and takes no press: Where
/// the files are.
///
/// @param setting the setting
/// @return true for Setting::game_files_location
[[nodiscard]] bool is_text(Setting setting) noexcept;

/// Tells whether a setting is an Off/On switch.
///
/// @param setting the setting
/// @return true for a switch, false for a slider, a level strip, a
///     drop-down, a button, a text row or a row of buttons
[[nodiscard]] bool is_switch(Setting setting) noexcept;

/// Tells whether a setting is a row of buttons that each open a folder: Your
/// files, whose buttons stand on its label line, right-aligned, and change
/// no setting.
///
/// @param setting the setting
/// @return true for Your files
[[nodiscard]] bool is_buttons(Setting setting) noexcept;

/// The columns between two of Your files' buttons.
inline constexpr int32_t folder_button_gap = 4;
/// Each of Your files' buttons' width, left to right: room for its caption
/// with clear columns each side.
inline constexpr std::array<int32_t, folder_button_count> folder_button_widths{46, 82, 44};
/// The width of Your files' buttons together, with the gaps between them.
inline constexpr int32_t folder_buttons_width = folder_button_widths[0] + folder_button_widths[1] +
                                                folder_button_widths[2] + 2 * folder_button_gap;

/// Returns one of Your files' buttons within the row's control area.
///
/// @param area the row's control area: the buttons together
/// @param index the button, from 0 at the left
/// @return its rectangle
[[nodiscard]] SourceRect folder_button(const SourceRect& area, std::size_t index) noexcept;

/// Returns the button of Your files under a point.
///
/// @param area the row's control area
/// @param x the point's column
/// @param y the point's row
/// @return the button, from 0 at the left; folder_button_count for none,
///     as between two buttons
[[nodiscard]] std::size_t folder_button_at(const SourceRect& area, int32_t x, int32_t y) noexcept;

/// Returns the caption of one of Your files' buttons.
///
/// @param index the button, from 0 at the left
/// @return "SAVES", "SCREENSHOTS" or "MODS"; empty past the last
[[nodiscard]] std::string_view folder_button_text(std::size_t index) noexcept;

/// Returns a line of a row's hint as it is drawn in a width: row_hint's
/// text, but Your files' first line, the player's own folder, as its tail
/// that fits (path_tail).
///
/// @param dialog the dialog
/// @param setting the row's setting
/// @param line the line, from 0
/// @param width the room, in source pixels
/// @param text_width a text's width in the small font
/// @return the text, in UTF-8
[[nodiscard]] std::string shown_hint_text(
    const Dialog& dialog,
    Setting setting,
    std::size_t line,
    int32_t width,
    const std::function<int32_t(std::string_view)>& text_width
);

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

/// Returns a drop-down's field width.
///
/// @param setting a drop-down's setting
/// @return wide_choice_width for Gyro pointer, else choice_width
[[nodiscard]] int32_t choice_field_width(Setting setting) noexcept;

/// What a dialog shows beyond each setting's own rows and lines.
struct RowContext {
    /// Controller's first row is the Steam Input notice (Dialog::steam_input).
    bool steam_input{};
    /// Maximum frame rate's hint has a second line naming a Steam Deck's
    /// screen rate (Dialog::steam_deck_panel_hz); 0 for none.
    uint32_t steam_deck_panel_hz{};
};

/// Returns what a dialog shows beyond each setting's own rows and lines.
///
/// @param dialog the dialog
/// @return its Steam Input notice and its Steam Deck's screen rate
[[nodiscard]] RowContext row_context(const Dialog& dialog) noexcept;

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
/// @param context what the dialog shows beyond each setting's rows:
///     Controller starts with the Steam Input notice while it applies
/// @return its settings, top to bottom
[[nodiscard]] std::span<const Setting>
section_settings(Page page, const SectionHooks* section, const RowContext& context = {});

/// Places the rows of a section; Developer's own, over its list, with
/// developer_row_padding.
///
/// A locked switch keeps its switch, faded, with its lock left of it, so
/// that its value shows; a locked switch or strip whose hint lines are its
/// status shows its lock where its control was.
///
/// @param page the section
/// @param locks the dialog's locks
/// @param scroll the rows the section is scrolled by from its top; not clamped
/// @param section a check's own section; null for the dialog's
/// @param context what the dialog shows beyond each setting's rows and
///     lines (row_context)
/// @return its rows
[[nodiscard]] Rows place_rows(
    Page page,
    const Locks& locks,
    int32_t scroll = 0,
    const SectionHooks* section = nullptr,
    const RowContext& context = {}
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

/// Returns the locks the open section's rows show: the dialog's, and Text
/// size locked Lock::needs_modern_fonts while the dialog shows Use modern
/// fonts for game text Off.
///
/// @param dialog the dialog
/// @return the locks
[[nodiscard]] Locks shown_locks(const Dialog& dialog) noexcept;

/// Places the open section's rows once, at its offset clamped to its limit,
/// with the locks shown_locks gives.
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

/// Tells whether a dialog of the engine's settings shows Developer's list
/// and its footer under Developer's rows: Developer is open, and no check's
/// own section takes its place (SectionHooks::settings).
///
/// @param dialog the dialog
/// @return true while it shows the list
[[nodiscard]] bool developer_page(const Dialog& dialog) noexcept;

/// Places Developer's list: the areas, and under each open one its
/// hacks, and under each open hack its summary, its scope, its notes and its
/// parameters, as Show Active Only filters them.
///
/// @param dialog the dialog
/// @param scroll the rows the list is scrolled by from its top; not clamped
/// @return the list
[[nodiscard]] List place_list(const Dialog& dialog, int32_t scroll = 0);

/// Moves a placed list up, every part of every row with it.
///
/// @param[in,out] list the list
/// @param by the rows it moves up; negative moves it down
void scroll_list(List& list, int32_t by) noexcept;

/// Returns the offset nearest the open one that shows one of the list's
/// rows whole.
///
/// @param open Developer's list (open_rows)
/// @param control the row's control
/// @return the offset, 0 to the list's limit; the open one for no such row
[[nodiscard]] int32_t list_scroll_showing(const ScrolledRows& open, int32_t control) noexcept;

/// Returns the row of a list's control.
///
/// @param list the list
/// @param control the control
/// @return the row; null when no row of the list is the control
[[nodiscard]] const ListRow* list_row(const List& list, int32_t control) noexcept;

/// Returns the lines a summary is broken into: at most
/// summary_line_characters each, broken between words, a word longer than
/// a line after one of its slashes or else at the line's end, with each
/// character the game's fonts lack written as ASCII.
///
/// @param summary the summary, UTF-8
/// @return the lines, in order
[[nodiscard]] std::vector<std::string> summary_lines(std::string_view summary);

/// Returns a text in the characters the game's fonts hold: a degree sign,
/// a middle dot, a multiplication sign and a plus-minus sign as " degrees",
/// "*", "x" and "+/-", Chinese, Japanese and Korean characters as they are,
/// for the modern fonts to draw, and any other character outside printable
/// ASCII as "?".
///
/// @param text the text, UTF-8
/// @return the text in printable ASCII and those characters
[[nodiscard]] std::string ascii_text(std::string_view text);

/// Returns a value as a list's slider shows it: a number with its unit,
/// "none", a word of an enumeration or a string.
///
/// @param value the value
/// @param unit what a number counts, such as "ticks"; empty for none
/// @return the text
[[nodiscard]] std::string list_value_text(const Value& value, std::string_view unit);

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

/// Returns the scroll bar's thumb in a scroll area, as scroll_thumb places
/// it in a section's.
///
/// @param area where the section scrolls
/// @param scroll the offset, 0 to `limit`
/// @param limit the section's limit, above 0
/// @param content_height the section's content height
/// @return the thumb
[[nodiscard]] SourceRect scroll_thumb(
    const ScrollArea& area, int32_t scroll, int32_t limit, int32_t content_height
) noexcept;

/// Returns the offset that puts the scroll bar's thumb's top at a row, to
/// the nearest row.
///
/// @param thumb_top the thumb's top row, clamped to its travel
/// @param limit the section's limit, above 0
/// @param content_height the section's content height
/// @return the offset, 0 to `limit`
[[nodiscard]] int32_t scroll_at(int32_t thumb_top, int32_t limit, int32_t content_height) noexcept;

/// Returns the offset that puts the scroll bar's thumb's top at a row in a
/// scroll area, as scroll_at does in a section's.
///
/// @param area where the section scrolls
/// @param thumb_top the thumb's top row, clamped to its travel
/// @param limit the section's limit, above 0
/// @param content_height the section's content height
/// @return the offset, 0 to `limit`
[[nodiscard]] int32_t scroll_at(
    const ScrollArea& area, int32_t thumb_top, int32_t limit, int32_t content_height
) noexcept;

/// Returns a section's entry in the list: at its place among the sections
/// its kind of dialog lists (dialog_pages), Developer under the line before
/// it.
///
/// @param page the section
/// @param touch the dialog lists Touch (Dialog::touch)
/// @param game_files the dialog lists Game files (Dialog::game_files)
/// @param controller the dialog lists Controller (Dialog::controller)
/// @return its rectangle
[[nodiscard]] SourceRect
list_item(Page page, bool touch = false, bool game_files = false, bool controller = false) noexcept;

/// Returns the line before the Developer section in the list.
///
/// @return its rectangle, one row high
[[nodiscard]] SourceRect list_divider() noexcept;

/// MANAGE…'s caption.
inline constexpr std::string_view manage_text = "MANAGE…";
/// The caption of Restore profile values.
inline constexpr std::string_view restore_profile_text = "RESTORE PROFILE VALUES";
/// What a rule (sim-scope) hack says under its summary.
inline constexpr std::string_view next_match_text = "Applies at next match";
/// What a hack that is off says under its summary.
inline constexpr std::string_view hack_off_text = "Off: it plays as 3.1c.";
/// What a hack this engine does not implement says under its summary.
inline constexpr std::string_view not_implemented_text = "Not implemented by this engine.";
/// The name of a list's length.
inline constexpr std::string_view items_text = "Items";

/// Returns Show Active Only's label.
///
/// @param active the hacks that are on (active_hack_count)
/// @param total every standard hack
/// @return "Show Active Only (X/Y)"
[[nodiscard]] std::string active_only_text(std::size_t active, std::size_t total);

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

/// A line of the text under a row, and whether it is drawn as a notice.
struct HintLine {
    std::string text; ///< the line; empty past the last
    /// Drawn in the notice colour, as locks are: what the player must know
    /// of the choice shown.
    bool notice{};
};

/// Returns a hint's line, as the source writes it: Include in device
/// backups' first line holds {device}, which row_hint fills, and the Game
/// files section's other rows have none of their own (row_hint gives the
/// host's texts).
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
/// @return 1 or 2; most_notice_lines for the Steam Input notice
[[nodiscard]] std::size_t hint_line_count(Setting setting) noexcept;

/// Returns the lines a row's hint takes in a dialog: hint_line_count, and
/// on a Steam Deck one more for Maximum frame rate, which names the rate it
/// starts at.
///
/// @param setting the setting
/// @param context what the dialog shows beyond each setting's lines
/// @return 1 or 2; most_notice_lines for the Steam Input notice
[[nodiscard]] std::size_t hint_line_count(Setting setting, const RowContext& context) noexcept;

/// The text of Controller's Steam Input notice, which its row breaks into
/// lines between words.
inline constexpr std::string_view steam_input_notice_text =
    "Steam Input is on: the trackpads and back grips reach the game as Steam's mouse and keys. "
    "Turn Steam Input off for Open Annihilation in Steam's controller settings to use them here.";
/// Maximum frame rate's second hint line on a Steam Deck: the screen's
/// rate, which the setting starts at, in place of {rate}.
inline constexpr std::string_view steam_deck_rate_text =
    "Steam Deck: starts at the screen's {rate} fps.";

/// Returns a line of the text under a row as the dialog shows it: the
/// hint's (hint_line); for the Game files rows the host's texts: the
/// summary and its sizes line, the backups hint with the device's name
/// (Dialog::game_files_device, "device" without one), and where the files
/// are, broken into lines between words (break_lines); for Your files
/// the player's own folder, then why the last folder asked for could not be
/// opened, as a notice; for the Steam Input notice its text, broken into
/// lines between words, as a notice; and on a Steam Deck Maximum frame
/// rate's second line, naming the rate it starts at.
///
/// @param dialog the dialog
/// @param setting the row's setting
/// @param line the line, from 0
/// @return the line; empty past the last
[[nodiscard]] HintLine row_hint(const Dialog& dialog, Setting setting, std::size_t line);

/// Returns a setting's label as the dialog shows it (label_of).
///
/// @param setting the setting
/// @return the label
[[nodiscard]] std::string_view row_label(Setting setting) noexcept;

/// Breaks a text into lines of at most a number of characters, between
/// words, after a location's mark between folders (›) where one stands in a
/// line's second half; a word longer than a line is cut at the line's end.
/// Characters are counted as UTF-8 characters, not bytes.
///
/// @param text the text, UTF-8
/// @param characters the most characters a line holds, 1 or more
/// @param most_lines the most lines kept; the rest of the text is dropped
/// @return the lines, in order; none for an empty text
[[nodiscard]] std::vector<std::string>
break_lines(std::string_view text, std::size_t characters, std::size_t most_lines);

/// Returns a section's entry in the list of a dialog of any kind: as
/// list_item places it for the engine's settings and a mod's options, at
/// the top for a Language dialog's one section.
///
/// @param dialog the dialog
/// @param page the section
/// @return its rectangle
[[nodiscard]] SourceRect dialog_list_item(const Dialog& dialog, Page page) noexcept;

/// Returns a slider's value as it is shown.
///
/// @param setting a slider setting
/// @param settings the settings shown
/// @return the text
[[nodiscard]] std::string value_text(Setting setting, const EngineSettings& settings);

/// Returns the settings a dialog's sliders show: the settings chosen, but
/// Screen size at the window's own size until its knob moves
/// (Dialog::window_screen_size).
///
/// @param dialog the dialog
/// @return the settings
[[nodiscard]] EngineSettings slider_settings(const Dialog& dialog);

/// Returns a slider's value as the dialog shows it: as value_text of
/// slider_settings, but "Custom" for Screen size at the window's own size
/// that the display does not offer (Dialog::custom_screen_size).
///
/// @param setting a slider setting
/// @param dialog the dialog
/// @return the text
[[nodiscard]] std::string value_text(Setting setting, const Dialog& dialog);

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
/// Mods' row of no mod.
inline constexpr std::string_view no_mod_text = "No Mod";
/// No Mod's version.
inline constexpr std::string_view no_mod_version_text = "3.1c";
/// No Mod's description.
inline constexpr std::string_view no_mod_description_text =
    "The game's own rules, as 3.1c plays them.";
/// What a mod row's version says when the folder has no oamod.yaml.
inline constexpr std::string_view no_profile_version_text = "N/A";
/// What a mod row's description says when the folder has no oamod.yaml.
inline constexpr std::string_view no_profile_description_text = "No oamod.yaml present";
/// The tag beside the title of the mod played.
inline constexpr std::string_view playing_text = "PLAYING";
/// The button under Mods' list.
inline constexpr std::string_view open_mods_folder_text = "OPEN MODS FOLDER";
/// The lines under Mods' list, naming the folders it lists.
inline constexpr std::array<std::string_view, 2> mods_folders_text{
    "Lists the mods in the game folder's mods folder and",
    "in Documents/Open Annihilation/Mods.",
};
/// What a shortened path or text ends or starts with.
inline constexpr std::string_view path_ellipsis = "...";
/// The Switch Mod question's heading.
inline constexpr std::string_view switch_heading_text = "SWITCH MOD";
/// What the Switch Mod question asks, with the mod's title in its place.
inline constexpr std::string_view switch_ask_text =
    "Switch to {title} now? The game reloads its data for the new mod and returns to the main "
    "menu. Your other settings are kept.";
/// The question's note for a folder without an oamod.yaml.
inline constexpr std::string_view switch_no_profile_text =
    "This folder has no oamod.yaml, so the game's own rules apply.";
/// The question's SWITCH caption.
inline constexpr std::string_view yes_text = "SWITCH";
/// A mod row's ROLL BACK caption, and the Roll Back Mod question's.
inline constexpr std::string_view roll_back_text = "ROLL BACK";
/// The Roll Back Mod question's heading.
inline constexpr std::string_view roll_back_heading_text = "ROLL BACK MOD";
/// The Roll Back Mod question's version line, filled with the versions.
inline constexpr std::string_view roll_back_versions_text = "{from} to {to}";
/// What the Roll Back Mod question asks, filled with the mod's title and
/// the versions.
inline constexpr std::string_view roll_back_ask_text =
    "Roll back {title} to {to}? {from} is kept in its place, and ROLL BACK again brings it back.";
/// What the Roll Back Mod question adds for the mod the game plays.
inline constexpr std::string_view roll_back_reload_text =
    "The game reloads its data for the mod and returns to the main menu.";
/// The question's CANCEL caption.
inline constexpr std::string_view no_text = "CANCEL";
/// What Mods says during a game, which keeps the mod it plays.
inline constexpr std::string_view mod_in_game_text =
    "Locked during a game. Choose the mod from the main menu.";
/// Your files' second hint line while no folder failed to open.
inline constexpr std::string_view user_folder_hint_text =
    "Saved games, screenshots, films and mods.";
/// What Mods says when the command line chose this run's mod.
inline constexpr std::string_view mod_from_command_line_text =
    "The command line chose this run's mod.";
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
