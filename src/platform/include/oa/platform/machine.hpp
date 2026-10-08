// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The machine the game runs on, where it changes what the game offers by
// default: a Raspberry Pi starts with settings its graphics keep up with, and
// so does a light machine (one processor, no SSE2 or little memory); a Steam
// Deck starts at its screen's refresh rate with larger touch controls; on
// Windows before Vista only one render driver may draw through the graphics
// card. Whether Steam's Game Mode runs the game is read from the environment.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string_view>

namespace oa::platform {

/// The start of every model name a Raspberry Pi's board gives, as in
/// "Raspberry Pi 4 Model B Rev 1.4" or "Raspberry Pi 400 Rev 1.0".
inline constexpr std::string_view raspberry_pi_model_prefix = "Raspberry Pi";

/// The file Linux names the board's model in, on boards described by a
/// device tree: the model's text, ended by a NUL.
inline constexpr char device_tree_model_path[] = "/proc/device-tree/model";

/// The most bytes of a model file that are read.
inline constexpr std::size_t model_file_limit = 256;

/// Tells whether a board's model names a Raspberry Pi.
///
/// @param model the model's text; a NUL and whatever follows it are ignored
/// @return true when the model starts with raspberry_pi_model_prefix,
///     letter case included
[[nodiscard]] bool raspberry_pi_model(std::string_view model) noexcept;

/// Tells whether a model file names a Raspberry Pi.
///
/// Reads at most model_file_limit bytes of the file.
///
/// @param model_file the file holding the board's model
/// @return true when the file can be read and its text names a Raspberry Pi
///     (raspberry_pi_model); false when it is missing or unreadable
[[nodiscard]] bool model_file_names_raspberry_pi(const std::filesystem::path& model_file);

/// Tells whether the game runs on a Raspberry Pi.
///
/// @return on Linux, whether device_tree_model_path names a Raspberry Pi
///     (model_file_names_raspberry_pi); false on every other system
[[nodiscard]] bool running_on_raspberry_pi();

/// The major version Windows Vista reports, the first Windows whose display
/// drivers recover from a fault in the graphics card.
inline constexpr uint32_t vista_major_version = 6;

/// Tells whether a Windows major version is one before Vista, such as XP's
/// 5.
///
/// @param major_version the major version the system reports
/// @return true under vista_major_version
[[nodiscard]] bool windows_before_vista(uint32_t major_version) noexcept;

/// Tells whether the game runs on Windows before Vista.
///
/// @return on Windows, whether the version the system itself reports, which
///     an application's manifest does not change, is before Vista
///     (windows_before_vista), and true where that version cannot be read;
///     false on every other system
[[nodiscard]] bool running_on_windows_before_vista() noexcept;

/// Tells whether the game runs on Linux, the Raspberry Pis included, where
/// a fault of the graphics card can lock the whole machine.
///
/// @return true on Linux; false on every other system
[[nodiscard]] bool running_on_linux() noexcept;

/// The physical memory under which a machine is light, in bytes: 512 MiB.
inline constexpr uint64_t light_machine_memory = uint64_t{512} * 1024 * 1024;

/// What the game reads of the machine it runs on to choose its starting settings.
struct MachineTraits {
    uint32_t processors{1}; ///< logical processors, at least 1
    uint64_t memory{};      ///< physical memory in bytes; 0 when the system does not say
    /// The processor runs SSE2 instructions: false only for a 32-bit x86
    /// processor without them, such as a Pentium III or an Athlon XP.
    bool sse2{true};
};

/// Tells whether a machine is light: it has one logical processor, its
/// processor lacks SSE2, or its physical memory is known and under
/// light_machine_memory.
///
/// @param machine the machine's traits
/// @return true for a light machine
[[nodiscard]] bool light_machine(const MachineTraits& machine) noexcept;

/// Reads the traits of the machine the game runs on.
///
/// @return its logical processors (processor_count), its physical memory as
///     the system reports it, and whether a 32-bit x86 processor reports
///     SSE2 (true on every other processor)
[[nodiscard]] MachineTraits read_machine_traits() noexcept;

/// The folder Linux describes the machine's maker and model in.
inline constexpr char dmi_folder_path[] = "/sys/class/dmi/id";
/// The maker a Steam Deck names.
inline constexpr std::string_view steam_deck_vendor = "Valve";
/// The product name of the Steam Deck with the LCD screen.
inline constexpr std::string_view steam_deck_lcd_model = "Jupiter";
/// The product name of the Steam Deck with the OLED screen.
inline constexpr std::string_view steam_deck_oled_model = "Galileo";
/// How many times a second the LCD model's screen refreshes.
inline constexpr uint32_t steam_deck_lcd_refresh_hz = 60;
/// How many times a second the OLED model's screen refreshes.
inline constexpr uint32_t steam_deck_oled_refresh_hz = 90;
/// The most bytes of a DMI file read.
inline constexpr std::size_t dmi_file_limit = 256;

/// Which Steam Deck the game runs on.
enum class SteamDeckModel : uint8_t {
    none, ///< not a Steam Deck
    lcd,  ///< the model with the LCD screen
    oled, ///< the model with the OLED screen
};

/// Tells which Steam Deck a maker and product name are.
///
/// @param vendor the maker's name; trailing white space is ignored
/// @param product the product's name; trailing white space is ignored
/// @return the model, or none for another machine
[[nodiscard]] SteamDeckModel
steam_deck_model(std::string_view vendor, std::string_view product) noexcept;

/// Tells which Steam Deck a DMI folder describes, from its sys_vendor and product_name (else
/// board_vendor and board_name).
///
/// Reads at most dmi_file_limit bytes of each file.
///
/// @param dmi_folder the folder
/// @return the model, or none when the files are missing, unreadable or name another machine
[[nodiscard]] SteamDeckModel steam_deck_model_in(const std::filesystem::path& dmi_folder);

/// Tells which Steam Deck the game runs on.
///
/// @return on Linux, the model dmi_folder_path describes (steam_deck_model_in); none on every
///     other system
[[nodiscard]] SteamDeckModel running_steam_deck_model();

/// Returns a model's screen refresh rate.
///
/// @param model the model
/// @return times a second; 0 for none
[[nodiscard]] uint32_t steam_deck_refresh_hz(SteamDeckModel model) noexcept;

/// Tells whether the environment's words say the game runs in Steam's Game Mode or Big
/// Picture: SteamGamepadUI is "1", or XDG_CURRENT_DESKTOP names gamescope.
///
/// @param gamepad_ui the SteamGamepadUI variable's value; empty when unset
/// @param current_desktop the XDG_CURRENT_DESKTOP variable's value; empty when unset
/// @return whether those words name Steam's Game Mode or Big Picture
[[nodiscard]] bool
steam_game_mode(std::string_view gamepad_ui, std::string_view current_desktop) noexcept;

/// Tells whether this run is in Steam's Game Mode, from the environment.
///
/// @return steam_game_mode of this process's environment
[[nodiscard]] bool running_in_steam_game_mode();

/// Tells whether a web address can be opened on a machine of this kind.
///
/// A Steam Deck in Game Mode has no browser to hand an address to, so the
/// answer is no. A Steam Deck in its desktop session answers yes, and so
/// does every other machine, including one running Steam's Big Picture.
///
/// @param steam_deck the machine is a Steam Deck
/// @param game_mode the session is Steam's Game Mode
/// @return false when the machine is a Steam Deck in Game Mode
[[nodiscard]] bool web_address_available(bool steam_deck, bool game_mode) noexcept;

/// Tells whether this machine can open a web address in the system's browser.
///
/// @return web_address_available for this machine: a Steam Deck
///     (running_steam_deck_model) in Game Mode (running_in_steam_game_mode)
///     answers no
[[nodiscard]] bool web_address_available() noexcept;

} // namespace oa::platform
