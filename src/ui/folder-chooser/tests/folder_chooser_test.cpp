// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The chooser's list and browser laid out at 1280x800 (the Steam Deck's
// screen) and 852x393 (a phone, with its safe area), the focus order, the
// keys, presses and scrolling.
#include "oa/test/check.hpp"
#include "oa/ui/folder_chooser.hpp"

#include <algorithm>
#include <string>
#include <vector>

namespace {

namespace chooser = oa::ui::folder_chooser;
namespace gf = oa::ui::game_files;
using chooser::Target;
using chooser::TargetKind;

/// The Steam Deck's screen, one canvas pixel a point.
chooser::Viewport deck_viewport() {
    chooser::Viewport viewport;
    viewport.width = 1280;
    viewport.height = 800;
    viewport.px_per_point = 1.0f;
    return viewport;
}

/// A phone's screen held sideways, at three canvas pixels a point, with its safe area.
chooser::Viewport phone_viewport() {
    chooser::Viewport viewport;
    viewport.width = 852 * 3;
    viewport.height = 393 * 3;
    viewport.px_per_point = 3.0f;
    viewport.safe = {59 * 3, 0, 59 * 3, 21 * 3};
    return viewport;
}

/// The list with two folders found (one usable), as the app fills it.
chooser::Model list_model() {
    chooser::Model model;
    model.version = "v0.7";
    model.installs = {
        {"/home/deck/.local/share/Steam/steamapps/common/Total Annihilation",
         "your Steam library",
         "Total Annihilation 3.1c with Core Contingency and Battle Tactics.",
         true},
        {"/home/deck/Games/Heroic/Total Annihilation",
         "Heroic",
         "Its archives lack guis/mainmenu.gui.",
         false},
    };
    model.offers_dialog = true;
    return model;
}

/// The browser in a folder with many folders, a few of them game folders.
///
/// @param usable the folder shown can be played
chooser::Model browse_model(bool usable) {
    chooser::Model model;
    model.version = "v0.7";
    model.view = chooser::View::browse;
    model.folder = "/home/deck/Games";
    model.place_labels = {"Home", "Downloads", "SD card"};
    model.has_parent = true;
    model.folder_usable = usable;
    model.folder_verdict =
        usable ? "Total Annihilation 3.1c." : "It holds no Total Annihilation archives.";
    for (int index = 0; index < 30; ++index)
        model.entries.push_back({"Folder " + std::to_string(index), index % 7 == 3});
    return model;
}

/// Returns a box's centre.
///
/// @param box the box
/// @return its centre
chooser::Point centre(chooser::Rect box) {
    return {box.x + box.width / 2, box.y + box.height / 2};
}

/// Finds a target's box.
///
/// @param layout the layout
/// @param target the target
/// @return the box; none when the target is not laid out
const chooser::HitBox* find(const chooser::Layout& layout, Target target) {
    for (const chooser::HitBox& hit : layout.targets)
        if (hit.target == target)
            return &hit;
    return nullptr;
}

/// Tells whether a box lies in another.
///
/// @param inner the box
/// @param outer the box it should lie in
/// @return true when it does
bool inside(chooser::Rect inner, chooser::Rect outer) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

/// Checks what every layout keeps: each control at least 44 points high, inside the safe area
/// across, and a focus order of the enabled targets in their order.
///
/// @param layout the layout
/// @param viewport its canvas
void check_controls(const chooser::Layout& layout, const chooser::Viewport& viewport) {
    const chooser::Rect safe{
        viewport.safe.left,
        viewport.safe.top,
        viewport.width - viewport.safe.left - viewport.safe.right,
        viewport.height - viewport.safe.top - viewport.safe.bottom,
    };
    const int least = static_cast<int>(gf::min_button_points * viewport.px_per_point);
    OA_CHECK(!layout.targets.empty());
    std::vector<Target> enabled;
    for (const chooser::HitBox& hit : layout.targets) {
        OA_CHECK(hit.box.height >= least);
        OA_CHECK(hit.box.x >= safe.x && hit.box.x + hit.box.width <= safe.x + safe.width);
        if (hit.clip.height <= 0)
            OA_CHECK(inside(hit.box, safe));
        if (hit.enabled)
            enabled.push_back(hit.target);
    }
    OA_CHECK(enabled == layout.focus_order);
    // Every painted item but the scrolled ones lies on the canvas.
    for (const gf::Item& item : layout.paint.items)
        if (item.clip.height <= 0)
            OA_CHECK(inside(item.box, {0, 0, viewport.width, viewport.height}));
}

/// Checks the list's targets, their order and where presses go.
void check_list() {
    for (const chooser::Viewport& viewport : {deck_viewport(), phone_viewport()}) {
        const chooser::Model model = list_model();
        const chooser::UiState state;
        const chooser::Layout layout = chooser::lay_out(model, state, viewport, {});
        check_controls(layout, viewport);
        const std::vector<Target> order{
            {TargetKind::install, 0},
            {TargetKind::browse, 0},
            {TargetKind::demo, 0},
            {TargetKind::dialog, 0},
            {TargetKind::quit, 0},
        };
        OA_CHECK(layout.focus_order == order);
        const chooser::HitBox* unusable = find(layout, {TargetKind::install, 1});
        OA_CHECK(unusable != nullptr && !unusable->enabled);
        if (unusable != nullptr && unusable->clip.height <= 0)
            OA_CHECK(
                chooser::hit_test(layout, centre(unusable->box), 100).kind == TargetKind::none
            );
        for (const Target& target : order) {
            const chooser::HitBox* hit = find(layout, target);
            OA_CHECK(hit != nullptr);
            if (hit != nullptr && (hit->clip.height <= 0 || inside(hit->box, hit->clip)))
                OA_CHECK(chooser::hit_test(layout, centre(hit->box), 0) == target);
        }
        // No dialog offered (Game Mode): no row for it.
        chooser::Model game_mode = list_model();
        game_mode.offers_dialog = false;
        const chooser::Layout without = chooser::lay_out(game_mode, state, viewport, {});
        OA_CHECK(find(without, {TargetKind::dialog, 0}) == nullptr);
        OA_CHECK(find(without, {TargetKind::demo, 0}) != nullptr);
        // Nothing found: the ways to a folder and Quit, and a notice above them.
        chooser::Model nothing;
        nothing.notice = "The Total Annihilation folder chosen earlier can no longer be used.";
        const chooser::Layout empty = chooser::lay_out(nothing, state, viewport, {});
        check_controls(empty, viewport);
        OA_CHECK(empty.focus_order.size() == 3);
        OA_CHECK(empty.focus_order.front() == (Target{TargetKind::browse, 0}));
        const bool notice_shown = std::any_of(
            empty.paint.items.begin(), empty.paint.items.end(), [](const gf::Item& item) {
                return item.role == gf::ItemRole::banner;
            }
        );
        OA_CHECK(notice_shown);
    }
    // Escape does nothing on the list.
    const chooser::Layout layout = chooser::lay_out(list_model(), {}, deck_viewport(), {});
    chooser::UiState state;
    OA_CHECK(chooser::key(layout, state, chooser::Key::escape).command == chooser::Command::none);
}

/// Checks the browser's targets, "Play this folder" and Back.
void check_browser() {
    for (const chooser::Viewport& viewport : {deck_viewport(), phone_viewport()}) {
        const chooser::UiState state;
        const chooser::Layout refused = chooser::lay_out(browse_model(false), state, viewport, {});
        check_controls(refused, viewport);
        const chooser::HitBox* choose = find(refused, {TargetKind::choose, 0});
        OA_CHECK(choose != nullptr && !choose->enabled);
        OA_CHECK(
            std::find(
                refused.focus_order.begin(),
                refused.focus_order.end(),
                Target{TargetKind::choose, 0}
            ) == refused.focus_order.end()
        );
        OA_CHECK(refused.focus_order.size() >= 5);
        OA_CHECK(refused.focus_order[0] == (Target{TargetKind::place, 0}));
        OA_CHECK(refused.focus_order[2] == (Target{TargetKind::place, 2}));
        OA_CHECK(refused.focus_order[3] == (Target{TargetKind::parent, 0}));
        OA_CHECK(refused.focus_order[4] == (Target{TargetKind::entry, 0}));
        OA_CHECK(refused.focus_order.back() == (Target{TargetKind::back, 0}));
        // Thirty folders do not fit: the rows scroll, the buttons stay.
        OA_CHECK(refused.scroll_max_points > 0);
        const chooser::HitBox* back = find(refused, {TargetKind::back, 0});
        OA_CHECK(back != nullptr && back->clip.height <= 0);
        const chooser::Layout usable = chooser::lay_out(browse_model(true), state, viewport, {});
        check_controls(usable, viewport);
        OA_CHECK(usable.focus_order.back() == (Target{TargetKind::choose, 0}));
        const chooser::HitBox* play = find(usable, {TargetKind::choose, 0});
        OA_CHECK(play != nullptr && play->enabled);
        if (play != nullptr)
            OA_CHECK(
                chooser::hit_test(usable, centre(play->box), 0) == (Target{TargetKind::choose, 0})
            );
        // Escape goes back to the list.
        chooser::UiState keys;
        OA_CHECK(
            chooser::key(usable, keys, chooser::Key::escape).command ==
            chooser::Command::back_to_list
        );
        // At the top of a drive there is no parent folder.
        chooser::Model root = browse_model(false);
        root.folder = "/";
        root.has_parent = false;
        const chooser::Layout top = chooser::lay_out(root, state, viewport, {});
        const chooser::HitBox* parent = find(top, {TargetKind::parent, 0});
        OA_CHECK(parent != nullptr && !parent->enabled);
        // An empty folder says so.
        chooser::Model empty = browse_model(false);
        empty.entries.clear();
        const chooser::Layout nothing = chooser::lay_out(empty, state, viewport, {});
        OA_CHECK(find(nothing, {TargetKind::entry, 0}) == nullptr);
        OA_CHECK(nothing.scroll_max_points == 0);
    }
}

/// Checks the keys: the first shows the focus, arrows and Tab move it and scroll it into view,
/// Enter presses, the page keys scroll.
void check_keys() {
    const chooser::Viewport viewport = phone_viewport();
    const chooser::Model model = browse_model(true);
    chooser::UiState state;
    chooser::Layout layout = chooser::lay_out(model, state, viewport, {});
    OA_CHECK(chooser::key(layout, state, chooser::Key::down).command == chooser::Command::none);
    OA_CHECK(state.focus_shown && state.focus == 0);
    layout = chooser::lay_out(model, state, viewport, {});
    const bool marked =
        std::any_of(layout.paint.items.begin(), layout.paint.items.end(), [](const gf::Item& item) {
            return item.focused;
        });
    OA_CHECK(marked);
    // Right along the places, to the parent folder, then down into the folders.
    static_cast<void>(chooser::key(layout, state, chooser::Key::right));
    OA_CHECK(state.focus == 1);
    static_cast<void>(chooser::key(layout, state, chooser::Key::right));
    static_cast<void>(chooser::key(layout, state, chooser::Key::right));
    OA_CHECK(state.focus == 3);
    static_cast<void>(chooser::key(layout, state, chooser::Key::left));
    OA_CHECK(state.focus == 2);
    static_cast<void>(chooser::key(layout, state, chooser::Key::down));
    OA_CHECK(
        layout.focus_order[static_cast<std::size_t>(state.focus)] == (Target{TargetKind::entry, 0})
    );
    // Down through the folders: each one focused is scrolled whole into view.
    for (int step = 0; step < 20; ++step) {
        layout = chooser::lay_out(model, state, viewport, {});
        static_cast<void>(chooser::key(layout, state, chooser::Key::down));
    }
    OA_CHECK(state.scroll_points > 0 && state.scroll_points <= layout.scroll_max_points);
    layout = chooser::lay_out(model, state, viewport, {});
    const Target focused = layout.focus_order[static_cast<std::size_t>(state.focus)];
    OA_CHECK(focused == (Target{TargetKind::entry, 20}));
    const chooser::HitBox* shown = find(layout, focused);
    OA_CHECK(shown != nullptr && inside(shown->box, shown->clip));
    OA_CHECK(
        chooser::key(layout, state, chooser::Key::enter).command == chooser::Command::enter_folder
    );
    OA_CHECK(chooser::key(layout, state, chooser::Key::enter).index == 20);
    // Tab and Shift+Tab go round the focus order.
    const int32_t count = static_cast<int32_t>(layout.focus_order.size());
    state.focus = count - 1;
    static_cast<void>(chooser::key(layout, state, chooser::Key::tab));
    OA_CHECK(state.focus == 0);
    static_cast<void>(chooser::key(layout, state, chooser::Key::back_tab));
    OA_CHECK(state.focus == count - 1);
    OA_CHECK(
        chooser::key(layout, state, chooser::Key::enter).command == chooser::Command::choose_folder
    );
    // Up from Back reaches the last folder, scrolled into view, and down from it the buttons.
    const auto back_place = std::find(
        layout.focus_order.begin(), layout.focus_order.end(), Target{TargetKind::back, 0}
    );
    OA_CHECK(back_place != layout.focus_order.end());
    state.focus = static_cast<int32_t>(back_place - layout.focus_order.begin());
    state.scroll_points = 0;
    layout = chooser::lay_out(model, state, viewport, {});
    static_cast<void>(chooser::key(layout, state, chooser::Key::up));
    OA_CHECK(
        layout.focus_order[static_cast<std::size_t>(state.focus)] == (Target{TargetKind::entry, 29})
    );
    layout = chooser::lay_out(model, state, viewport, {});
    const chooser::HitBox* last = find(layout, {TargetKind::entry, 29});
    OA_CHECK(state.scroll_points > 0 && last != nullptr && inside(last->box, last->clip));
    static_cast<void>(chooser::key(layout, state, chooser::Key::down));
    const TargetKind below = layout.focus_order[static_cast<std::size_t>(state.focus)].kind;
    OA_CHECK(below == TargetKind::back || below == TargetKind::choose);
    // Up from the top row goes nowhere.
    state.focus = 0;
    static_cast<void>(chooser::key(layout, state, chooser::Key::up));
    OA_CHECK(state.focus == 0);
    // The page keys scroll by the rows' height, within the range.
    state.scroll_points = 0;
    static_cast<void>(chooser::key(layout, state, chooser::Key::page_down));
    OA_CHECK(state.scroll_points > 0);
    for (int page = 0; page < 20; ++page)
        static_cast<void>(chooser::key(layout, state, chooser::Key::page_down));
    OA_CHECK(state.scroll_points == layout.scroll_max_points);
    static_cast<void>(chooser::key(layout, state, chooser::Key::page_up));
    OA_CHECK(state.scroll_points < layout.scroll_max_points);
    // Nothing to focus: keys do nothing.
    chooser::UiState none;
    OA_CHECK(chooser::key({}, none, chooser::Key::enter).command == chooser::Command::none);
    OA_CHECK(!none.focus_shown);
}

/// Checks presses: down marks the target, up on it gives its command, up elsewhere none, a
/// disabled target takes nothing and a press near a target reaches it.
void check_presses() {
    const chooser::Viewport viewport = deck_viewport();
    chooser::UiState state;
    chooser::Layout layout = chooser::lay_out(list_model(), state, viewport, {});
    const chooser::HitBox* browse = find(layout, {TargetKind::browse, 0});
    OA_CHECK(browse != nullptr);
    if (browse == nullptr)
        return;
    const chooser::Rect browse_box = browse->box;
    chooser::press_down(layout, state, centre(browse_box));
    OA_CHECK(state.pressed >= 0);
    layout = chooser::lay_out(list_model(), state, viewport, {});
    const bool held =
        std::any_of(layout.paint.items.begin(), layout.paint.items.end(), [](const gf::Item& item) {
            return item.pressed;
        });
    OA_CHECK(held);
    const chooser::Outcome opened = chooser::press_up(layout, state, centre(browse_box));
    OA_CHECK(opened.command == chooser::Command::open_browser);
    OA_CHECK(state.pressed == -1);
    OA_CHECK(
        layout.focus_order[static_cast<std::size_t>(state.focus)] == (Target{TargetKind::browse, 0})
    );
    // Slid off before coming up: nothing.
    chooser::press_down(layout, state, centre(browse_box));
    OA_CHECK(
        chooser::press_up(layout, state, {browse_box.x + browse_box.width / 2, 2}).command ==
        chooser::Command::none
    );
    // A disabled row takes nothing, not even the nearest target.
    const chooser::HitBox* unusable = find(layout, {TargetKind::install, 1});
    OA_CHECK(unusable != nullptr);
    if (unusable != nullptr) {
        chooser::press_down(layout, state, centre(unusable->box));
        OA_CHECK(state.pressed == -1);
        OA_CHECK(
            chooser::press_up(layout, state, centre(unusable->box)).command ==
            chooser::Command::none
        );
    }
    // A press just outside the Quit button reaches it.
    const chooser::HitBox* quit = find(layout, {TargetKind::quit, 0});
    OA_CHECK(quit != nullptr);
    if (quit != nullptr) {
        const chooser::Point near{
            quit->box.x + quit->box.width / 2, quit->box.y + quit->box.height + 10
        };
        chooser::press_down(layout, state, near);
        OA_CHECK(chooser::press_up(layout, state, near).command == chooser::Command::quit);
        OA_CHECK(chooser::hit_test(layout, near, 0).kind == TargetKind::none);
    }
    // The usable folder's row plays it.
    const chooser::HitBox* install = find(layout, {TargetKind::install, 0});
    OA_CHECK(install != nullptr);
    if (install != nullptr) {
        chooser::press_down(layout, state, centre(install->box));
        const chooser::Outcome played = chooser::press_up(layout, state, centre(install->box));
        OA_CHECK(played.command == chooser::Command::play_install && played.index == 0);
    }
    // A pointer press hides the focus the keys showed.
    state.focus_shown = true;
    chooser::press_down(layout, state, {1, 1});
    OA_CHECK(!state.focus_shown && state.pressed == -1);
}

/// Checks scrolling stays within the layout's range.
void check_scroll() {
    const chooser::Viewport viewport = phone_viewport();
    chooser::UiState state;
    const chooser::Layout layout = chooser::lay_out(browse_model(false), state, viewport, {});
    OA_CHECK(layout.scroll_max_points > 0);
    chooser::scroll(layout, state, 1000000);
    OA_CHECK(state.scroll_points == layout.scroll_max_points);
    chooser::scroll(layout, state, -1000000);
    OA_CHECK(state.scroll_points == 0);
    chooser::scroll(layout, state, 25);
    OA_CHECK(state.scroll_points == std::min(25, layout.scroll_max_points));
    // Scrolled to the end, the last folder shows whole in the rows.
    state.scroll_points = layout.scroll_max_points;
    const chooser::Layout end = chooser::lay_out(browse_model(false), state, viewport, {});
    const chooser::HitBox* last = find(end, {TargetKind::entry, 29});
    OA_CHECK(last != nullptr && inside(last->box, last->clip));
    // A layout that does not scroll keeps the scroll at 0.
    const chooser::Layout still = chooser::lay_out(list_model(), {}, deck_viewport(), {});
    OA_CHECK(still.scroll_max_points == 0);
    chooser::UiState held;
    chooser::scroll(still, held, 50);
    OA_CHECK(held.scroll_points == 0);
}

/// Checks that an empty layout reaches nothing.
void check_empty_layout() {
    const chooser::Layout layout{};
    OA_CHECK(chooser::hit_test(layout, {}, 0).kind == TargetKind::none);
    chooser::Viewport nothing;
    OA_CHECK(chooser::lay_out(list_model(), {}, nothing, {}).targets.empty());
}

} // namespace

/// Runs the checks.
int main() {
    check_empty_layout();
    check_list();
    check_browser();
    check_keys();
    check_presses();
    check_scroll();
    return oa::test::check_exit_status();
}
