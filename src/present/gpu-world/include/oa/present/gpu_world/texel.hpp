// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// What every page of the card-drawn world shares: the texel the terrain
// atlas and the sprite pages are both made of.

#include <cstdint>

namespace oa::present::gpu_world {

/// Bytes of one page texel: red, green, blue and alpha, in that order in
/// memory.
inline constexpr uint32_t texel_bytes = 4;

} // namespace oa::present::gpu_world
