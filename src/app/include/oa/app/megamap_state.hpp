// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the running match keeps for ui.megamap: its icons and their unit
// sets, the terrain picture fitted to the battlefield, the features the map
// placed, and the pointer's press on it.
#pragma once

#include "oa/data/defs/categories.hpp"
#include "oa/ui/hud/megamap.hpp"

#include <array>
#include <cstdint>
#include <vector>

namespace oa::app {

/// The largest icon drawn, in pixels; a larger picture is cut to it.
inline constexpr int32_t kMegamapIconLimit = 22;

/// One megamap icon: its palette indices, rows `width` apart.
struct MegamapIcon {
    int32_t width{};
    int32_t height{};
    std::vector<uint8_t> pixels;
};

/// One icon entry and the unit types it stands for.
struct MegamapIconEntry {
    MegamapIcon icon;
    const oa::data::defs::CategoryMask* types{}; ///< a category's mask, or one of `own_masks`
};

/// A feature the map placed, for the terrain picture's blobs.
struct MegamapFeature {
    int32_t cell_x{};
    int32_t cell_z{};
    int32_t width{}; ///< footprint in cells
    int32_t height{};
    uint8_t color{};
};

struct MegamapState {
    bool prepared{}; ///< icons read and features noted for this match
    oa::ui::hud::IconConfig config{};
    std::vector<MegamapIconEntry> entries; ///< side commanders' icons, then the lines'
    MegamapIcon unknown;
    MegamapIcon nothing;
    /// Unit sets made here (side commanders, the built-in icons' sets).
    std::vector<oa::data::defs::CategoryMaskStorage> own_words;
    std::vector<oa::data::defs::CategoryMask> own_masks;
    std::vector<MegamapFeature> features;
    /// The terrain picture and the layout it was made for.
    oa::ui::hud::MegamapLayout layout{};
    std::vector<uint8_t> terrain;
    /// The left button's press, in battlefield pixels, while it is held.
    bool pressed{};
    int32_t press_x{};
    int32_t press_y{};
    int32_t pointer_x{};
    int32_t pointer_y{};
    uint16_t hovered{}; ///< the unit whose icon is under the pointer
};

} // namespace oa::app
