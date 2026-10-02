// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's sections, rows and controls, where they lie, and
// what pointer and key events do to them.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <string>
#include <utility>

namespace oa::ui::engine_settings {

namespace geometry {

namespace {

/// AI & Pathfinding's rows.
constexpr std::array<Setting, 1> kPathSearchRows{Setting::path_search};
/// Controls & Input's rows.
constexpr std::array<Setting, 3> kControlsRows{
    Setting::wheel_zoom,
    Setting::escape_opens_menu,
    Setting::switch_alt,
};
/// Gameplay's rows.
constexpr std::array<Setting, 1> kGameplayRows{Setting::unit_limit};
/// Graphics' rows.
constexpr std::array<Setting, 3> kGraphicsRows{
    Setting::max_frame_rate,
    Setting::anti_aliasing,
    Setting::screen_size,
};
/// Developer's rows.
constexpr std::array<Setting, 1> kDeveloperRows{Setting::frame_stats};

/// The lowest level that draws units finer and needs the warning hint.
constexpr AntiAliasing kDemandingLevel = AntiAliasing::x8;

/// Returns a screen size's place among screen_sizes.
///
/// @param size the screen size
/// @return its index; 0, the desktop's, for a size not offered
int32_t screen_size_index(ScreenSize size) noexcept {
    const auto found = std::find(screen_sizes.begin(), screen_sizes.end(), size);
    return found == screen_sizes.end() ? 0 : static_cast<int32_t>(found - screen_sizes.begin());
}

/// Returns a setting's lock, a check's own section's when it gives one.
///
/// @param locks the dialog's locks
/// @param setting the setting
/// @param section a check's own section; null for the dialog's
/// @return why it cannot be changed now
Lock row_lock(const Locks& locks, Setting setting, const SectionHooks* section) {
    const Lock lock = lock_of(locks, setting);
    if (section == nullptr || section->lock == nullptr)
        return lock;
    return section->lock(section->context, setting, lock);
}

/// Tells whether a setting's hint lines are its status, as a check's own
/// section has it when it says.
///
/// @param setting the setting
/// @param section a check's own section; null for the dialog's
/// @return true when its hint lines are its status
bool row_hint_is_status(Setting setting, const SectionHooks* section) {
    if (section == nullptr || section->hint_is_status == nullptr)
        return hint_is_status(setting);
    return section->hint_is_status(section->context, setting);
}

/// Returns a section's place among Dialog::scroll.
///
/// @param page the section
/// @return its index, below page_count
std::size_t scroll_index(Page page) noexcept {
    return std::min(static_cast<std::size_t>(page), page_count - 1);
}

/// Returns a whole number of a range's steps, rounded to the nearest.
///
/// @param value the value
/// @param lowest the range's lowest value
/// @param step the step
/// @return (value - lowest) / step, rounded half up
int32_t steps_from(int64_t value, int64_t lowest, int64_t step) noexcept {
    return static_cast<int32_t>((value - lowest + step / 2) / step);
}

} // namespace

bool is_slider(Setting setting) noexcept {
    return setting == Setting::path_search || setting == Setting::unit_limit ||
           setting == Setting::max_frame_rate || setting == Setting::screen_size;
}

Slider slider_of(Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        return Slider{highest_path_search_multiplier};
    case Setting::unit_limit:
        return Slider{(highest_unit_limit - lowest_unit_limit) / unit_limit_step + 1};
    case Setting::max_frame_rate:
        return Slider{
            static_cast<int32_t>((highest_frame_rate - lowest_frame_rate) / frame_rate_step + 1)
        };
    case Setting::screen_size:
        return Slider{static_cast<int32_t>(screen_sizes.size())};
    default:
        return Slider{2};
    }
}

int32_t stop_of(const EngineSettings& settings, Setting setting) noexcept {
    const int32_t last = slider_of(setting).stops - 1;
    int32_t stop = 0;
    switch (setting) {
    case Setting::path_search:
        stop = path_search_multiplier(settings.path_search_nodes) - 1;
        break;
    case Setting::unit_limit:
        stop = steps_from(settings.unit_limit, lowest_unit_limit, unit_limit_step);
        break;
    case Setting::max_frame_rate:
        stop = steps_from(settings.max_frame_rate, lowest_frame_rate, frame_rate_step);
        break;
    case Setting::screen_size:
        stop = screen_size_index(settings.screen_size);
        break;
    default:
        break;
    }
    return std::clamp(stop, int32_t{0}, last);
}

void set_stop(EngineSettings& settings, Setting setting, int32_t stop) noexcept {
    const int32_t clamped = std::clamp(stop, int32_t{0}, slider_of(setting).stops - 1);
    switch (setting) {
    case Setting::path_search:
        settings.path_search_nodes = base_path_search_nodes * (clamped + 1);
        break;
    case Setting::unit_limit:
        settings.unit_limit = static_cast<uint16_t>(lowest_unit_limit + clamped * unit_limit_step);
        break;
    case Setting::max_frame_rate:
        settings.max_frame_rate =
            lowest_frame_rate + static_cast<uint32_t>(clamped) * frame_rate_step;
        break;
    case Setting::screen_size:
        settings.screen_size = screen_sizes[static_cast<std::size_t>(clamped)];
        break;
    default:
        break;
    }
}

Lock lock_of(const Locks& locks, Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        return locks.path_search;
    case Setting::unit_limit:
        return locks.unit_limit;
    case Setting::max_frame_rate:
        return locks.max_frame_rate;
    default:
        return Lock::none;
    }
}

bool hint_is_status(Setting) noexcept {
    return false;
}

