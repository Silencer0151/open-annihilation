// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The device events an input request carries, read from its JSON: keys
// going down and up with their SDL scancodes and key codes, typed text,
// pointer moves, button presses and releases, wheel turns and fingers. The
// endpoint hands them to the game through SDL's event queue (input.hpp);
// this part only reads and checks them, and needs no running game.
#pragma once

#include "oa/app/automation/json.hpp"

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oa::app::automation {

/// The most events one input request may carry.
inline constexpr size_t max_input_events = 1024;
/// The most bytes of text one text event may carry.
inline constexpr size_t max_input_text_bytes = 1024;
/// SDL's scancodes are below this (SDL_SCANCODE_COUNT).
inline constexpr int64_t scancode_limit = 512;
/// The most presses in a row a press or release may count.
inline constexpr int64_t max_clicks = 255;
/// The largest finger number a finger event may name.
inline constexpr int64_t max_finger = 255;

/// What one event does.
enum class InputKind : uint8_t {
    key_down,     ///< a key goes down
    key_up,       ///< a key comes up
    text,         ///< the characters a key press types
    pointer_move, ///< the pointer moves
    button_down,  ///< a pointer button goes down
    button_up,    ///< a pointer button comes up
    wheel,        ///< the wheel turns
    finger_down,  ///< a finger touches the screen
    finger_move,  ///< a finger moves on the screen
    finger_up,    ///< a finger leaves the screen
};

/// What an event's point is measured in.
enum class PointSpace : uint8_t {
    game,   ///< the game's canvas: the 640x480 screen on the menus, the window's pixels in a match
    window, ///< the window's own pixels
};

/// A pointer button.
enum class PointerButton : uint8_t {
    left,   ///< the left button
    middle, ///< the middle button
    right,  ///< the right button
};

/// One device event of an input request.
struct InputEvent {
    InputKind kind{};
    uint32_t scancode{}; ///< a key's SDL scancode
    uint32_t keycode{};  ///< a key's SDL key code; 0 to take the one its scancode has
    std::string text;    ///< the characters typed, UTF-8
    bool has_point{};    ///< x and y are given; else the pointer's last place
    int32_t x{};         ///< the point's column
    int32_t y{};         ///< the point's row
    PointSpace space{};  ///< what x and y are measured in
    PointerButton button{};
    uint8_t clicks{1}; ///< the presses in a row a press or release counts
    int32_t wheel_x{}; ///< the wheel's turn across, in notches, positive to the right
    int32_t wheel_y{}; ///< the wheel's turn, in notches, positive away from the player
    uint8_t finger{};  ///< which finger
};

/// Why an input request's events cannot be read.
struct InputError {
    std::string message; ///< what is wrong
    std::string field;   ///< the request's field it is in
};

/// Reads an input request's events.
///
/// `events` must be a list of at most max_input_events objects, each with a
/// `kind`: key_down and key_up take `scancode` (below scancode_limit) and
/// an optional `keycode` and `key`, the key's name; text takes `text`;
/// pointer_move, button_down, button_up, wheel and the finger events take
/// `x` and `y` (both or neither, optional but for pointer_move and the
/// finger events) and `space` (`game`, the default, or `window`);
/// button_down and button_up take `button` (`left`, the default, `right`
/// or `middle`) and `clicks` (1 to max_clicks, 1 by default); wheel takes
/// `dx` and `dy`; the finger events take `finger` (0 to max_finger, 0 by
/// default).
///
/// @param events the request's events member; null when it has none
/// @param[out] error what is wrong, when they cannot be read
/// @return the events in order, or nothing when they cannot be read
[[nodiscard]] std::optional<std::vector<InputEvent>>
decode_input_events(const Json* events, InputError& error);

} // namespace oa::app::automation
