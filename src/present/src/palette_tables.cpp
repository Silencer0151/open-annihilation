// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/palette_tables.hpp"
#include "oa/base/game_math.hpp"

#include <cmath>
#include <cstdint>
#include <cstring>
#include <new>

namespace oa::present {
using base::game_math::truncate_low32;
using base::game_math::truncate_to_int64;

namespace {

constexpr int32_t table_row = 256;
// Per-row scale steps of the shade and light tables.
constexpr double shade_row_step = 0.06875;
constexpr double light_row_step = 0.03333333333333333;
constexpr double light_base_scale = 1.0;
constexpr uint16_t shade_channel_limit = 0xFF;
constexpr int32_t light_channel_limit = 0xFF;
constexpr uint8_t blue_lift = 0x32;
constexpr int32_t blue_limit_bias = 0x3C;
constexpr uint8_t channel_max = 0xFF;
constexpr int64_t sprite_max_pixels = 16 * 1024 * 1024;
constexpr int32_t lens_max_width = 0x7FFF; // the block header stores 2 * width in 16 bits
constexpr int32_t lens_offset_bytes = 2;
constexpr int32_t lens_radius_divisor = 4;

bool load_table(uint8_t*& slot, int32_t size) noexcept {
    delete[] slot;
    slot = new (std::nothrow) uint8_t[static_cast<std::size_t>(size)]();
    return slot != nullptr;
}

void free_table(uint8_t*& slot) noexcept {
    delete[] slot;
    slot = nullptr;
}

// The shade channel: the truncated product kept when its low word is at most 0xFF.
uint8_t shade_channel(uint8_t channel, double scale) noexcept {
    const auto value =
        static_cast<uint16_t>(truncate_to_int64(static_cast<double>(channel) * scale));
    return value <= shade_channel_limit ? static_cast<uint8_t>(value) : channel_max;
}

uint8_t light_channel(uint8_t channel, double scale) noexcept {
    const int32_t value = truncate_low32(static_cast<double>(channel) * scale);
    return value > light_channel_limit ? channel_max : static_cast<uint8_t>(value);
}

TableSource load_session_table(
    uint8_t* (*build)(DisplayContext&, const Palette&) noexcept,
    void (*install)(DisplayContext&, const uint8_t*) noexcept,
    DisplayContext& display,
    const Palette& palette,
    std::span<const uint8_t> file,
    int32_t size
) noexcept {
    if (file.empty()) {
        build(display, palette);
        return TableSource::built;
    }
    if (file.size() != static_cast<std::size_t>(size)) {
        build(display, palette);
        return TableSource::rejected;
    }
    install(display, file.data());
    return TableSource::file;
}

void store_u16(void* data, int32_t index, uint16_t value) noexcept {
    std::memcpy(
        static_cast<uint8_t*>(data) + static_cast<std::ptrdiff_t>(index) * lens_offset_bytes,
        &value,
        sizeof value
    );
}

} // namespace

uint8_t gamma_channel(uint8_t channel, float gamma) noexcept {
    // The product of an 8-bit integer and a float is exact in double.
    const double scaled = static_cast<double>(channel) * static_cast<double>(gamma);
    if (scaled > static_cast<double>(gamma_channel_max))
        return gamma_channel_max;
    return static_cast<uint8_t>(static_cast<uint64_t>(truncate_to_int64(scaled)));
}

bool apply_palette_entries(
    DisplayContext& display,
    const PaletteEntry* entries,
    int32_t start,
    int32_t count,
    Palette& device
) noexcept {
    if (count <= 0)
        return true;
    if (start < 0 || start > OA_PALETTE_COLORS - count)
        return false;
    for (int32_t i = 0; i < count; ++i)
        display.palette.entries[start + i] = entries[i];
    for (int32_t i = start; i < start + count; ++i) {
        device.entries[i].r = gamma_channel(entries[i].r, display.gamma);
        device.entries[i].g = gamma_channel(entries[i].g, display.gamma);
        device.entries[i].b = gamma_channel(entries[i].b, display.gamma);
        device.entries[i].flags = 0;
    }
    return true;
}

void set_palette_gamma(DisplayContext& display, float gamma, Palette& device) noexcept {
    display.gamma = gamma;
    apply_palette_entries(display, display.palette.entries, 0, OA_PALETTE_COLORS, device);
}

bool set_display_palette(const DisplayContext& display) noexcept {
    return display.sink.present != nullptr;
}

bool load_alpha_table(DisplayContext& display) noexcept {
    return load_table(display.alpha_table, alpha_table_size);
}

void free_alpha_table(DisplayContext& display) noexcept {
    free_table(display.alpha_table);
}

bool load_shade_table(DisplayContext& display) noexcept {
    return load_table(display.shade_table, shade_table_size);
}

void free_shade_table(DisplayContext& display) noexcept {
    free_table(display.shade_table);
}

bool load_light_table(DisplayContext& display) noexcept {
    return load_table(display.light_table, light_table_size);
}

void free_light_table(DisplayContext& display) noexcept {
    free_table(display.light_table);
}

bool load_gray_table(DisplayContext& display) noexcept {
    return load_table(display.gray_table, gray_table_size);
}

void free_gray_table(DisplayContext& display) noexcept {
    free_table(display.gray_table);
}

bool load_blue_table(DisplayContext& display) noexcept {
    return load_table(display.blue_table, blue_table_size);
}

void free_blue_table(DisplayContext& display) noexcept {
    free_table(display.blue_table);
}

void sort_palette_by_brightness(
    const Palette& palette, int32_t brightness[OA_PALETTE_COLORS], uint8_t order[OA_PALETTE_COLORS]
) noexcept {
    for (int32_t i = 0; i < OA_PALETTE_COLORS; ++i) {
        const PaletteEntry& entry = palette.entries[i];
        brightness[i] = entry.r + entry.g + entry.b;
        order[i] = static_cast<uint8_t>(i);
    }
    for (int32_t i = 0; i < OA_PALETTE_COLORS; ++i) {
        for (int32_t j = i + 1; j < OA_PALETTE_COLORS; ++j) {
            if (brightness[j] < brightness[i]) {
                const int32_t weight = brightness[i];
                brightness[i] = brightness[j];
                brightness[j] = weight;
                const uint8_t index = order[i];
                order[i] = order[j];
                order[j] = index;
            }
        }
    }
}

uint8_t find_nearest_sorted_color(
    const Palette& palette,
    const int32_t brightness[OA_PALETTE_COLORS],
    const uint8_t order[OA_PALETTE_COLORS],
    uint8_t r,
    uint8_t g,
    uint8_t b
) noexcept {
    const int32_t target = r + g + b;
    int32_t best = nearest_color_no_match;
    uint8_t best_position = 0;
    int32_t position = 0;
    for (; position < OA_PALETTE_COLORS; ++position) {
        if (brightness[position] < target - nearest_color_window)
            continue;
        if (brightness[position] > target + nearest_color_window)
            break;
        const PaletteEntry& entry = palette.entries[order[position]];
        const int32_t dr = entry.r - r;
        const int32_t dg = entry.g - g;
        const int32_t db = entry.b - b;
        const int32_t distance = dr * dr + dg * dg + db * db;
        if (distance < best) {
            best = distance;
            best_position = static_cast<uint8_t>(position);
        }
    }
    if (best == nearest_color_no_match)
        best_position = static_cast<uint8_t>(position);
    return order[best_position];
}

uint8_t* build_alpha_table(DisplayContext& display, const Palette& palette) noexcept {
    if ((display.flags & display_flag_alpha_table) == 0 || display.alpha_table == nullptr)
        return nullptr;
    int32_t brightness[OA_PALETTE_COLORS];
    uint8_t order[OA_PALETTE_COLORS];
    sort_palette_by_brightness(palette, brightness, order);
    for (int32_t a = 0; a < OA_PALETTE_COLORS; ++a) {
        const PaletteEntry& first = palette.entries[a];
        uint8_t* row = display.alpha_table + a * table_row;
        for (int32_t b = 0; b < OA_PALETTE_COLORS; ++b) {
            if (a == b) {
                row[b] = static_cast<uint8_t>(a);
                continue;
            }
            const PaletteEntry& second = palette.entries[b];
            row[b] = find_nearest_sorted_color(
                palette,
                brightness,
                order,
                static_cast<uint8_t>((first.r + second.r) / 2),
                static_cast<uint8_t>((first.g + second.g) / 2),
                static_cast<uint8_t>((first.b + second.b) / 2)
            );
        }
    }
    return display.alpha_table;
}

void copy_alpha_table(DisplayContext& display, const uint8_t* table) noexcept {
    if ((display.flags & display_flag_alpha_table) == 0 || display.alpha_table == nullptr ||
        table == nullptr)
        return;
    std::memcpy(display.alpha_table, table, alpha_table_size);
}

uint8_t* build_shade_table(DisplayContext& display, const Palette& palette) noexcept {
    if ((display.flags & display_flag_shade_table) == 0 || display.shade_table == nullptr)
        return nullptr;
    int32_t brightness[OA_PALETTE_COLORS];
    uint8_t order[OA_PALETTE_COLORS];
    sort_palette_by_brightness(palette, brightness, order);
    double scale = 0.0;
    for (int32_t row = 0; row < ramp_table_rows; ++row) {
        uint8_t* out = display.shade_table + row * table_row;
        for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
            const PaletteEntry& entry = palette.entries[index];
            out[index] = find_nearest_sorted_color(
                palette,
                brightness,
                order,
                shade_channel(entry.r, scale),
                shade_channel(entry.g, scale),
                shade_channel(entry.b, scale)
            );
        }
        scale += shade_row_step;
    }
    return display.shade_table;
}

