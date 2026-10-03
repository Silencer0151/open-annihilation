// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/unit_labels.hpp"

#include <cstdint>
#include <cstdio>
#include <cstring>

namespace oa::ui::hud {
namespace {

/// Room for one looked-up word, its terminator included: a translation holds
/// at most 255 characters.
constexpr std::size_t kLabelWordBytes = 0x100;

const char* localized(Localize localize, void* user, const char* text) {
    if (localize == nullptr)
        return text;
    const char* found = localize(user, text);
    return found != nullptr ? found : text;
}

} // namespace

void format_kill_count(char* out, std::size_t size, uint16_t kills, Localize localize, void* user) {
    const char* noun_word = kills == 1 ? "kill" : "kills";
    if (kills < kVeteranKills) {
        std::snprintf(
            out, size, "%d %s", static_cast<int>(kills), localized(localize, user, noun_word)
        );
        return;
    }
    // The noun is copied before "Veteran" is looked up: a lookup may reuse
    // the text it returned for the noun.
    char noun[kLabelWordBytes];
    std::snprintf(noun, sizeof noun, "%s", localized(localize, user, noun_word));
    std::snprintf(
        out, size, "%d %s - %s", static_cast<int>(kills), noun, localized(localize, user, "Veteran")
    );
}

const char* shown_unit_caption(const char* caption, bool respell) noexcept {
    if (respell && caption != nullptr && std::strcmp(caption, kResurrectionFailedMisspelled) == 0)
        return kResurrectionFailed;
    return caption;
}

uint32_t veterancy_label_level(std::span<const uint16_t> thresholds, uint16_t kills) noexcept {
    // An upper bound over the ascending list, on the sign-extended count.
    const auto count = static_cast<uint32_t>(static_cast<int32_t>(static_cast<int16_t>(kills)));
    uint32_t level = 0;
    for (const uint16_t threshold : thresholds) {
        if (count < threshold)
            break;
        ++level;
    }
    return level;
}

void format_kill_count_at_level(
    char* out, std::size_t size, uint16_t kills, uint32_t level, Localize localize, void* user
) {
    if (level == 0) {
        const char* noun_word = kills == 1 ? "kill" : "kills";
        std::snprintf(
            out, size, "%d %s", static_cast<int>(kills), localized(localize, user, noun_word)
        );
        return;
    }
    std::snprintf(
        out,
        size,
        "%d %s - Vet%u",
        static_cast<int>(kills),
        localized(localize, user, "kills"),
        level
    );
}

} // namespace oa::ui::hud
