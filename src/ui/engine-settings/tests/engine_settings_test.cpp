// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings' defaults on each platform and preferences file, what they
// read from the preferences and an installation's totala.ini, what they
// write back, and the locks a game, the command line and the renderer put
// on them. The Language & Text switches and text size: their defaults, a
// file without them, a file with CR LF line ends, the round trip, the text
// size's range and the text style the drawing reads. Developer Mode's
// switch and the overrides kept under each profile's id. The Touch
// section's settings: their keys, defaults, words, the hold delay's range
// and stops, unknown values, and the round trip. The Game files section's
// backups switch: Off by default everywhere, read, written and restored as
// every switch.

#include "oa/ui/engine_settings.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

namespace settings = oa::ui::engine_settings;
using oa::platform::preferences::Values;
using settings::HardwareAcceleration;

/// The platform and file the tests run the player's own preferences on.
constexpr settings::Inputs players_own_on_linux{true, false, {}};

/// Returns a preferences map holding one key.
///
/// @param key the key
/// @param value its value
/// @return the map
Values one_key(std::string_view key, std::string value) {
    Values values;
    values[std::string{key}] = std::move(value);
    return values;
}

/// Returns the settings read from one key's value, with the defaults of an
/// explicit preferences file.
///
/// @param key the key
/// @param value its value
/// @return the settings
settings::EngineSettings read_one(std::string_view key, std::string value) {
    return settings::read_settings(one_key(key, std::move(value)), {}, false);
}

void defaults_play_as_without_the_settings() {
    const auto defaults = settings::default_settings({});
    CHECK(defaults.path_search_nodes == settings::base_path_search_nodes);
    CHECK(defaults.wheel_zoom);
    CHECK(!defaults.escape_opens_menu);
    CHECK(!defaults.switch_alt);
    CHECK(defaults.unit_limit == settings::default_unit_limit);
    CHECK(defaults.max_frame_rate == settings::highest_frame_rate);
    CHECK(defaults.anti_aliasing == settings::AntiAliasing::off);
    CHECK(!defaults.frame_stats);
    CHECK(defaults.hardware_acceleration == HardwareAcceleration::off);
    CHECK(!defaults.vertical_sync);
    CHECK(defaults == settings::EngineSettings{});
}

void hardware_acceleration_defaults_to_full_for_the_players_own_file_on_every_machine() {
    // The player's own file: Full on every platform and machine, a light
    // machine and a Raspberry Pi included; Vertical sync Off everywhere.
    const std::array<settings::Inputs, 5> own{{
        players_own_on_linux,
        {true, true, {}},
        {true, false, {}, true},
        {true, false, {}, false, true, {1024, 768}},
        {true, false, {}, true, true, {640, 480}},
    }};
    for (const auto& inputs : own) {
        const auto defaults = settings::default_settings(inputs);
        CHECK(defaults.hardware_acceleration == HardwareAcceleration::full);
        CHECK(!defaults.vertical_sync);
        CHECK(settings::read_settings({}, inputs, false) == defaults);
    }
    // A named file: Off, as the game plays without the setting, on every machine.
    for (auto inputs : own) {
        inputs.players_own_profile = false;
        const auto defaults = settings::default_settings(inputs);
        CHECK(defaults.hardware_acceleration == HardwareAcceleration::off);
        CHECK(!defaults.vertical_sync);
    }
}

void hardware_acceleration_reads_its_words_and_the_switchs_numbers() {
    // The words name the levels, and the levels' order is the dialog's.
    CHECK(settings::hardware_acceleration_text(HardwareAcceleration::off) == "off");
    CHECK(settings::hardware_acceleration_text(HardwareAcceleration::basic) == "basic");
    CHECK(settings::hardware_acceleration_text(HardwareAcceleration::full) == "full");
    for (const auto level : settings::hardware_acceleration_levels)
        CHECK(
            settings::hardware_acceleration_from_text(
                settings::hardware_acceleration_text(level)
            ) == level
        );
    CHECK(settings::hardware_acceleration_levels[0] == HardwareAcceleration::off);
    CHECK(settings::hardware_acceleration_levels[1] == HardwareAcceleration::basic);
    CHECK(settings::hardware_acceleration_levels[2] == HardwareAcceleration::full);
    for (const char* text : {"", "Off", "BASIC", "Full", "on", " full", "full ", "1"})
        CHECK(!settings::hardware_acceleration_from_text(text));
    for (const auto& inputs : {settings::Inputs{}, players_own_on_linux}) {
        const auto read = [&](std::string_view key, const char* text) {
            return settings::read_settings(one_key(key, text), inputs, false);
        };
        const auto level = [&](const char* text) {
            return read(settings::key::hardware_acceleration, text).hardware_acceleration;
        };
        CHECK(level("off") == HardwareAcceleration::off);
        CHECK(level("basic") == HardwareAcceleration::basic);
        CHECK(level("full") == HardwareAcceleration::full);
        // A file the On and Off switch wrote keeps working: 0 is Off and any
        // number above it Full, on either file.
        CHECK(level("0") == HardwareAcceleration::off);
        CHECK(level("1") == HardwareAcceleration::full);
        CHECK(level("7") == HardwareAcceleration::full);
        CHECK(level("-1") == HardwareAcceleration::off);
        CHECK(!read(settings::key::vertical_sync, "0").vertical_sync);
        CHECK(read(settings::key::vertical_sync, "1").vertical_sync);
        // Any other text gives the default: another word or letter case, or
        // what is neither a word nor a whole number. Vertical sync reads
        // only numbers.
        const auto defaults = settings::default_settings(inputs);
        for (const char* text :
             {"Off",
              "BASIC",
              "Full",
              "false",
              "no",
              "on",
              "",
              " 0",
              "0 ",
              "1.0",
              " full",
              "full "}) {
            CHECK(read(settings::key::hardware_acceleration, text) == defaults);
            CHECK(read(settings::key::vertical_sync, text) == defaults);
        }
        CHECK(read(settings::key::vertical_sync, "off") == defaults);
    }
    // Written as its word, and Vertical sync as 1 or 0, only when changed;
    // Restore defaults erases each at its default, the player's own file's
    // Full included.
    const auto own = settings::default_settings(players_own_on_linux);
    auto off = own;
    off.hardware_acceleration = HardwareAcceleration::off;
    off.vertical_sync = true;
    Values values;
    settings::write_settings(values, own, off, own, false);
    CHECK(values.size() == 2);
    CHECK(values.at(std::string{settings::key::hardware_acceleration}) == "off");
    CHECK(values.at(std::string{settings::key::vertical_sync}) == "1");
    CHECK(settings::read_settings(values, players_own_on_linux, false) == off);
    settings::write_settings(values, off, own, own, true);
    CHECK(values.empty());
    // Back to Full by hand, on the player's own file, writes full over the
    // switch's 0; Basic writes basic.
    values = one_key(settings::key::hardware_acceleration, "0");
    settings::write_settings(values, off, own, own, false);
    CHECK(values.at(std::string{settings::key::hardware_acceleration}) == "full");
    auto basic = own;
    basic.hardware_acceleration = HardwareAcceleration::basic;
    settings::write_settings(values, own, basic, own, false);
    CHECK(values.at(std::string{settings::key::hardware_acceleration}) == "basic");
    CHECK(settings::read_settings(values, players_own_on_linux, false) == basic);
    // A file that still holds the switch's 1 reads as the player's own
    // default, so Restore defaults then OK erases it.
    values = one_key(settings::key::hardware_acceleration, "1");
    CHECK(settings::read_settings(values, players_own_on_linux, false) == own);
    settings::write_settings(values, own, own, own, true);
    CHECK(values.empty());
}

void escape_opens_the_menu_by_default_only_on_macos_with_the_players_own_file() {
    CHECK(settings::default_settings({true, true, {}}).escape_opens_menu);
    CHECK(!settings::default_settings({false, true, {}}).escape_opens_menu);
    CHECK(!settings::default_settings(players_own_on_linux).escape_opens_menu);
    CHECK(!settings::default_settings({}).escape_opens_menu);
}

