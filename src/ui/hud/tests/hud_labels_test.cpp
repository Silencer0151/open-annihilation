// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "check.hpp"

#include "oa/ui/hud/game_fields.hpp"
#include "oa/ui/hud/resource_bar.hpp"
#include "oa/ui/hud/unit_labels.hpp"
#include "oa/data/defs/locale.hpp"
#include "oa/formats/tdf.hpp"
#include "oa/test/game_assets.hpp"

#include <cstdint>
#include <cstring>
#include <memory>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

using namespace oa;
using namespace oa::ui::hud;

namespace {

// A language lookup that gives every word through one buffer, as the
// runtime's lookup for the unit panel does: each word lasts only until the
// next lookup. A word maps through `table` when there is one, else through
// `words`, else is kept.
struct OneBufferLookup {
    const data::defs::LocaleTable* table{};
    std::vector<std::pair<std::string_view, std::string_view>> words;
    std::string word;

    static const char* look_up(void* user, const char* text) {
        auto& self = *static_cast<OneBufferLookup*>(user);
        self.word = text;
        if (self.table != nullptr)
            self.word = data::defs::locale_translate(self.table, text);
        for (const auto& [source, translation] : self.words)
            if (source == text)
                self.word = translation;
        return self.word.c_str();
    }
};

// 3.1c's kill line for `kills` through `lookup`.
std::string kill_line(uint16_t kills, OneBufferLookup& lookup) {
    char text[64];
    format_kill_count(text, sizeof text, kills, OneBufferLookup::look_up, &lookup);
    return text;
}

// The kill line under ui.veterancy-label for a type with `thresholds`.
std::string
labelled_kill_line(uint16_t kills, std::span<const uint16_t> thresholds, OneBufferLookup& lookup) {
    char text[64];
    format_kill_count_at_level(
        text,
        sizeof text,
        kills,
        veterancy_label_level(thresholds, kills),
        OneBufferLookup::look_up,
        &lookup
    );
    return text;
}

// veterancy.model's default thresholds, which are 3.1c's.
constexpr uint16_t kDefaultThresholds[] = {5, 10, 15, 20, 25};
// A type's own VeterancyThresholds whose first level is above 3.1c's five
// kills.
constexpr uint16_t kLateThresholds[] = {10, 20};

// The installed game's French words for the kill line, in its code page.
constexpr std::string_view kFrenchKill = "victime";
constexpr std::string_view kFrenchKills = "Victimes";
constexpr std::string_view kFrenchVeteran = "V\xe9t\xe9ran";

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

// Through a lookup that reuses its buffer, each word of the kill line is the
// one its place calls for: "kill" for exactly one, "kills" otherwise, and
// "Veteran" after the plural from five kills on, in English and in French.
void test_kill_count_through_one_buffer() {
    OneBufferLookup english;
    CHECK(kill_line(0, english) == "0 kills");
    CHECK(kill_line(1, english) == "1 kill");
    CHECK(kill_line(2, english) == "2 kills");
    CHECK(kill_line(4, english) == "4 kills");
    CHECK(kill_line(kVeteranKills, english) == "5 kills - Veteran");
    CHECK(kill_line(7, english) == "7 kills - Veteran");
    OneBufferLookup french;
    french.words = {{"kill", kFrenchKill}, {"kills", kFrenchKills}, {"Veteran", kFrenchVeteran}};
    CHECK(kill_line(0, french) == "0 Victimes");
    CHECK(kill_line(1, french) == "1 victime");
    CHECK(kill_line(2, french) == "2 Victimes");
    CHECK(kill_line(4, french) == "4 Victimes");
    CHECK(kill_line(kVeteranKills, french) == "5 Victimes - V\xe9t\xe9ran");
    CHECK(kill_line(7, french) == "7 Victimes - V\xe9t\xe9ran");
}

// ui.veterancy-label's level and line.
void test_veterancy_label() {
    const uint16_t thresholds[] = {5, 10, 15, 20, 25};
    CHECK(veterancy_label_level(thresholds, 0) == 0);
    CHECK(veterancy_label_level(thresholds, 4) == 0);
    CHECK(veterancy_label_level(thresholds, 5) == 1);
    CHECK(veterancy_label_level(thresholds, 14) == 2);
    CHECK(veterancy_label_level(thresholds, 25) == 5);
    CHECK(veterancy_label_level(thresholds, 1000) == 5);
    // A count from 32768 on reads as negative, widened past every threshold.
    const uint16_t high[] = {40000, 50000};
    CHECK(veterancy_label_level(high, 32767) == 0);
    CHECK(veterancy_label_level(high, 32768) == 2);
    CHECK(veterancy_label_level({}, 100) == 0);
    const uint16_t single[] = {1};
    char text[64];
    format_kill_count_at_level(text, sizeof text, 0, 0, nullptr, nullptr);
    CHECK(std::strcmp(text, "0 kills") == 0);
    format_kill_count_at_level(text, sizeof text, 1, 0, nullptr, nullptr);
    CHECK(std::strcmp(text, "1 kill") == 0);
    // Below level 1 there is no label, whatever the count.
    format_kill_count_at_level(text, sizeof text, 9, 0, nullptr, nullptr);
    CHECK(std::strcmp(text, "9 kills") == 0);
    // From level 1 the noun is always plural.
    format_kill_count_at_level(
        text, sizeof text, 1, veterancy_label_level(single, 1), nullptr, nullptr
    );
    CHECK(std::strcmp(text, "1 kills - Vet1") == 0);
    format_kill_count_at_level(text, sizeof text, 22, 4, nullptr, nullptr);
    CHECK(std::strcmp(text, "22 kills - Vet4") == 0);
    const Localize german = [](void*, const char* word) -> const char* {
        return std::strcmp(word, "kills") == 0 ? "Abschuesse" : nullptr;
    };
    format_kill_count_at_level(text, sizeof text, 12, 2, german, nullptr);
    CHECK(std::strcmp(text, "12 Abschuesse - Vet2") == 0);
}

// ui.veterancy-label through a lookup that reuses its buffer, in English and
// in French: below the first threshold the line is 3.1c's without "Veteran",
// from it on "<n> kills - Vet<L>"; "Vet" is not looked up.
void test_veterancy_label_through_one_buffer() {
    OneBufferLookup english;
    CHECK(labelled_kill_line(0, kDefaultThresholds, english) == "0 kills");
    CHECK(labelled_kill_line(1, kDefaultThresholds, english) == "1 kill");
    CHECK(labelled_kill_line(2, kDefaultThresholds, english) == "2 kills");
    CHECK(labelled_kill_line(4, kDefaultThresholds, english) == "4 kills");
    CHECK(labelled_kill_line(5, kDefaultThresholds, english) == "5 kills - Vet1");
    CHECK(labelled_kill_line(7, kDefaultThresholds, english) == "7 kills - Vet1");
    CHECK(labelled_kill_line(10, kDefaultThresholds, english) == "10 kills - Vet2");
    // A type whose first threshold is ten kills is not labelled at seven.
    CHECK(labelled_kill_line(7, kLateThresholds, english) == "7 kills");
    CHECK(labelled_kill_line(10, kLateThresholds, english) == "10 kills - Vet1");
    OneBufferLookup french;
    french.words = {
        {"kill", kFrenchKill}, {"kills", kFrenchKills}, {"Veteran", kFrenchVeteran}, {"Vet", "X"}
    };
    CHECK(labelled_kill_line(1, kDefaultThresholds, french) == "1 victime");
    CHECK(labelled_kill_line(2, kDefaultThresholds, french) == "2 Victimes");
    CHECK(labelled_kill_line(4, kDefaultThresholds, french) == "4 Victimes");
    CHECK(labelled_kill_line(5, kDefaultThresholds, french) == "5 Victimes - Vet1");
    CHECK(labelled_kill_line(7, kDefaultThresholds, french) == "7 Victimes - Vet1");
    CHECK(labelled_kill_line(7, kLateThresholds, french) == "7 Victimes");
}

// ui.interface-fixes resurrect-spelling: only the misspelled caption changes.
void test_resurrect_spelling() {
    CHECK(
        std::strcmp(shown_unit_caption("Ressurection failed", false), "Ressurection failed") == 0
    );
    CHECK(std::strcmp(shown_unit_caption("Ressurection failed", true), "Resurrection failed") == 0);
    CHECK(std::strcmp(shown_unit_caption("Resurrection failed", true), "Resurrection failed") == 0);
    CHECK(std::strcmp(shown_unit_caption("Cannot comply", true), "Cannot comply") == 0);
    CHECK(shown_unit_caption(nullptr, true) == nullptr);
}

// ui.interface-fixes bar-clamp: top panel art reaching the battlefield is cut.
void test_top_panel_rows() {
    CHECK(top_panel_rows(32, 32, false) == 32);
    CHECK(top_panel_rows(40, 32, false) == 40);
    CHECK(top_panel_rows(32, 32, true) == 31);
    CHECK(top_panel_rows(40, 32, true) == 31);
    CHECK(top_panel_rows(31, 32, true) == 31);
    CHECK(top_panel_rows(20, 32, true) == 20);
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
// layout's value, but for the bars' colours.
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
    // The bars' colours are palette index 0 without their keys, as in 3.1c,
    // not the layout's.
    SideLayout uncoloured;
    CHECK(parse_side_layout("[side2]{[ENERGYNUM]{x1=7;}}", 2, uncoloured));
    CHECK(uncoloured.metal_color == 0 && uncoloured.energy_color == 0);
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

// The kill line in the installed game's languages, each word looked up in
// gamedata/translate.tdf through one buffer as the runtime looks it up:
// English, which the file leaves as written, French and German.
void test_installed_kill_words(const AssetStore& assets) {
    const auto bytes = test::read_game_file(assets, "gamedata/translate.tdf");
    CHECK(!bytes.empty());
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    CHECK(
        formats::tdf::parse_text(
            &document,
            reinterpret_cast<const char*>(bytes.data()),
            static_cast<uint32_t>(bytes.size()),
            true,
            nullptr
        )
    );

    struct Language {
        const char* name;
        // Lines at 1, 2 and 7 kills, and at 7 under ui.veterancy-label.
        std::string_view one;
        std::string_view two;
        std::string_view seven;
        std::string_view seven_labelled;
    };

    const Language languages[] = {
        {"English", "1 kill", "2 kills", "7 kills - Veteran", "7 kills - Vet1"},
        {"French", "1 victime", "2 Victimes", "7 Victimes - V\xe9t\xe9ran", "7 Victimes - Vet1"},
        {"German",
         "1 Abschu\xdf",
         "2 Absch\xfcsse",
         "7 Absch\xfcsse - Veteran",
         "7 Absch\xfcsse - Vet1"},
    };
    for (const auto& language : languages) {
        data::defs::LocaleTable table;
        data::defs::locale_table_init(&table);
        CHECK(data::defs::locale_table_load(&table, &document, language.name));
        OneBufferLookup lookup;
        lookup.table = &table;
        CHECK(kill_line(1, lookup) == language.one);
        CHECK(kill_line(2, lookup) == language.two);
        CHECK(kill_line(7, lookup) == language.seven);
        CHECK(labelled_kill_line(7, kDefaultThresholds, lookup) == language.seven_labelled);
        data::defs::locale_table_free(&table);
    }
    formats::tdf::document_free(&document);
}

} // namespace

int main(int argc, char** argv) {
    if (test::game_data_requested(argc, argv)) {
        const auto assets = test::require_game_assets("the installed side layouts and kill words");
        test_installed_side_layouts(assets);
        test_installed_kill_words(assets);
        return 0;
    }
    test_kill_count();
    test_kill_count_through_one_buffer();
    test_veterancy_label();
    test_veterancy_label_through_one_buffer();
    test_top_panel_rows();
    test_resurrect_spelling();
    test_resource_bar();
    test_side_layout_numbers();
    return 0;
}
