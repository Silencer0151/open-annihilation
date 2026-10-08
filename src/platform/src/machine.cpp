// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/machine.hpp"

#include "oa/platform/system.hpp"

#include <fstream>
#include <ios>
#include <optional>
#include <string>
#include <string_view>

#if defined(_WIN32)
#include <windows.h>
#elif defined(__APPLE__)
#include <sys/sysctl.h>
#include <sys/types.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#if defined(__i386__) || defined(_M_IX86)
#if defined(_MSC_VER)
#include <intrin.h>
#else
#include <cpuid.h>
#endif
#endif

namespace oa::platform {

bool raspberry_pi_model(std::string_view model) noexcept {
    const std::string_view text = model.substr(0, model.find('\0'));
    return text.starts_with(raspberry_pi_model_prefix);
}

bool model_file_names_raspberry_pi(const std::filesystem::path& model_file) {
    std::ifstream input(model_file, std::ios::binary);
    if (!input)
        return false;
    std::string text(model_file_limit, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize length = input.gcount();
    if (length <= 0)
        return false;
    text.resize(static_cast<std::size_t>(length));
    return raspberry_pi_model(text);
}

bool running_on_raspberry_pi() {
#if defined(__linux__)
    return model_file_names_raspberry_pi(device_tree_model_path);
#else
    return false;
#endif
}

bool windows_before_vista(uint32_t major_version) noexcept {
    return major_version < vista_major_version;
}

bool running_on_windows_before_vista() noexcept {
#if defined(_WIN32)
    // The system's own version, which a compatibility manifest does not
    // change, from the system library every Windows process has loaded.
    using VersionFunction = LONG(WINAPI*)(OSVERSIONINFOW*);
    // What the version reader returns when it read the version.
    constexpr LONG version_read = 0;
    const HMODULE system_library = GetModuleHandleW(L"ntdll.dll");
    const auto read_version =
        system_library != nullptr
            ? reinterpret_cast<VersionFunction>(
                  reinterpret_cast<void*>(GetProcAddress(system_library, "RtlGetVersion"))
              )
            : nullptr;
    OSVERSIONINFOW version{};
    version.dwOSVersionInfoSize = sizeof version;
    // A version that cannot be read counts as before Vista, which keeps the
    // processor drawing.
    if (read_version == nullptr || read_version(&version) != version_read)
        return true;
    return windows_before_vista(static_cast<uint32_t>(version.dwMajorVersion));
#else
    return false;
#endif
}

bool running_on_linux() noexcept {
#if defined(__linux__)
    return true;
#else
    return false;
#endif
}

bool light_machine(const MachineTraits& machine) noexcept {
    return machine.processors <= 1 || !machine.sse2 ||
           (machine.memory != 0 && machine.memory < light_machine_memory);
}

namespace {

/// Returns the machine's physical memory as the system reports it.
///
/// @return bytes, or 0 when the system does not say
uint64_t physical_memory() noexcept {
#if defined(_WIN32)
    MEMORYSTATUSEX status{};
    status.dwLength = sizeof status;
    return GlobalMemoryStatusEx(&status) ? static_cast<uint64_t>(status.ullTotalPhys) : 0;
#elif defined(__APPLE__)
    uint64_t bytes = 0;
    std::size_t size = sizeof bytes;
    return sysctlbyname("hw.memsize", &bytes, &size, nullptr, 0) == 0 ? bytes : 0;
#elif defined(__linux__)
    const long pages = sysconf(_SC_PHYS_PAGES);
    const long page = sysconf(_SC_PAGESIZE);
    return pages > 0 && page > 0 ? static_cast<uint64_t>(pages) * static_cast<uint64_t>(page) : 0;
#else
    return 0;
#endif
}

/// Tells whether the processor runs SSE2 instructions.
///
/// @return the SSE2 bit of the processor's feature word on 32-bit x86; true elsewhere
bool runs_sse2() noexcept {
#if defined(__i386__) || defined(_M_IX86)
    // CPUID leaf 1 reports SSE2 in bit 26 of its EDX word.
    constexpr unsigned feature_leaf = 1;
    constexpr unsigned sse2_bit = 1U << 26;
#if defined(_MSC_VER)
    int words[4] = {};
    __cpuid(words, static_cast<int>(feature_leaf));
    return (static_cast<unsigned>(words[3]) & sse2_bit) != 0;
#else
    unsigned eax = 0, ebx = 0, ecx = 0, edx = 0;
    return __get_cpuid(feature_leaf, &eax, &ebx, &ecx, &edx) != 0 && (edx & sse2_bit) != 0;
#endif
#else
    return true;
#endif
}

} // namespace

MachineTraits read_machine_traits() noexcept {
    MachineTraits machine;
    machine.processors = processor_count();
    machine.memory = physical_memory();
    machine.sse2 = runs_sse2();
    return machine;
}

namespace {

/// The DMI file naming the machine's maker.
constexpr std::string_view dmi_system_vendor = "sys_vendor";
/// The DMI file naming the machine's product.
constexpr std::string_view dmi_product_name = "product_name";
/// The DMI file naming the main board's maker, read where the machine's own names say no Deck.
constexpr std::string_view dmi_board_vendor = "board_vendor";
/// The DMI file naming the main board.
constexpr std::string_view dmi_board_name = "board_name";
/// The environment variable Steam sets while its Game Mode or Big Picture runs the game.
constexpr char steam_gamepad_ui_variable[] = "SteamGamepadUI";
/// The value steam_gamepad_ui_variable holds then.
constexpr std::string_view steam_gamepad_ui_on = "1";
/// The environment variable naming the desktop session the game runs in.
constexpr char current_desktop_variable[] = "XDG_CURRENT_DESKTOP";
/// The desktop name Steam's Game Mode session gives in XDG_CURRENT_DESKTOP.
constexpr std::string_view gamescope_desktop = "gamescope";
/// The separator between the desktop names XDG_CURRENT_DESKTOP lists.
constexpr char desktop_separator = ':';

/// Returns a text without the white space at its end: spaces, tabs, line ends and NULs.
///
/// @param text the text
/// @return the text up to its last other character, a view into `text`
std::string_view without_trailing_space(std::string_view text) noexcept {
    constexpr std::string_view white_space{" \t\r\n\v\f\0", 7};
    const auto last = text.find_last_not_of(white_space);
    return last == std::string_view::npos ? std::string_view{} : text.substr(0, last + 1);
}

/// Tells whether two words are the same, ignoring the case of ASCII letters.
///
/// @param left a word
/// @param right another
/// @return true when they match
bool same_word(std::string_view left, std::string_view right) noexcept {
    const auto lower = [](char letter) noexcept {
        return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
    };
    if (left.size() != right.size())
        return false;
    for (std::size_t at = 0; at < left.size(); ++at)
        if (lower(left[at]) != lower(right[at]))
            return false;
    return true;
}

/// Reads the start of a small file, such as one of the DMI folder's.
///
/// @param file the file
/// @param limit the most bytes read
/// @return the bytes read; nothing when the file is missing, unreadable or empty
std::optional<std::string> file_start(const std::filesystem::path& file, std::size_t limit) {
    std::ifstream input(file, std::ios::binary);
    if (!input)
        return std::nullopt;
    std::string text(limit, '\0');
    input.read(text.data(), static_cast<std::streamsize>(text.size()));
    const std::streamsize length = input.gcount();
    if (length <= 0)
        return std::nullopt;
    text.resize(static_cast<std::size_t>(length));
    return text;
}

/// Tells which Steam Deck a pair of a DMI folder's files names.
///
/// @param dmi_folder the folder
/// @param vendor_file the file naming the maker
/// @param product_file the file naming the product
/// @return the model; none when either file is missing or unreadable, or they name another
///     machine
SteamDeckModel dmi_pair_model(
    const std::filesystem::path& dmi_folder,
    std::string_view vendor_file,
    std::string_view product_file
) {
    const auto vendor = file_start(dmi_folder / vendor_file, dmi_file_limit);
    const auto product = file_start(dmi_folder / product_file, dmi_file_limit);
    if (!vendor || !product)
        return SteamDeckModel::none;
    return steam_deck_model(*vendor, *product);
}

} // namespace

SteamDeckModel steam_deck_model(std::string_view vendor, std::string_view product) noexcept {
    if (without_trailing_space(vendor) != steam_deck_vendor)
        return SteamDeckModel::none;
    const std::string_view name = without_trailing_space(product);
    if (name == steam_deck_lcd_model)
        return SteamDeckModel::lcd;
    if (name == steam_deck_oled_model)
        return SteamDeckModel::oled;
    return SteamDeckModel::none;
}

SteamDeckModel steam_deck_model_in(const std::filesystem::path& dmi_folder) {
    const SteamDeckModel machine = dmi_pair_model(dmi_folder, dmi_system_vendor, dmi_product_name);
    if (machine != SteamDeckModel::none)
        return machine;
    return dmi_pair_model(dmi_folder, dmi_board_vendor, dmi_board_name);
}

SteamDeckModel running_steam_deck_model() {
#if defined(__linux__)
    return steam_deck_model_in(dmi_folder_path);
#else
    return SteamDeckModel::none;
#endif
}

uint32_t steam_deck_refresh_hz(SteamDeckModel model) noexcept {
    switch (model) {
    case SteamDeckModel::none:
        break;
    case SteamDeckModel::lcd:
        return steam_deck_lcd_refresh_hz;
    case SteamDeckModel::oled:
        return steam_deck_oled_refresh_hz;
    }
    return 0;
}

bool steam_game_mode(std::string_view gamepad_ui, std::string_view current_desktop) noexcept {
    if (gamepad_ui == steam_gamepad_ui_on)
        return true;
    // XDG_CURRENT_DESKTOP lists one or more desktop names, joined by colons.
    while (!current_desktop.empty()) {
        const auto separator = current_desktop.find(desktop_separator);
        const std::string_view name = current_desktop.substr(0, separator);
        if (same_word(name, gamescope_desktop))
            return true;
        if (separator == std::string_view::npos)
            break;
        current_desktop.remove_prefix(separator + 1);
    }
    return false;
}

bool running_in_steam_game_mode() {
    const auto gamepad_ui = environment_value(steam_gamepad_ui_variable);
    const auto current_desktop = environment_value(current_desktop_variable);
    return steam_game_mode(
        gamepad_ui.value_or(std::string{}), current_desktop.value_or(std::string{})
    );
}

bool web_address_available(bool steam_deck, bool game_mode) noexcept {
    return !(steam_deck && game_mode);
}

bool web_address_available() noexcept {
    return web_address_available(
        running_steam_deck_model() != SteamDeckModel::none, running_in_steam_game_mode()
    );
}

} // namespace oa::platform
