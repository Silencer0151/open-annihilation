// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The chooser's hit test, presses, keys, focus and scrolling
// (folder_chooser.hpp). Arrows move the focus to the nearest target in their
// direction, Tab through the focus order, and a target the focus reaches is
// scrolled into view.
#include "oa/ui/folder_chooser.hpp"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <optional>

namespace oa::ui::folder_chooser {

namespace {

/// How much more the distance across a direction counts than the distance along it, when the
/// arrows pick the nearest target in their direction.
constexpr int64_t across_weight = 2;

/// Returns the part of a target's box that can be seen and pressed: the box, cut by its clip.
///
/// @param hit the target
/// @return the visible box; empty when none of it shows
Rect visible_box(const HitBox& hit) noexcept {
    if (hit.clip.width <= 0 || hit.clip.height <= 0)
        return hit.box;
    const int left = std::max(hit.box.x, hit.clip.x);
    const int top = std::max(hit.box.y, hit.clip.y);
    const int right = std::min(hit.box.x + hit.box.width, hit.clip.x + hit.clip.width);
    const int bottom = std::min(hit.box.y + hit.box.height, hit.clip.y + hit.clip.height);
    if (right <= left || bottom <= top)
        return {};
    return {left, top, right - left, bottom - top};
}

/// Tells whether a point lies in a box.
///
/// @param box the box
/// @param point the point
/// @return true when it does
bool contains(Rect box, Point point) noexcept {
    return box.width > 0 && box.height > 0 && point.x >= box.x && point.x < box.x + box.width &&
           point.y >= box.y && point.y < box.y + box.height;
}

/// Returns how far a press reaches beyond a box: pick_reach_points at the layout's density.
///
/// @param layout the layout
/// @return canvas pixels
int press_reach(const Layout& layout) noexcept {
    const float scale = layout.paint.px_per_point > 0.0f ? layout.paint.px_per_point : 1.0f;
    return static_cast<int>(std::lround(oa::ui::game_files::pick_reach_points * scale));
}

/// Finds a target's box.
///
/// @param layout the layout
/// @param target the target
/// @return its place in Layout::targets; none when it is not laid out
std::optional<std::size_t> hit_of(const Layout& layout, Target target) noexcept {
    for (std::size_t index = 0; index < layout.targets.size(); ++index)
        if (layout.targets[index].target == target)
            return index;
    return std::nullopt;
}

/// Returns what pressing a target asks of the app.
///
/// @param target the target
/// @return its command
Outcome outcome_of(Target target) noexcept {
    switch (target.kind) {
    case TargetKind::none:
        break;
    case TargetKind::install:
        return {Command::play_install, target.index};
    case TargetKind::browse:
        return {Command::open_browser, 0};
    case TargetKind::dialog:
        return {Command::use_dialog, 0};
    case TargetKind::demo:
        return {Command::find_demo, 0};
    case TargetKind::quit:
        return {Command::quit, 0};
    case TargetKind::place:
        return {Command::open_place, target.index};
    case TargetKind::parent:
        return {Command::parent_folder, 0};
    case TargetKind::entry:
        return {Command::enter_folder, target.index};
    case TargetKind::choose:
        return {Command::choose_folder, 0};
    case TargetKind::back:
        return {Command::back_to_list, 0};
    }
    return {};
}

/// Scrolls the rows so that a target's box shows whole in its scrolling region, as far as
/// the layout's range allows.
///
/// @param layout the layout
/// @param[in,out] state what the chooser keeps between frames
/// @param target the target
void scroll_into_view(const Layout& layout, UiState& state, Target target) noexcept {
    const auto index = hit_of(layout, target);
    if (!index)
        return;
    const HitBox& hit = layout.targets[*index];
    if (hit.clip.height <= 0)
        return;
    const float scale = layout.paint.px_per_point > 0.0f ? layout.paint.px_per_point : 1.0f;
    int32_t points = 0;
    if (hit.box.y < hit.clip.y)
        points =
            -static_cast<int32_t>(std::ceil(static_cast<float>(hit.clip.y - hit.box.y) / scale));
    else if (hit.box.y + hit.box.height > hit.clip.y + hit.clip.height)
        points = static_cast<int32_t>(std::ceil(
            static_cast<float>(hit.box.y + hit.box.height - (hit.clip.y + hit.clip.height)) / scale
        ));
    if (points != 0)
        scroll(layout, state, points);
}

/// Returns where a target's box would lie with the rows unscrolled and laid out whole: the
/// scrolled ones moved back by the scroll, and the buttons below the rows moved down past
/// all of them. Directions are judged there, so that rows scrolled out of view lie between
/// the fixed parts as they do in the column.
///
/// @param layout the layout
/// @param state the scroll it was laid out with
/// @param hit the target
/// @return the box in the column's own coordinates
Rect column_box(const Layout& layout, const UiState& state, const HitBox& hit) noexcept {
    const float scale = layout.paint.px_per_point > 0.0f ? layout.paint.px_per_point : 1.0f;
    const int32_t most = std::max(0, layout.scroll_max_points);
    const int scrolled = static_cast<int>(
        std::lround(static_cast<float>(std::clamp(state.scroll_points, 0, most)) * scale)
    );
    const int whole = static_cast<int>(std::lround(static_cast<float>(most) * scale));
    Rect box = hit.box;
    const Rect& rows = layout.paint.rows;
    if (hit.clip.height > 0)
        box.y += scrolled;
    else if (rows.height > 0 && box.y >= rows.y + rows.height)
        box.y += whole;
    return box;
}

/// Picks the nearest target of the focus order in a direction from the focused one.
///
/// @param layout the layout
/// @param state the scroll it was laid out with
/// @param from the focused entry of the focus order
/// @param dx the direction across: -1 left, 1 right, 0 neither
/// @param dy the direction down: -1 up, 1 down, 0 neither
/// @return the entry of the focus order; none when nothing lies that way
std::optional<int32_t> nearest_in_direction(
    const Layout& layout, const UiState& state, int32_t from, int dx, int dy
) noexcept {
    const auto from_hit = hit_of(layout, layout.focus_order[static_cast<std::size_t>(from)]);
    if (!from_hit)
        return std::nullopt;
    const Rect origin = column_box(layout, state, layout.targets[*from_hit]);
    const int64_t origin_x = int64_t{origin.x} * 2 + origin.width;
    const int64_t origin_y = int64_t{origin.y} * 2 + origin.height;
    std::optional<int32_t> best;
    int64_t best_score = 0;
    for (std::size_t index = 0; index < layout.focus_order.size(); ++index) {
        if (static_cast<int32_t>(index) == from)
            continue;
        const auto hit = hit_of(layout, layout.focus_order[index]);
        if (!hit)
            continue;
        const Rect box = column_box(layout, state, layout.targets[*hit]);
        // Centres in doubled pixels, so that halves stay whole.
        const int64_t along_x = int64_t{box.x} * 2 + box.width - origin_x;
        const int64_t along_y = int64_t{box.y} * 2 + box.height - origin_y;
        const int64_t along = dx != 0 ? along_x * dx : along_y * dy;
        const int64_t across = dx != 0 ? std::llabs(along_y) : std::llabs(along_x);
        if (along <= 0)
            continue;
        // Across a row, only targets that share some of the focused one's height count.
        if (dx != 0 && (box.y >= origin.y + origin.height || box.y + box.height <= origin.y))
            continue;
        const int64_t score = along + across_weight * across;
        if (!best || score < best_score) {
            best = static_cast<int32_t>(index);
            best_score = score;
        }
    }
    return best;
}

} // namespace

Target hit_test(const Layout& layout, Point point, int reach_px) noexcept {
    // A target that holds the point takes it; a disabled one holds it to no target.
    for (std::size_t index = layout.targets.size(); index-- > 0;) {
        const HitBox& hit = layout.targets[index];
        if (contains(visible_box(hit), point))
            return hit.enabled ? hit.target : Target{};
    }
    Target nearest{};
    double nearest_distance = static_cast<double>(std::max(0, reach_px));
    bool found = false;
    for (std::size_t index = layout.targets.size(); index-- > 0;) {
        const HitBox& hit = layout.targets[index];
        if (!hit.enabled)
            continue;
        const Rect box = visible_box(hit);
        if (box.width <= 0 || box.height <= 0)
            continue;
        const int dx = point.x < box.x                ? box.x - point.x
                       : point.x >= box.x + box.width ? point.x - (box.x + box.width - 1)
                                                      : 0;
        const int dy = point.y < box.y                 ? box.y - point.y
                       : point.y >= box.y + box.height ? point.y - (box.y + box.height - 1)
                                                       : 0;
        const double distance =
            std::sqrt(static_cast<double>(dx) * dx + static_cast<double>(dy) * dy);
        if (distance <= nearest_distance && (!found || distance < nearest_distance)) {
            nearest = hit.target;
            nearest_distance = distance;
            found = true;
        }
    }
    return found ? nearest : Target{};
}

void press_down(const Layout& layout, UiState& state, Point point) noexcept {
    state.pressed = -1;
    state.focus_shown = false;
    const Target target = hit_test(layout, point, press_reach(layout));
    if (target.kind == TargetKind::none)
        return;
    if (const auto index = hit_of(layout, target))
        state.pressed = static_cast<int32_t>(*index);
}

Outcome press_up(const Layout& layout, UiState& state, Point point) noexcept {
    const int32_t pressed = state.pressed;
    state.pressed = -1;
    if (pressed < 0 || static_cast<std::size_t>(pressed) >= layout.targets.size())
        return {};
    const HitBox& held = layout.targets[static_cast<std::size_t>(pressed)];
    if (!held.enabled || hit_test(layout, point, press_reach(layout)) != held.target)
        return {};
    // The focus follows the press, for keys or a pad that come next.
    for (std::size_t index = 0; index < layout.focus_order.size(); ++index)
        if (layout.focus_order[index] == held.target)
            state.focus = static_cast<int32_t>(index);
    return outcome_of(held.target);
}

Outcome key(const Layout& layout, UiState& state, Key key) noexcept {
    const int32_t count = static_cast<int32_t>(layout.focus_order.size());
    if (key == Key::escape) {
        // Back from the browser to the list; the list has nowhere to go back to.
        if (hit_of(layout, Target{TargetKind::back, 0}))
            return {Command::back_to_list, 0};
        return {};
    }
    if (key == Key::page_up || key == Key::page_down) {
        const float scale = layout.paint.px_per_point > 0.0f ? layout.paint.px_per_point : 1.0f;
        const int32_t page = std::max<int32_t>(
            1,
            static_cast<int32_t>(std::lround(static_cast<float>(layout.paint.rows.height) / scale))
        );
        scroll(layout, state, key == Key::page_down ? page : -page);
        return {};
    }
    if (count == 0)
        return {};
    // The first key shows the focus where it is, or on the first target.
    if (!state.focus_shown || state.focus < 0 || state.focus >= count) {
        if (state.focus < 0 || state.focus >= count)
            state.focus = 0;
        state.focus_shown = true;
        scroll_into_view(layout, state, layout.focus_order[static_cast<std::size_t>(state.focus)]);
        return {};
    }
    std::optional<int32_t> next;
    switch (key) {
    case Key::tab:
        next = (state.focus + 1) % count;
        break;
    case Key::back_tab:
        next = (state.focus + count - 1) % count;
        break;
    case Key::up:
        next = nearest_in_direction(layout, state, state.focus, 0, -1);
        break;
    case Key::down:
        next = nearest_in_direction(layout, state, state.focus, 0, 1);
        break;
    case Key::left:
        next = nearest_in_direction(layout, state, state.focus, -1, 0);
        break;
    case Key::right:
        next = nearest_in_direction(layout, state, state.focus, 1, 0);
        break;
    case Key::enter:
        return outcome_of(layout.focus_order[static_cast<std::size_t>(state.focus)]);
    case Key::escape:
    case Key::page_up:
    case Key::page_down:
        break;
    }
    if (next) {
        state.focus = *next;
        scroll_into_view(layout, state, layout.focus_order[static_cast<std::size_t>(state.focus)]);
    }
    return {};
}

void scroll(const Layout& layout, UiState& state, int32_t points) noexcept {
    const int64_t wanted = int64_t{state.scroll_points} + points;
    state.scroll_points =
        static_cast<int32_t>(std::clamp<int64_t>(wanted, 0, std::max(0, layout.scroll_max_points)));
}

} // namespace oa::ui::folder_chooser
