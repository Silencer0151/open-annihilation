// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/ui/gui_layout.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string_view>

namespace oa::ui::gui_input {

// Panel-relative rectangle with inclusive edges: right=x+width-1 and
// bottom=y+height-1. Coordinates are signed because the record fields are
// sign-extended int16 values.
struct Rect {
    int32_t left = 0;
    int32_t top = 0;
    int32_t right = -1;
    int32_t bottom = -1;

    /// Reports whether a point lies inside the rectangle.
    [[nodiscard]] bool contains(int32_t x, int32_t y) const noexcept {
        return left <= x && x <= right && top <= y && y <= bottom;
    }
};

struct MenuObject {
    // Records of the menu's panel; a menu without a panel has an empty span.
    std::span<const ui::gui_layout::Gadget> gadgets;
    // Selected record; -1 means none.
    int32_t selected_index = -1;
};

/// Returns a gadget's panel-relative rectangle.
///
/// A type-zero record is placed at (0,0); right and bottom are always
/// computed from the stored dimensions.
///
/// @param gadgets Gadget records of one layout.
/// @param index Record to measure.
/// @return The inclusive rectangle, or nullopt for an index past the span.
[[nodiscard]] std::optional<Rect>
gadget_geometry(std::span<const ui::gui_layout::Gadget> gadgets, std::size_t index) noexcept;

/// Finds the record a click lands on, as the menu pointer path does.
///
/// Starts at record 1 (the root record is never a hit target) and considers
/// only active records of the types that take a click (up to the hot
/// surface).
///
/// @param menu Menu records and current selection.
/// @param x Pointer x relative to the root origin; the caller subtracts it.
/// @param y Pointer y relative to the root origin.
/// @return The last matching record when nothing was selected on entry, or nullopt.
/// @quirk With a preexisting selection it processes at most the first record
///        and then stops.
[[nodiscard]] std::optional<std::size_t>
hit_test(const MenuObject& menu, int32_t x, int32_t y) noexcept;

/// Compares the selected record's name with a query.
///
/// @param menu Menu records and current selection.
/// @param query Name to compare; anything after a NUL is ignored.
/// @return True when they match; false for an empty layout or no selection.
[[nodiscard]] bool button_result(const MenuObject& menu, std::string_view query) noexcept;

/// Returns the stage a button moves to when a click is released on it.
///
/// A plain push button with stages steps to its next stage and wraps to 0
/// past its last one; hold, toggle, check-box, frame-cycling and scroll-step
/// buttons, grayed buttons, buttons without stages and other records keep
/// their stage.
///
/// @param gadget The record the click was released on.
/// @param stage Its current stage.
/// @return The stage after the release.
[[nodiscard]] uint8_t
released_button_stage(const ui::gui_layout::Gadget& gadget, uint8_t stage) noexcept;

/// Clears the selected record to -1.
///
/// @param[in,out] menu Menu whose selection is cleared.
void clear_selection(MenuObject& menu) noexcept;

// The label drawing adds a second string at (+1, +3) only when this attribs
// bit is set and a type-7 texture resolves.
inline constexpr int32_t kLabelShadowAttribute = 8;

/// Gives every loaded label record the shadow attribute.
///
/// Run by each menu loader once its panel is set up. Records 1 through the
/// root's signed loaded count (loaded_total_gadgets) are visited, never the root.
///
/// @param[in,out] gadgets Layout records; a missing panel or a non-positive
///        count changes nothing.
void mark_label_shadows(std::span<ui::gui_layout::Gadget> gadgets) noexcept;

// Internal frontend control codes recognized by the key-down test. These values
// are message/control codes, not host scancodes.
enum class ControlKey : uint8_t {
    left = 0xF4,
    up = 0xF5,
    right = 0xF6,
    down = 0xF7,
    space = 0x20,
    shift = 0xF9,
    control = 0xFA,
    alt = 0xFB,
};

enum class VirtualKey : uint8_t {
    left = 0x25,
    up = 0x26,
    right = 0x27,
    down = 0x28,
    space = 0x20,
    shift = 0x10,
    control = 0x11,
    alt = 0x12,
};

inline constexpr uint16_t kAsyncKeyDownMask = 0xFFFEU;

/// Reports whether a key-state word shows the key down.
///
/// @param async_state Key-state word: bit 15 while held, bit 0 toggled by
///        presses; every bit except the low toggle bit counts.
/// @return True when any counted bit is set.
[[nodiscard]] constexpr bool key_is_down(uint16_t async_state) noexcept {
    return (async_state & kAsyncKeyDownMask) != 0;
}

/// Maps an internal control code to the virtual key behind it.
///
/// @param key Control code.
/// @return The virtual key, or nullopt for a code without one.
[[nodiscard]] std::optional<VirtualKey> physical_key(ControlKey key) noexcept;

// Key-state bit reporting that a key is held right now.
inline constexpr uint16_t kAsyncKeyHeld = 0x8000U;

// Host keyboard boundary: the key-state word of a virtual key.
using AsyncKeyState = uint16_t (*)(void* context, VirtualKey key);

/// Reports whether the key behind an internal control code is held.
///
/// Only the arrow, space, shift, control and alt codes have a key (not
/// pause, 0xF8); the host is not asked about any other code.
///
/// @param code Internal control code.
/// @param state Host keyboard query; null reports every key up.
/// @param context Value passed to `state`.
/// @return True when the key is down.
[[nodiscard]] bool control_key_down(int32_t code, AsyncKeyState state, void* context) noexcept;

enum class NavigationDirection : uint8_t { left, up, right, down };

/// Maps an arrow control code to a navigation direction.
///
/// @param key Control code.
/// @return The direction, or nullopt for a non-arrow code.
[[nodiscard]] std::optional<NavigationDirection> navigation_direction(ControlKey key) noexcept;

} // namespace oa::ui::gui_input