void the_unit_limit_defaults_to_the_installations_with_the_players_own_file() {
    constexpr std::string_view ini = "[Preferences]\r\nUnitLimit=500\r\n";
    CHECK(settings::default_settings({true, false, ini}).unit_limit == 500);
    CHECK(settings::default_settings({true, true, ini}).unit_limit == 500);
    // A named preferences file never reads the installation.
    CHECK(settings::default_settings({false, false, ini}).unit_limit == 250);
    CHECK(settings::default_settings({true, false, "[Preferences]\n"}).unit_limit == 250);
    CHECK(settings::default_settings({true, false, ""}).unit_limit == 250);
    // The installation's value is a default: a stored key wins over it.
    const auto stored = settings::read_settings(
        one_key(settings::key::unit_limit, "300"), {true, false, ini}, false
    );
    CHECK(stored.unit_limit == 300);
    const auto absent = settings::read_settings({}, {true, false, ini}, false);
    CHECK(absent.unit_limit == 500);
}

void a_raspberry_pi_starts_at_60_frames_without_anti_aliasing() {
    constexpr settings::Inputs pi{true, false, {}, true};
    const auto defaults = settings::default_settings(pi);
    CHECK(defaults.max_frame_rate == settings::raspberry_pi_frame_rate);
    CHECK(defaults.max_frame_rate == 60);
    CHECK(defaults.anti_aliasing == settings::AntiAliasing::off);
    // Every other default is as on any other machine.
    auto others = defaults;
    others.max_frame_rate = settings::highest_frame_rate;
    CHECK(others == settings::default_settings(players_own_on_linux));
    // A named preferences file plays as the game does everywhere.
    CHECK(settings::default_settings({false, false, {}, true}) == settings::default_settings({}));
    CHECK(settings::default_settings(players_own_on_linux).max_frame_rate == 120);

    // Without a stored value the Pi's defaults hold; a stored value wins.
    CHECK(settings::read_settings({}, pi, false) == defaults);
    const auto faster =
        settings::read_settings(one_key(settings::key::max_frame_rate, "120"), pi, false);
    CHECK(faster.max_frame_rate == 120);
    const auto smoother =
        settings::read_settings(one_key(settings::key::anti_aliasing, "4"), pi, false);
    CHECK(smoother.anti_aliasing == settings::AntiAliasing::x4);
    CHECK(smoother.max_frame_rate == 60);

    // The player's choice is stored; Restore defaults puts the Pi's back.
    auto chosen = defaults;
    chosen.max_frame_rate = 90;
    chosen.anti_aliasing = settings::AntiAliasing::x2;
    Values values;
    settings::write_settings(values, defaults, chosen, defaults, false);
    CHECK(values.at(std::string{settings::key::max_frame_rate}) == "90");
    CHECK(values.at(std::string{settings::key::anti_aliasing}) == "2");
    CHECK(settings::read_settings(values, pi, false) == chosen);
    settings::write_settings(values, chosen, defaults, defaults, true);
    CHECK(values.empty());
    CHECK(settings::read_settings(values, pi, false) == defaults);
}

void a_light_machine_starts_at_800x600_and_60_frames_without_anti_aliasing() {
    settings::Inputs light{true, false, {}, false, true, {1024, 768}};
    const auto defaults = settings::default_settings(light);
    CHECK(defaults.max_frame_rate == settings::light_machine_frame_rate);
    CHECK(defaults.max_frame_rate == 60);
    CHECK(defaults.anti_aliasing == settings::AntiAliasing::off);
    CHECK(defaults.screen_size == settings::light_machine_screen_size);
    CHECK((defaults.screen_size == settings::ScreenSize{800, 600}));
    // Every other default is as on any other machine.
    auto others = defaults;
    others.max_frame_rate = settings::highest_frame_rate;
    others.screen_size = settings::desktop_screen_size;
    CHECK(others == settings::default_settings(players_own_on_linux));
    // A desktop narrower or shorter than 800x600 starts at 640x480; an
    // unknown one at 800x600.
    light.desktop = {640, 480};
    CHECK(settings::default_settings(light).screen_size == settings::small_desktop_screen_size);
    light.desktop = {800, 480};
    CHECK((settings::default_settings(light).screen_size == settings::ScreenSize{640, 480}));
    light.desktop = {720, 600};
    CHECK((settings::default_settings(light).screen_size == settings::ScreenSize{640, 480}));
    light.desktop = {800, 600};
    CHECK((settings::default_settings(light).screen_size == settings::ScreenSize{800, 600}));
    light.desktop = {};
    CHECK((settings::default_settings(light).screen_size == settings::ScreenSize{800, 600}));
    // A named preferences file plays as the game does everywhere, and a
    // machine that is not light starts at the desktop's size.
    CHECK(
        settings::default_settings({false, false, {}, false, true, {1024, 768}}) ==
        settings::default_settings({})
    );
    CHECK(
        settings::default_settings({true, false, {}, false, false, {1024, 768}}).screen_size ==
        settings::desktop_screen_size
    );

    // Without a stored value the light defaults hold; a stored value wins.
    light.desktop = {1024, 768};
    CHECK(settings::read_settings({}, light, false) == defaults);
    const auto desktop =
        settings::read_settings(one_key(settings::key::screen_size, "desktop"), light, false);
    CHECK(desktop.screen_size == settings::desktop_screen_size);
    CHECK(desktop.max_frame_rate == 60);
    const auto larger =
        settings::read_settings(one_key(settings::key::screen_size, "1024x768"), light, false);
    CHECK((larger.screen_size == settings::ScreenSize{1024, 768}));

    // The player's choice is stored; Restore defaults puts the light machine's back.
    auto chosen = defaults;
    chosen.max_frame_rate = 120;
    chosen.screen_size = settings::desktop_screen_size;
    Values values;
    settings::write_settings(values, defaults, chosen, defaults, false);
    CHECK(values.at(std::string{settings::key::max_frame_rate}) == "120");
    CHECK(values.at(std::string{settings::key::screen_size}) == "desktop");
    CHECK(settings::read_settings(values, light, false) == chosen);
    settings::write_settings(values, chosen, defaults, defaults, true);
    CHECK(values.empty());
    CHECK(settings::read_settings(values, light, false) == defaults);
}

void screen_sizes_are_stored_as_text() {
    CHECK(settings::screen_size_text(settings::desktop_screen_size) == "desktop");
    CHECK(settings::screen_size_text({640, 480}) == "640x480");
    CHECK(settings::screen_size_text({1280, 1024}) == "1280x1024");
    for (const auto size : settings::screen_sizes)
        CHECK(settings::screen_size_from_text(settings::screen_size_text(size)) == size);
    // Only the offered sizes, written as stored, are read.
    CHECK(!settings::screen_size_from_text(""));
    CHECK(!settings::screen_size_from_text("Desktop"));
    CHECK(!settings::screen_size_from_text("800X600"));
    CHECK(!settings::screen_size_from_text("800x600 "));
    CHECK(!settings::screen_size_from_text("1920x1080"));
    CHECK(!settings::screen_size_from_text("0x0"));
    CHECK(
        read_one(settings::key::screen_size, "1920x1080").screen_size ==
        settings::desktop_screen_size
    );
    CHECK(
        (read_one(settings::key::screen_size, "640x480").screen_size ==
         settings::ScreenSize{640, 480})
    );
}

void totala_ini_gives_its_unit_limit_as_the_game_reads_it() {
    using settings::installation_unit_limit;
    CHECK(!installation_unit_limit(""));
    CHECK(!installation_unit_limit("UnitLimit=400\n"));
    CHECK(!installation_unit_limit("[Preferences]\nOther=1\n"));
    CHECK(!installation_unit_limit("[Other]\nUnitLimit=400\n"));
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=400\n") == 400);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=400") == 400);
    CHECK(installation_unit_limit("[preferences]\r\nunitlimit = 400\r\n") == 400);
    CHECK(installation_unit_limit("[ PREFERENCES ]\r\n  UNITLIMIT\t=\t333 \r\n") == 333);
    CHECK(installation_unit_limit("[Other]\nUnitLimit=99\n[Preferences]\nUnitLimit=400\n") == 400);
    // Values: the leading whole number, then 3.1c's own clamp.
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=abc\n") == 20);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=\n") == 20);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=3\n") == 20);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=-40\n") == 20);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=9999\n") == 500);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=99999999999999999999\n") == 500);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=300 ; more\n") == 300);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=300units\n") == 300);
    // Comments, the first line of the key and the first section count.
    CHECK(installation_unit_limit("[Preferences]\n;UnitLimit=100\nUnitLimit=400\n") == 400);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=400\nUnitLimit=100\n") == 400);
    CHECK(
        !installation_unit_limit("[Preferences]\nA=1\n[Other]\nB=2\n[Preferences]\nUnitLimit=400\n")
    );
    // Only the first installation_ini_limit bytes are read.
    const std::string head = "[Preferences]\n;";
    std::string padded =
        head + std::string(settings::installation_ini_limit - head.size() - 1, 'x');
    padded += "\nUnitLimit=400\n";
    CHECK(!installation_unit_limit(padded));
    std::string inside =
        head + std::string(settings::installation_ini_limit - head.size() - 20, 'x');
    inside += "\nUnitLimit=400\n";
    CHECK(installation_unit_limit(inside) == 400);
}

