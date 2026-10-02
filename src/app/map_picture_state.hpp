// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The map selection's picture (Runtime::MapPictureState): the minimap of the
// selected map's TNT as RGB, the part of it shown and where it is fitted in
// the MAPPIC gadget, and the picture handles the map modal holds.
// runtime_skirmish_host.cpp loads, fits and releases it; the frontend frame
// (runtime.cpp) draws it on the map selection screen.
#pragma once

#include "oa/app/runtime.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <vector>

namespace oa::app {

struct Runtime::MapPictureState {
    map_modal::PictureHandle handle{};  // the picture the map modal holds
    uintptr_t next_handle = 0;          // the last handle a load gave out
    std::vector<uint8_t> rgb;           // the minimap, 3 bytes a pixel
    std::array<uint8_t, 3> clear_rgb{}; // palette index 0 behind the fitted map
    std::size_t width = 0;              // the minimap's size in pixels
    std::size_t height = 0;
    std::size_t source_width = 0; // the part of the minimap shown
    std::size_t source_height = 0;
    int32_t destination_x = 0; // where it is drawn in MAPPIC, and how large
    int32_t destination_y = 0;
    int32_t destination_width = 0;
    int32_t destination_height = 0;
};

} // namespace oa::app
