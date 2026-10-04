// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/palette_tables.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>
#include <string>
#include <tuple>
#include <vector>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace oa::present;
using oa::Palette;
using oa::PaletteEntry;

// Bytes of PALETTE.PAL: four per colour.
constexpr std::size_t palette_file_bytes = std::tuple_size_v<oa::PaletteBytes>;

Palette gray_palette() {
    Palette palette{};
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        palette.entries[i] = PaletteEntry{
            static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i), 0
        };
    return palette;
}

void test_gamma() {
    CHECK(gamma_channel(100, 1.0F) == 100);
    CHECK(gamma_channel(100, 2.0F) == 200);
    CHECK(gamma_channel(200, 2.0F) == 255);
    CHECK(gamma_channel(255, 0.5F) == 127);
    CHECK(gamma_channel(10, -1.0F) == 246); // no lower clamp: low byte of -10
    CHECK(gamma_channel(1, std::numeric_limits<float>::infinity()) == 255);
    CHECK(gamma_channel(0, std::numeric_limits<float>::infinity()) == 0); // NaN product
    CHECK(gamma_channel(255, -std::numeric_limits<float>::max()) == 0);   // below int64 range

    DisplayContext display{};
    display.gamma = 2.0F;
    Palette device{};
    PaletteEntry source[8]{};
    for (int i = 0; i < 8; ++i)
        source[i] = PaletteEntry{
            static_cast<uint8_t>(10 * i),
            static_cast<uint8_t>(10 * i + 1),
            static_cast<uint8_t>(10 * i + 2),
            7
        };
    CHECK(apply_palette_entries(display, source, 0, 4, device));
    CHECK(display.palette.entries[3].r == 30 && display.palette.entries[3].flags == 7);
    CHECK(
        device.entries[3].r == 60 && device.entries[3].g == 62 && device.entries[3].b == 64 &&
        device.entries[3].flags == 0
    );

    // Non-zero start: slots take source[0..], device colours come from source[start..].
    Palette shifted{};
    CHECK(apply_palette_entries(display, source, 4, 2, shifted));
    CHECK(display.palette.entries[4].r == 0 && display.palette.entries[5].r == 10);
    CHECK(shifted.entries[4].r == 80 && shifted.entries[5].r == 100);
    CHECK(!apply_palette_entries(display, source, 250, 7, shifted));
    CHECK(!apply_palette_entries(display, source, -1, 2, shifted));
    CHECK(apply_palette_entries(display, source, 300, 0, shifted));

    Palette full{};
    set_palette_gamma(display, 0.5F, full);
    CHECK(display.gamma == 0.5F);
    CHECK(full.entries[3].r == 15 && full.entries[3].b == 16);
    CHECK(!set_display_palette(display));
}

void test_tables() {
    DisplayContext display{};
    CHECK(load_alpha_table(display) && display.alpha_table != nullptr);
    CHECK(load_shade_table(display) && display.shade_table != nullptr);
    CHECK(load_light_table(display) && display.light_table != nullptr);
    CHECK(load_gray_table(display) && display.gray_table != nullptr);
    CHECK(load_blue_table(display) && display.blue_table != nullptr);
    const auto free_tables = [&display] {
        free_alpha_table(display);
        free_shade_table(display);
        free_light_table(display);
        free_gray_table(display);
        free_blue_table(display);
    };
    if (display.alpha_table == nullptr || display.shade_table == nullptr ||
        display.light_table == nullptr || display.gray_table == nullptr ||
        display.blue_table == nullptr) {
        free_tables();
        return;
    }
    display.alpha_table[alpha_table_size - 1] = 1;
    display.shade_table[shade_table_size - 1] = 1;
    display.light_table[light_table_size - 1] = 1;
    display.gray_table[gray_table_size - 1] = 1;
    display.blue_table[blue_table_size - 1] = 1;

    const Palette palette = gray_palette();
    CHECK(build_alpha_table(display, palette) == nullptr); // flag clear
    display.flags = display_flag_alpha_table;
    CHECK(build_alpha_table(display, palette) == display.alpha_table);
    bool averaged = true;
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            averaged &= display.alpha_table[a * 256 + b] == (a + b) / 2;
    CHECK(averaged);

    std::vector<uint8_t> cached(alpha_table_size, 7);
    display.flags = 0;
    copy_alpha_table(display, cached.data());
    CHECK(display.alpha_table[0x1234] == (0x12 + 0x34) / 2); // flag clear: left alone
    display.flags = display_flag_alpha_table;
    copy_alpha_table(display, cached.data());
    CHECK(std::equal(cached.begin(), cached.end(), display.alpha_table));

    free_tables();
    CHECK(display.alpha_table == nullptr && display.blue_table == nullptr);
    CHECK(build_alpha_table(display, palette) == nullptr);
}