void absent_keys_and_values_that_are_not_numbers_give_the_defaults() {
    CHECK(settings::read_settings({}, {}, false) == settings::default_settings({}));
    for (const std::string_view key :
         {settings::key::path_search_nodes,
          settings::key::wheel_zoom,
          settings::key::escape_opens_menu,
          settings::key::unit_limit,
          settings::key::max_frame_rate,
          settings::key::anti_aliasing,
          settings::key::frame_stats}) {
        for (const char* text : {"", "abc", "12x", " 5", "5 ", "-", "+", "0x10", "1.5"})
            CHECK(read_one(key, text) == settings::default_settings({}));
    }
    // Other keys, the game's own included, leave the settings alone.
    Values others;
    others["Total Annihilation|UnitLimit"] = "1000";
    others["open-annihilation.game-directory"] = "/games/ta";
    CHECK(settings::read_settings(others, {}, false) == settings::default_settings({}));
}

void stored_values_are_read_and_clamped_into_their_ranges() {
    CHECK(read_one(settings::key::path_search_nodes, "5332").path_search_nodes == 5332);
    CHECK(read_one(settings::key::path_search_nodes, "2000").path_search_nodes == 2000);
    CHECK(read_one(settings::key::path_search_nodes, "+2666").path_search_nodes == 2666);
    CHECK(read_one(settings::key::path_search_nodes, "1").path_search_nodes == 1333);
    CHECK(read_one(settings::key::path_search_nodes, "-9").path_search_nodes == 1333);
    CHECK(read_one(settings::key::path_search_nodes, "99999").path_search_nodes == 10664);
    CHECK(
        read_one(settings::key::path_search_nodes, "999999999999999999999999").path_search_nodes ==
        10664
    );

    CHECK(!read_one(settings::key::wheel_zoom, "0").wheel_zoom);
    CHECK(read_one(settings::key::wheel_zoom, "1").wheel_zoom);
    CHECK(!read_one(settings::key::wheel_zoom, "-1").wheel_zoom);
    CHECK(read_one(settings::key::escape_opens_menu, "1").escape_opens_menu);
    CHECK(read_one(settings::key::escape_opens_menu, "7").escape_opens_menu);
    CHECK(!settings::read_settings(
               one_key(settings::key::escape_opens_menu, "0"), {true, true, {}}, false
    )
               .escape_opens_menu);
    CHECK(read_one(settings::key::frame_stats, "1").frame_stats);

    CHECK(read_one(settings::key::unit_limit, "1500").unit_limit == 1500);
    CHECK(read_one(settings::key::unit_limit, "477").unit_limit == 477);
    CHECK(read_one(settings::key::unit_limit, "30").unit_limit == 30);
    CHECK(read_one(settings::key::unit_limit, "5").unit_limit == 20);
    CHECK(read_one(settings::key::unit_limit, "4000").unit_limit == 1500);

    CHECK(read_one(settings::key::max_frame_rate, "60").max_frame_rate == 60);
    CHECK(read_one(settings::key::max_frame_rate, "63").max_frame_rate == 63);
    CHECK(read_one(settings::key::max_frame_rate, "35").max_frame_rate == 35);
    CHECK(read_one(settings::key::max_frame_rate, "30").max_frame_rate == 30);
    CHECK(read_one(settings::key::max_frame_rate, "29").max_frame_rate == 30);
    CHECK(read_one(settings::key::max_frame_rate, "10").max_frame_rate == 30);
    CHECK(read_one(settings::key::max_frame_rate, "0").max_frame_rate == 30);
    CHECK(read_one(settings::key::max_frame_rate, "500").max_frame_rate == 120);

    using settings::AntiAliasing;
    const auto level = [](const char* text) {
        return read_one(settings::key::anti_aliasing, text).anti_aliasing;
    };
    CHECK(level("-3") == AntiAliasing::off);
    CHECK(level("0") == AntiAliasing::off);
    CHECK(level("1") == AntiAliasing::off);
    CHECK(level("2") == AntiAliasing::x2);
    CHECK(level("3") == AntiAliasing::x2);
    CHECK(level("4") == AntiAliasing::x4);
    CHECK(level("5") == AntiAliasing::x4);
    CHECK(level("8") == AntiAliasing::x8);
    CHECK(level("15") == AntiAliasing::x8);
    CHECK(level("16") == AntiAliasing::x16);
    CHECK(level("100") == AntiAliasing::x16);
}

void switch_alt_comes_from_the_games_own_key() {
    CHECK(settings::read_settings({}, {}, true).switch_alt);
    CHECK(!settings::read_settings({}, {}, false).switch_alt);
    Values values;
    settings::EngineSettings chosen{};
    chosen.switch_alt = true;
    settings::write_settings(values, {}, chosen, {}, false);
    settings::write_settings(values, {}, chosen, {}, true);
    CHECK(values.empty());
}

/// Returns settings with every value away from its default.
settings::EngineSettings changed_settings() {
    settings::EngineSettings chosen{};
    chosen.path_search_nodes = 4 * settings::base_path_search_nodes;
    chosen.wheel_zoom = false;
    chosen.escape_opens_menu = true;
    chosen.unit_limit = 1000;
    chosen.max_frame_rate = 60;
    chosen.anti_aliasing = settings::AntiAliasing::x16;
    chosen.frame_stats = true;
    chosen.screen_size = {1024, 768};
    chosen.hardware_acceleration = HardwareAcceleration::basic;
    chosen.vertical_sync = true;
    chosen.modern_fonts = true;
    chosen.text_outline = false;
    chosen.text_shadow = false;
    chosen.text_background = true;
    chosen.text_size = 150;
    chosen.language = "de";
    return chosen;
}

void only_changed_settings_are_written() {
    const settings::EngineSettings defaults{};
    Values values;
    values["Total Annihilation|SwitchAlt"] = "1";
    settings::write_settings(values, defaults, defaults, defaults, false);
    CHECK(values.size() == 1);

    const auto chosen = changed_settings();
    settings::write_settings(values, defaults, chosen, defaults, false);
    CHECK(values.size() == 17);
    CHECK(values.at(std::string{settings::key::path_search_nodes}) == "5332");
    CHECK(values.at(std::string{settings::key::wheel_zoom}) == "0");
    CHECK(values.at(std::string{settings::key::escape_opens_menu}) == "1");
    CHECK(values.at(std::string{settings::key::unit_limit}) == "1000");
    CHECK(values.at(std::string{settings::key::max_frame_rate}) == "60");
    CHECK(values.at(std::string{settings::key::anti_aliasing}) == "16");
    CHECK(values.at(std::string{settings::key::frame_stats}) == "1");
    CHECK(values.at(std::string{settings::key::screen_size}) == "1024x768");
    CHECK(values.at(std::string{settings::key::hardware_acceleration}) == "basic");
    CHECK(values.at(std::string{settings::key::vertical_sync}) == "1");
    CHECK(values.at(std::string{settings::key::modern_fonts}) == "1");
    CHECK(values.at(std::string{settings::key::text_outline}) == "0");
    CHECK(values.at(std::string{settings::key::text_shadow}) == "0");
    CHECK(values.at(std::string{settings::key::text_background}) == "1");
    CHECK(values.at(std::string{settings::key::text_size}) == "150");
    CHECK(values.at(std::string{settings::key::language}) == "de");
    CHECK(values.at("Total Annihilation|SwitchAlt") == "1");
    CHECK(settings::read_settings(values, {}, false) == chosen);

    // One setting changed: only its key is written, even back to its default.
    Values one;
    auto back = chosen;
    back.max_frame_rate = settings::highest_frame_rate;
    settings::write_settings(one, chosen, back, defaults, false);
    CHECK(one.size() == 1);
    CHECK(one.at(std::string{settings::key::max_frame_rate}) == "120");

    // An unchanged setting keeps whatever its key holds.
    Values kept = one_key(settings::key::unit_limit, "garbage");
    settings::write_settings(kept, defaults, defaults, defaults, false);
    CHECK(kept.at(std::string{settings::key::unit_limit}) == "garbage");
}

