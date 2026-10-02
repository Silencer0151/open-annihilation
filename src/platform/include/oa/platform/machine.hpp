// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The machine the game runs on, where it changes what the game offers by
// default: a Raspberry Pi starts with settings its graphics keep up with, and
// so does a light machine (one processor, no SSE2 or little memory); on
// Windows before Vista only one render driver may draw through the graphics
// card.

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

} // namespace oa::platform
