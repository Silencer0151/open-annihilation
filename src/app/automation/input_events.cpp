// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The device events of an input request (input_events.hpp).
#include "input_events.hpp"

#include <array>
#include <limits>
#include <string_view>
#include <utility>

namespace oa::app::automation {
namespace {

// The field of the request every error names.
constexpr std::string_view kEventsField = "events";

// An event kind and the name a request gives it.
struct NamedKind {
    std::string_view name;
    InputKind kind{};
};

// Every event kind, by name.
constexpr std::array kKindNames = {
    NamedKind{"key_down", InputKind::key_down},
    NamedKind{"key_up", InputKind::key_up},
    NamedKind{"text", InputKind::text},
    NamedKind{"pointer_move", InputKind::pointer_move},
    NamedKind{"button_down", InputKind::button_down},
    NamedKind{"button_up", InputKind::button_up},
    NamedKind{"wheel", InputKind::wheel},
    NamedKind{"finger_down", InputKind::finger_down},
    NamedKind{"finger_move", InputKind::finger_move},
    NamedKind{"finger_up", InputKind::finger_up},
};

// A pointer button and its name.
struct NamedButton {
    std::string_view name;
    PointerButton button{};
};

// Every pointer button, by name.
constexpr std::array kButtonNames = {
    NamedButton{"left", PointerButton::left},
    NamedButton{"middle", PointerButton::middle},
    NamedButton{"right", PointerButton::right},
};

/// Reads events one at a time, noting the first thing wrong.
class EventReader {
  public:

    /// Starts reading.
    ///
    /// @param[out] error receives the first thing wrong
    explicit EventReader(InputError& error) : error_(error) {}

    /// Reads one event.
    ///
    /// @param value the event's JSON
    /// @param index its place in the list, for the message
    /// @param[out] event the event read
    /// @return false when it cannot be read; error says why
    bool read(const Json& value, size_t index, InputEvent& event) {
        index_ = index;
        if (value.type() != JsonType::object)
            return fail("is not an object");
        const std::string* kind_name = string_member(value, "kind");
        if (kind_name == nullptr)
            return fail("has no kind");
        bool known = false;
        for (const NamedKind& named : kKindNames)
            if (named.name == *kind_name) {
                event.kind = named.kind;
                known = true;
            }
        if (!known)
            return fail("has a kind the endpoint does not know: " + *kind_name);
        switch (event.kind) {
        case InputKind::key_down:
        case InputKind::key_up:
            return read_key(value, event);
        case InputKind::text:
            return read_text(value, event);
        case InputKind::pointer_move:
        case InputKind::finger_down:
        case InputKind::finger_move:
        case InputKind::finger_up:
            return read_point(value, event, true) && read_finger(value, event);
        case InputKind::button_down:
        case InputKind::button_up:
            return read_point(value, event, false) && read_button(value, event);
        case InputKind::wheel:
            return read_point(value, event, false) && read_wheel(value, event);
        }
        return fail("has a kind the endpoint does not know");
    }

  private:

    /// Notes what is wrong with the event being read.
    ///
    /// @param what what is wrong, after "event N "
    /// @return false
    bool fail(const std::string& what) {
        error_.message = "event " + std::to_string(index_) + " " + what;
        error_.field = kEventsField;
        return false;
    }

    /// Returns an object's string member.
    ///
    /// @param value the object
    /// @param name the member's name
    /// @return its text, or null when it is missing or not a string
    static const std::string* string_member(const Json& value, std::string_view name) {
        const Json* member = value.find(name);
        return member != nullptr ? member->string() : nullptr;
    }

    /// Reads an optional whole-number member within bounds.
    ///
    /// @param value the event
    /// @param name the member's name
    /// @param least its least value
    /// @param most its greatest value
    /// @param[in,out] out receives it; left as it is when the member is missing
    /// @return false when it is there but not a whole number within the bounds
    bool read_integer(
        const Json& value, std::string_view name, int64_t least, int64_t most, int64_t& out
    ) {
        const Json* member = value.find(name);
        if (member == nullptr || member->type() == JsonType::null)
            return true;
        const std::optional<int64_t> number = member->integer();
        if (!number || *number < least || *number > most)
            return fail(
                "has " + std::string(name) + " outside the whole numbers from " +
                std::to_string(least) + " to " + std::to_string(most)
            );
        out = *number;
        return true;
    }

    /// Reads a key's scancode and key code.
    ///
    /// @param value the event
    /// @param[in,out] event the event
    /// @return false when they cannot be read
    bool read_key(const Json& value, InputEvent& event) {
        if (value.find("scancode") == nullptr)
            return fail("has no scancode");
        int64_t scancode = 0;
        int64_t keycode = 0;
        if (!read_integer(value, "scancode", 1, scancode_limit - 1, scancode) ||
            !read_integer(value, "keycode", 0, std::numeric_limits<uint32_t>::max(), keycode))
            return false;
        event.scancode = static_cast<uint32_t>(scancode);
        event.keycode = static_cast<uint32_t>(keycode);
        return true;
    }