std::span<const Setting> section_settings(Page page, const SectionHooks* section) {
    if (section == nullptr || section->settings == nullptr)
        return page_settings(page);
    return section->settings(section->context, page);
}

Rows place_rows(Page page, const Locks& locks, int32_t scroll, const SectionHooks* section) {
    Rows placed{};
    int32_t top = first_row_top;
    const auto settings = section_settings(page, section);
    placed.rows.reserve(settings.size());
    for (std::size_t index = 0; index < settings.size(); ++index) {
        Row& row = placed.rows.emplace_back();
        row.setting = settings[index];
        row.control = first_row_control + static_cast<int32_t>(index);
        row.lock = row_lock(locks, row.setting, section);
        row.hint_is_status = row_hint_is_status(row.setting, section);
        row.top = top;
        const int32_t label_top = top + 1 + row_padding;
        const bool locked = row.lock != Lock::none;
        // The lock, right-aligned on the label line; the label ends short of it.
        const SourceRect right_lock{
            content_right - lock_width, label_top, lock_width, label_line_height
        };
        int32_t label_right = content_right;
        int32_t control_width = 0;
        if (row.setting == Setting::anti_aliasing)
            control_width = static_cast<int32_t>(anti_aliasing_levels.size()) * level_width + 2;
        else if (!is_slider(row.setting))
            control_width = switch_width;
        if (control_width == 0 || (locked && row.hint_is_status)) {
            // A slider's lock, or the lock of a switch whose hint lines are
            // its status, which stands where the switch was.
            if (locked) {
                row.lock_area = right_lock;
                label_right = row.lock_area.x - label_gap;
            }
        } else {
            row.control_area = {
                content_right - control_width, label_top, control_width, label_line_height
            };
            label_right = row.control_area.x - label_gap;
            // Any other locked control keeps its place, so that its value
            // shows, with its lock left of it. Its label keeps only the
            // columns left of the lock: 93 beside a switch, too few beside
            // the level strip, which no lock reaches.
            if (locked) {
                row.lock_area = {
                    row.control_area.x - label_gap - lock_width,
                    label_top,
                    lock_width,
                    label_line_height
                };
                label_right = row.lock_area.x - label_gap;
            }
        }
        row.label = {content_left, label_top, label_right - content_left, label_line_height};
        row.hint_lines = hint_line_count(row.setting);
        int32_t bottom = label_top + label_line_height + hint_gap;
        for (std::size_t line = 0; line < row.hint_lines; ++line) {
            row.hints[line] = {content_left, bottom, content_width, hint_line_height};
            bottom += hint_line_height;
        }
        if (is_slider(row.setting)) {
            bottom += slider_gap;
            const int32_t value_left = content_right - slider_value_width;
            row.control_area = {
                content_left,
                bottom,
                value_left - slider_value_gap - content_left,
                slider_line_height
            };
            row.value = {value_left, bottom, slider_value_width, slider_line_height};
            bottom += slider_line_height;
        }
        bottom += row_padding;
        row.height = bottom - top;
        top = bottom;
    }
    placed.bottom = top;
    scroll_rows(placed, scroll);
    return placed;
}

void scroll_rows(Rows& rows, int32_t by) noexcept {
    const auto lift = [by](SourceRect& rect) {
        if (rect.width > 0 && rect.height > 0)
            rect.y -= by;
    };
    for (Row& row : rows.rows) {
        row.top -= by;
        lift(row.label);
        lift(row.lock_area);
        for (SourceRect& hint : row.hints)
            lift(hint);
        lift(row.control_area);
        lift(row.value);
    }
    rows.bottom -= by;
}

int32_t content_height(const Rows& rows, int32_t scroll) noexcept {
    return rows.bottom + scroll + 1 + end_gap - first_row_top;
}

int32_t scroll_limit(int32_t content_height) noexcept {
    return std::max(content_height - view.height, int32_t{0});
}

ScrolledRows open_rows(const Dialog& dialog) {
    ScrolledRows open{};
    open.rows = place_rows(dialog.page, dialog.locks, 0, dialog.section_hooks);
    open.content_height = content_height(open.rows, 0);
    open.limit = scroll_limit(open.content_height);
    open.scroll = std::clamp(dialog.scroll[scroll_index(dialog.page)], int32_t{0}, open.limit);
    scroll_rows(open.rows, open.scroll);
    return open;
}

int32_t scroll_showing(const ScrolledRows& open, std::size_t index) noexcept {
    if (index >= open.rows.rows.size())
        return open.scroll;
    const Row& row = open.rows.rows[index];
    // The row's line, at the section's top, may come up to the view's first
    // row; the line under it, or the end gap under the last row, down to its
    // last. A row taller than the view would show its top.
    const int32_t line = row.top + open.scroll;
    const int32_t highest = line - view.y;
    const int32_t lowest = index + 1 == open.rows.rows.size()
                               ? open.limit
                               : line + row.height - (view.y + view.height - 1);
    const int32_t scroll = std::min(std::max(open.scroll, lowest), highest);
    return std::clamp(scroll, int32_t{0}, open.limit);
}

SourceRect scroll_thumb(int32_t scroll, int32_t limit, int32_t content_height) noexcept {
    const SourceRect inside{
        scroll_well.x + 1, scroll_well.y + 1, scroll_well.width - 2, scroll_well.height - 2
    };
    int32_t height = inside.height;
    if (content_height > view.height)
        height = std::max(least_thumb_height, inside.height * view.height / content_height);
    const int32_t travel = inside.height - height;
    int32_t top = inside.y;
    if (limit > 0)
        top += (travel * std::clamp(scroll, int32_t{0}, limit) + limit / 2) / limit;
    return {inside.x, top, inside.width, height};
}

