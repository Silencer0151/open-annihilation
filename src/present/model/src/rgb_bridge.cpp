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

// The slot a colour outside the palette is remembered in.
std::size_t nearest_slot_of(uint32_t rgb) noexcept {
    return static_cast<std::size_t>((rgb * 2654435761U) >> 16) & (bridge_nearest_slots - 1);
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

// The frame row an 8-bit row captures, as capture_tile picks it.
int32_t captured_row(const RgbBridge& bridge, int32_t y) noexcept {
    return std::clamp(
        rgb_y(bridge, y),
        std::max(bridge.area.y1, 0),
        std::min(bridge.area.y2, bridge.frame.height - 1)
    );
}

// The frame column an 8-bit column captures, as capture_tile picks it.
int32_t captured_column(const RgbBridge& bridge, int32_t x) noexcept {
    return std::clamp(
        rgb_x(bridge, x),
        std::max(bridge.area.x1, 0),
        std::min(bridge.area.x2, bridge.frame.width - 1)
    );
}

// The 8-bit row (or column) a frame row (or column) takes its pixel from
// when written back, as commit_tile maps it, kept within [first, last].
int32_t
written_from(int32_t origin, int32_t index, float scale, int32_t first, int32_t last) noexcept {
    return std::clamp(static_cast<int32_t>((index - origin) / scale), first, last);
}

// Whether `count` bytes all hold `value`.
bool all_bytes(const uint8_t* bytes, uint32_t count, uint8_t value) noexcept {
    constexpr uint64_t every_byte = 0x0101010101010101ULL;
    uint32_t i = 0;
    for (const uint64_t pattern = every_byte * value; i + sizeof(uint64_t) <= count;
         i += sizeof(uint64_t)) {
        uint64_t word = 0;
        std::memcpy(&word, bytes + i, sizeof(word));
        if (word != pattern)
            return false;
    }
    for (; i < count; ++i)
        if (bytes[i] != value)
            return false;
    return true;
}

// Sums, for each bridge pixel of a row of a sampled region, the palette
// colours of its drawn samples and counts them: four words a pixel, red,
// green, blue and the count. Returns whether any sample was drawn.
bool sum_drawn_samples(
    const RgbBridge& bridge, const SampledRegion& sampled, int32_t row, std::vector<uint32_t>& sums
) {
    const auto factor = static_cast<int32_t>(sampled.factor);
    const int32_t columns = sampled.region.x2 - sampled.region.x1 + 1;
    const int32_t width = sampled.surface.width;
    const int32_t offset = row - sampled.region.y1;
    const uint8_t* baseline =
        sampled.baseline.data() + static_cast<std::ptrdiff_t>(offset) * columns;
    const uint8_t* captured =
        sampled.captured_rows.data() + static_cast<std::ptrdiff_t>(offset) * width;
    std::fill(sums.begin(), sums.end(), 0);
    bool drawn = false;
    for (int32_t line = 0; line < factor; ++line) {
        const uint8_t* samples =
            sampled.surface.pixels +
            static_cast<std::ptrdiff_t>(offset * factor + line) * sampled.surface.pitch;
        if (std::memcmp(samples, captured, static_cast<std::size_t>(width)) == 0)
            continue;
        drawn = true;
        for (int32_t column = 0; column < columns; ++column) {
            const uint8_t under = baseline[column];
            const uint8_t* cell = samples + static_cast<std::ptrdiff_t>(column) * factor;
            if (all_bytes(cell, sampled.factor, under))
                continue;
            uint32_t* sum = sums.data() + static_cast<std::ptrdiff_t>(column) * 4;
            if (all_bytes(cell, sampled.factor, cell[0])) {
                const PaletteEntry& entry = bridge.palette.entries[cell[0]];
                sum[0] += entry.r * sampled.factor;
                sum[1] += entry.g * sampled.factor;
                sum[2] += entry.b * sampled.factor;
                sum[3] += sampled.factor;
                continue;
            }
            for (int32_t i = 0; i < factor; ++i) {
                const uint8_t value = cell[i];
                if (value == under)
                    continue;
                const PaletteEntry& entry = bridge.palette.entries[value];
                sum[0] += entry.r;
                sum[1] += entry.g;
                sum[2] += entry.b;
                ++sum[3];
            }
        }
    }
    return drawn;
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
    const std::size_t remembered = nearest_slot_of(rgb);
    if (bridge.nearest_keys[remembered] == (rgb | slot_used))
        return bridge.nearest_values[remembered];
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
    bridge.nearest_keys[remembered] = rgb | slot_used;
    bridge.nearest_values[remembered] = best;
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
        bridge.nearest_keys.assign(bridge_nearest_slots, 0);
        bridge.nearest_values.assign(bridge_nearest_slots, 0);
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

void bridge_open_sampled(
    RgbBridge& bridge, SampledRegion& sampled, const Rect32& region, uint32_t factor
) {
    sampled.factor = factor == 0 ? 1 : factor;
    const Rect32 clip{
        std::max(region.x1, 0),
        std::max(region.y1, 0),
        std::min(region.x2, bridge.surface.width - 1),
        std::min(region.y2, bridge.surface.height - 1)
    };
    if (clip.x1 > clip.x2 || clip.y1 > clip.y2) {
        sampled.region = {0, 0, -1, -1};
        sampled.samples.resize(std::max<std::size_t>(sampled.samples.size(), 1));
        present::init_surface(sampled.surface, 0, 0, 0, sampled.samples.data());
        return;
    }
    sampled.region = clip;
    const auto factor_rows = static_cast<int32_t>(sampled.factor);
    const int32_t columns = clip.x2 - clip.x1 + 1;
    const int32_t rows = clip.y2 - clip.y1 + 1;
    const int32_t width = columns * factor_rows;
    const int32_t height = rows * factor_rows;
    const auto size = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    if (sampled.samples.size() < size + 1)
        sampled.samples.resize(size + 1);
    sampled.baseline.resize(static_cast<std::size_t>(columns) * static_cast<std::size_t>(rows));
    sampled.captured_rows.resize(static_cast<std::size_t>(width) * static_cast<std::size_t>(rows));
    present::init_surface(sampled.surface, width, height, width, sampled.samples.data());
    thread_local std::vector<std::ptrdiff_t> offsets;
    offsets.resize(static_cast<std::size_t>(columns));
    for (int32_t column = 0; column < columns; ++column)
        offsets[static_cast<std::size_t>(column)] =
            static_cast<std::ptrdiff_t>(captured_column(bridge, clip.x1 + column)) * 3;
    for (int32_t row = 0; row < rows; ++row) {
        uint8_t* first =
            sampled.samples.data() + static_cast<std::ptrdiff_t>(row) * factor_rows * width;
        uint8_t* baseline = sampled.baseline.data() + static_cast<std::ptrdiff_t>(row) * columns;
        const uint8_t* frame_row =
            bridge.frame.rgb +
            static_cast<std::ptrdiff_t>(captured_row(bridge, clip.y1 + row)) * bridge.frame.stride;
        for (int32_t column = 0; column < columns; ++column) {
            const uint8_t* pixel = frame_row + offsets[static_cast<std::size_t>(column)];
            const uint8_t index = bridge_index(bridge, pixel[0], pixel[1], pixel[2]);
            baseline[column] = index;
            std::memset(
                first + static_cast<std::ptrdiff_t>(column) * factor_rows, index, sampled.factor
            );
        }
        for (int32_t line = 1; line < factor_rows; ++line)
            std::memcpy(
                first + static_cast<std::ptrdiff_t>(line) * width,
                first,
                static_cast<std::size_t>(width)
            );
        std::memcpy(
            sampled.captured_rows.data() + static_cast<std::ptrdiff_t>(row) * width,
            first,
            static_cast<std::size_t>(width)
        );
    }
}

void bridge_end_sampled(const RgbBridge& bridge, const SampledRegion& sampled) {
    const Rect32& region = sampled.region;
    if (region.x1 > region.x2 || region.y1 > region.y2)
        return;
    const uint32_t all = sampled.factor * sampled.factor;
    const int32_t top =
        std::max({rgb_start(bridge.area.y1, region.y1, bridge.scale), bridge.area.y1, 0});
    const int32_t bottom = std::min(
        {rgb_start(bridge.area.y1, region.y2 + 1, bridge.scale) - 1,
         bridge.area.y2,
         bridge.frame.height - 1}
    );
    const int32_t left =
        std::max({rgb_start(bridge.area.x1, region.x1, bridge.scale), bridge.area.x1, 0});
    const int32_t right = std::min(
        {rgb_start(bridge.area.x1, region.x2 + 1, bridge.scale) - 1,
         bridge.area.x2,
         bridge.frame.width - 1}
    );
    if (top > bottom || left > right)
        return;
    thread_local std::vector<int32_t> columns;
    thread_local std::vector<uint32_t> sums;
    columns.resize(static_cast<std::size_t>(right - left + 1));
    for (int32_t x = left; x <= right; ++x)
        columns[static_cast<std::size_t>(x - left)] =
            written_from(bridge.area.x1, x, bridge.scale, region.x1, region.x2) - region.x1;
    sums.resize(static_cast<std::size_t>(region.x2 - region.x1 + 1) * 4);
    int32_t summed = region.y1 - 1;
    bool drawn = false;
    for (int32_t y = top; y <= bottom; ++y) {
        const int32_t row = written_from(bridge.area.y1, y, bridge.scale, region.y1, region.y2);
        if (row != summed) {
            drawn = sum_drawn_samples(bridge, sampled, row, sums);
            summed = row;
        }
        if (!drawn)
            continue;
        uint8_t* pixel = bridge.frame.rgb + static_cast<std::ptrdiff_t>(y) * bridge.frame.stride +
                         static_cast<std::ptrdiff_t>(left) * 3;
        for (int32_t x = left; x <= right; ++x, pixel += 3) {
            const uint32_t* sum =
                sums.data() +
                static_cast<std::ptrdiff_t>(columns[static_cast<std::size_t>(x - left)]) * 4;
            const uint32_t count = sum[3];
            if (count == 0)
                continue;
            const uint32_t under = all - count;
            for (int32_t channel = 0; channel < 3; ++channel)
                pixel[channel] =
                    static_cast<uint8_t>((sum[channel] + under * pixel[channel] + all / 2) / all);
        }
    }
}

} // namespace oa::present::model