uint8_t* build_light_table(DisplayContext& display, const Palette& palette) noexcept {
    if ((display.flags & display_flag_light_table) == 0 || display.light_table == nullptr)
        return nullptr;
    int32_t brightness[OA_PALETTE_COLORS];
    uint8_t order[OA_PALETTE_COLORS];
    sort_palette_by_brightness(palette, brightness, order);
    for (int32_t row = 0; row < ramp_table_rows; ++row) {
        // Two roundings, as the game: the step product, then the sum.
        const double step = static_cast<double>(row) * light_row_step;
        const double scale = step + light_base_scale;
        uint8_t* out = display.light_table + row * table_row;
        for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
            const PaletteEntry& entry = palette.entries[index];
            out[index] = find_nearest_sorted_color(
                palette,
                brightness,
                order,
                light_channel(entry.r, scale),
                light_channel(entry.g, scale),
                light_channel(entry.b, scale)
            );
        }
    }
    return display.light_table;
}

uint8_t* build_gray_table(DisplayContext& display, const Palette& palette) noexcept {
    if ((display.flags & display_flag_gray_table) == 0 || display.gray_table == nullptr)
        return nullptr;
    int32_t brightness[OA_PALETTE_COLORS];
    uint8_t order[OA_PALETTE_COLORS];
    sort_palette_by_brightness(palette, brightness, order);
    for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
        const PaletteEntry& entry = palette.entries[index];
        const auto gray = static_cast<uint8_t>((entry.r + entry.g + entry.b) / 3);
        display.gray_table[index] =
            find_nearest_sorted_color(palette, brightness, order, gray, gray, gray);
    }
    return display.gray_table;
}

