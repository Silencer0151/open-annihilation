// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The device class, the latches, the orders' names, the hit tests, the help
// lines, the labels and the status hint (touch_hud.hpp). Every text is
// English and untranslated; the drawing translates it.
#include "oa/ui/touch_hud.hpp"

#include "touch_hud_rects.hpp"

#include <algorithm>

namespace oa::ui::touch_hud {

namespace {

/// The order panel names, by Order.
constexpr std::array<std::string_view, order_count> order_names{
    "MOVE",
    "ATTACK",
    "PATROL",
    "DEFEND",
    "STOP",
    "BLAST",
    "RECLAIM",
    "REPAIR",
    "CAPTURE",
    "LOAD",
    "UNLOAD",
};

/// The drawn labels, by Order.
constexpr std::array<std::string_view, order_count> order_labels{
    "MOVE",
    "ATTACK",
    "PATROL",
    "GUARD",
    "STOP",
    "D-GUN",
    "RECLAIM",
    "REPAIR",
    "CAPTURE",
    "LOAD",
    "UNLOAD",
};

/// The number of radial items (blast and load share a slot).
constexpr std::size_t radial_item_count = 13;

/// The radial items' labels, by RadialItem.
constexpr std::array<std::string_view, radial_item_count> radial_labels{
    "MOVE",
    "PATROL",
    "ATTACK",
    "D-GUN",
    "LOAD",
    "CAPTURE",
    "STOP",
    "INFO",
    "TYPE",
    "RECLAIM",
    "REPAIR",
    "UNLOAD",
    "GUARD",
};

/// The radial items' help lines, by RadialItem.
constexpr std::array<std::string_view, radial_item_count> radial_help{
    "MOVE: the selection moves to the held point.",
    "PATROL: the selection patrols to the held point.",
    "ATTACK: the selection attacks the unit or ground at the held point.",
    "D-GUN: the commander fires the D-gun at the held point.",
    "LOAD: the transport loads the unit at the held point.",
    "CAPTURE: the selection captures the unit at the held point.",
    "STOP: the selection stops at once.",
    "INFO: shows the information of the unit at the held point.",
    "TYPE: selects every unit of the held unit's type.",
    "RECLAIM: the selection reclaims the wreck or unit at the held point.",
    "REPAIR: the selection repairs or helps build the unit at the held point.",
    "UNLOAD: the transport unloads at the held point.",
    "GUARD: the selection guards the unit at the held point.",
};

/// A word a 3.1c order panel gadget's name holds, and the help line its long press shows.
struct GadgetHelp {
    std::string_view word; ///< upper case
    std::string_view help; ///< untranslated
};

/// The order panel gadgets' help lines, tried in order: a word that holds another comes
/// first (MOVEORD before MOVE, UNLOAD before LOAD).
constexpr std::array<GadgetHelp, 19> gadget_helps{{
    {"FIREORD", "Fire orders: tap to step through Hold fire, Return fire and Fire at will."},
    {"MOVEORD", "Move orders: tap to step through Hold position, Maneuver and Roam."},
    {"ONOFF", "On/Off: tap to switch the selected units on or off."},
    {"CLOAK", "Cloak: tap to turn cloaking on or off; a cloaked unit uses energy."},
    {"ORDERS", "ORDERS: shows the orders page."},
    {"BUILD", "BUILD: shows the build pages."},
    {"PREV", "PREV: shows the build page before this one."},
    {"NEXT", "NEXT: shows the build page after this one."},
    {"MOVE", "MOVE: tap to arm it, then tap where the selection goes."},
    {"ATTACK", "ATTACK: tap to arm it, then tap the unit or ground to fire at."},
    {"PATROL", "PATROL: tap to arm it, then tap the points to patrol between."},
    {"DEFEND", "GUARD: tap to arm it, then tap the unit to guard."},
    {"STOP", "STOP: the selection stops at once."},
    {"BLAST", "D-GUN: tap to arm it, then tap where the commander fires it."},
    {"RECLAIM", "RECLAIM: tap to arm it, then tap the wreck or unit to reclaim."},
    {"REPAIR", "REPAIR: tap to arm it, then tap the unit to repair or help build."},
    {"CAPTURE", "CAPTURE: tap to arm it, then tap the unit to capture."},
    {"UNLOAD", "UNLOAD: tap to arm it, then tap where the transport unloads."},
    {"LOAD", "LOAD: tap to arm it, then tap the unit to load."},
}};

/// Returns whether a name holds a word, ignoring the name's case.
///
/// @param name the gadget's name
/// @param word an upper case word
/// @return whether the word appears in the name
bool holds_word(std::string_view name, std::string_view word) noexcept {
    if (word.empty() || name.size() < word.size())
        return false;
    for (std::size_t start = 0; start + word.size() <= name.size(); ++start) {
        bool same = true;
        for (std::size_t at = 0; at < word.size() && same; ++at) {
            char letter = name[start + at];
            if (letter >= 'a' && letter <= 'z')
                letter = static_cast<char>(letter - 'a' + 'A');
            same = letter == word[at];
        }
        if (same)
            return true;
    }
    return false;
}

/// The SELECT ▾ items' labels, by SelectItem.
constexpr std::array<std::string_view, select_item_count> select_labels{
    "ALL",
    "BUILDERS",
    "FACTORIES",
    "AIRCRAFT",
    "ON SCREEN",
    "COMMANDER",
    "SAME TYPE",
    "CENTRE",
    "FOLLOW",
    "NEXT UNIT",
    "NEXT REPORT",
};

/// The speed popover's labels, by SpeedItem.
constexpr std::array<std::string_view, 2> speed_labels{"SLOWER", "FASTER"};

/// The phone menu's labels, by PhoneMenuItem.
constexpr std::array<std::string_view, 4> phone_menu_labels{
    "GAME MENU",
    "SLOWER",
    "FASTER",
    "CHAT",
};

/// The group chips' labels, by group number 1..9 (0 unused).
constexpr std::array<std::string_view, 10> group_labels{
    "",
    "1",
    "2",
    "3",
    "4",
    "5",
    "6",
    "7",
    "8",
    "9",
};

/// Returns the word a tap's action is shown with in the status hint.
///
/// @param action what the tap does
/// @return the word, empty for none
std::string_view tap_action_word(TapAction action) noexcept {
    switch (action) {
    case TapAction::none:
        return {};
    case TapAction::select:
        return "SELECT";
    case TapAction::move:
        return "MOVE";
    case TapAction::attack:
        return "ATTACK";
    case TapAction::guard:
        return "GUARD";
    case TapAction::patrol:
        return "PATROL";
    case TapAction::repair:
        return "REPAIR";
    case TapAction::assist:
        return "ASSIST";
    case TapAction::reclaim:
        return "RECLAIM";
    case TapAction::capture:
        return "CAPTURE";
    case TapAction::load:
        return "LOAD";
    case TapAction::unload:
        return "UNLOAD";
    case TapAction::blast:
        return "D-GUN";
    case TapAction::build:
        return "BUILD";
    case TapAction::place:
        return "PLACE";
    }
    return {};
}

/// Returns whether a control belongs to the radial menu.
///
/// @param control the control
/// @return whether it is a wedge or the hub
bool radial_control(Control control) noexcept {
    return control == Control::radial_item || control == Control::radial_hub;
}

} // namespace

DeviceClass classify_device(int width_points, int height_points) noexcept {
    return std::min(width_points, height_points) < phone_short_side_points ? DeviceClass::phone
                                                                           : DeviceClass::tablet;
}

Latch latch_for(ActionClass action) noexcept {
    switch (action) {
    case ActionClass::selection:
        return Latch::add;
    case ActionClass::order:
        return Latch::queue;
    case ActionClass::build_button:
        return Latch::times_five;
    }
    return Latch::queue;
}

void Latches::press(Latch latch, uint64_t now_ms) noexcept {
    auto& state = states_[static_cast<std::size_t>(latch)];
    state.held = true;
    state.used_while_held = false;
    state.pressed_ms = now_ms;
}

void Latches::release(Latch latch, uint64_t now_ms, uint32_t hold_ms) noexcept {
    auto& state = states_[static_cast<std::size_t>(latch)];
    if (!state.held)
        return;
    const uint64_t held_ms = now_ms >= state.pressed_ms ? now_ms - state.pressed_ms : 0;
    if (held_ms < hold_ms && !state.used_while_held)
        state.latched = !state.latched;
    state.held = false;
    state.used_while_held = false;
}

void Latches::cancel(Latch latch) noexcept {
    auto& state = states_[static_cast<std::size_t>(latch)];
    state.held = false;
    state.used_while_held = false;
}

void Latches::used(ActionClass action, LatchMode mode) noexcept {
    auto& state = states_[static_cast<std::size_t>(latch_for(action))];
    if (state.held)
        state.used_while_held = true;
    if (mode == LatchMode::one_action)
        state.latched = false;
}

void Latches::clear() noexcept {
    states_ = {};
}

bool Latches::active(Latch latch) const noexcept {
    const auto& state = states_[static_cast<std::size_t>(latch)];
    return state.latched || state.held;
}

bool Latches::latched(Latch latch) const noexcept {
    return states_[static_cast<std::size_t>(latch)].latched;
}

bool Latches::held(Latch latch) const noexcept {
    return states_[static_cast<std::size_t>(latch)].held;
}

std::string_view order_name(Order order) noexcept {
    const auto index = static_cast<std::size_t>(order);
    return index < order_names.size() ? order_names[index] : std::string_view{};
}

std::string_view order_label(Order order) noexcept {
    const auto index = static_cast<std::size_t>(order);
    return index < order_labels.size() ? order_labels[index] : std::string_view{};
}

ActionClass action_class(TapAction action) noexcept {
    return action == TapAction::select ? ActionClass::selection : ActionClass::order;
}

std::optional<ControlRect> hit(const Frame& frame, Point point, int radius_px) noexcept {
    const std::size_t count = std::min<std::size_t>(frame.control_count, max_controls);
    // Exact: the topmost control under the point. Neighbouring wedges' rectangles may share
    // corners, so among wedges the nearest label wins.
    for (std::size_t index = count; index > 0; --index) {
        const ControlRect& control = frame.controls[index - 1];
        if (!rects::contains(control.rect, point))
            continue;
        if (control.control != Control::radial_item)
            return control;
        const ControlRect* best = &control;
        int64_t best_distance = INT64_MAX;
        for (std::size_t other = 0; other < count; ++other) {
            const ControlRect& wedge = frame.controls[other];
            if (wedge.control != Control::radial_item || !rects::contains(wedge.rect, point))
                continue;
            const Point middle = rects::centre(wedge.rect);
            const int64_t dx = middle.x - point.x;
            const int64_t dy = middle.y - point.y;
            if (dx * dx + dy * dy < best_distance) {
                best_distance = dx * dx + dy * dy;
                best = &wedge;
            }
        }
        return *best;
    }
    // Near: the control whose rectangle is nearest within the radius; ties go to the topmost.
    if (radius_px > 0) {
        const int64_t limit = int64_t{radius_px} * radius_px;
        const ControlRect* best = nullptr;
        int64_t best_distance = limit + 1;
        for (std::size_t index = 0; index < count; ++index) {
            const ControlRect& control = frame.controls[index];
            if (rects::empty(control.rect))
                continue;
            const int64_t distance = rects::distance_squared(control.rect, point);
            if (distance <= limit && distance <= best_distance) {
                best_distance = distance;
                best = &control;
            }
        }
        if (best != nullptr)
            return *best;
    }
    // Outside an open sheet or radial: the tap closes it.
    bool modal = !rects::empty(frame.sheet);
    for (std::size_t index = 0; index < count && !modal; ++index)
        modal = radial_control(frame.controls[index].control);
    if (modal && !rects::contains(frame.sheet, point)) {
        ControlRect outside{};
        outside.control = Control::sheet_outside;
        return outside;
    }
    return std::nullopt;
}

int nearest_rect(const Rect* rects_in, std::size_t count, Point point, int radius_px) noexcept {
    if (rects_in == nullptr || radius_px < 0)
        return -1;
    const int64_t limit = int64_t{radius_px} * radius_px;
    int best = -1;
    int64_t best_distance = limit + 1;
    for (std::size_t index = 0; index < count; ++index) {
        if (rects::empty(rects_in[index]))
            continue;
        const int64_t distance = rects::distance_squared(rects_in[index], point);
        if (distance <= limit && distance <= best_distance) {
            best_distance = distance;
            best = static_cast<int>(index);
        }
    }
    return best;
}

bool covers(const Frame& frame, Point point) noexcept {
    const std::size_t count = std::min<std::size_t>(frame.control_count, max_controls);
    for (std::size_t index = 0; index < count; ++index) {
        if (rects::contains(frame.controls[index].rect, point))
            return true;
    }
    for (const Rect& region :
         {frame.sheet,
          frame.banner,
          frame.placement_bar,
          frame.status,
          frame.minimap,
          frame.resources}) {
        if (rects::contains(region, point))
            return true;
    }
    return false;
}

std::string_view control_help(Control control, uint8_t index) noexcept {
    switch (control) {
    case Control::none:
        return {};
    case Control::queue:
        return "QUEUE: Shift for orders, building and placement. Tap to latch, hold for one.";
    case Control::add:
        return "ADD: Shift for selecting, so taps, boxes and groups add to the selection. Tap to "
               "latch, hold for one.";
    case Control::times_five:
        return "x5: Shift for the build buttons, five at a time. Tap to latch, hold for one.";
    case Control::clear:
        return "CLEAR: takes back the armed order or the building being placed, else clears the "
               "selection.";
    case Control::select_menu:
        return "SELECT: selects all units, builders, factories, aircraft, the units on screen, "
               "the commander or the same type.";
    case Control::group_store:
        return "STORE: stores the selection as the lowest free group.";
    case Control::group_chip:
        return "Group: tap to select it, tap again to centre on it, hold to store the selection "
               "in it.";
    case Control::pause:
        return "PAUSE: pauses or resumes the game.";
    case Control::speed:
        return "SPEED: makes the game slower or faster.";
    case Control::chat:
        return "CHAT: writes a message to the other players.";
    case Control::centre:
        return "CENTRE: centres the view on the selected unit.";
    case Control::follow:
        return "FOLLOW: the view follows the selected unit; tap again for the next one.";
    case Control::next_unit:
        return "NEXT: centres the view on the next unit not yet visited.";
    case Control::info:
        return "INFO: shows the selected unit's information.";
    case Control::menu:
        return "MENU: opens the game menu.";
    case Control::build_drawer:
        return "BUILD: opens the builder's build pages, or the orders page for other units.";
    case Control::zoom_out:
        return "Zoom out: shows more of the battlefield.";
    case Control::zoom_in:
        return "Zoom in: shows the battlefield closer.";
    case Control::order_slot:
        return "Order: tap to arm it, then tap where it goes; tap again to take it back.";
    case Control::more:
        return "MORE: the other orders, fire and move orders, on and off, cloak and "
               "self-destruct.";
    case Control::drawer_build_tab:
        return "BUILD: shows the builder's build pages.";
    case Control::drawer_orders_tab:
        return "ORDERS: shows the orders page.";
    case Control::drawer_close:
        return "Closes the build drawer.";
    case Control::drawer_prev:
        return "Shows the previous page.";
    case Control::drawer_next:
        return "Shows the next page.";
    case Control::place_cancel:
        return "CANCEL: stops placing the building.";
    case Control::banner_cancel:
        return "Takes back the armed order.";
    case Control::menu_item:
        return "Chooses this item.";
    case Control::more_item:
        if (index == static_cast<uint8_t>(MoreItem::self_destruct))
            return "SELF-DESTRUCT: hold to start or stop the selection's self-destruct countdown.";
        return "INFO: shows the selected unit's information.";
    case Control::radial_item:
        if (index < radial_help.size())
            return radial_help[index];
        return "Gives this order at the held point.";
    case Control::radial_hub:
        return "QUEUE: the next order picked is queued after the selection's orders.";
    case Control::sheet_outside:
        return "Closes the open menu.";
    }
    return {};
}

std::string_view gadget_help(std::string_view name) noexcept {
    for (const auto& entry : gadget_helps)
        if (holds_word(name, entry.word))
            return entry.help;
    return {};
}

std::string_view control_label(Control control, uint8_t index, const HudState& state) noexcept {
    switch (control) {
    case Control::none:
    case Control::sheet_outside:
        return {};
    case Control::queue:
        return "QUEUE";
    case Control::add:
        return "ADD";
    case Control::times_five:
        return "x5";
    case Control::clear:
        return "CLEAR";
    case Control::select_menu:
        return "SELECT";
    case Control::group_store:
        return "STORE";
    case Control::group_chip:
        return index < group_labels.size() ? group_labels[index] : std::string_view{};
    case Control::pause:
        return "PAUSE";
    case Control::speed:
        return "SPEED";
    case Control::chat:
        return "CHAT";
    case Control::centre:
        return "CENTRE";
    case Control::follow:
        return "FOLLOW";
    case Control::next_unit:
        return "NEXT";
    case Control::info:
        return "INFO";
    case Control::menu:
        return "MENU";
    case Control::build_drawer:
        return state.builder_selected ? "BUILD" : "ORDERS";
    case Control::zoom_out:
        return "-";
    case Control::zoom_in:
        return "+";
    case Control::order_slot:
        if (index >= state.rail.size())
            return {};
        if (state.rail[index].more)
            return "MORE";
        return order_label(state.rail[index].order);
    case Control::more:
        return "MORE";
    case Control::drawer_build_tab:
        return "BUILD";
    case Control::drawer_orders_tab:
        return "ORDERS";
    case Control::drawer_close:
        return "CLOSE";
    case Control::drawer_prev:
        return "PREV";
    case Control::drawer_next:
        return "NEXT";
    case Control::place_cancel:
        return "CANCEL";
    case Control::banner_cancel:
        return "CANCEL";
    case Control::menu_item:
        return menu_item_label(state.sheet, index);
    case Control::more_item:
        if (index == static_cast<uint8_t>(MoreItem::info))
            return "INFO";
        if (index == static_cast<uint8_t>(MoreItem::self_destruct))
            return "SELF-DESTRUCT · HOLD";
        return {};
    case Control::radial_item:
        return index < radial_labels.size() ? radial_labels[index] : std::string_view{};
    case Control::radial_hub:
        return "QUEUE";
    }
    return {};
}

std::string status_hint(TapAction tap, TapAction enemy) {
    // While placing, the line says how to place.
    if (tap == TapAction::place)
        return "DRAG TO MOVE · DOUBLE-TAP OR HOLD TO PLACE";
    std::string hint;
    if (const auto word = tap_action_word(tap); !word.empty()) {
        hint += "TAP: ";
        hint += word;
    }
    if (const auto word = tap_action_word(enemy); !word.empty()) {
        if (!hint.empty())
            hint += " · ";
        hint += "ENEMY: ";
        hint += word;
    }
    return hint;
}

std::string_view menu_item_label(Sheet sheet, uint8_t index) noexcept {
    switch (sheet) {
    case Sheet::select_menu:
        return index < select_labels.size() ? select_labels[index] : std::string_view{};
    case Sheet::speed:
        return index < speed_labels.size() ? speed_labels[index] : std::string_view{};
    case Sheet::phone_menu:
        return index < phone_menu_labels.size() ? phone_menu_labels[index] : std::string_view{};
    case Sheet::none:
    case Sheet::more:
    case Sheet::drawer:
        return {};
    }
    return {};
}

} // namespace oa::ui::touch_hud
