// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installer's own file system operations that its sources share
// (files.cpp), and its shared text helpers.
#pragma once

#include <filesystem>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::app::mod_install::detail {

#ifdef _WIN32
/// Returns a path in the form that reaches past the system's 259
/// characters: absolute, with backslashes, after "\\?\" (or "\\?\UNC\").
///
/// @param path the path
/// @return its extended spelling
[[nodiscard]] std::wstring extended_path(const std::filesystem::path& path);
#endif

/// Tells whether a refused rename is worth trying again: another program,
/// such as a file sync or a virus scanner, holds the files for a moment.
///
/// @param error the rename's error
/// @return true for a sharing or lock violation or a refused access on
///         Windows, and a busy resource elsewhere
[[nodiscard]] bool retryable(const std::error_code& error) noexcept;

/// Removes one file, link, junction or empty folder, never entering a link;
/// a read-only file on Windows is made writable first.
///
/// @param path the entry
/// @param[out] error why it could not be removed
/// @return true when it was removed
bool remove_entry(const std::filesystem::path& path, std::error_code& error) noexcept;

/// Syncs a folder's entries to the disk where the system needs it (not on
/// Windows); a folder that cannot be opened is left.
///
/// @param folder the folder
void sync_folder(const std::filesystem::path& folder) noexcept;

/// Syncs a folder's entries as sync_folder does and, on macOS, where a sync
/// stops at the drive, has the drive write everything it holds to its
/// storage; a folder that cannot be opened is left.
///
/// @param folder the folder
void flush_to_storage(const std::filesystem::path& folder) noexcept;

/// Returns a text with its ASCII letters lowered.
///
/// @param text the text
/// @return the text
[[nodiscard]] std::string folded(std::string_view text);

/// Returns a path as UTF-8.
///
/// @param path the path
/// @return its UTF-8 spelling
[[nodiscard]] std::string utf8_of(const std::filesystem::path& path);

/// Returns a path from UTF-8.
///
/// @param text the UTF-8 spelling
/// @return the path
[[nodiscard]] std::filesystem::path path_of(std::string_view text);

/// Writes a line to standard error, which the log keeps, after
/// "open-annihilation: mod install: ".
///
/// @param line the line
void log_line(std::string_view line);

} // namespace oa::app::mod_install::detail