uint8_t* build_blue_table(DisplayContext& display, const Palette& palette) noexcept {
    if ((display.flags & display_flag_blue_table) == 0 || display.blue_table == nullptr)
        return nullptr;
    int32_t brightness[OA_PALETTE_COLORS];
    uint8_t order[OA_PALETTE_COLORS];
    sort_palette_by_brightness(palette, brightness, order);
    for (int32_t index = 0; index < OA_PALETTE_COLORS; ++index) {
        const PaletteEntry& entry = palette.entries[index];
        const int32_t half_blue = entry.b >> 1;
        const uint8_t blue = half_blue + blue_limit_bias > channel_max
                                 ? channel_max
                                 : static_cast<uint8_t>(half_blue + blue_lift);
        display.blue_table[index] = find_nearest_sorted_color(
            palette,
            brightness,
            order,
            static_cast<uint8_t>(entry.r >> 1),
            static_cast<uint8_t>(entry.g >> 1),
            blue
        );
    }
    return display.blue_table;
}

void copy_shade_table(DisplayContext& display, const uint8_t* table) noexcept {
    if ((display.flags & display_flag_shade_table) != 0 && display.shade_table != nullptr &&
        table != nullptr)
        std::memcpy(display.shade_table, table, shade_table_size);
}

void copy_light_table(DisplayContext& display, const uint8_t* table) noexcept {
    if ((display.flags & display_flag_light_table) != 0 && display.light_table != nullptr &&
        table != nullptr)
        std::memcpy(display.light_table, table, light_table_size);
}

TableSource load_session_alpha_table(
    DisplayContext& display, const Palette& palette, std::span<const uint8_t> file
) noexcept {
    return load_session_table(
        build_alpha_table, copy_alpha_table, display, palette, file, alpha_table_size
    );
}

TableSource load_session_shade_table(
    DisplayContext& display, const Palette& palette, std::span<const uint8_t> file
) noexcept {
    return load_session_table(
        build_shade_table, copy_shade_table, display, palette, file, shade_table_size
    );
}

TableSource load_session_light_table(
    DisplayContext& display, const Palette& palette, std::span<const uint8_t> file
) noexcept {
    return load_session_table(
        build_light_table, copy_light_table, display, palette, file, light_table_size
    );
}

void init_session_gray_table(DisplayContext& display, const Palette& palette) noexcept {
    build_gray_table(display, palette);
}

