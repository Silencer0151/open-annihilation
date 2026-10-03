// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// ui.megamap's pieces: the fitted layout and its two-way mapping, the
// downscaled terrain snapped or dithered, feature blob colours, the icon
// file, the icon chosen for a unit and its recoloured pixels, and the rings
// with their minimums.
#include "oa/ui/hud/megamap.hpp"

#include "check.hpp"
#include "fixtures.hpp"

#include <array>
#include <cstring>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

void layout() {
    // A 4096x2048 map in a 512x416 view: full width, 256 high, centred.
    const auto fitted = megamap_layout(128, 32, 512, 416, 4096, 2048);
    CHECK(
        fitted.left == 128 && fitted.width == 512 && fitted.height == 256 && fitted.top == 32 + 80
    );
    const auto tall = megamap_layout(0, 0, 512, 416, 1024, 2048);
    CHECK(tall.height == 416 && tall.width == 208 && tall.left == 152 && tall.top == 0);
    const auto at = megamap_point(fitted, 2048, 1024);
    CHECK(at[0] == 128 + 256 && at[1] == 112 + 128);
    const auto back = megamap_map_point(fitted, 128 + 256, 112 + 128);
    CHECK(back.has_value() && (*back)[0] == 2048 && (*back)[1] == 1024);
    CHECK(!megamap_map_point(fitted, 128, 100).has_value());
    CHECK(megamap_layout(0, 0, 0, 10, 10, 10).width == 0);
}

void terrain() {
    // A palette of greys: entry n is (n, n, n).
    std::vector<uint8_t> palette(256 * 4);
    for (int n = 0; n < 256; ++n)
        palette[n * 4] = palette[n * 4 + 1] = palette[n * 4 + 2] = static_cast<uint8_t>(n);
    // A 4x2 map of 10 and 20 alternating columns, to 2x1: each pixel the mean, 15.
    TerrainSource source{};
    source.pixel = [](void*, int32_t x, int32_t) -> uint8_t { return x % 2 == 0 ? 10 : 20; };
    auto picture = downscale_terrain(source, 4, 2, 2, 1, palette, false);
    CHECK(picture.size() == 2 && picture[0] == 15 && picture[1] == 15);
    // Dithered with two nearest entries 15 and 14 (or 16): odd pixels take the second.
    picture = downscale_terrain(source, 4, 2, 2, 1, palette, true);
    CHECK(picture[0] == 15 && picture[1] != 15);
    CHECK(downscale_terrain(source, 0, 2, 2, 1, palette, false).empty());
}

void features() {
    FeatureDef def{};
    std::snprintf(def.name, sizeof def.name, "%s", "Spire");
    CHECK(feature_blob_color(def) == kFeatureBlobSpire);
    def.metal = 5.0F;
    CHECK(feature_blob_color(def) == kFeatureBlobReclaimable);
    def.metal = 0.0F;
    std::snprintf(def.name, sizeof def.name, "%s", "Tree");
    CHECK(feature_blob_color(def) == kFeatureBlobOther);
}

void icon_file() {
    const char* text = "; Megamap Icon Settings\n"
                       "[Option] ; General\n"
                       "FillColor=245;  ; replaced\n"
                       "SelectedColor=255;\n"
                       "UseCircleHover=FALSE;\n"
                       "UseDefaultIcon=FALSE;\n"
                       "[ICON]\n"
                       "Unknow=UNKNOWN.PCX;\n"
                       "Nothing=NONE.pcx;\n"
                       "NukeIcon=NUKEICON.pcx;\n"
                       "AIR=AIR.pcx\n"
                       "LAB=FACTORY.pcx\n"
                       "BLDG=BUILDING.pcx\n";
    auto config = parse_icon_config(text, 9);
    CHECK(config.options.fill_color == 245 && config.options.selected_color == 255);
    CHECK(config.options.transparent_color == 9 && config.options.hover_color == 0x54);
    CHECK(!config.options.default_icons && !config.options.circle_hover);
    CHECK(config.unknown_file == "UNKNOWN.PCX" && config.nothing_file == "NONE.pcx");
    CHECK(config.nuke_file == "NUKEICON.pcx");
    CHECK(
        config.lines.size() == 3 && config.lines[0].category == "air" &&
        config.lines[1].file == "FACTORY.pcx"
    );
    // The profile's count caps the category lines read.
    config = parse_icon_config(text, 2);
    CHECK(config.lines.size() == 2);
}

void choosing() {
    std::array<uint32_t, 2> air_words{1u << 3, 0};
    std::array<uint32_t, 2> lab_words{(1u << 3) | (1u << 5), 0};
    const data::defs::CategoryMask air{air_words.data(), 2};
    const data::defs::CategoryMask lab{lab_words.data(), 2};
    const std::array<const data::defs::CategoryMask*, 3> masks{nullptr, &air, &lab};
    std::size_t line = 99;
    CHECK(choose_icon(false, 3, masks, line) == IconChoice::nothing);
    CHECK(choose_icon(true, 3, masks, line) == IconChoice::category && line == 1);
    CHECK(choose_icon(true, 5, masks, line) == IconChoice::category && line == 2);
    CHECK(choose_icon(true, 6, masks, line) == IconChoice::unknown);
    IconOptions options{};
    options.fill_color = 245;
    options.selected_color = 255;
    CHECK(!icon_pixel(options, 9, 80, false, false).has_value());
    CHECK(*icon_pixel(options, 245, 80, false, false) == 80);
    CHECK(*icon_pixel(options, 255, 80, true, false) == 255);
    CHECK(!icon_pixel(options, 255, 80, false, false).has_value());
    CHECK(*icon_pixel(options, 255, 80, false, true) == 0x54);
    options.circle_hover = true;
    CHECK(!icon_pixel(options, 255, 80, false, true).has_value());
    CHECK(*icon_pixel(options, 17, 80, false, false) == 17);
}

void rings() {
    hud_test::TestWorld w;
    auto& game = w.game();
    for (uint8_t i = 0; i < 16; ++i)
        game.ui_colors[i] = static_cast<uint8_t>(0xb0 + i);
    UnitDef def{};
    def.radar_distance = 1200;
    def.sonar_distance = 100;
    def.radar_distance_jam = 64;
    def.sonar_distance_jam = 0;
    Unit unit{};
    unit.weapons[1].def = oa_ref_from_index(2);
    game.weapon_defs[2].flags = OA_WEAPON_FLAG_INTERCEPTOR;
    game.weapon_defs[2].coverage = 2000;
    std::array<MegamapRing, 5> out{};
    // 128, 128, 64, 64, 512: the sonar falls short of its minimum.
    auto count = megamap_rings(*w.world, unit, def, {128, 128, 64, 64, 512}, out);
    CHECK(count == 3 && out[0].radius == 1200 && out[0].color == 0xba);
    CHECK(
        out[1].radius == 64 && out[1].color == 0xbc && out[2].radius == 2000 && out[2].color == 0xbf
    );
    // 3.1c's minimums draw every ring above 0 but the anti-nuke's below 512.
    game.weapon_defs[2].coverage = 400;
    count = megamap_rings(*w.world, unit, def, {0, 0, 0, 0, 512}, out);
    CHECK(count == 3 && out[1].radius == 100);
}

} // namespace

int main() {
    layout();
    terrain();
    features();
    icon_file();
    choosing();
    rings();
    std::puts("ui-hud-megamap-test: ok");
    return 0;
}
