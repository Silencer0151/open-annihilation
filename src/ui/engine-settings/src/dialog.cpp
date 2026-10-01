// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings dialog's sections, rows and controls, where they lie, and
// what pointer and key events do to them.

#include "oa/ui/engine_settings/dialog.hpp"

#include "geometry.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <string>

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

Rows place_rows(Page page, const Locks& locks) noexcept {
    Rows placed{};
    int32_t top = first_row_top;
    const auto settings = page_settings(page);
    for (std::size_t index = 0; index < settings.size() && index < most_rows; ++index) {
        Row& row = placed.rows[index];
        row.setting = settings[index];
        row.control = first_row_control + static_cast<int32_t>(index);
        row.lock = lock_of(locks, row.setting);
        row.top = top;
        const int32_t label_top = top + 1 + row_padding;
        int32_t label_right = content_right;
        if (row.lock != Lock::none) {
            row.lock_area = {content_right - lock_width, label_top, lock_width, label_line_height};
            label_right = row.lock_area.x - label_gap;
        }
        if (row.setting == Setting::anti_aliasing) {
            const auto levels = static_cast<int32_t>(anti_aliasing_levels.size());
            const int32_t strip_width = levels * level_width + 2;
            row.control_area = {
                content_right - strip_width, label_top, strip_width, label_line_height
            };
            label_right = row.control_area.x - label_gap;
        } else if (!is_slider(row.setting)) {
            row.control_area = {
                content_right - switch_width, label_top, switch_width, label_line_height
            };
            label_right = row.control_area.x - label_gap;
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
        ++placed.count;
    }
    placed.bottom = top;
    return placed;
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

/// Returns the row a control is, when it is one of the open section's.
///
/// @param rows the open section's rows
/// @param control the control
/// @return the row; nullptr for a control that is not a row's
const layout::Row* row_of(const layout::Rows& rows, int32_t control) noexcept {
    const int32_t index = control - first_row_control;
    if (index < 0 || static_cast<std::size_t>(index) >= rows.count)
        return nullptr;
    return &rows.rows[static_cast<std::size_t>(index)];
}

/// Returns the control under a point that a press can act on.
///
/// A locked row's control takes no press.
///
/// @param rows the open section's rows
/// @param x the point's column
/// @param y the point's row
/// @return the control; no_control when none is there
int32_t control_at(const layout::Rows& rows, int32_t x, int32_t y) noexcept {
    for (std::size_t index = 0; index < rows.count; ++index) {
        const layout::Row& row = rows.rows[index];
        if (row.lock == Lock::none && contains(row.control_area, x, y))
            return row.control;
    }
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
    for (std::size_t index = 0; index < rows.count; ++index) {
        if (rows.rows[index].lock == Lock::none)
            order.push_back(rows.rows[index].control);
    }
    order.push_back(restore_control);
    order.push_back(cancel_control);
    order.push_back(ok_control);
    for (int32_t index = 0; index < static_cast<int32_t>(layout::page_count); ++index)
        order.push_back(first_page_control + index);
    return order;
}

/// Moves the focus to the next or previous control.
///
/// @param[in,out] dialog the dialog
/// @param rows the open section's rows
/// @param forward true for the next control, false for the previous
/// @return DialogAction::redraw
DialogAction move_focus(Dialog& dialog, const layout::Rows& rows, bool forward) {
    const auto order = focus_order(rows);
    const auto found = std::find(order.begin(), order.end(), dialog.focused);
    if (found == order.end()) {
        dialog.focused = forward ? order.front() : order.back();
        return DialogAction::redraw;
    }
    const auto count = static_cast<std::ptrdiff_t>(order.size());
    const std::ptrdiff_t at = found - order.begin();
    const std::ptrdiff_t next = (at + (forward ? 1 : count - 1)) % count;
    dialog.focused = order[static_cast<std::size_t>(next)];
    return DialogAction::redraw;
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

/// Resets every setting the dialog can change to its default.
///
/// @param[in,out] dialog the dialog
/// @return what the reset asks of the host
DialogAction restore_defaults(Dialog& dialog) noexcept {
    const EngineSettings before = dialog.chosen;
    EngineSettings restored = dialog.defaults;
    if (dialog.locks.path_search != Lock::none)
        restored.path_search_nodes = before.path_search_nodes;
    if (dialog.locks.unit_limit != Lock::none)
        restored.unit_limit = before.unit_limit;
    if (dialog.locks.max_frame_rate != Lock::none)
        restored.max_frame_rate = before.max_frame_rate;
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
    if (dialog.focused >= first_row_control && dialog.focused < restore_control)
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
    const layout::Rows rows = layout::place_rows(dialog.page, dialog.locks);
    if (dialog.dragging) {
        const layout::Row* row = row_of(rows, dialog.pressed);
        if (row != nullptr)
            return drag_to(dialog, *row, x);
    }
    const int32_t hovered = control_at(rows, x, y);
    if (hovered == dialog.hovered)
        return DialogAction::none;
    dialog.hovered = hovered;
    return DialogAction::redraw;
}

DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y) {
    const layout::Rows rows = layout::place_rows(dialog.page, dialog.locks);
    const int32_t control = control_at(rows, x, y);
    dialog.hovered = control;
    dialog.pressed = control;
    dialog.dragging = false;
    if (control == no_control)
        return DialogAction::none;
    if (dialog.focused != no_control)
        dialog.focused = control;
    const layout::Row* row = row_of(rows, control);
    if (row != nullptr && layout::is_slider(row->setting)) {
        dialog.dragging = true;
        const DialogAction action = drag_to(dialog, *row, x);
        return action;
    }
    return DialogAction::redraw;
}

DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y) {
    const layout::Rows rows = layout::place_rows(dialog.page, dialog.locks);
    const int32_t pressed = dialog.pressed;
    const bool dragged = dialog.dragging;
    dialog.pressed = no_control;
    dialog.dragging = false;
    if (pressed == no_control)
        return DialogAction::none;
    const int32_t control = control_at(rows, x, y);
    dialog.hovered = control;
    if (dragged || control != pressed)
        return DialogAction::redraw;
    const layout::Row* row = row_of(rows, control);
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
    return activate(dialog, rows, control);
}

DialogAction dialog_key(Dialog& dialog, DialogKey key) {
    const layout::Rows rows = layout::place_rows(dialog.page, dialog.locks);
    switch (key) {
    case DialogKey::enter:
        return accept(dialog);
    case DialogKey::escape:
        return cancel(dialog);
    case DialogKey::down:
    case DialogKey::tab:
        return move_focus(dialog, rows, true);
    case DialogKey::up:
    case DialogKey::back_tab:
        return move_focus(dialog, rows, false);
    default:
        break;
    }
    if (dialog.focused == no_control)
        return move_focus(dialog, rows, true);
    if (key == DialogKey::space)
        return activate(dialog, rows, dialog.focused);
    const bool up = key == DialogKey::right;
    const layout::Row* row = row_of(rows, dialog.focused);
    if (row != nullptr) {
        if (row->lock != Lock::none)
            return DialogAction::none;
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
    const layout::Rows rows = layout::place_rows(dialog.page, dialog.locks);
    for (std::size_t index = 0; index < rows.count; ++index) {
        const layout::Row& row = rows.rows[index];
        text_part(row.label, layout::label_of(row.setting), DialogFont::regular);
        if (row.lock != Lock::none) {
            const layout::SourceRect text_area{
                row.lock_area.x + layout::padlock_width + layout::padlock_gap,
                row.lock_area.y,
                row.lock_area.width - layout::padlock_width - layout::padlock_gap,
                row.lock_area.height,
            };
            control_part(
                {row.lock_area.x, row.lock_area.y, layout::padlock_width, row.lock_area.height},
                no_control
            );
            text_part(text_area, layout::lock_text(row.lock), DialogFont::small);
        }
        for (std::size_t line = 0; line < row.hint_lines; ++line)
            text_part(
                row.hints[line],
                layout::hint_line(row.setting, dialog.chosen, line),
                DialogFont::small
            );
        if (row.setting == Setting::anti_aliasing) {
            for (std::size_t level = 0; level < anti_aliasing_levels.size(); ++level) {
                parts.push_back(
                    LayoutPart{
                        {row.control_area.x + 1 + static_cast<int32_t>(level) * layout::level_width,
                         row.control_area.y + 1,
                         layout::level_width,
                         row.control_area.height - 2},
                        std::string(layout::level_caption(anti_aliasing_levels[level])),
                        DialogFont::small,
                        0,
                        row.control,
                    }
                );
            }
        } else if (layout::is_slider(row.setting)) {
            control_part(row.control_area, row.lock == Lock::none ? row.control : no_control);
            text_part(
                row.value, layout::value_text(row.setting, dialog.chosen), DialogFont::regular
            );
        } else {
            const int32_t half = (row.control_area.width - 2) / 2;
            parts.push_back(
                LayoutPart{
                    {row.control_area.x + 1,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::off_text),
                    DialogFont::small,
                    0,
                    row.control,
                }
            );
            parts.push_back(
                LayoutPart{
                    {row.control_area.x + 1 + half,
                     row.control_area.y + 1,
                     half,
                     row.control_area.height - 2},
                    std::string(layout::on_text),
                    DialogFont::small,
                    0,
                    row.control,
                }
            );
        }
    }

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