// The shipped PALETTE.ALP is the build_alpha_table table of PALETTE.PAL.
void test_shipped_pair_table(const oa::AssetStore& assets) {
    const auto pal = oa::test::read_game_file(assets, "palettes/palette.pal");
    const auto alp = oa::test::read_game_file(assets, "palettes/palette.alp");
    CHECK(
        pal.size() == palette_file_bytes && alp.size() == static_cast<std::size_t>(alpha_table_size)
    );
    if (pal.size() != palette_file_bytes ||
        alp.size() != static_cast<std::size_t>(alpha_table_size))
        return;
    std::vector<uint8_t> table(alpha_table_size, 0);
    DisplayContext display{};
    display.alpha_table = table.data();
    display.flags = display_flag_alpha_table;
    build_alpha_table(display, palette_from_bytes(pal));
    CHECK(std::equal(alp.begin(), alp.end(), table.begin()));
}

void test_nearest() {
    Palette palette{};
    palette.entries[0] = PaletteEntry{200, 200, 200, 0};
    palette.entries[1] = PaletteEntry{10, 0, 0, 0};
    palette.entries[2] = PaletteEntry{0, 10, 0, 0};
    for (int i = 3; i < 256; ++i)
        palette.entries[i] = PaletteEntry{255, 255, 255, 0};
    int32_t brightness[256];
    uint8_t order[256];
    sort_palette_by_brightness(palette, brightness, order);
    CHECK(std::is_sorted(brightness, brightness + 256));
    CHECK(
        brightness[0] == 10 && brightness[1] == 10 && brightness[2] == 600 && brightness[3] == 765
    );
    CHECK(order[2] == 0);
    CHECK((order[0] == 1 && order[1] == 2) || (order[0] == 2 && order[1] == 1));
    CHECK(find_nearest_sorted_color(palette, brightness, order, 0, 9, 0) == 2);
    CHECK(find_nearest_sorted_color(palette, brightness, order, 190, 200, 200) == 0);
    // No candidate: the scan stops at the first brighter entry (sorted position 2).
    CHECK(find_nearest_sorted_color(palette, brightness, order, 100, 100, 100) == order[2]);
    // No candidate after a full pass wraps to sorted position 0.
    Palette dark{};
    int32_t dark_brightness[256];
    uint8_t dark_order[256];
    sort_palette_by_brightness(dark, dark_brightness, dark_order);
    CHECK(
        find_nearest_sorted_color(dark, dark_brightness, dark_order, 200, 200, 200) == dark_order[0]
    );
}

// A display owning every lookup table, with the table flags start_display sets.
DisplayContext table_display() {
    DisplayContext display{};
    display.flags = display_flag_alpha_table | display_flag_shade_table | display_flag_light_table |
                    display_flag_gray_table | display_flag_blue_table;
    load_alpha_table(display);
    load_shade_table(display);
    load_light_table(display);
    load_gray_table(display);
    load_blue_table(display);
    return display;
}

void free_tables(DisplayContext& display) {
    free_alpha_table(display);
    free_shade_table(display);
    free_light_table(display);
    free_gray_table(display);
    free_blue_table(display);
}

