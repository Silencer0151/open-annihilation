// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The encoder of a director script: builds the document tree with its keys
// in the order oascript.hpp lists them.

#include "oa/formats/oascript.hpp"

#include <string>
#include <utility>

namespace oa::formats::oascript {

namespace {

/// Returns a mapping entry holding a number.
///
/// @param key the key
/// @param value the number
/// @return the entry
Node number_entry(std::string key, Decimal value) {
    Node node{};
    node.kind = NodeKind::number;
    node.key = std::move(key);
    node.number = value;
    return node;
}

/// Returns a mapping entry holding a whole number.
///
/// @param key the key
/// @param value the number
/// @return the entry
Node whole_entry(std::string key, uint32_t value) {
    return number_entry(std::move(key), Decimal{int64_t{value}, 0});
}

/// Returns a mapping entry holding a string.
///
/// @param key the key
/// @param text the string
/// @return the entry
Node string_entry(std::string key, std::string text) {
    Node node{};
    node.kind = NodeKind::string;
    node.key = std::move(key);
    node.text = std::move(text);
    return node;
}

/// Returns a mapping entry holding a boolean.
///
/// @param key the key
/// @param value the boolean
/// @return the entry
Node boolean_entry(std::string key, bool value) {
    Node node{};
    node.kind = NodeKind::boolean;
    node.key = std::move(key);
    node.boolean = value;
    return node;
}

/// Returns an empty mapping, as an entry under a key or, with an empty key,
/// as a sequence item.
///
/// @param key the key
/// @return the mapping
Node mapping_entry(std::string key) {
    Node node{};
    node.kind = NodeKind::mapping;
    node.key = std::move(key);
    return node;
}

/// Returns the entry of a camera.
///
/// @param key cameraStart or cameraEnd
/// @param camera the camera
/// @return the entry
Node camera_entry(std::string key, const CameraState& camera) {
    Node node{mapping_entry(std::move(key))};
    Node position{mapping_entry("position")};
    position.children.push_back(number_entry("x", camera.position.x));
    position.children.push_back(number_entry("y", camera.position.y));
    position.children.push_back(number_entry("z", camera.position.z));
    node.children.push_back(std::move(position));
    if (camera.orientation.has_value()) {
        Node orientation{mapping_entry("orientation")};
        orientation.children.push_back(number_entry("pitch", camera.orientation->pitch));
        orientation.children.push_back(number_entry("yaw", camera.orientation->yaw));
        orientation.children.push_back(number_entry("roll", camera.orientation->roll));
        node.children.push_back(std::move(orientation));
    }
    return node;
}

/// Returns the name a transition kind is written as.
///
/// @param kind the kind
/// @return fade, wipe, dissolve or checkerboard
const char* transition_name(TransitionKind kind) noexcept {
    switch (kind) {
    case TransitionKind::fade:
        return "fade";
    case TransitionKind::wipe:
        return "wipe";
    case TransitionKind::dissolve:
        return "dissolve";
    case TransitionKind::checkerboard:
        return "checkerboard";
    }
    return "dissolve";
}

/// Returns the item of one shot.
///
/// @param shot the shot
/// @return the mapping
Node shot_item(const Shot& shot) {
    Node node{mapping_entry(std::string{})};
    node.children.push_back(whole_entry("tick", shot.tick));
    if (shot.camera_start.has_value())
        node.children.push_back(camera_entry("cameraStart", *shot.camera_start));
    node.children.push_back(camera_entry("cameraEnd", shot.camera_end));
    if (shot.motion.kind == Motion::spring) {
        Node motion{mapping_entry("motion")};
        Node spring{mapping_entry("spring")};
        spring.children.push_back(number_entry("frequency", shot.motion.frequency));
        spring.children.push_back(number_entry("dampingRatio", shot.motion.damping_ratio));
        motion.children.push_back(std::move(spring));
        node.children.push_back(std::move(motion));
    }
    if (shot.transition.has_value()) {
        Node transition{mapping_entry("transition")};
        transition.children.push_back(string_entry("type", transition_name(shot.transition->kind)));
        transition.children.push_back(number_entry("duration", shot.transition->duration));
        node.children.push_back(std::move(transition));
    }
    return node;
}

} // namespace

Node encode_script(const Script& script) {
    Node root{mapping_entry(std::string{})};
    root.children.push_back(whole_entry("oascript", script_version));

    Node input{mapping_entry("input")};
    // A script names its recording, or else its stage.
    if (script.input.stage.empty())
        input.children.push_back(string_entry("demo", script.input.recording));
    else
        input.children.push_back(string_entry("stage", script.input.stage));
    input.children.push_back(number_entry("tickrate", script.input.tickrate));
    root.children.push_back(std::move(input));

    Node output{mapping_entry("output")};
    Node resolution{mapping_entry("resolution")};
    resolution.children.push_back(whole_entry("width", script.output.width));
    resolution.children.push_back(whole_entry("height", script.output.height));
    output.children.push_back(std::move(resolution));
    output.children.push_back(number_entry("framerate", script.output.framerate));
    Node chunking{mapping_entry("chunking")};
    chunking.children.push_back(
        string_entry("mode", script.output.chunking.mode == ChunkMode::ticks ? "ticks" : "seconds")
    );
    chunking.children.push_back(number_entry("length", script.output.chunking.length));
    output.children.push_back(std::move(chunking));
    output.children.push_back(boolean_entry("showUx", script.output.show_ux));
    root.children.push_back(std::move(output));

    Node director{mapping_entry("director")};
    Node shots{mapping_entry("shots")};
    shots.kind = NodeKind::sequence;
    for (const Shot& shot : script.director.shots)
        shots.children.push_back(shot_item(shot));
    director.children.push_back(std::move(shots));
    if (script.director.end_tick.has_value())
        director.children.push_back(whole_entry("endTick", *script.director.end_tick));
    root.children.push_back(std::move(director));
    return root;
}

std::string write_script(const Script& script, DocumentForm form) {
    return write_document(encode_script(script), form);
}

} // namespace oa::formats::oascript
