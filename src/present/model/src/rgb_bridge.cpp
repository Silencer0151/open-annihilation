// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// 8-bit drawing over an RGB frame.
#include "oa/present/model/rgb_bridge.hpp"

#include "oa/present/surface.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

namespace oa::present::model {
namespace {

constexpr std::size_t exact_slots = 1024; // power of two, > 2 * palette size
constexpr uint32_t slot_used = 0x1000000U;

uint32_t pack(uint8_t r, uint8_t g, uint8_t b) noexcept {
    return (static_cast<uint32_t>(r) << 16) | (static_cast<uint32_t>(g) << 8) | b;
}

std::size_t slot_of(uint32_t rgb) noexcept {
    return static_cast<std::size_t>((rgb * 2654435761U) >> 22) & (exact_slots - 1);
}

int32_t rgb_x(const RgbBridge& bridge, int32_t x) noexcept {
    return bridge.area.x1 +
           static_cast<int32_t>(std::floor((static_cast<double>(x) + 0.5) * bridge.scale));
}

int32_t rgb_y(const RgbBridge& bridge, int32_t y) noexcept {
    return bridge.area.y1 +
           static_cast<int32_t>(std::floor((static_cast<double>(y) + 0.5) * bridge.scale));
}

// First RGB column (or row) whose 8-bit source is at or after `index`.
int32_t rgb_start(int32_t origin, int32_t index, float scale) noexcept {
    return origin + static_cast<int32_t>(std::ceil(static_cast<double>(index) * scale));
}

void capture_tile(RgbBridge& bridge, int32_t tx, int32_t ty) {
    const int32_t x0 = tx * bridge_tile_side;
    const int32_t y0 = ty * bridge_tile_side;
    const int32_t x1 = std::min(x0 + bridge_tile_side, bridge.surface.width);
    const int32_t y1 = std::min(y0 + bridge_tile_side, bridge.surface.height);
    for (int32_t y = y0; y < y1; ++y) {
        const int32_t sy = std::clamp(
            rgb_y(bridge, y),
            std::max(bridge.area.y1, 0),
            std::min(bridge.area.y2, bridge.frame.height - 1)
        );
        const uint8_t* row =
            bridge.frame.rgb + static_cast<std::ptrdiff_t>(sy) * bridge.frame.stride;
        uint8_t* out = bridge.pixels.data() + static_cast<std::ptrdiff_t>(y) * bridge.surface.pitch;
        for (int32_t x = x0; x < x1; ++x) {
            const int32_t sx = std::clamp(
                rgb_x(bridge, x),
                std::max(bridge.area.x1, 0),
                std::min(bridge.area.x2, bridge.frame.width - 1)
            );
            const uint8_t* pixel = row + static_cast<std::ptrdiff_t>(sx) * 3;
            out[x] = bridge_index(bridge, pixel[0], pixel[1], pixel[2]);
        }
        std::memcpy(
            bridge.baseline.data() + static_cast<std::ptrdiff_t>(y) * bridge.surface.pitch + x0,
            out + x0,
            static_cast<std::size_t>(x1 - x0)
        );
    }
}

void commit_tile(RgbBridge& bridge, int32_t tx, int32_t ty) {
    const int32_t x0 = tx * bridge_tile_side;
    const int32_t y0 = ty * bridge_tile_side;
    const int32_t x1 = std::min(x0 + bridge_tile_side, bridge.surface.width);
    const int32_t y1 = std::min(y0 + bridge_tile_side, bridge.surface.height);
    const int32_t top = std::max({rgb_start(bridge.area.y1, y0, bridge.scale), bridge.area.y1, 0});
    const int32_t bottom = std::min(
        {rgb_start(bridge.area.y1, y1, bridge.scale) - 1, bridge.area.y2, bridge.frame.height - 1}
    );
    const int32_t left = std::max({rgb_start(bridge.area.x1, x0, bridge.scale), bridge.area.x1, 0});
    const int32_t right = std::min(
        {rgb_start(bridge.area.x1, x1, bridge.scale) - 1, bridge.area.x2, bridge.frame.width - 1}
    );
    for (int32_t y = top; y <= bottom; ++y) {
        const int32_t sy =
            std::min(static_cast<int32_t>((y - bridge.area.y1) / bridge.scale), y1 - 1);
        const auto offset = static_cast<std::ptrdiff_t>(sy) * bridge.surface.pitch;
        uint8_t* row = bridge.frame.rgb + static_cast<std::ptrdiff_t>(y) * bridge.frame.stride;
        for (int32_t x = left; x <= right; ++x) {
            const int32_t sx =
                std::min(static_cast<int32_t>((x - bridge.area.x1) / bridge.scale), x1 - 1);
            const uint8_t value = bridge.pixels[static_cast<std::size_t>(offset + sx)];
            if (value == bridge.baseline[static_cast<std::size_t>(offset + sx)])
                continue;
            const PaletteEntry& entry = bridge.palette.entries[value];
            uint8_t* pixel = row + static_cast<std::ptrdiff_t>(x) * 3;
            pixel[0] = entry.r;
            pixel[1] = entry.g;
            pixel[2] = entry.b;
        }
    }
}

} // namespace

uint8_t bridge_index(RgbBridge& bridge, uint8_t r, uint8_t g, uint8_t b) {
    const uint32_t rgb = pack(r, g, b);
    for (std::size_t slot = slot_of(rgb);; slot = (slot + 1) & (exact_slots - 1)) {
        const uint32_t key = bridge.exact_keys[slot];
        if (key == (rgb | slot_used))
            return bridge.exact_values[slot];
        if (key == 0)
            break;
    }
    if (const auto found = bridge.nearest.find(rgb); found != bridge.nearest.end())
        return found->second;
    uint8_t best = 0;
    int32_t best_distance = INT32_MAX;
    for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
        const PaletteEntry& entry = bridge.palette.entries[index];
        const int32_t dr = entry.r - r;
        const int32_t dg = entry.g - g;
        const int32_t db = entry.b - b;
        const int32_t distance = dr * dr + dg * dg + db * db;
        if (distance < best_distance) {
            best_distance = distance;
            best = static_cast<uint8_t>(index);
        }
    }
    bridge.nearest.emplace(rgb, best);
    return best;
}

