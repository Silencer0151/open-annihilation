// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/world_renderer/world_display_modes.hpp"

#include <cstdint>

namespace oa::present::world_renderer {

int32_t sort_display_modes(DisplayMode* modes, int32_t count, int32_t minimum_height) noexcept {
    if (modes == nullptr)
        return 0;
    for (int32_t i = 0; i < count; ++i) {
        for (int32_t j = count - 1; j > i; --j) {
            const auto& a = modes[j];
            const auto& b = modes[i];
            if (a.width < b.width || (a.width == b.width && a.height < b.height)) {
                const auto t = modes[i];
                modes[i] = modes[j];
                modes[j] = t;
            }
        }
    }
    for (int32_t i = 0; i < count; ++i) {
        if (modes[i].width < minimum_mode_width || modes[i].height < minimum_height) {
            for (int32_t k = i; k < count - 1; ++k)
                modes[k] = modes[k + 1];
            --i;
            --count;
        }
    }
    return count;
}

bool cycle_display_mode(
    DisplayMode* modes,
    int32_t count,
    int32_t current_width,
    int32_t current_height,
    bool backwards,
    DisplayMode& chosen
) noexcept {
    count = sort_display_modes(modes, count);
    for (int32_t index = 0; index < count; ++index) {
        if (modes[index].width != current_width || modes[index].height != current_height)
            continue;
        auto next = index;
        if (backwards) {
            if (--next < 0)
                next = count - 1;
        } else if (++next >= count) {
            next = 0;
        }
        chosen = modes[next];
        return true;
    }
    return false;
}

} // namespace oa::present::world_renderer