int32_t scroll_at(int32_t thumb_top, int32_t limit, int32_t content_height) noexcept {
    const SourceRect thumb = scroll_thumb(0, limit, content_height);
    const int32_t travel = scroll_well.height - 2 - thumb.height;
    if (travel <= 0 || limit <= 0)
        return 0;
    const int32_t along = std::clamp(thumb_top - thumb.y, int32_t{0}, travel);
    return (limit * along + travel / 2) / travel;
}

SourceRect list_item(Page page) noexcept {
    const auto index = static_cast<int32_t>(page);
    int32_t top = list_first_top + index * (list_item_height + list_item_gap);
    if (page == Page::developer)
        top = list_divider().y + 1 + list_divider_margin;
    return {list_item_left, top, list_item_width, list_item_height};
}

SourceRect list_divider() noexcept {
    const int32_t above = static_cast<int32_t>(Page::developer);
    const int32_t row = list_first_top + above * (list_item_height + list_item_gap) -
                        list_item_gap + list_divider_margin;
    return {
        list_item_left + list_divider_inset,
        row,
        list_item_width - 2 * list_divider_inset,
        1,
    };
}

SourceRect footer_button(int32_t control) noexcept {
    if (control == restore_control)
        return restore_button;
    if (control == cancel_control)
        return cancel_button;
    return ok_button;
}

int32_t knob_column(const SourceRect& track, int32_t stop, int32_t stops) noexcept {
    const int32_t first = track.x + knob_width / 2;
    const int32_t travel = track.width - knob_width;
    if (stops < 2)
        return first;
    return first +
           (travel * std::clamp(stop, int32_t{0}, stops - 1) + (stops - 1) / 2) / (stops - 1);
}

int32_t stop_at(const SourceRect& track, int32_t column, int32_t stops) noexcept {
    const int32_t first = track.x + knob_width / 2;
    const int32_t travel = track.width - knob_width;
    if (stops < 2 || travel <= 0)
        return 0;
    const int32_t along = std::clamp(column - first, int32_t{0}, travel);
    return (along * (stops - 1) + travel / 2) / travel;
}

std::size_t level_at(const SourceRect& strip, int32_t column) noexcept {
    const int32_t along = std::max(column - strip.x - 1, int32_t{0}) / level_width;
    return std::min(static_cast<std::size_t>(along), anti_aliasing_levels.size() - 1);
}

std::size_t level_index(AntiAliasing level) noexcept {
    const auto found = std::find(anti_aliasing_levels.begin(), anti_aliasing_levels.end(), level);
    if (found == anti_aliasing_levels.end())
        return 0;
    return static_cast<std::size_t>(found - anti_aliasing_levels.begin());
}

std::string_view page_name(Page page) noexcept {
    switch (page) {
    case Page::path_search:
        return "AI & Pathfinding";
    case Page::controls:
        return "Controls & Input";
    case Page::gameplay:
        return "Gameplay";
    case Page::graphics:
        return "Graphics";
    case Page::developer:
        return "Developer";
    }
    return {};
}

std::string_view page_heading(Page page) noexcept {
    switch (page) {
    case Page::path_search:
        return "AI & PATHFINDING";
    case Page::controls:
        return "CONTROLS & INPUT";
    case Page::gameplay:
        return "GAMEPLAY";
    case Page::graphics:
        return "GRAPHICS";
    case Page::developer:
        return "DEVELOPER";
    }
    return {};
}

std::string_view label_of(Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        return "Pathfinding cycles";
    case Setting::wheel_zoom:
        return "Mouse wheel zoom";
    case Setting::escape_opens_menu:
        return "Escape opens the game menu";
    case Setting::switch_alt:
        return "Select groups without Alt";
    case Setting::unit_limit:
        return "Unit limit";
    case Setting::max_frame_rate:
        return "Maximum frame rate";
    case Setting::anti_aliasing:
        return "Enhanced anti-aliasing";
    case Setting::screen_size:
        return "Screen size";
    case Setting::frame_stats:
        return "Show performance statistics";
    }
    return {};
}

std::string_view
hint_line(Setting setting, const EngineSettings& settings, std::size_t line) noexcept {
    // Each hint is broken where it reads best, so each line fits the
    // section's width in the small font.
    using Lines = std::array<std::string_view, most_hint_lines>;
    Lines lines{};
    switch (setting) {
    case Setting::path_search:
        lines = {"More cycles find routes faster but use more CPU.", {}};
        break;
    case Setting::wheel_zoom:
        lines = {"Scroll to zoom the battlefield in and out.", {}};
        break;
    case Setting::escape_opens_menu:
        lines = {"The first press clears the selection,", "the second opens the menu."};
        break;
    case Setting::switch_alt:
        lines = {"A number key selects its group on its own.", {}};
        break;
    case Setting::unit_limit:
        lines = {"Units each player can have.", "Applies from the next game."};
        break;
    case Setting::max_frame_rate:
        lines = {"Lower it to save power.", {}};
        break;
    case Setting::anti_aliasing:
        if (settings.anti_aliasing == AntiAliasing::x16)
            lines = {"Units drawn at 16x and scaled down.", "Needs a fast CPU."};
        else if (
            static_cast<uint8_t>(settings.anti_aliasing) >= static_cast<uint8_t>(kDemandingLevel)
        )
            lines = {"Units drawn at 8x and scaled down.", "Needs a fast CPU."};
        else
            lines = {"Units drawn at higher resolution and scaled down", "for smoother edges."};
        break;
    case Setting::screen_size:
        lines = {"Full screen at this size, or a window of it.", "Applies from the next start."};
        break;
    case Setting::frame_stats:
        lines = {"Frame and tick times over the battlefield.", {}};
        break;
    }
    return line < lines.size() ? lines[line] : std::string_view{};
}

