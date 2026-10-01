// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The machine the game runs on, where it changes what the game offers by
// default: a Raspberry Pi starts with settings its graphics keep up with.

#include <cstddef>
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

} // namespace oa::platform
