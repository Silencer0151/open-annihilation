// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/sim/profile.hpp"

namespace oa::sim::profile {

void accumulate(ProfileTimes& times, uint32_t now, Category category) noexcept {
    auto& slot = times.pending[static_cast<int32_t>(category)];
    slot = static_cast<int32_t>(static_cast<uint32_t>(slot) + (now - times.sampled_at));
    times.sampled_at = now;
}

void begin_window(ProfileTimes& times, uint32_t now) noexcept {
    uint32_t total = 0;
    for (int32_t i = 0; i < category_count; ++i) {
        total += static_cast<uint32_t>(times.pending[i]);
        times.shown[i] = times.pending[i];
        times.pending[i] = 0;
    }
    times.shown_total = static_cast<int32_t>(total) < 1 ? 1 : static_cast<int32_t>(total);
    times.sampled_at = now;
}

} // namespace oa::sim::profile