void init_session_blue_table(DisplayContext& display, const Palette& palette) noexcept {
    build_blue_table(display, palette);
}

PaletteTableSources load_session_tables(
    DisplayContext& display, const Palette& palette, const PaletteTableFiles& files
) noexcept {
    PaletteTableSources sources;
    sources.alpha = load_session_alpha_table(display, palette, files.alpha);
    sources.shade = load_session_shade_table(display, palette, files.shade);
    sources.light = load_session_light_table(display, palette, files.light);
    init_session_gray_table(display, palette);
    init_session_blue_table(display, palette);
    return sources;
}

SpriteBuffer create_two_plane_sprite(int32_t width, int32_t height) {
    SpriteBuffer buffer;
    if (width < 0 || height < 0 || static_cast<int64_t>(width) * height > sprite_max_pixels)
        return buffer;
    const auto plane = static_cast<std::size_t>(width) * static_cast<std::size_t>(height);
    buffer.pixels.assign(plane * 2, 0);
    buffer.sprite.width = static_cast<uint16_t>(width);
    buffer.sprite.height = static_cast<uint16_t>(height);
    buffer.sprite.data = buffer.pixels.data();
    buffer.sprite.aux = buffer.pixels.data() + plane;
    return buffer;
}

SpriteBuffer build_lens_frame(int32_t width, int32_t height, int32_t scale) {
    if (width < 0 || width > lens_max_width)
        return {};
    SpriteBuffer lens = create_two_plane_sprite(width * lens_offset_bytes, height);
    if (lens.sprite.data == nullptr && !(width == 0 || height == 0))
        return lens;
    const int32_t half_width = width / 2;
    const int32_t half_height = height / 2;
    lens.sprite.origin_x = static_cast<int16_t>(half_width);
    lens.sprite.width = static_cast<uint16_t>(lens.sprite.width >> 1);
    lens.sprite.origin_y = static_cast<int16_t>(half_height);
    const int32_t radius = width / lens_radius_divisor;
    const double strength = static_cast<double>(scale);
    for (int32_t y = 0; y < height; ++y) {
        const int32_t dy = y - static_cast<int16_t>(half_height);
        for (int32_t x = 0; x < width; ++x) {
            const int32_t dx = x - static_cast<int16_t>(half_width);
            const double distance = std::sqrt(static_cast<double>(dx * dx + dy * dy));
            const int32_t index = y * width + x;
            if (truncate_low32(distance) >= radius) {
                store_u16(lens.sprite.data, index, lens_outside);
                continue;
            }
            const double factor = (static_cast<double>(half_width) - distance) / strength;
            const auto source_y =
                static_cast<uint16_t>(truncate_low32(static_cast<double>(dy) / factor));
            const auto source_x =
                static_cast<uint32_t>(truncate_low32(static_cast<double>(dx) / factor));
            const uint32_t offset = (static_cast<uint32_t>(source_y) - static_cast<uint32_t>(y) +
                                     static_cast<uint32_t>(half_height)) *
                                        static_cast<uint32_t>(width) +
                                    source_x - static_cast<uint32_t>(x) +
                                    static_cast<uint32_t>(half_width);
            store_u16(lens.sprite.data, index, static_cast<uint16_t>(offset));
        }
    }
    return lens;
}

uint16_t lens_offset(const Sprite& lens, int32_t index) noexcept {
    uint16_t value = 0;
    const auto* data = static_cast<const uint8_t*>(lens.data);
    std::memcpy(
        &value, data + static_cast<std::ptrdiff_t>(index) * lens_offset_bytes, sizeof value
    );
    return value;
}

void blend_downsample_sprite(
    const DisplayContext& display, const Sprite& source, Sprite& target
) noexcept {
    const uint8_t* alpha = display.alpha_table;
    if (alpha == nullptr)
        return;
    const auto* pixels = static_cast<const uint8_t*>(source.data);
    auto* out = static_cast<uint8_t*>(target.data);
    const int32_t source_pitch = source.width;
    for (int32_t y = 0; y < target.height; ++y) {
        const uint8_t* top = pixels + static_cast<std::ptrdiff_t>(2 * y) * source_pitch;
        const uint8_t* bottom = top + source_pitch;
        for (int32_t x = 0; x < target.width; ++x) {
            const uint8_t upper = alpha[top[2 * x] * table_row + top[2 * x + 1]];
            const uint8_t lower = alpha[bottom[2 * x] * table_row + bottom[2 * x + 1]];
            out[static_cast<std::ptrdiff_t>(y) * target.width + x] =
                alpha[upper * table_row + lower];
        }
    }
}

} // namespace oa::present