std::size_t hint_line_count(Setting setting) noexcept {
    switch (setting) {
    case Setting::escape_opens_menu:
    case Setting::unit_limit:
    case Setting::anti_aliasing:
    case Setting::screen_size:
        return 2;
    default:
        return 1;
    }
}

std::string value_text(Setting setting, const EngineSettings& settings) {
    switch (setting) {
    case Setting::path_search:
        return std::to_string(path_search_multiplier(settings.path_search_nodes)) + "x";
    case Setting::unit_limit:
        return std::to_string(settings.unit_limit) + " per player";
    case Setting::max_frame_rate:
        return std::to_string(settings.max_frame_rate) + " fps";
    case Setting::screen_size:
        return settings.screen_size == desktop_screen_size
                   ? std::string{"Desktop"}
                   : std::to_string(settings.screen_size.width) + " x " +
                         std::to_string(settings.screen_size.height);
    default:
        return {};
    }
}

std::string_view lock_text(Lock lock) noexcept {
    switch (lock) {
    case Lock::none:
        return {};
    case Lock::in_game:
        return "Locked during a game";
    case Lock::set_by_host:
        return "Set by the host";
    case Lock::command_line:
        return "Set on the command line";
    }
    return {};
}

std::string_view level_caption(AntiAliasing level) noexcept {
    switch (level) {
    case AntiAliasing::off:
        return "Off";
    case AntiAliasing::x2:
        return "2x";
    case AntiAliasing::x3:
        return "3x";
    case AntiAliasing::x4:
        return "4x";
    case AntiAliasing::x8:
        return "8x";
    case AntiAliasing::x16:
        return "16x";
    }
    return {};
}

} // namespace geometry

namespace layout = geometry;

