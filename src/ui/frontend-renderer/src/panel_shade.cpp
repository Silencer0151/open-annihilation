// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The darkening of the panel under a panel opened with
// gui_input::panel_flag::shade_below, over an RGB image.
#include "oa/ui/frontend_renderer.hpp"

#include "oa/present/display.hpp"
#include "oa/present/raster.hpp"
#include "oa/present/surface.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <unordered_map>

namespace oa::ui::frontend_renderer {

namespace {

uint32_t rgb_key(const uint8_t* rgb) {
    return static_cast<uint32_t>(rgb[0]) << 16 | static_cast<uint32_t>(rgb[1]) << 8 | rgb[2];
}

// An image pixel's palette index: the lowest entry holding its colour, else
// the nearest by summed channel difference.
class PaletteIndex {
  public:

    explicit PaletteIndex(const PaletteBytes& palette) : palette_(palette) {
        for (std::size_t entry = palette_color_count; entry-- > 0;)
            index_of_[rgb_key(&palette_[entry * palette_entry_bytes])] =
                static_cast<uint8_t>(entry);
    }

    uint8_t operator()(const uint8_t* rgb) {
        const auto key = rgb_key(rgb);
        if (const auto found = index_of_.find(key); found != index_of_.end())
            return found->second;
        int32_t best = std::numeric_limits<int32_t>::max();
        uint8_t nearest = 0;
        for (std::size_t entry = 0; entry < palette_color_count; ++entry) {
            const auto* colour = &palette_[entry * palette_entry_bytes];
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

    const PaletteBytes& palette_;
    std::unordered_map<uint32_t, uint8_t> index_of_;
};

} // namespace

void shade_panel_below(
    Surface& surface,
    int x,
    int y,
    int width,
    int height,
    const PaletteBytes& palette,
    const uint8_t* shade_table,
    const uint8_t* keep
) {
    if (shade_table == nullptr ||
        surface.rgb.size() != static_cast<std::size_t>(surface.width) * surface.height * 3U)
        return;
    const int left = std::max(x, 0);
    const int top = std::max(y, 0);
    const int right =
        static_cast<int>(std::min<int64_t>(int64_t{x} + std::max(width, 0), surface.width));
    const int bottom =
        static_cast<int>(std::min<int64_t>(int64_t{y} + std::max(height, 0), surface.height));
    if (left >= right || top >= bottom)
        return;
    const int area_width = right - left;
    const int area_height = bottom - top;
    const auto at = [&](int column, int row) {
        return &surface.rgb
                    [(static_cast<std::size_t>(row) * surface.width +
                      static_cast<std::size_t>(column)) *
                     3U];
    };
    auto area = present::create_surface(area_width, area_height);
    PaletteIndex index_of(palette);
    for (int row = 0; row < area_height; ++row)
        for (int column = 0; column < area_width; ++column)
            area.pixels[static_cast<std::size_t>(row) * area_width + column] =
                index_of(at(left + column, top + row));
    present::DisplayContext display;
    display.shade_table = const_cast<uint8_t*>(shade_table);
    display.width = area_width;
    display.height = area_height;
    auto* previous = present::display_context();
    present::bind_display(&display);
    Rect32 whole{0, 0, area_width - 1, area_height - 1};
    const auto shaded = present::shade_rect_level(&area.surface, &whole, shade_below_level);
    present::bind_display(previous);
    if (shaded == 0)
        return;
    for (int row = 0; row < area_height; ++row)
        for (int column = 0; column < area_width; ++column) {
            auto* out = at(left + column, top + row);
            if (keep != nullptr && rgb_key(out) == rgb_key(keep))
                continue;
            const auto entry = static_cast<std::size_t>(
                                   area.pixels[static_cast<std::size_t>(row) * area_width + column]
                               ) *
                               palette_entry_bytes;
            std::memcpy(out, &palette[entry], 3);
        }
}

} // namespace oa::ui::frontend_renderer
