// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/audio/mixer.hpp"

namespace oa::audio {

struct StereoGain {
    float left{1.0F};
    float right{1.0F};
};

/// Computes the stereo gains of a placed voice.
///
/// Attenuates and pans as the 3D buffer a placed voice plays through, heard
/// by the default listener (at the origin, facing +z, +y up, rolloff 1): full level
/// within the minimum distance, min/d beyond it, held from the maximum
/// distance on; the channel away from the source fades with the sine of its
/// bearing. A voice with 3D processing off plays at full level on both sides.
///
/// @param spatial Placement of the voice, as returned by voice_spatial().
/// @return Linear gains for the left and right channels, 0..1.
[[nodiscard]] StereoGain spatial_stereo_gain(const Spatial& spatial) noexcept;

} // namespace oa::audio