namespace {

/// Tells whether a point lies in a rectangle.
///
/// @param rect the rectangle
/// @param x the point's column
/// @param y the point's row
/// @return true inside it
bool contains(const layout::SourceRect& rect, int32_t x, int32_t y) noexcept {
    return x >= rect.x && y >= rect.y && x < rect.x + rect.width && y < rect.y + rect.height;
}

/// Tells whether a rectangle lies wholly in another.
///
/// @param rect the rectangle
/// @param outer the other
/// @return true when no part of it lies outside
bool wholly_in(const layout::SourceRect& rect, const layout::SourceRect& outer) noexcept {
    return rect.x >= outer.x && rect.y >= outer.y && rect.x + rect.width <= outer.x + outer.width &&
           rect.y + rect.height <= outer.y + outer.height;
}

/// Returns the row a control is, when it is one of the open section's.
///
/// @param rows the open section's rows
/// @param control the control
/// @return the row; nullptr for a control that is not a row's
const layout::Row* row_of(const layout::Rows& rows, int32_t control) noexcept {
    const int32_t index = control - first_row_control;
    if (index < 0 || static_cast<std::size_t>(index) >= rows.rows.size())
        return nullptr;
    return &rows.rows[static_cast<std::size_t>(index)];
}

/// Returns the control under a point that a press can act on.
///
/// A row's control answers only on the part the view shows, and a locked
/// row's takes no press. The scroll bar answers while the section scrolls.
///
/// @param open the open section's rows
/// @param x the point's column
/// @param y the point's row
/// @return the control; no_control when none is there
int32_t control_at(const layout::ScrolledRows& open, int32_t x, int32_t y) noexcept {
    if (contains(layout::view, x, y)) {
        for (const layout::Row& row : open.rows.rows) {
            if (row.lock == Lock::none && contains(row.control_area, x, y))
                return row.control;
        }
    }
    if (open.limit > 0 && contains(layout::scroll_hit, x, y))
        return scroll_bar_control;
    for (const int32_t control : {restore_control, cancel_control, ok_control}) {
        if (contains(layout::footer_button(control), x, y))
            return control;
    }
    for (std::size_t index = 0; index < layout::page_count; ++index) {
        const auto page = static_cast<Page>(index);
        if (contains(layout::list_item(page), x, y))
            return page_control(page);
    }
    return no_control;
}

/// Returns the controls the keyboard focus moves through, in order: the
/// open section's rows that can be changed, the footer's buttons left to
/// right, then the sections' entries.
///
/// @param rows the open section's rows
/// @return the controls
std::vector<int32_t> focus_order(const layout::Rows& rows) {
    std::vector<int32_t> order;
    for (const layout::Row& row : rows.rows) {
        if (row.lock == Lock::none)
            order.push_back(row.control);
    }
    order.push_back(restore_control);
    order.push_back(cancel_control);
    order.push_back(ok_control);
    for (int32_t index = 0; index < static_cast<int32_t>(layout::page_count); ++index)
        order.push_back(first_page_control + index);
    return order;
}

/// Notes where the pointer is, so that the hover can follow the rows a
/// scroll moves under it.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column
/// @param y the pointer's row
void note_pointer(Dialog& dialog, int32_t x, int32_t y) noexcept {
    dialog.pointer_known = true;
    dialog.pointer_x = x;
    dialog.pointer_y = y;
}

/// Finds the control under the last pointer point again, after a scroll
/// moved the rows under it; a held press keeps its hover.
///
/// @param[in,out] dialog the dialog
/// @param open the open section's rows, at the offset the scroll left
void hover_again(Dialog& dialog, const layout::ScrolledRows& open) noexcept {
    if (!dialog.pointer_known || dialog.pressed != no_control)
        return;
    dialog.hovered = control_at(open, dialog.pointer_x, dialog.pointer_y);
}

/// Scrolls the open section to an offset, moving its placed rows with it.
/// No scroll changes a setting or moves the focus.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows (layout::open_rows), left at
///     the new offset
/// @param offset the offset, clamped to the section's limit
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction scroll_to(Dialog& dialog, layout::ScrolledRows& open, int32_t offset) noexcept {
    const int32_t next = std::clamp(offset, int32_t{0}, open.limit);
    dialog.scroll[layout::scroll_index(dialog.page)] = next;
    if (next == open.scroll)
        return DialogAction::none;
    layout::scroll_rows(open.rows, next - open.scroll);
    open.scroll = next;
    hover_again(dialog, open);
    return DialogAction::redraw;
}

/// Scrolls the least that shows a row whole, when a control is one of the
/// open section's rows; nothing moves while a press is held.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the offset shown
/// @param control the control
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction show_row(Dialog& dialog, layout::ScrolledRows& open, int32_t control) noexcept {
    if (control < first_row_control || dialog.pressed != no_control)
        return DialogAction::none;
    return scroll_to(
        dialog,
        open,
        layout::scroll_showing(open, static_cast<std::size_t>(control - first_row_control))
    );
}

/// Moves the focus to the next or previous control, and scrolls its row
/// into view when it is a row.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the offset shown
/// @param forward true for the next control, false for the previous
/// @return DialogAction::redraw
DialogAction move_focus(Dialog& dialog, layout::ScrolledRows& open, bool forward) {
    const auto order = focus_order(open.rows);
    const auto found = std::find(order.begin(), order.end(), dialog.focused);
    if (found == order.end()) {
        dialog.focused = forward ? order.front() : order.back();
    } else {
        const auto count = static_cast<std::ptrdiff_t>(order.size());
        const std::ptrdiff_t at = found - order.begin();
        const std::ptrdiff_t next = (at + (forward ? 1 : count - 1)) % count;
        dialog.focused = order[static_cast<std::size_t>(next)];
    }
    static_cast<void>(show_row(dialog, open, dialog.focused));
    return DialogAction::redraw;
}

/// Scrolls the open section for Page Up, Page Down, Home or End, whatever
/// has the focus; nothing moves while a press is held.
///
/// @param[in,out] dialog the dialog
/// @param[in,out] open the open section's rows, left at the new offset
/// @param key the key
/// @return DialogAction::redraw when the section moved, else DialogAction::none
DialogAction scroll_key(Dialog& dialog, layout::ScrolledRows& open, DialogKey key) noexcept {
    if (dialog.pressed != no_control)
        return DialogAction::none;
    int32_t next = open.scroll;
    if (key == DialogKey::page_up)
        next -= layout::page_step;
    else if (key == DialogKey::page_down)
        next += layout::page_step;
    else if (key == DialogKey::home)
        next = 0;
    else
        next = open.limit;
    return scroll_to(dialog, open, next);
}

/// Reports a change of the chosen settings, or only a look's.
///
/// @param before the chosen settings before the event
/// @param after the chosen settings after it
/// @return DialogAction::changed when they differ, else DialogAction::redraw
DialogAction changed_or_redraw(const EngineSettings& before, const EngineSettings& after) noexcept {
    return before == after ? DialogAction::redraw : DialogAction::changed;
}

/// Sets a switch.
///
/// @param[in,out] settings the settings
/// @param setting a switch setting
/// @param on true for On
void set_switch(EngineSettings& settings, Setting setting, bool on) noexcept {
    switch (setting) {
    case Setting::wheel_zoom:
        settings.wheel_zoom = on;
        break;
    case Setting::escape_opens_menu:
        settings.escape_opens_menu = on;
        break;
    case Setting::switch_alt:
        settings.switch_alt = on;
        break;
    case Setting::frame_stats:
        settings.frame_stats = on;
        break;
    default:
        break;
    }
}

/// Moves a row's control one step down or up: a switch to Off or On, a
/// slider one stop, the level strip one level.
///
/// @param[in,out] settings the settings
/// @param setting the row's setting
/// @param up true for a step up
void step(EngineSettings& settings, Setting setting, bool up) noexcept {
    if (layout::is_slider(setting)) {
        layout::set_stop(settings, setting, layout::stop_of(settings, setting) + (up ? 1 : -1));
        return;
    }
    if (setting == Setting::anti_aliasing) {
        const std::size_t index = layout::level_index(settings.anti_aliasing);
        if (up && index + 1 < anti_aliasing_levels.size())
            settings.anti_aliasing = anti_aliasing_levels[index + 1];
        else if (!up && index > 0)
            settings.anti_aliasing = anti_aliasing_levels[index - 1];
        return;
    }
    set_switch(settings, setting, up);
}

/// Copies one setting's value.
///
/// @param[in,out] to the settings it is copied into
/// @param from the settings it is copied from
/// @param setting the setting
void copy_setting(EngineSettings& to, const EngineSettings& from, Setting setting) noexcept {
    switch (setting) {
    case Setting::path_search:
        to.path_search_nodes = from.path_search_nodes;
        break;
    case Setting::wheel_zoom:
        to.wheel_zoom = from.wheel_zoom;
        break;
    case Setting::escape_opens_menu:
        to.escape_opens_menu = from.escape_opens_menu;
        break;
    case Setting::switch_alt:
        to.switch_alt = from.switch_alt;
        break;
    case Setting::unit_limit:
        to.unit_limit = from.unit_limit;
        break;
    case Setting::max_frame_rate:
        to.max_frame_rate = from.max_frame_rate;
        break;
    case Setting::anti_aliasing:
        to.anti_aliasing = from.anti_aliasing;
        break;
    case Setting::screen_size:
        to.screen_size = from.screen_size;
        break;
    case Setting::frame_stats:
        to.frame_stats = from.frame_stats;
        break;
    }
}

/// Resets every setting the dialog can change to its default. Each locked
/// setting, found through its row's lock on every section, keeps its value.
///
/// @param[in,out] dialog the dialog
/// @return what the reset asks of the host
DialogAction restore_defaults(Dialog& dialog) {
    const EngineSettings before = dialog.chosen;
    EngineSettings restored = dialog.defaults;
    for (std::size_t index = 0; index < page_count; ++index) {
        const auto page = static_cast<Page>(index);
        for (const Setting setting : layout::section_settings(page, dialog.section_hooks)) {
            if (layout::row_lock(dialog.locks, setting, dialog.section_hooks) != Lock::none)
                copy_setting(restored, before, setting);
        }
    }
    dialog.chosen = restored;
    dialog.restored = true;
    return changed_or_redraw(before, dialog.chosen);
}

/// Closes the dialog keeping what it shows.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::accepted
DialogAction accept(Dialog& dialog) noexcept {
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::accepted;
}

/// Closes the dialog putting back what it opened with.
///
/// @param[in,out] dialog the dialog
/// @return DialogAction::cancelled
DialogAction cancel(Dialog& dialog) noexcept {
    dialog.chosen = dialog.opened;
    dialog.pressed = no_control;
    dialog.dragging = false;
    return DialogAction::cancelled;
}

/// Shows a section.
///
/// @param[in,out] dialog the dialog
/// @param page the section
/// @return DialogAction::redraw
DialogAction show_page(Dialog& dialog, Page page) noexcept {
    dialog.page = page;
    dialog.wheel_rows = 0.0F;
    if (dialog.focused >= first_row_control)
        dialog.focused = page_control(page);
    return DialogAction::redraw;
}

/// Presses a button, or flips a switch, as Space or a click does.
///
/// @param[in,out] dialog the dialog
/// @param rows the open section's rows
/// @param control the control
/// @return what it asks of the host
DialogAction activate(Dialog& dialog, const layout::Rows& rows, int32_t control) {
    if (control == restore_control)
        return restore_defaults(dialog);
    if (control == cancel_control)
        return cancel(dialog);
    if (control == ok_control)
        return accept(dialog);
    if (control >= first_page_control &&
        control < first_page_control + static_cast<int32_t>(layout::page_count))
        return show_page(dialog, static_cast<Page>(control - first_page_control));
    const layout::Row* row = row_of(rows, control);
    if (row == nullptr || row->lock != Lock::none || layout::is_slider(row->setting) ||
        row->setting == Setting::anti_aliasing)
        return DialogAction::none;
    const EngineSettings before = dialog.chosen;
    bool on = false;
    switch (row->setting) {
    case Setting::wheel_zoom:
        on = dialog.chosen.wheel_zoom;
        break;
    case Setting::escape_opens_menu:
        on = dialog.chosen.escape_opens_menu;
        break;
    case Setting::switch_alt:
        on = dialog.chosen.switch_alt;
        break;
    case Setting::frame_stats:
        on = dialog.chosen.frame_stats;
        break;
    default:
        break;
    }
    set_switch(dialog.chosen, row->setting, !on);
    return changed_or_redraw(before, dialog.chosen);
}

/// Sets a slider to the stop under a column.
///
/// @param[in,out] dialog the dialog
/// @param row the slider's row
/// @param column the column
/// @return what it asks of the host
DialogAction drag_to(Dialog& dialog, const layout::Row& row, int32_t column) noexcept {
    const EngineSettings before = dialog.chosen;
    const int32_t stops = layout::slider_of(row.setting).stops;
    layout::set_stop(dialog.chosen, row.setting, layout::stop_at(row.control_area, column, stops));
    return changed_or_redraw(before, dialog.chosen);
}

} // namespace

