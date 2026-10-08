// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Telling a Raspberry Pi from its board's model, given as text and as the
// model file Linux keeps it in; telling a light machine from its processors,
// SSE2 and memory, and reading this machine's; telling Windows before Vista
// from its major version; telling a Steam Deck and its screen from the maker
// and product names, given as text and as DMI folders; and telling Steam's
// Game Mode from the environment's words; and whether a web address can be
// opened, which a Steam Deck in Game Mode cannot.

#include "oa/platform/machine.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace {

namespace fs = std::filesystem;
namespace platform = oa::platform;

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::cerr << file << ':' << line << ": check failed: " << expression << '\n';
    ++failures;
}

#define CHECK(expression) check((expression), #expression, __FILE__, __LINE__)

using namespace std::string_literals;

/// Writes a model file as Linux gives it: the text, then a NUL.
///
/// @param path the file
/// @param contents the file's bytes
void write_file(const fs::path& path, const std::string& contents) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
}

void every_raspberry_pi_model_is_one() {
    CHECK(platform::raspberry_pi_model("Raspberry Pi 5 Model B Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 4 Model B Rev 1.4"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 400 Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 3 Model B Plus Rev 1.3"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 2 Model B Rev 1.1"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi Compute Module 4 Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi Zero 2 W Rev 1.0"));
    CHECK(platform::raspberry_pi_model("Raspberry Pi 4 Model B Rev 1.4\0"s));
    CHECK(platform::raspberry_pi_model(platform::raspberry_pi_model_prefix));
}

void other_boards_are_not() {
    CHECK(!platform::raspberry_pi_model(""));
    CHECK(!platform::raspberry_pi_model("linux,dummy-virt"));
    CHECK(!platform::raspberry_pi_model("Pine64 RockPro64 v2.1"));
    CHECK(!platform::raspberry_pi_model("Radxa ROCK 5B"));
    CHECK(!platform::raspberry_pi_model("raspberry pi 4 model b"));
    CHECK(!platform::raspberry_pi_model(" Raspberry Pi 4 Model B"));
    CHECK(!platform::raspberry_pi_model("Raspberry"));
    CHECK(!platform::raspberry_pi_model("\0Raspberry Pi 4 Model B"s));
}

void model_files_are_read_up_to_their_limit() {
    std::error_code error;
    const fs::path folder = oa::test::make_scratch_directory("oa-platform-machine-test");
    const fs::path model = folder / "model";

    write_file(model, "Raspberry Pi 4 Model B Rev 1.4\0"s);
    CHECK(platform::model_file_names_raspberry_pi(model));
    write_file(model, "Raspberry Pi 5 Model B Rev 1.0");
    CHECK(platform::model_file_names_raspberry_pi(model));
    write_file(model, "linux,dummy-virt\0"s);
    CHECK(!platform::model_file_names_raspberry_pi(model));
    write_file(model, "");
    CHECK(!platform::model_file_names_raspberry_pi(model));
    // The name past the read limit is not seen.
    write_file(model, std::string(platform::model_file_limit, ' ') + "Raspberry Pi 4");
    CHECK(!platform::model_file_names_raspberry_pi(model));
    write_file(model, "Raspberry Pi 4" + std::string(platform::model_file_limit * 4, 'x'));
    CHECK(platform::model_file_names_raspberry_pi(model));

    CHECK(!platform::model_file_names_raspberry_pi(folder / "missing"));
    CHECK(!platform::model_file_names_raspberry_pi(folder));
    fs::remove_all(folder, error);
}

void only_linux_reads_the_device_tree() {
#if !defined(__linux__)
    CHECK(!platform::running_on_raspberry_pi());
#else
    CHECK(
        platform::running_on_raspberry_pi() ==
        platform::model_file_names_raspberry_pi(platform::device_tree_model_path)
    );
#endif
}

void only_linux_is_linux() {
#if defined(__linux__)
    CHECK(platform::running_on_linux());
#else
    CHECK(!platform::running_on_linux());
#endif
}

} // namespace

void light_machines_are_told_apart() {
    constexpr uint64_t mebibyte = uint64_t{1024} * 1024;
    const auto machine = [](uint32_t processors, uint64_t memory, bool sse2) {
        platform::MachineTraits traits;
        traits.processors = processors;
        traits.memory = memory;
        traits.sse2 = sse2;
        return traits;
    };
    // A Pentium III with 256 MiB: one processor, no SSE2, little memory.
    CHECK(platform::light_machine(machine(1, 256 * mebibyte, false)));
    // Each trait alone makes a machine light.
    CHECK(platform::light_machine(machine(1, 4096 * mebibyte, true)));
    CHECK(platform::light_machine(machine(2, 4096 * mebibyte, false)));
    CHECK(platform::light_machine(machine(4, 511 * mebibyte, true)));
    CHECK(platform::light_machine(machine(4, platform::light_machine_memory - 1, true)));
    // A machine with none of them is not.
    CHECK(!platform::light_machine(machine(2, platform::light_machine_memory, true)));
    CHECK(!platform::light_machine(machine(2, 1024 * mebibyte, true)));
    CHECK(!platform::light_machine(machine(24, 192 * 1024 * mebibyte, true)));
    // Unknown memory leaves the processor to decide.
    CHECK(!platform::light_machine(machine(2, 0, true)));
    CHECK(platform::light_machine(machine(1, 0, true)));
    // The defaults are a machine of one processor, so light.
    CHECK(platform::light_machine(platform::MachineTraits{}));
}

void this_machine_is_read() {
    const auto traits = platform::read_machine_traits();
    CHECK(traits.processors >= 1);
#if !defined(__i386__) && !defined(_M_IX86)
    CHECK(traits.sse2);
#endif
#if defined(_WIN32) || defined(__APPLE__) || defined(__linux__)
    CHECK(traits.memory != 0);
#endif
    std::cout << "this machine: " << traits.processors << " processor(s), "
              << traits.memory / (uint64_t{1024} * 1024) << " MiB, SSE2 "
              << (traits.sse2 ? "yes" : "no") << ", "
              << (platform::light_machine(traits) ? "light" : "not light") << '\n';
}

/// Windows before Vista is told by its major version: XP and Server 2003
/// report 5, Vista 6 and Windows 10 and 11 report 10. Only Windows reads
/// its own; every other system is never before Vista.
void windows_before_vista_is_told_apart() {
    CHECK(platform::windows_before_vista(0));
    CHECK(platform::windows_before_vista(5));
    CHECK(platform::windows_before_vista(platform::vista_major_version - 1));
    CHECK(!platform::windows_before_vista(platform::vista_major_version));
    CHECK(!platform::windows_before_vista(10));
#if !defined(_WIN32)
    CHECK(!platform::running_on_windows_before_vista());
#endif
    std::cout << "this system: "
              << (platform::running_on_windows_before_vista() ? "Windows before Vista"
                                                              : "not Windows before Vista")
              << '\n';
}

/// Writes a DMI folder's maker and product files, as Linux gives them: each
/// name, then a line end.
///
/// @param folder the folder, made when missing
/// @param vendor_file the maker's file's name
/// @param vendor the maker's name
/// @param product_file the product's file's name
/// @param product the product's name
void write_dmi_pair(
    const fs::path& folder,
    std::string_view vendor_file,
    const std::string& vendor,
    std::string_view product_file,
    const std::string& product
) {
    std::error_code error;
    fs::create_directories(folder, error);
    write_file(folder / vendor_file, vendor + "\n");
    write_file(folder / product_file, product + "\n");
}

/// The Steam Deck's maker with Jupiter is the LCD model, with Galileo the
/// OLED; white space at the end of either name does not count, other
/// makers and products are not a Deck, and the names match letter for
/// letter.
void steam_decks_are_told_by_their_names() {
    using platform::SteamDeckModel;
    CHECK(platform::steam_deck_model("Valve", "Jupiter") == SteamDeckModel::lcd);
    CHECK(platform::steam_deck_model("Valve", "Galileo") == SteamDeckModel::oled);
    CHECK(platform::steam_deck_model("Valve\n", "Jupiter\n") == SteamDeckModel::lcd);
    CHECK(platform::steam_deck_model("Valve \t\r\n", "Galileo  \n") == SteamDeckModel::oled);
    CHECK(platform::steam_deck_model("Valve\0"s, "Jupiter\0"s) == SteamDeckModel::lcd);
    CHECK(
        platform::steam_deck_model(platform::steam_deck_vendor, platform::steam_deck_lcd_model) ==
        SteamDeckModel::lcd
    );
    CHECK(
        platform::steam_deck_model(platform::steam_deck_vendor, platform::steam_deck_oled_model) ==
        SteamDeckModel::oled
    );
    CHECK(platform::steam_deck_model("Dell Inc.", "Jupiter") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model("LENOVO", "20XW004GUS") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model("Valve", "Index") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model("valve", "jupiter") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model(" Valve", "Jupiter") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model("Valve", "Jupiter2") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model("", "") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model("Valve", "") == SteamDeckModel::none);
}

/// The LCD model refreshes 60 times a second and the OLED 90; no Deck has no rate.
void steam_deck_screens_have_their_rates() {
    using platform::SteamDeckModel;
    CHECK(platform::steam_deck_refresh_hz(SteamDeckModel::lcd) == 60);
    CHECK(platform::steam_deck_refresh_hz(SteamDeckModel::oled) == 90);
    CHECK(platform::steam_deck_refresh_hz(SteamDeckModel::none) == 0);
    CHECK(platform::steam_deck_lcd_refresh_hz == 60);
    CHECK(platform::steam_deck_oled_refresh_hz == 90);
}

/// A DMI folder names the Deck in sys_vendor and product_name, else in
/// board_vendor and board_name; a missing or empty file, another maker, or
/// a name past the read limit is no Deck.
void dmi_folders_are_read() {
    using platform::SteamDeckModel;
    std::error_code error;
    const fs::path root = oa::test::make_scratch_directory("oa-platform-machine-dmi-test");

    const fs::path jupiter = root / "jupiter";
    write_dmi_pair(jupiter, "sys_vendor", "Valve", "product_name", "Jupiter");
    CHECK(platform::steam_deck_model_in(jupiter) == SteamDeckModel::lcd);

    const fs::path galileo = root / "galileo";
    write_dmi_pair(galileo, "sys_vendor", "Valve", "product_name", "Galileo");
    write_dmi_pair(galileo, "board_vendor", "Valve", "board_name", "Galileo");
    CHECK(platform::steam_deck_model_in(galileo) == SteamDeckModel::oled);

    // Another maker's machine, its board too.
    const fs::path desktop = root / "desktop";
    write_dmi_pair(
        desktop, "sys_vendor", "Micro-Star International Co., Ltd.", "product_name", "MS-7C02"
    );
    write_dmi_pair(
        desktop,
        "board_vendor",
        "Micro-Star International Co., Ltd.",
        "board_name",
        "B450 TOMAHAWK MAX"
    );
    CHECK(platform::steam_deck_model_in(desktop) == SteamDeckModel::none);

    // The board's names serve where the machine's own are missing or name
    // no Deck.
    const fs::path board_only = root / "board-only";
    write_dmi_pair(board_only, "board_vendor", "Valve", "board_name", "Jupiter");
    CHECK(platform::steam_deck_model_in(board_only) == SteamDeckModel::lcd);
    const fs::path blank_system = root / "blank-system";
    write_dmi_pair(
        blank_system,
        "sys_vendor",
        "To Be Filled By O.E.M.",
        "product_name",
        "To Be Filled By O.E.M."
    );
    write_dmi_pair(blank_system, "board_vendor", "Valve", "board_name", "Galileo");
    CHECK(platform::steam_deck_model_in(blank_system) == SteamDeckModel::oled);

    // A file without its line end reads the same; an empty one names nothing.
    const fs::path bare = root / "bare";
    fs::create_directories(bare, error);
    write_file(bare / "sys_vendor", "Valve");
    write_file(bare / "product_name", "Jupiter");
    CHECK(platform::steam_deck_model_in(bare) == SteamDeckModel::lcd);
    write_file(bare / "product_name", "");
    CHECK(platform::steam_deck_model_in(bare) == SteamDeckModel::none);

    // A maker's file alone is not enough.
    const fs::path half = root / "half";
    fs::create_directories(half, error);
    write_file(half / "sys_vendor", "Valve\n");
    CHECK(platform::steam_deck_model_in(half) == SteamDeckModel::none);

    // Only the first dmi_file_limit bytes of a file are read: a name past
    // them is not seen, and white space running past them still ends a name.
    const fs::path long_names = root / "long";
    write_dmi_pair(
        long_names,
        "sys_vendor",
        "Valve",
        "product_name",
        std::string(platform::dmi_file_limit, ' ') + "Jupiter"
    );
    CHECK(platform::steam_deck_model_in(long_names) == SteamDeckModel::none);
    write_dmi_pair(
        long_names,
        "sys_vendor",
        "Valve",
        "product_name",
        "Jupiter" + std::string(platform::dmi_file_limit, ' ')
    );
    CHECK(platform::steam_deck_model_in(long_names) == SteamDeckModel::lcd);
    write_dmi_pair(
        long_names,
        "sys_vendor",
        "Valve",
        "product_name",
        "Jupiter" + std::string(platform::dmi_file_limit, 'x')
    );
    CHECK(platform::steam_deck_model_in(long_names) == SteamDeckModel::none);

    CHECK(platform::steam_deck_model_in(root / "missing") == SteamDeckModel::none);
    CHECK(platform::steam_deck_model_in(root) == SteamDeckModel::none);
    fs::remove_all(root, error);
}

/// Only Linux reads the machine's DMI folder; every other system runs on no Deck.
void only_linux_reads_the_dmi_folder() {
#if !defined(__linux__)
    CHECK(platform::running_steam_deck_model() == platform::SteamDeckModel::none);
#else
    CHECK(
        platform::running_steam_deck_model() ==
        platform::steam_deck_model_in(platform::dmi_folder_path)
    );
#endif
}

/// Steam's Game Mode and Big Picture set SteamGamepadUI to 1; Game Mode's
/// session also names gamescope among XDG_CURRENT_DESKTOP's desktops, in
/// any letter case. Any other words are a desktop.
void steam_game_mode_is_told_by_the_environment() {
    CHECK(platform::steam_game_mode("1", ""));
    CHECK(platform::steam_game_mode("1", "KDE"));
    CHECK(platform::steam_game_mode("", "gamescope"));
    CHECK(platform::steam_game_mode("", "Gamescope"));
    CHECK(platform::steam_game_mode("0", "GAMESCOPE"));
    CHECK(platform::steam_game_mode("", "KDE:gamescope"));
    CHECK(platform::steam_game_mode("", "gamescope:KDE"));
    CHECK(!platform::steam_game_mode("", ""));
    CHECK(!platform::steam_game_mode("0", "KDE"));
    CHECK(!platform::steam_game_mode("", "GNOME"));
    CHECK(!platform::steam_game_mode("", "ubuntu:GNOME"));
    CHECK(!platform::steam_game_mode("", "gamescope-session"));
    CHECK(!platform::steam_game_mode("", "KDE:"));
    CHECK(!platform::steam_game_mode("true", "KDE"));
    CHECK(!platform::steam_game_mode("11", ""));
    std::cout << "this run: "
              << (platform::running_in_steam_game_mode() ? "in Steam's Game Mode"
                                                         : "not in Steam's Game Mode")
              << ", Steam Deck screen "
              << platform::steam_deck_refresh_hz(platform::running_steam_deck_model()) << " Hz\n";
}

/// A Steam Deck in Game Mode cannot open a web address. A Deck in its
/// desktop session can, and so can any other machine, Big Picture included.
void a_steam_deck_in_game_mode_cannot_open_a_web_address() {
    CHECK(!platform::web_address_available(true, true));
    CHECK(platform::web_address_available(true, false));
    CHECK(platform::web_address_available(false, true));
    CHECK(platform::web_address_available(false, false));
    const bool steam_deck = platform::running_steam_deck_model() != platform::SteamDeckModel::none;
    CHECK(
        platform::web_address_available() ==
        platform::web_address_available(steam_deck, platform::running_in_steam_game_mode())
    );
}

int main() {
    every_raspberry_pi_model_is_one();
    other_boards_are_not();
    model_files_are_read_up_to_their_limit();
    only_linux_reads_the_device_tree();
    only_linux_is_linux();
    light_machines_are_told_apart();
    windows_before_vista_is_told_apart();
    this_machine_is_read();
    steam_decks_are_told_by_their_names();
    steam_deck_screens_have_their_rates();
    dmi_folders_are_read();
    only_linux_reads_the_dmi_folder();
    steam_game_mode_is_told_by_the_environment();
    a_steam_deck_in_game_mode_cannot_open_a_web_address();
    return failures == 0 ? 0 : 1;
}