void restore_defaults_erases_the_keys_of_settings_at_their_defaults() {
    const settings::EngineSettings defaults{};
    const auto stored = changed_settings();
    Values values;
    settings::write_settings(values, defaults, stored, defaults, false);
    values["Total Annihilation|SwitchAlt"] = "1";

    // Restore defaults, then OK: every key goes, and other keys stay.
    Values restored = values;
    settings::write_settings(restored, stored, defaults, defaults, true);
    CHECK(restored.size() == 1);
    CHECK(restored.count("Total Annihilation|SwitchAlt") == 1);
    CHECK(settings::read_settings(restored, {}, false) == defaults);

    // Restore defaults, then a setting set again: it is written; one set back
    // to the value the dialog opened with keeps its key.
    auto chosen = defaults;
    chosen.unit_limit = 750;
    chosen.anti_aliasing = stored.anti_aliasing;
    Values again = values;
    settings::write_settings(again, stored, chosen, defaults, true);
    CHECK(again.size() == 3);
    CHECK(again.at(std::string{settings::key::unit_limit}) == "750");
    CHECK(again.at(std::string{settings::key::anti_aliasing}) == "16");
    CHECK(settings::read_settings(again, {}, false) == chosen);

    // A default that depends on the platform is erased, not written.
    const auto mac_defaults = settings::default_settings({true, true, {}});
    Values mac = one_key(settings::key::escape_opens_menu, "0");
    auto mac_opened = mac_defaults;
    mac_opened.escape_opens_menu = false;
    settings::write_settings(mac, mac_opened, mac_defaults, mac_defaults, true);
    CHECK(mac.empty());
}

void the_round_trip_keeps_every_value() {
    for (const auto level : settings::anti_aliasing_levels) {
        auto chosen = changed_settings();
        chosen.anti_aliasing = level;
        chosen.unit_limit = settings::lowest_unit_limit;
        chosen.max_frame_rate = settings::lowest_frame_rate;
        chosen.path_search_nodes = settings::highest_path_search_nodes;
        chosen.screen_size =
            settings::screen_sizes[static_cast<std::size_t>(level) % settings::screen_sizes.size()];
        Values values;
        settings::write_settings(values, {}, chosen, {}, false);
        CHECK(settings::read_settings(values, players_own_on_linux, false) == chosen);
        // Every level of Hardware acceleration, against the player's own
        // file's Full.
        const auto own = settings::default_settings(players_own_on_linux);
        chosen.vertical_sync = false;
        for (const auto acceleration : settings::hardware_acceleration_levels) {
            chosen.hardware_acceleration = acceleration;
            Values against_own;
            settings::write_settings(against_own, own, chosen, own, false);
            CHECK(
                settings::read_settings(against_own, players_own_on_linux, false)
                    .hardware_acceleration == acceleration
            );
            CHECK(!settings::read_settings(against_own, players_own_on_linux, false).vertical_sync);
        }
    }
}

void the_frame_rate_goes_down_to_a_frame_a_tick() {
    // The lowest maximum frame rate is the simulation's 30 ticks a second,
    // and the setting's steps reach it from the highest.
    CHECK(settings::lowest_frame_rate == 30);
    CHECK(
        (settings::highest_frame_rate - settings::lowest_frame_rate) % settings::frame_rate_step ==
        0
    );
    settings::EngineSettings chosen{};
    chosen.max_frame_rate = settings::lowest_frame_rate;
    Values values;
    settings::write_settings(values, {}, chosen, {}, false);
    CHECK(values.at(std::string{settings::key::max_frame_rate}) == "30");
    CHECK(settings::read_settings(values, players_own_on_linux, false).max_frame_rate == 30);
}