std::span<const Setting> page_settings(Page page) noexcept {
    switch (page) {
    case Page::path_search:
        return layout::kPathSearchRows;
    case Page::controls:
        return layout::kControlsRows;
    case Page::gameplay:
        return layout::kGameplayRows;
    case Page::graphics:
        return layout::kGraphicsRows;
    case Page::developer:
        return layout::kDeveloperRows;
    }
    return {};
}

void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page
) {
    dialog = Dialog{};
    dialog.opened = current;
    dialog.chosen = current;
    dialog.defaults = defaults;
    dialog.locks = locks;
    dialog.version = std::string(version);
    dialog.page = page;
}

DialogAction dialog_pointer_move(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
    layout::ScrolledRows open = layout::open_rows(dialog);
    if (dialog.dragging) {
        // The thumb follows the pointer's row only, and the offset the thumb.
        if (dialog.pressed == scroll_bar_control)
            return scroll_to(
                dialog,
                open,
                layout::scroll_at(y - dialog.scroll_grab, open.limit, open.content_height)
            );
        const layout::Row* row = row_of(open.rows, dialog.pressed);
        if (row != nullptr)
            return drag_to(dialog, *row, x);
    }
    const int32_t hovered = control_at(open, x, y);
    if (hovered == dialog.hovered)
        return DialogAction::none;
    dialog.hovered = hovered;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
    layout::ScrolledRows open = layout::open_rows(dialog);
    const int32_t control = control_at(open, x, y);
    dialog.hovered = control;
    dialog.pressed = control;
    dialog.dragging = false;
    if (control == no_control)
        return DialogAction::none;
    if (control == scroll_bar_control) {
        // On the thumb, the press grabs it at the row pressed; on the well,
        // the thumb's middle jumps to the pointer and the drag starts there.
        // The scroll bar takes no focus, so the focus stays where it is.
        dialog.dragging = true;
        const layout::SourceRect thumb =
            layout::scroll_thumb(open.scroll, open.limit, open.content_height);
        if (y >= thumb.y && y < thumb.y + thumb.height) {
            dialog.scroll_grab = y - thumb.y;
            return DialogAction::redraw;
        }
        dialog.scroll_grab = thumb.height / 2;
        static_cast<void>(scroll_to(
            dialog, open, layout::scroll_at(y - dialog.scroll_grab, open.limit, open.content_height)
        ));
        return DialogAction::redraw;
    }
    if (dialog.focused != no_control)
        dialog.focused = control;
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr && layout::is_slider(row->setting)) {
        dialog.dragging = true;
        const DialogAction action = drag_to(dialog, *row, x);
        return action;
    }
    return DialogAction::redraw;
}

DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y) {
    note_pointer(dialog, x, y);
    const layout::ScrolledRows open = layout::open_rows(dialog);
    const int32_t pressed = dialog.pressed;
    const bool dragged = dialog.dragging;
    dialog.pressed = no_control;
    dialog.dragging = false;
    dialog.scroll_grab = 0;
    if (pressed == no_control)
        return DialogAction::none;
    const int32_t control = control_at(open, x, y);
    dialog.hovered = control;
    if (dragged || control != pressed)
        return DialogAction::redraw;
    const layout::Row* row = row_of(open.rows, control);
    if (row != nullptr) {
        const EngineSettings before = dialog.chosen;
        if (row->setting == Setting::anti_aliasing) {
            dialog.chosen.anti_aliasing =
                anti_aliasing_levels[layout::level_at(row->control_area, x)];
        } else {
            const bool on = x >= row->control_area.x + row->control_area.width / 2;
            set_switch(dialog.chosen, row->setting, on);
        }
        return changed_or_redraw(before, dialog.chosen);
    }
    return activate(dialog, open.rows, control);
}

DialogAction dialog_key(Dialog& dialog, DialogKey key) {
    layout::ScrolledRows open = layout::open_rows(dialog);
    const layout::Rows& rows = open.rows;
    switch (key) {
    case DialogKey::enter:
        return accept(dialog);
    case DialogKey::escape:
        return cancel(dialog);
    case DialogKey::down:
    case DialogKey::tab:
        return move_focus(dialog, open, true);
    case DialogKey::up:
    case DialogKey::back_tab:
        return move_focus(dialog, open, false);
    case DialogKey::page_up:
    case DialogKey::page_down:
    case DialogKey::home:
    case DialogKey::end:
        return scroll_key(dialog, open, key);
    default:
        break;
    }
    if (dialog.focused == no_control)
        return move_focus(dialog, open, true);
    // A key that acts on a row brings it into view first, so that the
    // player sees what it changed.
    const DialogAction shown = show_row(dialog, open, dialog.focused);
    const auto or_shown = [shown](DialogAction action) {
        return action == DialogAction::none ? shown : action;
    };
    if (key == DialogKey::space)
        return or_shown(activate(dialog, rows, dialog.focused));
    const bool up = key == DialogKey::right;
    const layout::Row* row = row_of(rows, dialog.focused);
    if (row != nullptr) {
        if (row->lock != Lock::none)
            return shown;
        const EngineSettings before = dialog.chosen;
        step(dialog.chosen, row->setting, up);
        return changed_or_redraw(before, dialog.chosen);
    }
    // Left and Right move along the footer's buttons.
    constexpr std::array<int32_t, 3> footer{restore_control, cancel_control, ok_control};
    const auto found = std::find(footer.begin(), footer.end(), dialog.focused);
    if (found == footer.end())
        return DialogAction::none;
    const auto at = static_cast<std::size_t>(found - footer.begin());
    const std::size_t next = up ? std::min(at + 1, footer.size() - 1) : (at == 0 ? 0 : at - 1);
    if (next == at)
        return DialogAction::none;
    dialog.focused = footer[next];
    return DialogAction::redraw;
}

DialogAction dialog_wheel(Dialog& dialog, int32_t x, int32_t y, float notches) {
    if (!dialog_contains(x, y) || dialog.pressed != no_control || !std::isfinite(notches))
        return DialogAction::none;
    note_pointer(dialog, x, y);
    layout::ScrolledRows open = layout::open_rows(dialog);
    if (open.limit == 0) {
        dialog.wheel_rows = 0.0F;
        return DialogAction::none;
    }
    // Away from the player scrolls towards the top. A turn larger than the
    // section scrolls to its end.
    const float reach = static_cast<float>(open.limit) + 1.0F;
    const float rows = std::clamp(
        dialog.wheel_rows - notches * static_cast<float>(layout::wheel_step), -reach, reach
    );
    const auto whole = static_cast<int32_t>(rows);
    dialog.wheel_rows = rows - static_cast<float>(whole);
    const int32_t next = std::clamp(open.scroll + whole, int32_t{0}, open.limit);
    // What is carried towards an end the section has reached is dropped.
    if ((next == 0 && dialog.wheel_rows < 0.0F) || (next == open.limit && dialog.wheel_rows > 0.0F))
        dialog.wheel_rows = 0.0F;
    return scroll_to(dialog, open, next);
}

bool dialog_contains(int32_t x, int32_t y) noexcept {
    return x >= 0 && y >= 0 && x < dialog_width && y < dialog_height;
}