// On a gray palette entry v is the colour (v, v, v), so every table maps to
// the gray its builder computes.
void test_ramp_tables() {
    DisplayContext display = table_display();
    const Palette palette = gray_palette();
    CHECK(build_shade_table(display, palette) == display.shade_table);
    CHECK(build_light_table(display, palette) == display.light_table);
    CHECK(build_gray_table(display, palette) == display.gray_table);
    CHECK(build_blue_table(display, palette) == display.blue_table);
    bool shade = true;
    bool light = true;
    double shade_scale = 0.0;
    for (int row = 0; row < ramp_table_rows; ++row) {
        const double step = row * 0.03333333333333333;
        const double light_scale = step + 1.0;
        for (int index = 0; index < 256; ++index) {
            const int shaded = std::min(static_cast<int>(index * shade_scale), 255);
            const int lit = std::min(static_cast<int>(index * light_scale), 255);
            shade &= display.shade_table[row * 256 + index] == shaded;
            light &= display.light_table[row * 256 + index] == lit;
        }
        shade_scale += 0.06875;
    }
    CHECK(shade);
    CHECK(light);
    CHECK(display.shade_table[31 * 256 + 100] == 213); // 100 * 31 * 0.06875 truncated
    CHECK(display.light_table[30 * 256 + 100] == 200);
    bool gray = true;
    bool blue = true;
    for (int index = 0; index < 256; ++index) {
        gray &= display.gray_table[index] == index;
        // Nearest gray to (h, h, h + 50) with h = index / 2 is h + 17.
        blue &= display.blue_table[index] == index / 2 + 17;
    }
    CHECK(gray);
    CHECK(blue);

    // Only entries within the brightness window of the gray compete: for
    // red's and blue's gray (85, 85, 85) black and white lie outside it.
    Palette mixed{};
    for (auto& entry : mixed.entries)
        entry = PaletteEntry{255, 255, 255, 0};
    mixed.entries[0] = PaletteEntry{0, 0, 0, 0};
    mixed.entries[1] = PaletteEntry{255, 0, 0, 0};
    mixed.entries[2] = PaletteEntry{90, 90, 90, 0};
    mixed.entries[3] = PaletteEntry{0, 0, 255, 0};
    build_gray_table(display, mixed);
    CHECK(display.gray_table[0] == 0 && display.gray_table[1] == 2 && display.gray_table[3] == 2);
    CHECK(mixed.entries[display.gray_table[255]].r == 255);

    // Each builder needs its flag.
    display.flags = 0;
    CHECK(build_shade_table(display, palette) == nullptr);
    CHECK(build_light_table(display, palette) == nullptr);
    CHECK(build_gray_table(display, palette) == nullptr);
    CHECK(build_blue_table(display, palette) == nullptr);
    free_tables(display);
}

void test_session_tables() {
    DisplayContext display = table_display();
    const Palette palette = gray_palette();
    std::vector<uint8_t> alpha(alpha_table_size, 7);
    std::vector<uint8_t> shade(shade_table_size, 8);
    std::vector<uint8_t> short_light(light_table_size - 1, 9);
    PaletteTableFiles files{alpha, shade, short_light};
    const PaletteTableSources sources = load_session_tables(display, palette, files);
    CHECK(sources.alpha == TableSource::file && sources.shade == TableSource::file);
    CHECK(sources.light == TableSource::rejected);
    CHECK(std::memcmp(display.alpha_table, alpha.data(), alpha.size()) == 0);
    CHECK(std::memcmp(display.shade_table, shade.data(), shade.size()) == 0);
    CHECK(display.light_table[30 * 256 + 100] == 200); // built instead
    CHECK(display.gray_table[77] == 77 && display.blue_table[100] == 67);
    CHECK(load_session_alpha_table(display, palette, {}) == TableSource::built);
    CHECK(display.alpha_table[10 * 256 + 20] == 15);

    // Without its flag a loaded table is not installed.
    display.flags = static_cast<uint16_t>(display.flags & ~display_flag_shade_table);
    std::vector<uint8_t> other(shade_table_size, 3);
    copy_shade_table(display, other.data());
    CHECK(display.shade_table[0] == 8);
    free_tables(display);
}

