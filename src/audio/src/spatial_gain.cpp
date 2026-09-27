// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/audio/spatial_gain.hpp"

#include <algorithm>
#include <cmath>

namespace oa::audio {

StereoGain spatial_stereo_gain(const Spatial& spatial) noexcept {
    if (spatial.mode != SpatialMode::normal)
        return {};
    const float distance =
        std::sqrt(spatial.x * spatial.x + spatial.y * spatial.y + spatial.z * spatial.z);
    const float heard = std::min(distance, spatial.max_distance);
    const float level = spatial.min_distance > 0.0F && heard > spatial.min_distance
                            ? spatial.min_distance / heard
                            : 1.0F;
    const float bearing = distance > 0.0F ? spatial.x / distance : 0.0F;
    return {
        level * (bearing > 0.0F ? 1.0F - bearing : 1.0F),
        level * (bearing < 0.0F ? 1.0F + bearing : 1.0F)
    };
}

} // namespace oa::audio
