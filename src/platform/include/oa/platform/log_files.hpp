// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The game's log: standard output and standard error written to a folder of
// files instead of the terminal. A file gives way to a new one when it passes
// a size or when the UTC day changes, and the folder keeps only the recent
// files.

#include <chrono>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <string_view>

namespace oa::platform::log_files {

// Limits of the log folder. The defaults keep at most 25 files of about
// 10 MB each, none begun more than seven days ago: about 250 MB at most.
struct Limits {
    uint64_t max_file_bytes{10ull * 1024 * 1024}; // a file this size or larger gives way
    int32_t max_files{25};   // files the folder keeps, the current one included
    int32_t max_age_days{7}; // files begun earlier than this are deleted
};

/// Returns the name of a log file begun at `begun`.
///
/// The name records the UTC date and time, as open-annihilation-YYYYMMDD-HHMMSS.log,
/// with -N before the extension when `sequence` is above zero, so that files
/// begun in the same second stay apart.
///
/// @param begun when the file is begun
/// @param sequence 0 for the first file of that second, then 1, 2, ...
/// @return the file name
std::string file_name(std::chrono::sys_seconds begun, int32_t sequence);

/// Returns when a log file was begun, as its name records it.
///
/// @param name a file name, without a folder
/// @return the UTC time, or nullopt when the name is not a log file's
std::optional<std::chrono::sys_seconds> begun_at(std::string_view name);

/// Returns whether a log file must give way to a new one.
///
/// @param begun when the file was begun
/// @param bytes the file's size
/// @param now the time now
/// @param limits the folder's limits
/// @return true when the file holds max_file_bytes or more, or was begun on an
///     earlier UTC day
bool must_roll(
    std::chrono::sys_seconds begun,
    uint64_t bytes,
    std::chrono::sys_seconds now,
    const Limits& limits
);

/// Returns the file to write in `folder` at `now`.
///
/// The newest log file is kept on when it need not roll, so short runs of the
/// same day share a file; otherwise the result is a new file name, begun now.
///
/// @param folder the log folder
/// @param now the time now
/// @param limits the folder's limits
/// @return the file's path; it need not exist
std::filesystem::path choose_file(
    const std::filesystem::path& folder, std::chrono::sys_seconds now, const Limits& limits
);

/// Deletes old log files from `folder`.
///
/// First every log file begun more than max_age_days before `now` goes, then
/// the oldest ones until at most max_files are left. `current` is never
/// deleted, and files whose names are not log files' are left alone.
///
/// @param folder the log folder
/// @param now the time now
/// @param limits the folder's limits
/// @param current the file being written
void prune(
    const std::filesystem::path& folder,
    std::chrono::sys_seconds now,
    const Limits& limits,
    const std::filesystem::path& current
);

/// Returns whether another program reads the game's output.
///
/// True when standard output or standard error is a pipe or a file, as when a
/// test, a script or `program > file` captures it; false when both go to a
/// terminal or nowhere, as for a game started from a terminal or the desktop.
///
/// @return true when the output is captured
bool output_captured();

/// Sends standard output and standard error to a log file in `folder`.
///
/// Creates the folder, picks the file with choose_file(), prunes the folder and
/// writes a first line naming the time. Everything the program and its
/// libraries write to either stream goes to the file from then on.
///
/// @param folder the log folder
/// @param limits the folder's limits
/// @return false, leaving both streams as they were, when the folder or the
///     file cannot be opened
bool begin(const std::filesystem::path& folder, const Limits& limits = {});

/// Moves the streams to a new file when the current one must roll at `now`.
///
/// Does nothing before begin() has succeeded.
///
/// @param now the time now
void maintain(std::chrono::sys_seconds now);

/// Calls maintain() with the time now, at most once a second.
///
/// Cheap enough to call every frame.
void maintain();

/// Writes `text` to the terminal the program was started from.
///
/// Before begin() has succeeded this is standard error. Afterwards it is the
/// standard error the program started with, and only when that was a
/// terminal; a program started from the desktop has none.
///
/// @param text the text, written as it is
/// @return false when there is no terminal to write to
bool write_to_terminal(std::string_view text);

/// Returns the file the streams are written to.
///
/// @return the path, or an empty path before begin() has succeeded
std::filesystem::path current_file();

} // namespace oa::platform::log_files
