// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Haptics, prompts and the button glyphs (pad_controls.hpp).
#include "oa/ui/pad_controls.hpp"

namespace oa::ui::pad_controls {

namespace {

// Motor speeds, 0..0xffff, from the softest touch to a thump.
/// A pointer tick at Light.
constexpr uint16_t motor_whisper = 0x1800;
/// A pointer tick at Strong; a wedge change at Light.
constexpr uint16_t motor_soft = 0x2800;
/// A wedge change, a box started or a queue reduced at Light.
constexpr uint16_t motor_light = 0x3800;
/// Those at Strong; a target detent or a hold started at Light.
constexpr uint16_t motor_medium = 0x5800;
/// A target detent or a hold started at Strong; a refused site at Light.
constexpr uint16_t motor_firm = 0x8800;
/// A refused site at Strong.
constexpr uint16_t motor_hard = 0xc000;

// How long each kind of feel runs, in milliseconds.
/// A pointer tick or a wedge change.
constexpr uint32_t tick_ms = 12;
/// A target detent, a box started or a queue reduced.
constexpr uint32_t detent_ms = 20;
/// A hold started.
constexpr uint32_t hold_ms = 35;
/// A refused site's thump.
constexpr uint32_t thump_ms = 70;
/// How much longer a feel runs at Strong, in milliseconds.
constexpr uint32_t strong_extra_ms = 6;

// Trackpad pulse gains, in decibels.
/// A light feel's tick at Light.
constexpr int8_t pulse_light_gain_db = -6;
/// A firm feel's click at Light.
constexpr int8_t pulse_firm_gain_db = 0;
/// How much louder every pulse is at Strong.
constexpr int8_t pulse_strong_extra_db = 6;

// The D-pad glyph's lit arm (GlyphSpec::direction).
/// The up arm.
constexpr uint8_t dpad_up_arm = 0;
/// The right arm.
constexpr uint8_t dpad_right_arm = 1;
/// The down arm.
constexpr uint8_t dpad_down_arm = 2;
/// The left arm.
constexpr uint8_t dpad_left_arm = 3;

/// A feel's motors at one strength, before the sides are chosen.
struct FeelStrength {
    uint16_t light{};  ///< the motor speed at Light
    uint16_t strong{}; ///< the motor speed at Strong
    uint32_t ms{};     ///< how long at Light, milliseconds
};

/// Returns a feel's motor speeds and length.
///
/// @param feel the moment
/// @return its strengths
FeelStrength strength_of(Feel feel) noexcept {
    switch (feel) {
    case Feel::pointer_tick:
        return {motor_whisper, motor_soft, tick_ms};
    case Feel::wedge_change:
        return {motor_soft, motor_light, tick_ms};
    case Feel::box_started:
    case Feel::queue_reduced:
        return {motor_light, motor_medium, detent_ms};
    case Feel::target_detent:
        return {motor_medium, motor_firm, detent_ms};
    case Feel::hold_started:
        return {motor_medium, motor_firm, hold_ms};
    case Feel::site_refused:
        return {motor_firm, motor_hard, thump_ms};
    }
    return {};
}

/// Returns the side a feel plays on: both for a hold started, the right (the pointer's thumb)
/// for every other.
///
/// @param feel the moment
/// @return the side
PadSide side_of(Feel feel) noexcept {
    return feel == Feel::hold_started ? PadSide::both : PadSide::right;
}

/// Returns whether a feel is firm: a click on the trackpad rather than a tick.
///
/// @param feel the moment
/// @return whether it is
bool firm(Feel feel) noexcept {
    return feel == Feel::target_detent || feel == Feel::hold_started || feel == Feel::site_refused;
}

/// Returns a face button's letters in the Deck and Xbox sets.
///
/// @param button the face button
/// @return its letter
std::string_view abxy_letter(PadButton button) noexcept {
    switch (button) {
    case PadButton::a:
        return "A";
    case PadButton::b:
        return "B";
    case PadButton::x:
        return "X";
    case PadButton::y:
        return "Y";
    default:
        return {};
    }
}

/// Returns a face button's letter in the Nintendo set, which labels by place differently: the
/// bottom button B, the right A, the left Y, the top X.
///
/// @param button the face button, by place
/// @return its letter
std::string_view nintendo_letter(PadButton button) noexcept {
    switch (button) {
    case PadButton::a:
        return "B";
    case PadButton::b:
        return "A";
    case PadButton::x:
        return "Y";
    case PadButton::y:
        return "X";
    default:
        return {};
    }
}

/// Returns a face button's glyph in a style.
///
/// @param button the face button
/// @param style the glyph set
/// @return its glyph
GlyphSpec face_glyph(PadButton button, GlyphStyle style) noexcept {
    switch (style) {
    case GlyphStyle::playstation:
        switch (button) {
        case PadButton::a:
            return {GlyphShape::cross};
        case PadButton::b:
            return {GlyphShape::circle};
        case PadButton::x:
            return {GlyphShape::square};
        default:
            return {GlyphShape::triangle};
        }
    case GlyphStyle::nintendo:
        return {GlyphShape::letter_circle, nintendo_letter(button)};
    case GlyphStyle::steam_deck:
    case GlyphStyle::xbox:
        break;
    }
    return {GlyphShape::letter_circle, abxy_letter(button)};
}

/// Returns a bumper's or trigger's label in a style.
///
/// @param button l1, r1, l2 or r2
/// @param style the glyph set
/// @return its label
std::string_view shoulder_label(PadButton button, GlyphStyle style) noexcept {
    switch (style) {
    case GlyphStyle::xbox:
        switch (button) {
        case PadButton::l1:
            return "LB";
        case PadButton::r1:
            return "RB";
        case PadButton::l2:
            return "LT";
        default:
            return "RT";
        }
    case GlyphStyle::nintendo:
        switch (button) {
        case PadButton::l1:
            return "L";
        case PadButton::r1:
            return "R";
        case PadButton::l2:
            return "ZL";
        default:
            return "ZR";
        }
    case GlyphStyle::steam_deck:
    case GlyphStyle::playstation:
        break;
    }
    switch (button) {
    case PadButton::l1:
        return "L1";
    case PadButton::r1:
        return "R1";
    case PadButton::l2:
        return "L2";
    default:
        return "R2";
    }
}

/// Returns a grip's label in a style: the Deck's names, or the Xbox paddles' own (P1 and P2
/// under the right hand, P3 and P4 under the left, the upper one first).
///
/// @param button l4, l5, r4 or r5
/// @param style the glyph set
/// @return its label
std::string_view grip_label(PadButton button, GlyphStyle style) noexcept {
    const bool paddles = style == GlyphStyle::xbox;
    switch (button) {
    case PadButton::l4:
        return paddles ? "P3" : "L4";
    case PadButton::l5:
        return paddles ? "P4" : "L5";
    case PadButton::r4:
        return paddles ? "P1" : "R4";
    default:
        return paddles ? "P2" : "R5";
    }
}

} // namespace

std::optional<Rumble> rumble_for(Feel feel, Haptics strength) noexcept {
    if (strength == Haptics::off)
        return std::nullopt;
    const FeelStrength motors = strength_of(feel);
    const bool strong = strength == Haptics::strong;
    const uint16_t speed = strong ? motors.strong : motors.light;
    Rumble rumble{};
    rumble.ms = motors.ms + (strong ? strong_extra_ms : 0);
    // The Deck plays the low motor on the left pad and the high motor on the right.
    const PadSide side = side_of(feel);
    if (side != PadSide::right)
        rumble.low = speed;
    if (side != PadSide::left)
        rumble.high = speed;
    return rumble;
}

std::optional<TrackpadPulse> trackpad_pulse_for(Feel feel, Haptics strength) noexcept {
    if (strength == Haptics::off)
        return std::nullopt;
    TrackpadPulse pulse{};
    pulse.side = side_of(feel);
    pulse.kind = firm(feel) ? PulseKind::click : PulseKind::tick;
    int8_t gain = firm(feel) ? pulse_firm_gain_db : pulse_light_gain_db;
    if (strength == Haptics::strong)
        gain = static_cast<int8_t>(gain + pulse_strong_extra_db);
    pulse.gain_db = gain;
    return pulse;
}

bool prompts_shown(Prompts prompts) noexcept {
    return prompts != Prompts::off;
}

GlyphStyle glyph_style_for(Prompts prompts, PadType type) noexcept {
    switch (prompts) {
    case Prompts::steam_deck:
        return GlyphStyle::steam_deck;
    case Prompts::xbox:
        return GlyphStyle::xbox;
    case Prompts::playstation:
        return GlyphStyle::playstation;
    case Prompts::nintendo:
        return GlyphStyle::nintendo;
    case Prompts::automatic:
    case Prompts::off:
        break;
    }
    switch (type) {
    case PadType::steam_deck:
        return GlyphStyle::steam_deck;
    case PadType::playstation:
        return GlyphStyle::playstation;
    case PadType::nintendo:
        return GlyphStyle::nintendo;
    case PadType::unknown:
    case PadType::standard:
    case PadType::xbox:
        break;
    }
    return GlyphStyle::xbox;
}

GlyphSpec glyph_spec(PadButton button, GlyphStyle style) noexcept {
    switch (button) {
    case PadButton::none:
        return {};
    case PadButton::a:
    case PadButton::b:
    case PadButton::x:
    case PadButton::y:
        return face_glyph(button, style);
    case PadButton::view:
        return {GlyphShape::view, style == GlyphStyle::nintendo ? "-" : ""};
    case PadButton::menu:
        return {GlyphShape::menu, style == GlyphStyle::nintendo ? "+" : ""};
    case PadButton::l1:
    case PadButton::r1:
    case PadButton::l2:
    case PadButton::r2:
        return {GlyphShape::pill, shoulder_label(button, style)};
    case PadButton::l4:
    case PadButton::l5:
    case PadButton::r4:
    case PadButton::r5:
        return {GlyphShape::pill, grip_label(button, style)};
    case PadButton::l3:
        return {GlyphShape::stick, {}, 0, Side::left, true};
    case PadButton::r3:
        return {GlyphShape::stick, {}, 0, Side::right, true};
    case PadButton::left_stick_touch:
        return {GlyphShape::stick, {}, 0, Side::left, false};
    case PadButton::right_stick_touch:
        return {GlyphShape::stick, {}, 0, Side::right, false};
    case PadButton::dpad_up:
        return {GlyphShape::dpad, {}, dpad_up_arm};
    case PadButton::dpad_right:
        return {GlyphShape::dpad, {}, dpad_right_arm};
    case PadButton::dpad_down:
        return {GlyphShape::dpad, {}, dpad_down_arm};
    case PadButton::dpad_left:
        return {GlyphShape::dpad, {}, dpad_left_arm};
    case PadButton::left_pad:
        return {GlyphShape::trackpad, {}, 0, Side::left, true};
    case PadButton::right_pad:
        return {GlyphShape::trackpad, {}, 0, Side::right, true};
    }
    return {};
}

} // namespace oa::ui::pad_controls