// The shipped PALETTE.ALP, .SHD and .LHT are what the builders make from
// PALETTE.PAL, and the session loaders install their bytes unchanged.
void test_installed_tables(const oa::AssetStore& assets) {
    const auto pal = oa::test::read_game_file(assets, "palettes/palette.pal");
    const auto alp = oa::test::read_game_file(assets, "palettes/palette.alp");
    const auto shd = oa::test::read_game_file(assets, "palettes/palette.shd");
    const auto lht = oa::test::read_game_file(assets, "palettes/palette.lht");
    CHECK(pal.size() == palette_file_bytes && !alp.empty() && !shd.empty() && !lht.empty());
    if (pal.size() != palette_file_bytes || alp.empty() || shd.empty() || lht.empty())
        return;
    const Palette palette = palette_from_bytes(pal);
    DisplayContext built = table_display();
    build_alpha_table(built, palette);
    build_shade_table(built, palette);
    build_light_table(built, palette);
    CHECK(
        alp.size() == static_cast<std::size_t>(alpha_table_size) &&
        std::memcmp(built.alpha_table, alp.data(), alp.size()) == 0
    );
    CHECK(
        shd.size() == static_cast<std::size_t>(shade_table_size) &&
        std::memcmp(built.shade_table, shd.data(), shd.size()) == 0
    );
    CHECK(
        lht.size() == static_cast<std::size_t>(light_table_size) &&
        std::memcmp(built.light_table, lht.data(), lht.size()) == 0
    );
    free_tables(built);

    DisplayContext loaded = table_display();
    const PaletteTableSources sources =
        load_session_tables(loaded, palette, PaletteTableFiles{alp, shd, lht});
    CHECK(
        sources.alpha == TableSource::file && sources.shade == TableSource::file &&
        sources.light == TableSource::file
    );
    CHECK(std::memcmp(loaded.alpha_table, alp.data(), alp.size()) == 0);
    CHECK(std::memcmp(loaded.shade_table, shd.data(), shd.size()) == 0);
    CHECK(std::memcmp(loaded.light_table, lht.data(), lht.size()) == 0);
    free_tables(loaded);
}

void test_lens() {
    SpriteBuffer block = create_two_plane_sprite(6, 4);
    CHECK(block.sprite.width == 6 && block.sprite.height == 4 && block.pixels.size() == 48);
    CHECK(block.sprite.data == block.pixels.data() && block.sprite.aux == block.pixels.data() + 24);
    CHECK(create_two_plane_sprite(-1, 4).sprite.data == nullptr);

    constexpr int size = 40;
    SpriteBuffer lens = build_lens_frame(size, size, 4);
    CHECK(lens.sprite.width == size && lens.sprite.height == size);
    CHECK(lens.sprite.origin_x == size / 2 && lens.sprite.origin_y == size / 2);
    CHECK(lens.pixels.size() == static_cast<std::size_t>(size * size * 2 * 2));
    CHECK(lens_offset(lens.sprite, 0) == lens_outside);
    CHECK(lens_offset(lens.sprite, (size / 2) * size + size / 2) == 0);
    int inside = 0;
    for (int i = 0; i < size * size; ++i)
        inside += lens_offset(lens.sprite, i) != lens_outside;
    CHECK(inside > 0 && inside < size * size);
    CHECK(build_lens_frame(0x8000, 1, 1).sprite.data == nullptr);
}

void test_downsample() {
    DisplayContext display{};
    CHECK(load_alpha_table(display));
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            display.alpha_table[a * 256 + b] = static_cast<uint8_t>(std::max(a, b));
    SpriteBuffer source = create_sprite(4, 4);
    for (int i = 0; i < 16; ++i)
        source.pixels[static_cast<std::size_t>(i)] = static_cast<uint8_t>((i * 37) & 0xFF);
    SpriteBuffer target = create_sprite(2, 2);
    blend_downsample_sprite(display, source.sprite, target.sprite);
    for (int y = 0; y < 2; ++y)
        for (int x = 0; x < 2; ++x) {
            const auto* p = source.pixels.data();
            const int top = std::max(p[(2 * y) * 4 + 2 * x], p[(2 * y) * 4 + 2 * x + 1]);
            const int bottom = std::max(p[(2 * y + 1) * 4 + 2 * x], p[(2 * y + 1) * 4 + 2 * x + 1]);
            CHECK(target.pixels[static_cast<std::size_t>(y * 2 + x)] == std::max(top, bottom));
        }
    // Order check: the upper pair's blend is the table row.
    for (int a = 0; a < 256; ++a)
        for (int b = 0; b < 256; ++b)
            display.alpha_table[a * 256 + b] = static_cast<uint8_t>(a);
    blend_downsample_sprite(display, source.sprite, target.sprite);
    CHECK(target.pixels[0] == source.pixels[0] && target.pixels[3] == source.pixels[10]);
    free_alpha_table(display);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        const auto assets = oa::test::require_game_assets("the installed palette tables");
        test_installed_tables(assets);
        test_shipped_pair_table(assets);
    } else {
        test_gamma();
        test_tables();
        test_nearest();
        test_ramp_tables();
        test_session_tables();
        test_lens();
        test_downsample();
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("palette table tests passed");
    return 0;
}
