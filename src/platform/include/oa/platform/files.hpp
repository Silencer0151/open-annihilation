// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// File boundary. Engine code opens, reads, positions and closes files through
// this table of function pointers; the default implementation uses the host C
// library, whose streams already serialise concurrent access.

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <filesystem>

namespace oa::platform {

struct FileHandle;

enum class SeekOrigin : int32_t { begin = 0, current = 1, end = 2 };

struct Files {
    void* context{};
    // mode uses C stdio spelling ("rb", "wb"...). Null on failure.
    FileHandle* (*open)(void* context, const char* path, const char* mode){};
    // Returns whole elements read, like fread.
    std::size_t (*read)(
        void* context, FileHandle* file, void* buffer, std::size_t size, std::size_t count
    ){};
    std::size_t (*write)(
        void* context, FileHandle* file, const void* buffer, std::size_t size, std::size_t count
    ){};
    // Current offset, or -1 on failure.
    int64_t (*tell)(void* context, FileHandle* file){};
    // 0 on success, nonzero on failure.
    int32_t (*seek)(void* context, FileHandle* file, int64_t offset, SeekOrigin origin){};
    void (*close)(void* context, FileHandle* file){};
};

/// Opens a file as a C stream, as fopen does.
///
/// Other streams and programs may open the same file at the same time, for
/// reading and for writing, on every system. On Windows the file is opened by
/// its path's wide spelling, so that a path longer than 259 characters opens
/// where long paths are turned on.
///
/// @param path the file's path, in the encoding the host C library reads
///        narrow paths in
/// @param mode the mode in fopen's spelling, such as "rb" or "ab"
/// @return the stream, or null when the file cannot be opened
[[nodiscard]] std::FILE* open_file(const char* path, const char* mode) noexcept;

/// Opens a file as a C stream, as fopen does, by a path of any spelling.
///
/// On Windows the file is opened by its path's wide spelling, so that a name
/// outside the system's code page opens too. Other streams and programs may
/// open the same file at the same time, for reading and for writing.
///
/// @param path the file's path
/// @param mode the mode in fopen's spelling, such as "rb" or "ab"
/// @return the stream, or null when the file cannot be opened
[[nodiscard]] std::FILE* open_file(const std::filesystem::path& path, const char* mode) noexcept;

/// Returns the most characters a path may have for the system to open it.
///
/// That is 1,023 on macOS and 4,095 on Linux. On Windows it is 259, unless
/// the system lets this program open longer paths (Windows 10, version 1607,
/// or later, with long paths turned on), when it is 32,767.
///
/// @return the length, counted in the path's own characters: bytes, or
///         UTF-16 units on Windows
[[nodiscard]] std::size_t longest_path() noexcept;

/// Reports whether the system opens paths longer than 259 characters only
/// once long paths are turned on in it, as Windows does.
///
/// @return true on Windows while long paths are not turned on, or the system
///         is older than Windows 10, version 1607
[[nodiscard]] bool long_paths_turned_off() noexcept;

/// Returns the file boundary backed by the host C library.
///
/// @return the boundary; its context is null
[[nodiscard]] Files stdio_files() noexcept;

/// Writes a serialised diagnostic line to standard error.
///
/// @param format printf-style format
/// @return the number of characters written, or a negative value on failure
int log_message(const char* format, ...) noexcept
#if defined(__GNUC__) || defined(__clang__)
    __attribute__((format(printf, 1, 2)))
#endif
    ;

} // namespace oa::platform