std::vector<LayoutPart> dialog_layout(const Dialog& dialog) {
    std::vector<LayoutPart> parts;
    const auto text_part =
        [&parts](
            layout::SourceRect rect, std::string_view text, DialogFont font, int32_t tracking = 0
        ) { parts.push_back(LayoutPart{rect, std::string(text), font, tracking, no_control}); };
    const auto control_part = [&parts](layout::SourceRect rect, int32_t control) {
        parts.push_back(LayoutPart{rect, {}, DialogFont::regular, 0, control});
    };

    // The header: the mark, the title, and at the right the version and the
    // shared game's note.
    control_part(layout::header_mark, no_control);
    const int32_t title_left =
        layout::header_mark.x + layout::header_mark.width + layout::header_gap;
    text_part(
        {title_left, layout::header_top, layout::title_width, layout::header_height},
        layout::title_text,
        DialogFont::regular,
        layout::heading_tracking
    );
    const int32_t suffix_left = title_left + layout::title_width + layout::header_gap;
    text_part(
        {suffix_left, layout::header_top, layout::title_suffix_width, layout::header_height},
        layout::title_suffix_text,
        DialogFont::regular,
        layout::heading_tracking
    );
    const layout::SourceRect version{
        layout::content_right - layout::version_width,
        layout::header_top,
        layout::version_width,
        layout::header_height,
    };
    text_part(version, dialog.version, DialogFont::small);
    if (dialog.locks.shared_game) {
        const int32_t shared_left = suffix_left + layout::title_suffix_width + layout::header_gap;
        text_part(
            {shared_left,
             layout::header_top,
             version.x - layout::version_gap - shared_left,
             layout::header_height},
            layout::shared_game_text,
            DialogFont::small
        );
    }

    // The section list.
    for (std::size_t index = 0; index < layout::page_count; ++index) {
        const auto page = static_cast<Page>(index);
        const layout::SourceRect item = layout::list_item(page);
        parts.push_back(
            LayoutPart{
                {item.x + layout::list_text_offset,
                 item.y,
                 item.width - layout::list_text_offset - layout::list_text_margin,
                 item.height},
                std::string(layout::page_name(page)),
                DialogFont::regular,
                0,
                page_control(page),
            }
        );
    }
    control_part(layout::list_divider(), no_control);

    // The open section.
    text_part(
        layout::heading,
        layout::page_heading(dialog.page),
        DialogFont::small,
        layout::heading_tracking
    );
    // The rows: only the parts wholly in the view are listed, so that each
    // listed control is pressed where it is drawn and each text is whole.
    const layout::ScrolledRows open = layout::open_rows(dialog);
    const auto row_part = [&parts](LayoutPart part) {
        if (wholly_in(part.rect, layout::view))
            parts.push_back(std::move(part));
    };
    const auto row_text = [&row_part](
                              layout::SourceRect rect, std::string_view text, DialogFont font
                          ) { row_part(LayoutPart{rect, std::string(text), font, 0, no_control}); };
    for (const layout::Row& row : open.rows.rows) {
        // A locked row's control is drawn but takes no press.
        const int32_t control = row.lock == Lock::none ? row.control : no_control;
        row_text(row.label, layout::label_of(row.setting), DialogFont::regular);
        if (row.lock != Lock::none) {
            const layout::SourceRect text_area{
                row.lock_area.x + layout::padlock_width + layout::padlock_gap,
                row.lock_area.y,
                row.lock_area.width - layout::padlock_width - layout::padlock_gap,
                row.lock_area.height,
            };
            row_part(
                LayoutPart{
                    {row.lock_area.x, row.lock_area.y, layout::padlock_width, row.lock_area.height},
                    {},
                    DialogFont::regular,
                    0,
                    no_control,
                }
            );
            row_text(text_area, layout::lock_text(row.lock), DialogFont::small);
        }
        for (std::size_t line = 0; line < row.hint_lines; ++line)
            row_text(
                row.hints[line],
                layout::hint_line(row.setting, dialog.chosen, line),
                DialogFont::small
            );
        if (row.setting == Setting::anti_aliasing) {
            for (std::size_t level = 0; level < anti_aliasing_levels.size(); ++level) {
                row_part(
                    LayoutPart{
                        {row.control_area.x + 1 + static_cast<int32_t>(level) * layout::level_width,
                         row.control_area.y + 1,
                         layout::level_width,
                         row.control_area.height - 2},
                        std::string(layout::level_caption(anti_aliasing_levels[level])),
                        DialogFont::small,
                        0,
                        control,
                    }
                );
            }
        } else if (layout::is_slider(row.setting)) {
            row_part(LayoutPart{row.control_area, {}, DialogFont::regular, 0, control});
            row_text(
                row.value, layout::value_text(row.setting, dialog.chosen), DialogFont::regular
            );
        } else if (row.control_area.width > 0) {
            const int32_t half = (row.control_area.width - 2) / 2;
            row_part(
                LayoutPart{
                    {row.control_area.x + 1,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::off_text),
                    DialogFont::small,
                    0,
                    control,
                }
            );
            row_part(
                LayoutPart{
                    {row.control_area.x + 1 + half,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::on_text),
                    DialogFont::small,
                    0,
                    control,
                }
            );
        }
    }
    if (open.limit > 0)
        control_part(layout::scroll_well, scroll_bar_control);

    // The footer.
    const std::array<std::pair<int32_t, std::string_view>, 3> buttons{{
        {restore_control, layout::restore_text},
        {cancel_control, layout::cancel_text},
        {ok_control, layout::ok_text},
    }};
    for (const auto& [control, caption] : buttons) {
        parts.push_back(
            LayoutPart{
                layout::footer_button(control),
                std::string(caption),
                DialogFont::small,
                0,
                control,
            }
        );
    }
    return parts;
}

} // namespace oa::ui::engine_settings
