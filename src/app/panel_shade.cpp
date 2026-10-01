// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The darkening of the panel under a panel opened with
// ui::gui_input::panel_flag::shade_below, over an RGB frame.
#include "panel_shade.hpp"

#include "oa/present/raster.hpp"
#include "oa/present/surface.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace oa::app {

namespace {

uint32_t rgb_key(const uint8_t* rgb) {
    return static_cast<uint32_t>(rgb[0]) << 16 | static_cast<uint32_t>(rgb[1]) << 8 | rgb[2];
}

// A frame pixel's palette index: the lowest entry holding its colour, else
// the nearest by summed channel difference.
class PaletteIndex {
  public:

    explicit PaletteIndex(const oa::PaletteBytes& palette) : palette_(palette) {
        for (std::size_t entry = oa::palette_color_count; entry-- > 0;)
            index_of_[rgb_key(&palette_[entry * oa::palette_entry_bytes])] =
                static_cast<uint8_t>(entry);
    }

    uint8_t operator()(const uint8_t* rgb) {
        const auto key = rgb_key(rgb);
        if (const auto found = index_of_.find(key); found != index_of_.end())
            return found->second;
        int32_t best = std::numeric_limits<int32_t>::max();
        uint8_t nearest = 0;
        for (std::size_t entry = 0; entry < oa::palette_color_count; ++entry) {
            const auto* colour = &palette_[entry * oa::palette_entry_bytes];
            const int32_t distance = std::abs(colour[0] - rgb[0]) + std::abs(colour[1] - rgb[1]) +
                                     std::abs(colour[2] - rgb[2]);
            if (distance < best) {
                best = distance;
                nearest = static_cast<uint8_t>(entry);
            }
        }
        index_of_.emplace(key, nearest);
        return nearest;
    }

  private:

    const oa::PaletteBytes& palette_;
    std::unordered_map<uint32_t, uint8_t> index_of_;
};

} // namespace

void shade_panel_below(
    renderer::Surface& frame,
    oa::ui::display_layout::Rect panel,
    const oa::PaletteBytes& palette,
    oa::present::DisplayContext& display
) {
    const int32_t left = std::max(panel.x, 0);
    const int32_t top = std::max(panel.y, 0);
    const int32_t right = std::min(panel.x + panel.width, static_cast<int32_t>(frame.width));
    const int32_t bottom = std::min(panel.y + panel.height, static_cast<int32_t>(frame.height));
    if (left >= right || top >= bottom)
        return;
    const int32_t width = right - left;
    const int32_t height = bottom - top;
    auto area = oa::present::create_surface(width, height);
    PaletteIndex index_of(palette);
    const auto at = [&](int32_t x, int32_t y) {
        return &frame.rgb
                    [(static_cast<std::size_t>(y) * frame.width + static_cast<std::size_t>(x)) *
                     3U];
    };
    for (int32_t y = 0; y < height; ++y)
        for (int32_t x = 0; x < width; ++x)
            area.pixels[static_cast<std::size_t>(y) * width + x] = index_of(at(left + x, top + y));
    auto* previous = oa::present::display_context();
    oa::present::bind_display(&display);
    Rect32 whole{0, 0, width - 1, height - 1};
    const auto shaded = oa::present::shade_rect_level(&area.surface, &whole, kShadeBelowLevel);
    oa::present::bind_display(previous);
    if (shaded == 0)
        return;
    for (int32_t y = 0; y < height; ++y)
        for (int32_t x = 0; x < width; ++x) {
            const auto entry =
                static_cast<std::size_t>(area.pixels[static_cast<std::size_t>(y) * width + x]) *
                oa::palette_entry_bytes;
            std::memcpy(at(left + x, top + y), &palette[entry], 3);
        }
}

} // namespace oa::app
