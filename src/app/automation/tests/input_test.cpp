// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The device events of the automation endpoint's input requests: each kind
// read with its fields and their defaults, as a client sends a click, a
// typed character and a held key; and the events that cannot be read,
// each refused with what is wrong and the field it is in.
#include "input_events.hpp"
#include "oa/test/check.hpp"

#include <cstddef>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace automation = oa::app::automation;
using automation::InputKind;

/// Reads the events of a request's JSON.
///
/// @param request the request object, as JSON text
/// @param[out] error what is wrong, when they cannot be read
/// @return the events, or nothing
std::optional<std::vector<automation::InputEvent>>
decode(std::string_view request, automation::InputError& error) {
    automation::JsonError json_error;
    const auto parsed = automation::parse_json(request, json_error);
    OA_CHECK(parsed.has_value());
    if (!parsed)
        return std::nullopt;
    return automation::decode_input_events(parsed->find("events"), error);
}

/// Tells whether a request's events are refused, naming the events field
/// and a message that holds a phrase.
///
/// @param request the request object, as JSON text
/// @param phrase a phrase the message holds
/// @return true when they are refused so
bool refused(std::string_view request, std::string_view phrase) {
    automation::InputError error;
    const auto events = decode(request, error);
    return !events && error.field == "events" && error.message.find(phrase) != std::string::npos;
}

/// A click as a client sends it, a key pressed and released with the text
/// it types, a wheel turn and a finger read with their fields.
void check_read() {
    automation::InputError error;
    const auto click = decode(
        R"({"events":[{"kind":"pointer_move","x":160,"y":220,"space":"game"},)"
        R"({"kind":"button_down","x":160,"y":220,"space":"game","button":"left","clicks":1},)"
        R"({"kind":"button_up","button":"right","clicks":2}]})",
        error
    );
    OA_CHECK(click && click->size() == 3);
    if (click && click->size() == 3) {
        const auto& move = (*click)[0];
        OA_CHECK(move.kind == InputKind::pointer_move && move.has_point);
        OA_CHECK(move.x == 160 && move.y == 220 && move.space == automation::PointSpace::game);
        OA_CHECK((*click)[1].kind == InputKind::button_down);
        OA_CHECK((*click)[1].button == automation::PointerButton::left);
        OA_CHECK((*click)[1].clicks == 1 && (*click)[1].has_point);
        const auto& up = (*click)[2];
        OA_CHECK(up.kind == InputKind::button_up && !up.has_point);
        OA_CHECK(up.button == automation::PointerButton::right && up.clicks == 2);
    }

    const auto typed = decode(
        R"({"events":[{"kind":"key_down","key":"a","scancode":4,"keycode":97},)"
        R"({"kind":"text","text":"aé"},{"kind":"key_up","key":"a","scancode":4}]})",
        error
    );
    OA_CHECK(typed && typed->size() == 3);
    if (typed && typed->size() == 3) {
        OA_CHECK((*typed)[0].kind == InputKind::key_down);
        OA_CHECK((*typed)[0].scancode == 4 && (*typed)[0].keycode == 97);
        OA_CHECK((*typed)[1].kind == InputKind::text && (*typed)[1].text == "a\xc3\xa9");
        // Without a key code, the one its scancode has is taken as it is pushed.
        OA_CHECK((*typed)[2].kind == InputKind::key_up && (*typed)[2].keycode == 0);
    }

    const auto others = decode(
        R"({"events":[{"kind":"wheel","dy":-2},)"
        R"({"kind":"button_down"},)"
        R"({"kind":"finger_down","x":1200,"y":40,"space":"window","finger":3},)"
        R"({"kind":"finger_up","x":1200,"y":40}]})",
        error
    );
    OA_CHECK(others && others->size() == 4);
    if (others && others->size() == 4) {
        OA_CHECK((*others)[0].kind == InputKind::wheel && (*others)[0].wheel_y == -2);
        OA_CHECK((*others)[0].wheel_x == 0 && !(*others)[0].has_point);
        OA_CHECK((*others)[1].button == automation::PointerButton::left);
        OA_CHECK((*others)[1].clicks == 1);
        OA_CHECK((*others)[2].kind == InputKind::finger_down && (*others)[2].finger == 3);
        OA_CHECK((*others)[2].space == automation::PointSpace::window);
        OA_CHECK((*others)[3].finger == 0 && (*others)[3].space == automation::PointSpace::game);
    }

    const auto none = decode(R"({"events":[]})", error);
    OA_CHECK(none && none->empty());
}

