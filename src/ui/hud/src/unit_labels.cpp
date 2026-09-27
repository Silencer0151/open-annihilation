// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/hud/unit_labels.hpp"

#include <cstdint>
#include <cstdio>

namespace oa::ui::hud {
namespace {

const char* localized(Localize localize, void* user, const char* text) {
    if (localize == nullptr)
        return text;
    const char* found = localize(user, text);
    return found != nullptr ? found : text;
}

} // namespace

void format_kill_count(char* out, std::size_t size, uint16_t kills, Localize localize, void* user) {
    const char* plural = localized(localize, user, "kills");
    const char* singular = localized(localize, user, "kill");
    const char* noun = kills == 1 ? singular : plural;
    if (kills < kVeteranKills)
        std::snprintf(out, size, "%d %s", static_cast<int>(kills), noun);
    else
        std::snprintf(
            out,
            size,
            "%d %s - %s",
            static_cast<int>(kills),
            noun,
            localized(localize, user, "Veteran")
        );
}

} // namespace oa::ui::hud