void bridge_begin(
    RgbBridge& bridge,
    const RgbFrame& frame,
    const Rect32& area,
    float scale,
    const Palette& palette
) {
    bridge.frame = frame;
    bridge.area = area;
    bridge.scale = scale > 0.0F ? scale : 1.0F;
    const bool palette_changed = std::memcmp(&bridge.palette, &palette, sizeof(Palette)) != 0;
    if (palette_changed || bridge.exact_keys.empty()) {
        bridge.palette = palette;
        bridge.exact_keys.assign(exact_slots, 0);
        bridge.exact_values.assign(exact_slots, 0);
        bridge.nearest.clear();
        for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
            const PaletteEntry& entry = palette.entries[index];
            const uint32_t rgb = pack(entry.r, entry.g, entry.b);
            std::size_t slot = slot_of(rgb);
            while (bridge.exact_keys[slot] != 0 && bridge.exact_keys[slot] != (rgb | slot_used))
                slot = (slot + 1) & (exact_slots - 1);
            if (bridge.exact_keys[slot] == 0) {
                bridge.exact_keys[slot] = rgb | slot_used;
                bridge.exact_values[slot] = static_cast<uint8_t>(index);
            }
        }
    }
    const int32_t width =
        std::max(0, static_cast<int32_t>(std::ceil((area.x2 - area.x1 + 1) / bridge.scale)));
    const int32_t height =
        std::max(0, static_cast<int32_t>(std::ceil((area.y2 - area.y1 + 1) / bridge.scale)));
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (bridge.pixels.size() < size + 1) {
        bridge.pixels.resize(size + 1);
        bridge.baseline.resize(size + 1);
    }
    present::init_surface(bridge.surface, width, height, width, bridge.pixels.data());
    bridge.tiles_x = (width + bridge_tile_side - 1) / bridge_tile_side;
    bridge.tiles_y = (height + bridge_tile_side - 1) / bridge_tile_side;
    bridge.captured.assign(static_cast<std::size_t>(bridge.tiles_x) * bridge.tiles_y, 0);
}

void bridge_open(RgbBridge& bridge, const Rect32& region) {
    const int32_t left = region.x1;
    const int32_t top = region.y1;
    const int32_t right = region.x2;
    const int32_t bottom = region.y2;
    Rect32 clip{
        std::max(left, 0),
        std::max(top, 0),
        std::min(right, bridge.surface.width - 1),
        std::min(bottom, bridge.surface.height - 1)
    };
    if (clip.x1 > clip.x2 || clip.y1 > clip.y2) {
        // An empty clip: the draw routines reject everything.
        present::set_surface_clip(bridge.surface, {0, 0, -1, -1});
        return;
    }
    for (int32_t ty = clip.y1 / bridge_tile_side; ty <= clip.y2 / bridge_tile_side; ++ty) {
        for (int32_t tx = clip.x1 / bridge_tile_side; tx <= clip.x2 / bridge_tile_side; ++tx) {
            auto& state = bridge.captured[static_cast<std::size_t>(ty) * bridge.tiles_x + tx];
            if (state == 0) {
                capture_tile(bridge, tx, ty);
                state = 1;
            }
        }
    }
    present::set_surface_clip(bridge.surface, clip);
}

void bridge_end(RgbBridge& bridge) {
    for (int32_t ty = 0; ty < bridge.tiles_y; ++ty)
        for (int32_t tx = 0; tx < bridge.tiles_x; ++tx)
            if (bridge.captured[static_cast<std::size_t>(ty) * bridge.tiles_x + tx] != 0)
                commit_tile(bridge, tx, ty);
    std::fill(bridge.captured.begin(), bridge.captured.end(), 0);
}

} // namespace oa::present::model
