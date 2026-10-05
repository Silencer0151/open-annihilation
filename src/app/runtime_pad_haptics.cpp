// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The gamepad's feel: a trackpad pulse where the driver takes it, else a
// rumble (docs/controllers.md). The Steam Deck plays a rumble's low motor
// on its left trackpad and its high motor on its right; an SDL whose Deck
// driver forwards the controller's own haptic report plays the trackpads'
// tick and click instead.
#include "oa/app/runtime.hpp"
#include "pad_state.hpp"
#include "oa/ui/pad_controls.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <cstddef>
#include <optional>
#include <tuple>
#include <utility>

namespace oa::app {
namespace {

namespace pc = oa::ui::pad_controls;

/// The bytes of the controller's haptic report SDL_SendGamepadEffect is given: the report
/// number, then the controller's 64-byte feature report.
constexpr std::size_t kHapticReportBytes = 65;
/// The report number, first byte of the report.
constexpr uint8_t kHapticReportNumber = 0;
/// The feature report's type for a trackpad haptic command.
constexpr uint8_t kHapticCommandType = 0xEA;
/// The command's length after the report's two-byte header: side, command, intensity, gain,
/// then the tone's and sweep's fields, which a tick and a click leave zero.
constexpr uint8_t kHapticCommandLength = 19;
/// Where the report number sits.
constexpr std::size_t kReportNumberByte = 0;
/// Where the feature report's type sits.
constexpr std::size_t kReportTypeByte = 1;
/// Where the command's length sits.
constexpr std::size_t kReportLengthByte = 2;
/// Where the trackpad the pulse plays on sits.
constexpr std::size_t kReportSideByte = 3;
/// Where the command (tick or click) sits.
constexpr std::size_t kReportCommandByte = 4;
/// Where the pulse's intensity sits.
constexpr std::size_t kReportIntensityByte = 5;
/// Where the pulse's gain in decibels sits.
constexpr std::size_t kReportGainByte = 6;
/// The side byte for the left trackpad.
constexpr uint8_t kSideLeft = 0x01;
/// The side byte for the right trackpad.
constexpr uint8_t kSideRight = 0x02;
/// The side byte for both trackpads.
constexpr uint8_t kSideBoth = 0x03;
/// The command byte for the trackpad's tick.
constexpr uint8_t kCommandTick = 1;
/// The command byte for the trackpad's click.
constexpr uint8_t kCommandClick = 2;
/// The intensity byte that leaves the controller's own intensity.
constexpr uint8_t kIntensityDefault = 0;

/// Returns a side mirrored for the left-handed roles.
///
/// @param side the right-handed role's side
/// @param left_handed whether the roles are mirrored
/// @return the physical side
pc::PadSide mirrored_side(pc::PadSide side, bool left_handed) noexcept {
    if (!left_handed || side == pc::PadSide::both)
        return side;
    return side == pc::PadSide::left ? pc::PadSide::right : pc::PadSide::left;
}

/// Builds the controller's haptic report for a trackpad pulse.
///
/// @param pulse the pulse
/// @param left_handed whether the roles are mirrored (the pointer on the left pad)
/// @return the report, SDL_SendGamepadEffect's data
std::array<uint8_t, kHapticReportBytes>
haptic_report(const pc::TrackpadPulse& pulse, bool left_handed) noexcept {
    std::array<uint8_t, kHapticReportBytes> report{};
    report[kReportNumberByte] = kHapticReportNumber;
    report[kReportTypeByte] = kHapticCommandType;
    report[kReportLengthByte] = kHapticCommandLength;
    switch (mirrored_side(pulse.side, left_handed)) {
    case pc::PadSide::left:
        report[kReportSideByte] = kSideLeft;
        break;
    case pc::PadSide::right:
        report[kReportSideByte] = kSideRight;
        break;
    case pc::PadSide::both:
        report[kReportSideByte] = kSideBoth;
        break;
    }
    report[kReportCommandByte] = pulse.kind == pc::PulseKind::click ? kCommandClick : kCommandTick;
    report[kReportIntensityByte] = kIntensityDefault;
    report[kReportGainByte] = static_cast<uint8_t>(pulse.gain_db);
    return report;
}

} // namespace

void Runtime::play_pad_feel(oa::ui::pad_controls::Feel feel) const {
    PadState* state = pad_.get();
    if (state == nullptr)
        return;
    // The pad in use, as active_pad finds it.
    OpenPad* pad = nullptr;
    for (auto& open : state->pads) {
        if (open.id == 0 || open.gamepad == nullptr)
            continue;
        if (open.id == state->active) {
            pad = &open;
            break;
        }
        if (pad == nullptr)
            pad = &open;
    }
    if (pad == nullptr)
        return;
    const auto chosen = pad_settings();
    if (chosen.haptics == pc::Haptics::off)
        return;
    // The Deck's own trackpad tick and click, where SDL's driver takes the
    // controller's haptic report; a refusal leaves rumble for the rest of
    // the run.
    if (pad->traits.type == pc::PadType::steam_deck &&
        pad->traits.trackpads >= pad_trackpad_count && pad->trackpad_pulses) {
        if (const auto pulse = pc::trackpad_pulse_for(feel, chosen.haptics)) {
            const auto report = haptic_report(*pulse, chosen.left_handed);
            if (SDL_SendGamepadEffect(pad->gamepad, report.data(), static_cast<int>(report.size())))
                return;
            pad->trackpad_pulses = false;
        }
    }
    const auto rumble = pc::rumble_for(feel, chosen.haptics);
    if (!rumble)
        return;
    // The low motor plays on the left pad and the high on the right, so the
    // left-handed roles swap them.
    uint16_t low = rumble->low;
    uint16_t high = rumble->high;
    if (chosen.left_handed)
        std::swap(low, high);
    std::ignore = SDL_RumbleGamepad(pad->gamepad, low, high, rumble->ms);
}

} // namespace oa::app
