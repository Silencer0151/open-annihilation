// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The settings' defaults on each platform and preferences file, what they
// read from the preferences and an installation's totala.ini, what they
// write back, and the locks a game puts on them.

#include "oa/ui/engine_settings.hpp"

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
    CHECK(defaults == settings::EngineSettings{});
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
    CHECK(read_one(settings::key::max_frame_rate, "10").max_frame_rate == 40);
    CHECK(read_one(settings::key::max_frame_rate, "0").max_frame_rate == 40);
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
    CHECK(values.size() == 8);
    CHECK(values.at(std::string{settings::key::path_search_nodes}) == "5332");
    CHECK(values.at(std::string{settings::key::wheel_zoom}) == "0");
    CHECK(values.at(std::string{settings::key::escape_opens_menu}) == "1");
    CHECK(values.at(std::string{settings::key::unit_limit}) == "1000");
    CHECK(values.at(std::string{settings::key::max_frame_rate}) == "60");
    CHECK(values.at(std::string{settings::key::anti_aliasing}) == "16");
    CHECK(values.at(std::string{settings::key::frame_stats}) == "1");
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
        Values values;
        settings::write_settings(values, {}, chosen, {}, false);
        CHECK(settings::read_settings(values, players_own_on_linux, false) == chosen);
    }
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
}

} // namespace

int main() {
    defaults_play_as_without_the_settings();
    escape_opens_the_menu_by_default_only_on_macos_with_the_players_own_file();
    the_unit_limit_defaults_to_the_installations_with_the_players_own_file();
    totala_ini_gives_its_unit_limit_as_the_game_reads_it();
    absent_keys_and_values_that_are_not_numbers_give_the_defaults();
    stored_values_are_read_and_clamped_into_their_ranges();
    switch_alt_comes_from_the_games_own_key();
    only_changed_settings_are_written();
    restore_defaults_erases_the_keys_of_settings_at_their_defaults();
    the_round_trip_keeps_every_value();
    the_path_credit_shows_as_whole_cycles();
    shared_games_and_replays_search_at_one_cycle();
    a_game_locks_the_next_game_settings();
    if (failures != 0)
        return 1;
    std::cout << "engine settings: ok\n";
    return 0;
}
