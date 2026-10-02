// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/game_fields.hpp"
#include "oa/ui/hud/resource_bar.hpp"
#include "oa/ui/hud/unit_labels.hpp"
#include "oa/test/game_assets.hpp"

#include <cstring>
#include <memory>
#include <string>

using namespace oa;
using namespace oa::ui::hud;

namespace {

void test_kill_count() {
    char text[64];
    format_kill_count(text, sizeof text, 0, nullptr, nullptr);
    CHECK(std::strcmp(text, "0 kills") == 0);
    format_kill_count(text, sizeof text, 1, nullptr, nullptr);
    CHECK(std::strcmp(text, "1 kill") == 0);
    format_kill_count(text, sizeof text, 4, nullptr, nullptr);
    CHECK(std::strcmp(text, "4 kills") == 0);
    format_kill_count(text, sizeof text, 5, nullptr, nullptr);
    CHECK(std::strcmp(text, "5 kills - Veteran") == 0);
    const Localize german = [](void*, const char* word) -> const char* {
        return std::strcmp(word, "Veteran") == 0 ? "Veteran!" : nullptr;
    };
    format_kill_count(text, sizeof text, 12, german, nullptr);
    CHECK(std::strcmp(text, "12 kills - Veteran!") == 0);
}

void test_resource_bar() {
    // SIDEDATA ENERGYBAR x1=468 x2=595: the fill's inclusive right edge is
    // x1 + (x2 - x1) * shown / capacity, truncated.
    auto trough = trough_columns(500.0F, 1000.0F, 0.0F, 500.0F, 468, 595);
    CHECK(trough.fill && trough.fill_right == 531 && !trough.marker);
    trough = trough_columns(0.0F, 1000.0F, 0.0F, 0.0F, 468, 595);
    CHECK(trough.fill && trough.fill_right == 468);
    trough = trough_columns(1000.0F, 1000.0F, 250.0F, 1000.0F, 468, 595);
    CHECK(trough.fill_right == 595 && trough.marker && trough.marker_left == 499);
    trough = trough_columns(200.0F, 1000.0F, 250.0F, 200.0F, 468, 595);
    CHECK(!trough.marker);
    trough = trough_columns(5.0F, 0.0F, 250.0F, 1000.0F, 468, 595);
    CHECK(!trough.fill && !trough.marker);

    CHECK(ease_toward(1000, 0) == 125 && ease_toward(1000, 125) == 234);
    CHECK(ease_toward(1000, 995) == 996 && ease_toward(1000, 1000) == 1000);
    CHECK(ease_toward(0, 1000) == 875 && ease_toward(0, 3) == 2);

    // A lone ARM commander building an ARMMEX: 25 energy and 1 metal made per
    // 30-tick settlement, 521 / 1800 * 300 energy and 50 / 1800 * 300 metal
    // spent on the build.
    Player player{};
    player.index = 3;
    player.energy = 1000.0F;
    player.metal = 437.5F;
    player.energy_storage = 1000.0F;
    player.metal_storage = 1000.0F;
    player.energy_produced = 25.0F;
    player.energy_requested = 521.0F / 1800.0F * 300.0F;
    player.metal_produced = 1.0F;
    player.metal_requested = 50.0F / 1800.0F * 300.0F;
    ResourceReadout readout{};
    update_resource_readout(readout, player, 0);
    CHECK(readout.player == 3 && readout.energy == 125.0F && readout.metal == 54.0F);
    CHECK(readout.energy_produced == 0.0F && display_timer(player) == 0);
    update_resource_readout(readout, player, 1);
    CHECK(readout.energy == 234.0F && readout.metal == 101.0F);
    CHECK(readout.energy_produced == 25.0F && readout.metal_requested == player.metal_requested);
    // The requested rates read Player.energy_requested and Player.metal_requested.
    CHECK(readout.energy_requested == 521.0F / 1800.0F * 300.0F);
    CHECK(oa::player_metal_requested(&player) == 50.0F / 1800.0F * 300.0F);
    CHECK(display_timer(player) == 30);
    player.energy_produced = 50.0F;
    update_resource_readout(readout, player, 30);
    CHECK(readout.energy_produced == 25.0F && display_timer(player) == 30);
    update_resource_readout(readout, player, 31);
    CHECK(readout.energy_produced == 50.0F && display_timer(player) == 60);
    player.energy_storage = 200.0F;
    update_resource_readout(readout, player, 31);
    CHECK(readout.energy == 200.0F && readout.energy_storage == 200.0F);

    // The top bar and the unit panel draw production in UI colour 10 and
    // use in UI colour 12.
    auto game = std::make_unique<Game>();
    for (int slot = 0; slot < 0x100; ++slot)
        game->ui_colors[slot] = static_cast<uint8_t>(0x80 + slot);
    const auto produced = static_cast<uint8_t>(0x80 + 10);
    const auto consumed = static_cast<uint8_t>(0x80 + 12);
    CHECK(readout_color(*game, kReadoutTextColor) == 0x80 + 15);
    auto rate = format_energy_rate(*game, 25.0F, true);
    CHECK(std::strcmp(rate.text, "25") == 0 && rate.color == produced);
    rate = format_energy_rate(*game, player.energy_requested, false);
    CHECK(std::strcmp(rate.text, "86") == 0 && rate.color == consumed);
    rate = format_energy_rate(*game, -40.7F, false);
    CHECK(std::strcmp(rate.text, "40") == 0);
    rate = format_energy_rate(*game, 123456.0F, true);
    CHECK(std::strcmp(rate.text, "123K") == 0);
    rate = format_energy_rate(*game, -123456.0F, false);
    CHECK(std::strcmp(rate.text, "-123K") == 0);
    rate = format_metal_rate(*game, 1.0F, true);
    CHECK(std::strcmp(rate.text, "1.0") == 0 && rate.color == produced);
    rate = format_metal_rate(*game, player.metal_requested, false);
    CHECK(std::strcmp(rate.text, "8.3") == 0 && rate.color == consumed);
    rate = format_metal_rate(*game, -2.3F, false);
    CHECK(std::strcmp(rate.text, "2.3") == 0);
    rate = format_unit_rate(*game, player.energy_requested, false, false);
    CHECK(std::strcmp(rate.text, "-87") == 0 && rate.color == consumed);
    rate = format_unit_rate(*game, 25.0F, false, true);
    CHECK(std::strcmp(rate.text, "+25") == 0 && rate.color == produced);
    rate = format_unit_rate(*game, player.metal_requested, true, false);
    CHECK(std::strcmp(rate.text, "-8.3") == 0);
    rate = format_unit_rate(*game, -3.0F, true, true);
    CHECK(std::strcmp(rate.text, "+0.0") == 0);
    auto picture = radar_picture(2048, 1024, 126);
    CHECK(picture.x == 0 && picture.width == 126 && picture.height == 63 && picture.y == 31);
    picture = radar_picture(1024, 4096, 126);
    CHECK(picture.y == 0 && picture.height == 126 && picture.width == 31 && picture.x == 47);
}

// A side's keys are read as the game reads TDF numbers: a key that is present
// reads its digits, 0 when it has none, and only a missing key keeps the
// layout's value.
void test_side_layout_numbers() {
    SideLayout layout;
    CHECK(parse_side_layout(
        "[side1]{metalcolor=+5; energycolor=; [METALNUM]{x1=12abc; y1=;} [ENERGYNUM]{x1=7;}}",
        1,
        layout
    ));
    CHECK(layout.metal_color == 5 && layout.energy_color == 0);
    CHECK(layout.metal_num_x == 12 && layout.metal_num_y == 0);
    CHECK(layout.energy_num_x == 7 && layout.energy_num_y == 18);
}

// Both sides' bar layouts in the installed game's sidedata.tdf.
void test_installed_side_layouts(const AssetStore& assets) {
    const auto bytes = test::read_game_file(assets, "gamedata/sidedata.tdf");
    CHECK(!bytes.empty());
    const std::string text(bytes.begin(), bytes.end());
    for (int side = 0; side < 2; ++side) {
        SideLayout layout;
        CHECK(parse_side_layout(text, side, layout));
        CHECK(layout.metal_bar.x == 218 && layout.metal_bar.y == 12);
        CHECK(layout.metal_bar.width == 128 && layout.metal_bar.height == 3);
        CHECK(layout.metal_num_x == 278 && layout.metal_num_y == 18);
        CHECK(layout.energy_bar.width > 0);
        // The unit panel's second line, logo and build readout.
        CHECK(layout.logo2.x == 132 && layout.logo2.y == 455 && layout.logo2.width == 21);
        CHECK(layout.mission_text.x == 385 && layout.mission_text.y == 449);
        CHECK(layout.unit_name2.x == 555 && layout.unit_name2.y == 452);
        CHECK(layout.damage_bar2.x == 510 && layout.damage_bar2.width == 91);
        CHECK(layout.name.x == 132 && layout.name.y == 452);
        CHECK(layout.description.x == 132 && layout.description.y == 465);
    }
    SideLayout layout;
    CHECK(!parse_side_layout(text, 7, layout));
    CHECK(!parse_side_layout("", 0, layout));
}

} // namespace

int main(int argc, char** argv) {
    if (test::game_data_requested(argc, argv)) {
        test_installed_side_layouts(test::require_game_assets("the installed side layouts"));
        return 0;
    }
    test_kill_count();
    test_resource_bar();
    test_side_layout_numbers();
    return 0;
}
