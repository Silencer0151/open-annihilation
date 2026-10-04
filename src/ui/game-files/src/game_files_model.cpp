// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Game files screen's presses and keys (game_files.hpp): what a press or
// a key does to the sheets, the switches, the focus and the scroll, and what
// it asks the app to do.
#include "game_files_internal.hpp"

#include <algorithm>
#include <cmath>

namespace oa::ui::game_files {

namespace {

/// How far an arrow key scrolls, in points.
constexpr float arrow_scroll_points = 40.0f;
/// The share of the rows' height a page key scrolls.
constexpr float page_scroll_share = 0.9f;

/// Returns the item of a control that a press can reach (after the last backdrop).
///
/// @param layout the layout last drawn
/// @param control the control
/// @return the item; null when the layout has no such control
const Item* find_item(const Layout& layout, Control control) noexcept {
    if (control.kind == ControlKind::none)
        return nullptr;
    std::size_t first = 0;
    for (std::size_t index = 0; index < layout.items.size(); ++index)
        if (layout.items[index].role == ItemRole::backdrop)
            first = index + 1;
    for (std::size_t index = first; index < layout.items.size(); ++index)
        if (layout.items[index].control == control)
            return &layout.items[index];
    return nullptr;
}

/// Returns the look-only outcome.
///
/// @return Command::redraw
Outcome redraw() noexcept {
    return {Command::redraw, 0};
}

/// Opens a sheet over the step, its list at the top.
///
/// @param[in,out] model the model
/// @param sheet the sheet
/// @param part the row it is about
void open_sheet(Model& model, Sheet sheet, uint16_t part = 0) noexcept {
    model.sheet = sheet;
    model.sheet_part = part;
    model.scroll_points = 0;
}

/// Closes the sheet; the step's rows start again at the top.
///
/// @param[in,out] model the model
void close_sheet(Model& model) noexcept {
    model.sheet = Sheet::none;
    model.scroll_points = 0;
}

/// Acts on a sheet's button.
///
/// @param[in,out] model the model
/// @param control the button
/// @return what the app must do
Outcome act_on_sheet(Model& model, Control control) {
    if (control.kind != ControlKind::sheet_option)
        return {};
    const detail::SheetContent sheet = detail::sheet_content(model);
    if (control.index >= sheet.actions.size())
        return {};
    const detail::Action& action = sheet.actions[control.index];
    if (!action.enabled)
        return {};
    Outcome outcome{action.command, 0};
    if (action.command == Command::manage_remove || action.command == Command::remove_demo_data)
        outcome.index = model.sheet_part;
    close_sheet(model);
    if (outcome.command == Command::none)
        return redraw();
    return outcome;
}

/// Acts on a control as a finished press does.
///
/// @param[in,out] model the model
/// @param control the control
/// @return what the app must do
Outcome act(Model& model, Control control) {
    if (model.sheet != Sheet::none)
        return act_on_sheet(model, control);
    switch (control.kind) {
    case ControlKind::none:
    case ControlKind::sheet_option:
        return {};
    case ControlKind::language:
        if (model.management)
            return {};
        return {Command::open_language, 0};
    case ControlKind::choose_folder:
        return {Command::pick_game_folder, 0};
    case ControlKind::i_have_copied:
        return {Command::check_copied, 0};
    case ControlKind::choose_installer:
        return {Command::pick_installer, 0};
    case ControlKind::banner_action:
    case ControlKind::banner_discard: {
        const detail::BannerContent banner = detail::banner_content(model);
        for (const detail::Action& action : banner.actions)
            if (action.control == control)
                return {action.command, 0};
        return {};
    }
    case ControlKind::cancel:
        return {Command::cancel_scan, 0};
    case ControlKind::nested_choice:
        if (control.index >= model.nested.size())
            return {};
        return {Command::use_nested, control.index};
    case ControlKind::check_it:
        return {Command::check_in_place, 0};
    case ControlKind::part_switch:
        if (control.index >= model.parts.size() || !model.parts[control.index].has_switch)
            return {};
        model.parts[control.index].on = !model.parts[control.index].on;
        return redraw();
    case ControlKind::part_why:
        if (control.index >= model.parts.size())
            return {};
        open_sheet(model, Sheet::mod_errors, control.index);
        return redraw();
    case ControlKind::show_left_out:
        open_sheet(model, Sheet::left_out_list);
        return redraw();
    case ControlKind::copy:
        if (model.space_short)
            return {};
        if (model.replace) {
            open_sheet(model, Sheet::replace_confirm);
            return redraw();
        }
        return {Command::start_copy, 0};
    case ControlKind::check_space:
        return {Command::recheck_space, 0};
    case ControlKind::stop:
        open_sheet(model, Sheet::stop);
        return redraw();
    case ControlKind::play:
        return {Command::play, 0};
    case ControlKind::remove_old:
        open_sheet(model, Sheet::remove_old_confirm);
        return redraw();
    case ControlKind::problem_action: {
        const detail::ProblemContent problem = detail::problem_content(model);
        if (control.index >= problem.actions.size())
            return {};
        return {problem.actions[control.index].command, control.index};
    }
    case ControlKind::back:
        return {Command::back, 0};
    case ControlKind::manage_check:
        return {Command::manage_check, 0};
    case ControlKind::manage_add:
        if (model.pending_replacement)
            return {};
        open_sheet(model, Sheet::add_files);
        return redraw();
    case ControlKind::manage_remove:
        if (control.index >= model.parts.size() || model.pending_replacement)
            return {};
        open_sheet(model, Sheet::remove_part_confirm, control.index);
        return redraw();
    case ControlKind::manage_replace:
        if (model.pending_replacement)
            return {};
        return {Command::manage_replace, 0};
    case ControlKind::manage_remove_all:
        if (model.pending_replacement)
            return {};
        open_sheet(model, Sheet::remove_all_confirm);
        return redraw();
    case ControlKind::manage_cancel_pending:
        return {Command::manage_cancel_pending, 0};
    case ControlKind::manage_done:
        if (detail::change_waits(model)) {
            open_sheet(model, Sheet::scheduled_note);
            return redraw();
        }
        return {Command::done, 0};
    }
    return {};
}

/// Scrolls the rows so that a control's item shows whole (or its top, when taller).
///
/// @param[in,out] model the model
/// @param layout the layout last drawn
/// @param control the control
void scroll_into_view(Model& model, const Layout& layout, Control control) {
    const Item* item = find_item(layout, control);
    if (item == nullptr || layout.rows.height <= 0 || item->clip.height <= 0)
        return;
    const Rect rows = layout.rows;
    int delta = 0;
    if (item->box.y < rows.y || item->box.height > rows.height)
        delta = item->box.y - rows.y;
    else if (item->box.y + item->box.height > rows.y + rows.height)
        delta = item->box.y + item->box.height - (rows.y + rows.height);
    if (delta == 0)
        return;
    const float scale = layout.px_per_point > 0.0f ? layout.px_per_point : 1.0f;
    const float points = static_cast<float>(delta) / scale;
    const auto step = static_cast<int32_t>(delta > 0 ? std::ceil(points) : std::floor(points));
    model.scroll_points = std::clamp(
        model.scroll_points + step, int32_t{0}, std::max(int32_t{0}, layout.scroll_max_points)
    );
}

/// Returns the main button a Return presses when no control has the focus.
///
/// @param layout the layout last drawn
/// @return its control; none when there is none
Control main_control(const Layout& layout) noexcept {
    std::size_t first = 0;
    for (std::size_t index = 0; index < layout.items.size(); ++index)
        if (layout.items[index].role == ItemRole::backdrop)
            first = index + 1;
    for (std::size_t index = first; index < layout.items.size(); ++index) {
        const Item& item = layout.items[index];
        if (item.role == ItemRole::button_main && item.enabled &&
            item.control.kind != ControlKind::none)
            return item.control;
    }
    return {};
}

/// What Esc does on a step with no sheet.
///
/// @param[in,out] model the model
/// @return what the app must do
Outcome escape_step(Model& model) {
    switch (model.step) {
    case Step::first_run:
        return model.management ? Outcome{Command::back, 0} : Outcome{};
    case Step::looking:
        return {Command::cancel_scan, 0};
    case Step::nested_offer:
    case Step::already_there:
    case Step::ready_to_copy:
    case Step::problem:
        return {Command::back, 0};
    case Step::copying:
        open_sheet(model, Sheet::stop);
        return redraw();
    case Step::checking:
        return {};
    case Step::ready_to_play:
        return model.management ? Outcome{Command::back, 0} : Outcome{};
    case Step::manage:
        return act(model, {ControlKind::manage_done});
    }
    return {};
}

} // namespace

Outcome press_down(Model& model, Interaction& interaction, const Layout& layout, Control control) {
    static_cast<void>(model);
    const Item* item = find_item(layout, control);
    if (item == nullptr || !item->enabled) {
        const bool was_pressed = interaction.pressed.kind != ControlKind::none;
        interaction.pressed = {};
        return was_pressed ? redraw() : Outcome{};
    }
    interaction.pressed = control;
    return redraw();
}

Outcome press_up(Model& model, Interaction& interaction, const Layout& layout, Control control) {
    const Control pressed = interaction.pressed;
    interaction.pressed = {};
    const Outcome look = pressed.kind != ControlKind::none ? redraw() : Outcome{};
    if (control.kind == ControlKind::none)
        return look;
    if (pressed.kind != ControlKind::none && !(pressed == control))
        return look;
    const Item* item = find_item(layout, control);
    if (item == nullptr || !item->enabled)
        return look;
    interaction.focused = control;
    const Outcome outcome = act(model, control);
    if (outcome.command == Command::none)
        return look;
    return outcome;
}

Outcome key(Model& model, Interaction& interaction, const Layout& layout, Key key) {
    switch (key) {
    case Key::tab:
    case Key::back_tab: {
        const std::vector<Control>& order = layout.focus_order;
        if (order.empty())
            return {};
        const auto found = std::find(order.begin(), order.end(), interaction.focused);
        std::size_t next = 0;
        if (found == order.end() || !interaction.focus_shown) {
            next = key == Key::tab ? 0 : order.size() - 1;
            if (found != order.end() && !interaction.focus_shown)
                next = static_cast<std::size_t>(found - order.begin());
        } else {
            const auto at = static_cast<std::size_t>(found - order.begin());
            next =
                key == Key::tab ? (at + 1) % order.size() : (at + order.size() - 1) % order.size();
        }
        interaction.focused = order[next];
        interaction.focus_shown = true;
        scroll_into_view(model, layout, interaction.focused);
        return redraw();
    }
    case Key::enter:
    case Key::space: {
        Control target{};
        const bool focus_live =
            interaction.focus_shown &&
            std::find(layout.focus_order.begin(), layout.focus_order.end(), interaction.focused) !=
                layout.focus_order.end();
        if (focus_live)
            target = interaction.focused;
        else if (key == Key::enter)
            target = main_control(layout);
        if (target.kind == ControlKind::none)
            return {};
        const Item* item = find_item(layout, target);
        if (item == nullptr || !item->enabled)
            return {};
        interaction.pressed = {};
        const Outcome outcome = act(model, target);
        return outcome.command == Command::none ? Outcome{} : outcome;
    }
    case Key::escape:
        interaction.pressed = {};
        if (model.sheet != Sheet::none) {
            close_sheet(model);
            return redraw();
        }
        return escape_step(model);
    case Key::up:
        return scroll(model, layout, -arrow_scroll_points);
    case Key::down:
        return scroll(model, layout, arrow_scroll_points);
    case Key::page_up:
    case Key::page_down: {
        const float scale = layout.px_per_point > 0.0f ? layout.px_per_point : 1.0f;
        const float page = std::max(
            arrow_scroll_points, static_cast<float>(layout.rows.height) / scale * page_scroll_share
        );
        return scroll(model, layout, key == Key::page_up ? -page : page);
    }
    }
    return {};
}

Outcome scroll(Model& model, const Layout& layout, float points) {
    const int32_t most = std::max(int32_t{0}, layout.scroll_max_points);
    const auto wanted = static_cast<int64_t>(model.scroll_points) +
                        static_cast<int64_t>(std::lround(static_cast<double>(points)));
    const auto next = static_cast<int32_t>(std::clamp<int64_t>(wanted, 0, most));
    if (next == model.scroll_points)
        return {};
    model.scroll_points = next;
    return redraw();
}

} // namespace oa::ui::game_files