    /// Reads the characters a text event types.
    ///
    /// @param value the event
    /// @param[in,out] event the event
    /// @return false when they cannot be read
    bool read_text(const Json& value, InputEvent& event) {
        const std::string* text = string_member(value, "text");
        if (text == nullptr)
            return fail("has no text");
        if (text->size() > max_input_text_bytes)
            return fail("has more than " + std::to_string(max_input_text_bytes) + " bytes of text");
        if (text->find('\0') != std::string::npos)
            return fail("has a zero character in its text");
        event.text = *text;
        return true;
    }

    /// Reads an event's point and the space it is measured in.
    ///
    /// @param value the event
    /// @param[in,out] event the event
    /// @param needed the event needs a point
    /// @return false when it cannot be read, or a needed one is missing
    bool read_point(const Json& value, InputEvent& event, bool needed) {
        const bool has_x = value.find("x") != nullptr;
        const bool has_y = value.find("y") != nullptr;
        if (has_x != has_y)
            return fail("has one of x and y without the other");
        if (!has_x && needed)
            return fail("has no x and y");
        if (const std::string* space = string_member(value, "space")) {
            if (*space == "game")
                event.space = PointSpace::game;
            else if (*space == "window")
                event.space = PointSpace::window;
            else
                return fail("has a space that is neither game nor window: " + *space);
        } else if (
            value.find("space") != nullptr && value.find("space")->type() != JsonType::null
        ) {
            return fail("has a space that is not a string");
        }
        if (!has_x)
            return true;
        constexpr int64_t kLeast = std::numeric_limits<int32_t>::min();
        constexpr int64_t kMost = std::numeric_limits<int32_t>::max();
        int64_t x = 0;
        int64_t y = 0;
        if (!read_integer(value, "x", kLeast, kMost, x) ||
            !read_integer(value, "y", kLeast, kMost, y))
            return false;
        event.has_point = true;
        event.x = static_cast<int32_t>(x);
        event.y = static_cast<int32_t>(y);
        return true;
    }

    /// Reads a press's or release's button and clicks.
    ///
    /// @param value the event
    /// @param[in,out] event the event
    /// @return false when they cannot be read
    bool read_button(const Json& value, InputEvent& event) {
        if (const Json* button = value.find("button");
            button != nullptr && button->type() != JsonType::null) {
            const std::string* name = button->string();
            bool known = false;
            for (const NamedButton& named : kButtonNames)
                if (name != nullptr && named.name == *name) {
                    event.button = named.button;
                    known = true;
                }
            if (!known)
                return fail("has a button that is not left, right or middle");
        }
        int64_t clicks = 1;
        if (!read_integer(value, "clicks", 1, max_clicks, clicks))
            return false;
        event.clicks = static_cast<uint8_t>(clicks);
        return true;
    }

    /// Reads a wheel's turn.
    ///
    /// @param value the event
    /// @param[in,out] event the event
    /// @return false when it cannot be read
    bool read_wheel(const Json& value, InputEvent& event) {
        constexpr int64_t kNotches = 1000;
        int64_t across = 0;
        int64_t away = 0;
        if (!read_integer(value, "dx", -kNotches, kNotches, across) ||
            !read_integer(value, "dy", -kNotches, kNotches, away))
            return false;
        event.wheel_x = static_cast<int32_t>(across);
        event.wheel_y = static_cast<int32_t>(away);
        return true;
    }

    /// Reads which finger a finger event is.
    ///
    /// @param value the event
    /// @param[in,out] event the event
    /// @return false when it cannot be read
    bool read_finger(const Json& value, InputEvent& event) {
        int64_t finger = 0;
        if (!read_integer(value, "finger", 0, max_finger, finger))
            return false;
        event.finger = static_cast<uint8_t>(finger);
        return true;
    }

    InputError& error_;
    size_t index_{};
};

} // namespace

std::optional<std::vector<InputEvent>> decode_input_events(const Json* events, InputError& error) {
    if (events == nullptr || events->type() != JsonType::array) {
        error = {"events is a list of device events", std::string(kEventsField)};
        return std::nullopt;
    }
    if (events->elements().size() > max_input_events) {
        error = {
            "events holds more than " + std::to_string(max_input_events) + " events",
            std::string(kEventsField)
        };
        return std::nullopt;
    }
    std::vector<InputEvent> decoded;
    decoded.reserve(events->elements().size());
    EventReader reader(error);
    size_t index = 0;
    for (const Json& value : events->elements()) {
        InputEvent event;
        if (!reader.read(value, index, event))
            return std::nullopt;
        decoded.push_back(std::move(event));
        ++index;
    }
    return decoded;
}

} // namespace oa::app::automation
