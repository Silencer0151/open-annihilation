// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// File boundary. Engine code opens, reads, positions and closes files through
// this table of function pointers; the default implementation uses the host C
// library, whose streams already serialise concurrent access.

#include <cstddef>
#include <cstdint>

namespace oa::platform {

struct FileHandle;

enum class SeekOrigin : int32_t { begin = 0, current = 1, end = 2 };

struct Files {
    void* context;
    // mode uses C stdio spelling ("rb", "wb"...). Null on failure.
    FileHandle* (*open)(void* context, const char* path, const char* mode);
    // Returns whole elements read, like fread.
    std::size_t (*read)(
        void* context, FileHandle* file, void* buffer, std::size_t size, std::size_t count
    );
    std::size_t (*write)(
        void* context, FileHandle* file, const void* buffer, std::size_t size, std::size_t count
    );
    // Current offset, or -1 on failure.
    int64_t (*tell)(void* context, FileHandle* file);
    // 0 on success, nonzero on failure.
    int32_t (*seek)(void* context, FileHandle* file, int64_t offset, SeekOrigin origin);
    void (*close)(void* context, FileHandle* file);
};

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