void the_path_credit_shows_as_whole_cycles() {
    CHECK(settings::path_search_multiplier(1333) == 1);
    CHECK(settings::path_search_multiplier(1999) == 1);
    CHECK(settings::path_search_multiplier(2000) == 2);
    CHECK(settings::path_search_multiplier(2666) == 2);
    CHECK(settings::path_search_multiplier(10664) == 8);
    CHECK(settings::path_search_multiplier(0) == 1);
    CHECK(settings::path_search_multiplier(-5000) == 1);
    CHECK(settings::path_search_multiplier(1'000'000) == 8);
    CHECK(settings::path_search_multiplier(2'147'483'647) == 8);
}

void shared_games_and_replays_search_at_one_cycle() {
    settings::EngineSettings chosen{};
    chosen.path_search_nodes = 3 * settings::base_path_search_nodes;
    CHECK(settings::match_path_search_nodes(chosen, false) == 3999);
    CHECK(settings::match_path_search_nodes(chosen, true) == settings::base_path_search_nodes);
}

void a_mods_unit_limits_set_the_range_and_the_default() {
    // A mod's limits of 1,500 in 20 to 1,500, and the largest a profile may name.
    settings::Inputs raised{};
    raised.units_per_player = {1500, 20, 1500, 1500};
    CHECK(settings::default_settings(raised).unit_limit == 1500);
    CHECK(settings::highest_offered_unit_limit(raised.units_per_player) == 1500);
    raised.players_own_profile = true;
    raised.installation_ini = "[Preferences]\nUnitLimit=1200\n";
    CHECK(settings::default_settings(raised).unit_limit == 1200);
    settings::Inputs largest{};
    largest.units_per_player = {
        oa::data::limits::highest_units_per_player,
        20,
        oa::data::limits::highest_units_per_player,
        1500
    };
    CHECK(
        settings::highest_offered_unit_limit(largest.units_per_player) ==
        oa::data::limits::highest_units_per_player
    );
    const auto stored = [&](const char* text) {
        return settings::read_settings(one_key(settings::key::unit_limit, text), largest, false)
            .unit_limit;
    };
    CHECK(stored("6000") == 6000);
    CHECK(stored("99999") == oa::data::limits::highest_units_per_player);
    CHECK(stored("5") == 20);
    // 3.1c's limits keep the setting's own highest stop.
    CHECK(settings::highest_offered_unit_limit({}) == settings::highest_unit_limit);
}

void a_mods_path_budget_scales_the_credit() {
    settings::EngineSettings chosen{};
    chosen.path_search_nodes = 3 * settings::base_path_search_nodes;
    CHECK(settings::match_path_search_nodes(chosen, false, 66650) == 3 * 66650);
    CHECK(settings::match_path_search_nodes(chosen, true, 66650) == 66650);
    // The largest budget a profile may name, times eight, holds to that budget.
    chosen.path_search_nodes = settings::highest_path_search_nodes;
    CHECK(
        settings::match_path_search_nodes(
            chosen, false, oa::data::limits::highest_path_search_nodes
        ) == oa::data::limits::highest_path_search_nodes
    );
    CHECK(
        settings::match_path_search_nodes(
            chosen, true, oa::data::limits::highest_path_search_nodes
        ) == oa::data::limits::highest_path_search_nodes
    );
}

void a_game_locks_the_next_game_settings() {
    const auto alone = settings::settings_locks({true, false, false, false});
    CHECK(alone.path_search == settings::Lock::in_game);
    CHECK(alone.unit_limit == settings::Lock::in_game);
    CHECK(alone.max_frame_rate == settings::Lock::none);
    CHECK(!alone.shared_game);
    const auto shared = settings::settings_locks({true, true, false, false});
    CHECK(shared.path_search == settings::Lock::set_by_host);
    CHECK(shared.unit_limit == settings::Lock::set_by_host);
    CHECK(shared.shared_game);
    const auto replay = settings::settings_locks({true, false, true, false});
    CHECK(replay.path_search == settings::Lock::set_by_host);
    CHECK(replay.unit_limit == settings::Lock::set_by_host);
    CHECK(!replay.shared_game);
    const auto menu = settings::settings_locks({});
    CHECK(menu.path_search == settings::Lock::none);
    CHECK(menu.unit_limit == settings::Lock::none);
    CHECK(!menu.shared_game);
    const auto command_line = settings::settings_locks({false, false, false, true});
    CHECK(command_line.max_frame_rate == settings::Lock::command_line);
    CHECK(command_line.path_search == settings::Lock::none);
    // --max-fps locks only its own row.
    CHECK(command_line.hardware_acceleration == settings::Lock::none);
    CHECK(command_line.vertical_sync == settings::Lock::none);
}

void the_renderer_settings_lock_by_the_flags_and_the_renderer() {
    using settings::Lock;
    // Hardware acceleration: either flag, then nothing in the game that
    // could help; never a game, shared or not, so Off stays possible.
    for (const bool in_game : {false, true})
        for (const bool shared : {false, true})
            for (const bool replay : {false, true}) {
                settings::GameState state{in_game, shared, replay, false};
                CHECK(settings::settings_locks(state).hardware_acceleration == Lock::none);
                state.acceleration_unavailable = true;
                CHECK(settings::settings_locks(state).hardware_acceleration == Lock::unavailable);
                state.renderer_from_command_line = true;
                CHECK(settings::settings_locks(state).hardware_acceleration == Lock::command_line);
                state.acceleration_unavailable = false;
                CHECK(settings::settings_locks(state).hardware_acceleration == Lock::command_line);
                // Neither touches Vertical sync or the other rows.
                const auto locks = settings::settings_locks(state);
                const auto plain =
                    settings::settings_locks(settings::GameState{in_game, shared, replay, false});
                CHECK(locks.vertical_sync == plain.vertical_sync);
                CHECK(locks.path_search == plain.path_search);
                CHECK(locks.max_frame_rate == Lock::none);
            }
    // Vertical sync: during a shared game or a replay, not in a game played
    // alone; a renderer that cannot wait for the display wins over the game.
    CHECK(settings::settings_locks({}).vertical_sync == Lock::none);
    CHECK(settings::settings_locks({true, false, false, false}).vertical_sync == Lock::none);
    CHECK(settings::settings_locks({true, true, false, false}).vertical_sync == Lock::in_game);
    CHECK(settings::settings_locks({true, false, true, false}).vertical_sync == Lock::in_game);
    // A shared or replay flag outside a game locks nothing.
    CHECK(settings::settings_locks({false, true, true, false}).vertical_sync == Lock::none);
    for (const bool in_game : {false, true})
        for (const bool shared : {false, true}) {
            settings::GameState state{in_game, shared, false, false};
            state.vertical_sync_unavailable = true;
            CHECK(settings::settings_locks(state).vertical_sync == Lock::unavailable);
            CHECK(settings::settings_locks(state).hardware_acceleration == Lock::none);
        }
}

/// The Language & Text switches' keys, and the text size's.
constexpr std::array<std::string_view, 5> text_keys{
    settings::key::modern_fonts,
    settings::key::text_outline,
    settings::key::text_shadow,
    settings::key::text_background,
    settings::key::text_size,
};

void language_and_text_defaults_to_modern_fonts_with_outline_and_shadow() {
    // The player's own file, on every platform and machine: modern fonts,
    // their outline and shadow On, the background Off; the text style the
    // drawing reads is TextStyle's own default.
    const std::array<settings::Inputs, 4> own{{
        players_own_on_linux,
        {true, true, {}},
        {true, false, {}, true},
        {true, false, {}, false, true, {1024, 768}},
    }};
    for (const auto& inputs : own) {
        const auto defaults = settings::default_settings(inputs);
        CHECK(defaults.modern_fonts);
        CHECK(defaults.text_outline);
        CHECK(defaults.text_shadow);
        CHECK(!defaults.text_background);
        CHECK(defaults.text_size == 80);
        CHECK(settings::text_style(defaults) == oa::present::TextStyle{});
        CHECK(settings::text_style(defaults).size == 80);
    }
    // A named file draws game text in the game's own fonts; the outline,
    // shadow and background keep their defaults.
    for (auto inputs : own) {
        inputs.players_own_profile = false;
        const auto defaults = settings::default_settings(inputs);
        CHECK(!defaults.modern_fonts);
        CHECK(defaults.text_outline && defaults.text_shadow && !defaults.text_background);
        CHECK(defaults.text_size == 80);
        const auto style = settings::text_style(defaults);
        CHECK(!style.modern_fonts && style.outline && style.shadow && !style.background);
        CHECK(style.size == 80);
    }
}

void a_file_without_the_text_switches_reads_their_defaults() {
    // A file an earlier version wrote: other settings and the game's own
    // keys, and none of Language & Text's.
    Values earlier;
    earlier[std::string{settings::key::wheel_zoom}] = "0";
    earlier[std::string{settings::key::max_frame_rate}] = "60";
    earlier[std::string{settings::key::hardware_acceleration}] = "basic";
    earlier["Total Annihilation|UnitLimit"] = "1000";
    for (const auto& inputs : {settings::Inputs{}, players_own_on_linux}) {
        const auto defaults = settings::default_settings(inputs);
        const auto read = settings::read_settings(earlier, inputs, false);
        CHECK(!read.wheel_zoom && read.max_frame_rate == 60);
        CHECK(read.modern_fonts == defaults.modern_fonts);
        CHECK(read.text_outline == defaults.text_outline);
        CHECK(read.text_shadow == defaults.text_shadow);
        CHECK(read.text_background == defaults.text_background);
        // Without its key the text size is the default, 80%.
        CHECK(read.text_size == 80);
        CHECK(settings::text_style(read) == settings::text_style(defaults));
        // A value that is no whole number gives the default too.
        for (const auto key : text_keys)
            for (const char* text : {"", "on", "true", " 1", "1.0"})
                CHECK(
                    settings::text_style(
                        settings::read_settings(one_key(key, text), inputs, false)
                    ) == settings::text_style(defaults)
                );
    }
    // Each reads as a switch: on above 0.
    const auto own = [](std::string_view key, const char* text) {
        return settings::read_settings(one_key(key, text), players_own_on_linux, false);
    };
    CHECK(!own(settings::key::modern_fonts, "0").modern_fonts);
    CHECK(read_one(settings::key::modern_fonts, "1").modern_fonts);
    CHECK(!own(settings::key::text_outline, "0").text_outline);
    CHECK(!own(settings::key::text_shadow, "-1").text_shadow);
    CHECK(own(settings::key::text_background, "1").text_background);
    CHECK(own(settings::key::text_background, "3").text_background);
}

void the_text_switches_round_trip_and_restore() {
    const auto defaults = settings::default_settings(players_own_on_linux);
    // Every combination of the four, chosen in a dialog opened on the
    // defaults: only those away from it are written, and they read back.
    for (uint32_t bits = 0; bits < 16; ++bits) {
        auto chosen = defaults;
        chosen.modern_fonts = (bits & 1U) != 0;
        chosen.text_outline = (bits & 2U) != 0;
        chosen.text_shadow = (bits & 4U) != 0;
        chosen.text_background = (bits & 8U) != 0;
        Values values;
        settings::write_settings(values, defaults, chosen, defaults, false);
        std::size_t changed = 0;
        for (const auto& [key, member] :
             std::array<std::pair<std::string_view, bool settings::EngineSettings::*>, 4>{{
                 {settings::key::modern_fonts, &settings::EngineSettings::modern_fonts},
                 {settings::key::text_outline, &settings::EngineSettings::text_outline},
                 {settings::key::text_shadow, &settings::EngineSettings::text_shadow},
                 {settings::key::text_background, &settings::EngineSettings::text_background},
             }}) {
            if (chosen.*member == defaults.*member)
                continue;
            ++changed;
            CHECK(values.at(std::string{key}) == (chosen.*member ? "1" : "0"));
        }
        CHECK(values.size() == changed);
        const auto read = settings::read_settings(values, players_own_on_linux, false);
        CHECK(read == chosen);
        CHECK(settings::text_style(read) == settings::text_style(chosen));
        // Restore defaults, then OK: their keys go.
        Values restored = values;
        settings::write_settings(restored, chosen, defaults, defaults, true);
        CHECK(restored.empty());
    }
}

void the_text_size_reads_writes_and_restores_in_its_range() {
    CHECK(settings::key::text_size == "open-annihilation.text-size");
    CHECK(settings::lowest_text_size == 50 && settings::highest_text_size == 300);
    CHECK(settings::text_size_step == 10 && settings::default_text_size == 80);
    // Stored sizes are clamped into 50 to 300, and one between the steps
    // is kept as stored; what is no whole number gives the default.
    const auto size = [](const char* text) {
        return settings::read_settings(
                   one_key(settings::key::text_size, text), players_own_on_linux, false
        )
            .text_size;
    };
    CHECK(size("50") == 50 && size("80") == 80 && size("150") == 150 && size("300") == 300);
    CHECK(size("85") == 85 && size("+120") == 120);
    CHECK(size("10") == 50 && size("0") == 50 && size("-20") == 50);
    CHECK(size("301") == 300 && size("99999999999999") == 300);
    for (const char* text : {"", "big", "80%", " 80", "80 ", "1.5", "0x50"})
        CHECK(size(text) == 80);
    // It reads the same with a named file, where modern fonts start Off.
    CHECK(read_one(settings::key::text_size, "200").text_size == 200);
    CHECK(read_one(settings::key::text_size, "200").modern_fonts == false);

    // Written as its percent only when it changed, and read back; Restore
    // defaults, then OK, erases it.
    const auto defaults = settings::default_settings(players_own_on_linux);
    for (int32_t percent = settings::lowest_text_size; percent <= settings::highest_text_size;
         percent += settings::text_size_step) {
        auto chosen = defaults;
        chosen.text_size = percent;
        Values values;
        settings::write_settings(values, defaults, chosen, defaults, false);
        if (percent == settings::default_text_size) {
            CHECK(values.empty());
            continue;
        }
        CHECK(values.size() == 1);
        CHECK(values.at(std::string{settings::key::text_size}) == std::to_string(percent));
        const auto read = settings::read_settings(values, players_own_on_linux, false);
        CHECK(read == chosen);
        CHECK(settings::text_style(read).size == percent);
        settings::write_settings(values, chosen, defaults, defaults, true);
        CHECK(values.empty());
    }
    // Back to the default by hand keeps a key, written as 80.
    Values values = one_key(settings::key::text_size, "150");
    auto larger = defaults;
    larger.text_size = 150;
    settings::write_settings(values, larger, defaults, defaults, false);
    CHECK(values.at(std::string{settings::key::text_size}) == "80");
}

void the_language_reads_writes_and_restores() {
    namespace languages = oa::data::languages;
    // The player's own file follows the operating system; a named file plays
    // in English, as the game does without a language on its command line.
    CHECK(settings::default_settings(players_own_on_linux).language == "system");
    CHECK(settings::default_settings(settings::Inputs{}).language == "en");
    // A file without the key, as an older version wrote, reads as the
    // default: the system's language with the player's own file.
    Values earlier;
    earlier[std::string{settings::key::wheel_zoom}] = "0";
    CHECK(settings::read_settings(earlier, players_own_on_linux, false).language == "system");
    CHECK(settings::read_settings(earlier, settings::Inputs{}, false).language == "en");
    CHECK(settings::stored_language(earlier, true) == "system");
    // "system" or a known tag, in any case and with spaces around it; any
    // other value, a tag this build does not know included, the default.
    const auto read = [](const char* text, bool own) {
        return settings::stored_language(one_key(settings::key::language, text), own);
    };
    CHECK(read("system", false) == "system");
    CHECK(read("System", false) == "system");
    CHECK(read("de", true) == "de");
    CHECK(read("DE", true) == "de");
    CHECK(read(" fr ", true) == "fr");
    CHECK(read("es", true) == "es" && read("it", true) == "it" && read("en", true) == "en");
    for (const char* other : {"", "pt", "de-AT", "german", "1", "xx_YY"}) {
        CHECK(read(other, true) == "system");
        CHECK(read(other, false) == "en");
    }
    for (const auto& language : languages::known_languages())
        CHECK(read(std::string(language.tag).c_str(), true) == language.tag);
    // A changed language is written as its tag; one left alone is not
    // written, so a tag a later version wrote stays in the file.
    const auto defaults = settings::default_settings(players_own_on_linux);
    auto chosen = defaults;
    chosen.language = "it";
    Values written;
    settings::write_settings(written, defaults, chosen, defaults, false);
    CHECK(written.at(std::string{settings::key::language}) == "it");
    CHECK(settings::read_settings(written, players_own_on_linux, false).language == "it");
    Values kept = one_key(settings::key::language, "pt");
    const auto opened = settings::read_settings(kept, players_own_on_linux, false);
    CHECK(opened.language == "system");
    settings::write_settings(kept, opened, opened, defaults, false);
    CHECK(kept.at(std::string{settings::key::language}) == "pt");
    // Back to System default is written too, and Restore defaults erases it.
    settings::write_settings(written, chosen, defaults, defaults, false);
    CHECK(written.at(std::string{settings::key::language}) == "system");
    settings::write_settings(written, chosen, defaults, defaults, true);
    CHECK(written.count(std::string{settings::key::language}) == 0);
    // The command line's language locks the setting for the run; no game does.
    settings::GameState state{};
    CHECK(settings::settings_locks(state).language == settings::Lock::none);
    state.in_game = true;
    state.shared_game = true;
    CHECK(settings::settings_locks(state).language == settings::Lock::none);
    state.language_from_command_line = true;
    CHECK(settings::settings_locks(state).language == settings::Lock::command_line);
    // The language changes nothing the text style draws.
    CHECK(settings::text_style(chosen) == settings::text_style(defaults));
}

void a_preferences_file_with_crlf_line_ends_reads_its_settings() {
    const auto folder =
        std::filesystem::temp_directory_path() /
        ("oa-engine-settings-crlf-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    std::filesystem::create_directories(folder);
    const auto file = folder / "preferences.conf";
    std::ofstream(file, std::ios::binary) << "open-annihilation-preferences 1\r\n"
                                             "\"open-annihilation.max-fps\" \"60\"\r\n"
                                             "\"open-annihilation.modern-fonts\" \"0\"\r\n"
                                             "\"open-annihilation.text-background\" \"1\"\r\n"
                                             "\"open-annihilation.text-shadow\" \"0\"\r\n"
                                             "\"open-annihilation.text-size\" \"120\"\r\n";
    Values values;
    bool loaded = true;
    try {
        values = oa::platform::preferences::load(file);
    } catch (const std::exception& error) {
        std::cerr << "CR LF preferences: " << error.what() << '\n';
        loaded = false;
    }
    std::error_code ignored;
    std::filesystem::remove_all(folder, ignored);
    CHECK(loaded);
    const auto read = settings::read_settings(values, players_own_on_linux, false);
    CHECK(read.max_frame_rate == 60);
    CHECK(!read.modern_fonts);
    CHECK(read.text_outline);
    CHECK(!read.text_shadow);
    CHECK(read.text_background);
    CHECK(read.text_size == 120);
}

void developer_mode_and_its_overrides_are_kept_under_the_profiles_id() {
    namespace profiles = oa::data::mod_profile;
    const settings::EngineSettings defaults{};
    CHECK(!defaults.developer_mode && defaults.hack_overrides.empty());
    CHECK(!settings::default_settings(players_own_on_linux).developer_mode);
    // A file without them reads Off and no override.
    settings::Inputs base{};
    base.profile_id = profiles::base_game_id;
    const auto none = settings::read_settings({}, base, false);
    CHECK(!none.developer_mode && none.hack_overrides.empty());

    auto chosen = defaults;
    chosen.developer_mode = true;
    chosen.hack_overrides = {
        profiles::HackOverride{
            "ai.attack-wave-size", true, {{"units", profiles::make_integer(40)}}
        },
        profiles::HackOverride{"repair.rate", false, {}},
    };
    Values values;
    settings::write_settings(values, defaults, chosen, defaults, false, {}, profiles::base_game_id);
    const std::string base_key = std::string{settings::key::hack_overrides} + "ta-3.1c";
    CHECK(values.size() == 2);
    CHECK(values.at(std::string{settings::key::developer_mode}) == "1");
    CHECK(values.at(base_key) == "{\"ai.attack-wave-size\":{\"units\":40},\"repair.rate\":false}");
    const auto read = settings::read_settings(values, base, false);
    CHECK(read.developer_mode);
    CHECK(read.hack_overrides.size() == chosen.hack_overrides.size());
    for (const auto& override : chosen.hack_overrides) {
        const auto* found = profiles::find_override(read.hack_overrides, override.hack);
        CHECK(found != nullptr && *found == override);
    }

    // Each profile keeps its own; without an id none is read or written.
    settings::Inputs mod{};
    mod.profile_id = "some-mod";
    CHECK(settings::read_settings(values, mod, false).hack_overrides.empty());
    CHECK(settings::read_settings(values, {}, false).hack_overrides.empty());
    Values without_id;
    settings::write_settings(without_id, defaults, chosen, defaults, false);
    CHECK(without_id.size() == 1 && !without_id.contains(base_key));

    // Restore defaults turns Developer Mode off and keeps the overrides.
    auto restored_choice = defaults;
    restored_choice.hack_overrides = chosen.hack_overrides;
    Values restored = values;
    settings::write_settings(
        restored, chosen, restored_choice, defaults, true, {}, profiles::base_game_id
    );
    CHECK(!restored.contains(std::string{settings::key::developer_mode}));
    CHECK(restored.at(base_key) == values.at(base_key));

    // With none left, the key goes; an unchanged list is not written.
    auto cleared = chosen;
    cleared.hack_overrides.clear();
    Values emptied = values;
    settings::write_settings(emptied, chosen, cleared, defaults, false, {}, profiles::base_game_id);
    CHECK(!emptied.contains(base_key));
    Values kept = one_key(base_key, "garbage");
    settings::write_settings(kept, chosen, chosen, defaults, false, {}, profiles::base_game_id);
    CHECK(kept.at(base_key) == "garbage");

    // A text the profile grammar does not read as a mapping gives none.
    CHECK(
        settings::read_settings(one_key(base_key, "{not json"), base, false).hack_overrides.empty()
    );
}

} // namespace

void the_mod_is_stored_as_its_folder() {
    const std::vector<std::string> folders{"/games/ta/mods/alpha", "/games/ta/mods/beta"};
    settings::Inputs inputs{};
    inputs.mod_folders = folders;
    const settings::EngineSettings defaults{};
    CHECK(defaults.mod == 0);

    Values values;
    auto chosen = defaults;
    chosen.mod = 2;
    settings::write_settings(values, defaults, chosen, defaults, false, folders);
    CHECK(values.at(std::string{settings::key::mod_directory}) == "/games/ta/mods/beta");
    CHECK(settings::read_settings(values, inputs, false).mod == 2);

    // A folder no longer offered reads as none; no mod erases the key.
    CHECK(settings::read_settings(values, {}, false).mod == 0);
    settings::write_settings(values, chosen, defaults, defaults, false, folders);
    CHECK(!values.contains(std::string{settings::key::mod_directory}));

    // Restore defaults forgets the mod.
    values[std::string{settings::key::mod_directory}] = folders[0];
    auto first = defaults;
    first.mod = 1;
    settings::write_settings(values, first, defaults, defaults, true, folders);
    CHECK(!values.contains(std::string{settings::key::mod_directory}));
}

void the_touch_settings_default_alike_everywhere() {
    CHECK(settings::key::touch_drag == "open-annihilation.touch-drag");
    CHECK(settings::key::touch_hold_delay == "open-annihilation.touch-hold-delay");
    CHECK(settings::key::touch_latches == "open-annihilation.touch-latches");
    CHECK(settings::key::touch_haptics == "open-annihilation.touch-haptics");
    CHECK(settings::key::touch_left_handed == "open-annihilation.touch-left-handed");
    settings::Inputs own_mac = players_own_on_linux;
    own_mac.macos = true;
    settings::Inputs pi = players_own_on_linux;
    pi.raspberry_pi = true;
    settings::Inputs light = players_own_on_linux;
    light.light_machine = true;
    for (const auto& inputs : {settings::Inputs{}, players_own_on_linux, own_mac, pi, light}) {
        const auto defaults = settings::default_settings(inputs);
        CHECK(defaults.touch_drag == settings::TouchDrag::automatic);
        CHECK(defaults.touch_hold_ms == settings::default_touch_hold_ms);
        CHECK(defaults.touch_hold_ms == 350);
        CHECK(defaults.touch_latches == settings::TouchLatches::stay_on);
        CHECK(defaults.touch_haptics);
        CHECK(!defaults.touch_left_handed);
        // A file without the keys reads the defaults.
        CHECK(settings::read_settings({}, inputs, false) == defaults);
    }
}

void the_touch_settings_read_their_words_and_drop_others() {
    // The words each way is kept as, and back.
    for (const auto drag : settings::touch_drag_choices)
        CHECK(settings::touch_drag_from_text(settings::touch_drag_text(drag)) == drag);
    for (const auto latches : settings::touch_latches_choices)
        CHECK(settings::touch_latches_from_text(settings::touch_latches_text(latches)) == latches);
    CHECK(settings::touch_drag_text(settings::TouchDrag::automatic) == "automatic");
    CHECK(settings::touch_drag_text(settings::TouchDrag::box) == "box");
    CHECK(settings::touch_drag_text(settings::TouchDrag::scroll) == "scroll");
    CHECK(settings::touch_latches_text(settings::TouchLatches::stay_on) == "stay-on");
    CHECK(settings::touch_latches_text(settings::TouchLatches::one_action) == "one-action");
    const auto drag = [](const char* text) {
        return read_one(settings::key::touch_drag, text).touch_drag;
    };
    CHECK(drag("box") == settings::TouchDrag::box);
    CHECK(drag("scroll") == settings::TouchDrag::scroll);
    CHECK(drag("automatic") == settings::TouchDrag::automatic);
    for (const char* text : {"", "Box", "SCROLL", " box", "box ", "1", "2", "boxes", "drag"})
        CHECK(drag(text) == settings::TouchDrag::automatic);
    const auto latches = [](const char* text) {
        return read_one(settings::key::touch_latches, text).touch_latches;
    };
    CHECK(latches("one-action") == settings::TouchLatches::one_action);
    CHECK(latches("stay-on") == settings::TouchLatches::stay_on);
    for (const char* text : {"", "one_action", "One-action", "1", "once", "one-action "})
        CHECK(latches(text) == settings::TouchLatches::stay_on);
    // The switches read as every switch does; a value that is no number is
    // dropped for the default.
    CHECK(!read_one(settings::key::touch_haptics, "0").touch_haptics);
    CHECK(!read_one(settings::key::touch_haptics, "-1").touch_haptics);
    CHECK(read_one(settings::key::touch_haptics, "1").touch_haptics);
    CHECK(read_one(settings::key::touch_haptics, "off").touch_haptics);
    CHECK(read_one(settings::key::touch_left_handed, "1").touch_left_handed);
    CHECK(read_one(settings::key::touch_left_handed, "7").touch_left_handed);
    CHECK(!read_one(settings::key::touch_left_handed, "0").touch_left_handed);
    CHECK(!read_one(settings::key::touch_left_handed, "yes").touch_left_handed);
    // One key changes its own setting alone.
    auto expected = settings::read_settings({}, {}, false);
    expected.touch_drag = settings::TouchDrag::scroll;
    CHECK(read_one(settings::key::touch_drag, "scroll") == expected);
}

void the_hold_delay_is_held_to_its_range_and_stops() {
    static_assert(settings::snapped_touch_hold_ms(275) == 300);
    static_assert(settings::snapped_touch_hold_ms(274) == 250);
    CHECK(settings::lowest_touch_hold_ms == 250 && settings::highest_touch_hold_ms == 700);
    CHECK(settings::touch_hold_step_ms == 50);
    const auto hold = [](const char* text) {
        return read_one(settings::key::touch_hold_delay, text).touch_hold_ms;
    };
    // Each stop reads as itself.
    for (uint32_t ms = 250; ms <= 700; ms += 50)
        CHECK(hold(std::to_string(ms).c_str()) == ms);
    // Out of range, the nearest end; between stops, the nearest stop, half a
    // step up.
    CHECK(hold("0") == 250 && hold("-40") == 250 && hold("249") == 250 && hold("100") == 250);
    CHECK(hold("701") == 700 && hold("5000") == 700 && hold("99999999999999") == 700);
    CHECK(hold("274") == 250 && hold("275") == 300 && hold("299") == 300 && hold("324") == 300);
    CHECK(hold("326") == 350 && hold("374") == 350 && hold("375") == 400 && hold("699") == 700);
    CHECK(hold("+450") == 450);
    // What is no whole number gives the default.
    for (const char* text : {"", "fast", "350ms", " 350", "350 ", "0.4", "0x100"})
        CHECK(hold(text) == settings::default_touch_hold_ms);
}

void the_touch_settings_round_trip_and_restore() {
    const auto defaults = settings::default_settings(players_own_on_linux);

    struct Change {
        std::string_view key;
        std::string_view text;
        void (*apply)(settings::EngineSettings&);
    };

    const std::array<Change, 6> changes{{
        {settings::key::touch_drag,
         "box",
         [](settings::EngineSettings& chosen) { chosen.touch_drag = settings::TouchDrag::box; }},
        {settings::key::touch_drag,
         "scroll",
         [](settings::EngineSettings& chosen) { chosen.touch_drag = settings::TouchDrag::scroll; }},
        {settings::key::touch_hold_delay,
         "600",
         [](settings::EngineSettings& chosen) { chosen.touch_hold_ms = 600; }},
        {settings::key::touch_latches,
         "one-action",
         [](settings::EngineSettings& chosen) {
             chosen.touch_latches = settings::TouchLatches::one_action;
         }},
        {settings::key::touch_haptics,
         "0",
         [](settings::EngineSettings& chosen) { chosen.touch_haptics = false; }},
        {settings::key::touch_left_handed, "1", [](settings::EngineSettings& chosen) {
             chosen.touch_left_handed = true;
         }},
    }};
    // Each change alone writes its key alone, reads back, and Restore
    // defaults then OK erases it.
    for (const Change& change : changes) {
        auto chosen = defaults;
        change.apply(chosen);
        Values values;
        settings::write_settings(values, defaults, chosen, defaults, false);
        CHECK(values.size() == 1);
        CHECK(values.at(std::string{change.key}) == change.text);
        CHECK(settings::read_settings(values, players_own_on_linux, false) == chosen);
        settings::write_settings(values, chosen, defaults, defaults, true);
        CHECK(values.empty());
    }
    // All of them at once: five keys, read back alike.
    auto chosen = defaults;
    for (const Change& change : changes)
        change.apply(chosen);
    Values values;
    settings::write_settings(values, defaults, chosen, defaults, false);
    CHECK(values.size() == 5);
    CHECK(values.at(std::string{settings::key::touch_drag}) == "scroll");
    CHECK(settings::read_settings(values, players_own_on_linux, false) == chosen);
    // An unchanged setting is never written, even away from its default.
    Values again;
    settings::write_settings(again, chosen, chosen, defaults, false);
    CHECK(again.empty());
    // Back to the defaults by hand keeps keys, written as the defaults.
    settings::write_settings(values, chosen, defaults, defaults, false);
    CHECK(values.at(std::string{settings::key::touch_drag}) == "automatic");
    CHECK(values.at(std::string{settings::key::touch_hold_delay}) == "350");
    CHECK(values.at(std::string{settings::key::touch_latches}) == "stay-on");
    CHECK(values.at(std::string{settings::key::touch_haptics}) == "1");
    CHECK(values.at(std::string{settings::key::touch_left_handed}) == "0");
    CHECK(settings::read_settings(values, players_own_on_linux, false) == defaults);
}

void the_backups_switch_is_off_by_default_and_round_trips() {
    CHECK(settings::key::game_files_backed_up == "open-annihilation.game-files-backed-up");
    settings::Inputs own_mac = players_own_on_linux;
    own_mac.macos = true;
    settings::Inputs light = players_own_on_linux;
    light.light_machine = true;
    for (const auto& inputs : {settings::Inputs{}, players_own_on_linux, own_mac, light}) {
        CHECK(!settings::default_settings(inputs).game_files_backed_up);
        CHECK(!settings::read_settings({}, inputs, false).game_files_backed_up);
    }
    // It reads as every switch does.
    CHECK(read_one(settings::key::game_files_backed_up, "1").game_files_backed_up);
    CHECK(read_one(settings::key::game_files_backed_up, "3").game_files_backed_up);
    CHECK(!read_one(settings::key::game_files_backed_up, "0").game_files_backed_up);
    CHECK(!read_one(settings::key::game_files_backed_up, "on").game_files_backed_up);
    auto expected = settings::read_settings({}, {}, false);
    expected.game_files_backed_up = true;
    CHECK(read_one(settings::key::game_files_backed_up, "1") == expected);
    // Turned on, only its key is written; Restore defaults then erases it.
    const auto defaults = settings::default_settings(players_own_on_linux);
    auto chosen = defaults;
    chosen.game_files_backed_up = true;
    Values values;
    settings::write_settings(values, defaults, chosen, defaults, false);
    CHECK(values.size() == 1);
    CHECK(values.at(std::string{settings::key::game_files_backed_up}) == "1");
    CHECK(settings::read_settings(values, players_own_on_linux, false) == chosen);
    settings::write_settings(values, chosen, defaults, defaults, true);
    CHECK(values.empty());
    // Turned off again by hand, the key stays, written as Off.
    settings::write_settings(values, defaults, chosen, defaults, false);
    settings::write_settings(values, chosen, defaults, defaults, false);
    CHECK(values.at(std::string{settings::key::game_files_backed_up}) == "0");
    CHECK(!settings::read_settings(values, players_own_on_linux, false).game_files_backed_up);
}

int main() {
    defaults_play_as_without_the_settings();
    escape_opens_the_menu_by_default_only_on_macos_with_the_players_own_file();
    the_unit_limit_defaults_to_the_installations_with_the_players_own_file();
    a_raspberry_pi_starts_at_60_frames_without_anti_aliasing();
    a_light_machine_starts_at_800x600_and_60_frames_without_anti_aliasing();
    screen_sizes_are_stored_as_text();
    totala_ini_gives_its_unit_limit_as_the_game_reads_it();
    absent_keys_and_values_that_are_not_numbers_give_the_defaults();
    stored_values_are_read_and_clamped_into_their_ranges();
    switch_alt_comes_from_the_games_own_key();
    only_changed_settings_are_written();
    restore_defaults_erases_the_keys_of_settings_at_their_defaults();
    the_round_trip_keeps_every_value();
    the_frame_rate_goes_down_to_a_frame_a_tick();
    the_path_credit_shows_as_whole_cycles();
    shared_games_and_replays_search_at_one_cycle();
    a_mods_unit_limits_set_the_range_and_the_default();
    a_mods_path_budget_scales_the_credit();
    a_game_locks_the_next_game_settings();
    the_mod_is_stored_as_its_folder();
    hardware_acceleration_defaults_to_full_for_the_players_own_file_on_every_machine();
    hardware_acceleration_reads_its_words_and_the_switchs_numbers();
    the_renderer_settings_lock_by_the_flags_and_the_renderer();
    language_and_text_defaults_to_modern_fonts_with_outline_and_shadow();
    a_file_without_the_text_switches_reads_their_defaults();
    the_text_switches_round_trip_and_restore();
    the_text_size_reads_writes_and_restores_in_its_range();
    the_language_reads_writes_and_restores();
    a_preferences_file_with_crlf_line_ends_reads_its_settings();
    developer_mode_and_its_overrides_are_kept_under_the_profiles_id();
    the_touch_settings_default_alike_everywhere();
    the_touch_settings_read_their_words_and_drop_others();
    the_hold_delay_is_held_to_its_range_and_stops();
    the_touch_settings_round_trip_and_restore();
    the_backups_switch_is_off_by_default_and_round_trips();
    if (failures != 0)
        return 1;
    std::cout << "engine settings: ok\n";
    return 0;
}