/// Events that cannot be read are refused, with what is wrong.
void check_refused() {
    OA_CHECK(refused(R"({})", "list of device events"));
    OA_CHECK(refused(R"({"events":{"kind":"text"}})", "list of device events"));
    OA_CHECK(refused(R"({"events":[7]})", "event 0 is not an object"));
    OA_CHECK(refused(R"({"events":[{"x":1}]})", "event 0 has no kind"));
    OA_CHECK(refused(R"({"events":[{"kind":"dance"}]})", "kind the endpoint does not know"));
    OA_CHECK(refused(R"({"events":[{"kind":"key_down"}]})", "has no scancode"));
    OA_CHECK(refused(R"({"events":[{"kind":"key_down","scancode":0}]})", "scancode"));
    OA_CHECK(refused(R"({"events":[{"kind":"key_up","scancode":512}]})", "scancode"));
    OA_CHECK(refused(R"({"events":[{"kind":"key_up","scancode":"4"}]})", "scancode"));
    OA_CHECK(refused(R"({"events":[{"kind":"key_up","scancode":4,"keycode":-1}]})", "keycode"));
    OA_CHECK(refused(R"({"events":[{"kind":"text"}]})", "has no text"));
    OA_CHECK(refused(R"({"events":[{"kind":"text","text":"a\u0000b"}]})", "zero character"));
    OA_CHECK(refused(R"({"events":[{"kind":"pointer_move"}]})", "has no x and y"));
    OA_CHECK(refused(R"({"events":[{"kind":"pointer_move","x":3}]})", "without the other"));
    OA_CHECK(refused(R"({"events":[{"kind":"pointer_move","x":1.5,"y":2}]})", "has x outside"));
    OA_CHECK(
        refused(R"({"events":[{"kind":"pointer_move","x":1,"y":4294967296}]})", "has y outside")
    );
    OA_CHECK(refused(R"({"events":[{"kind":"pointer_move","x":1,"y":2,"space":"map"}]})", "space"));
    OA_CHECK(refused(R"({"events":[{"kind":"wheel","x":1,"y":2,"space":3}]})", "space"));
    OA_CHECK(refused(R"({"events":[{"kind":"button_up","button":"fourth"}]})", "button"));
    OA_CHECK(refused(R"({"events":[{"kind":"button_up","clicks":0}]})", "clicks"));
    OA_CHECK(refused(R"({"events":[{"kind":"button_up","clicks":256}]})", "clicks"));
    OA_CHECK(refused(R"({"events":[{"kind":"wheel","dy":1001}]})", "dy"));
    OA_CHECK(refused(R"({"events":[{"kind":"finger_up","x":1,"y":1,"finger":256}]})", "finger"));
    // The first event that cannot be read is the one named.
    OA_CHECK(refused(R"({"events":[{"kind":"wheel"},{"kind":"text"}]})", "event 1 has no text"));

    std::string long_text(automation::max_input_text_bytes + 1, 'a');
    OA_CHECK(
        refused(R"({"events":[{"kind":"text","text":")" + long_text + R"("}]})", "bytes of text")
    );
    std::string many = R"({"events":[)";
    for (size_t index = 0; index <= automation::max_input_events; ++index)
        many += index == 0 ? R"({"kind":"wheel"})" : R"(,{"kind":"wheel"})";
    many += "]}";
    OA_CHECK(refused(many, "more than"));
}

} // namespace

int main() {
    check_read();
    check_refused();
    return oa::test::check_exit_status();
}
