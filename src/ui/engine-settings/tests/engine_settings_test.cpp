// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings' defaults on each platform and preferences file, what they
// read from the preferences and an installation's totala.ini, what they
// write back, and the locks a game, the command line and the renderer put
// on them.

#include "oa/ui/engine_settings.hpp"

#include <array>
#include <iostream>
#include <string>

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
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=abc\n") == 21);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=\n") == 21);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=3\n") == 21);
    CHECK(installation_unit_limit("[Preferences]\nUnitLimit=-40\n") == 21);
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
    CHECK(read_one(settings::key::unit_limit, "5").unit_limit == 21);
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
    CHECK(level("3") == AntiAliasing::x3);
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
    CHECK(values.size() == 11);
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

} // namespace

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
    a_game_locks_the_next_game_settings();
    hardware_acceleration_defaults_to_full_for_the_players_own_file_on_every_machine();
    hardware_acceleration_reads_its_words_and_the_switchs_numbers();
    the_renderer_settings_lock_by_the_flags_and_the_renderer();
    if (failures != 0)
        return 1;
    std::cout << "engine settings: ok\n";
    return 0;
}
