// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/files.hpp"

#include "oa/base/threads.hpp"

#include <cstdarg>
#include <cstdint>
#include <cstdio>

#if defined(_WIN32)
#include <share.h>
#endif

namespace oa::platform {
namespace {

std::FILE* stream(FileHandle* file) noexcept {
    return reinterpret_cast<std::FILE*>(file);
}

FileHandle* stdio_open(void*, const char* path, const char* mode) {
    return reinterpret_cast<FileHandle*>(open_file(path, mode));
}

std::size_t stdio_read(void*, FileHandle* file, void* buffer, std::size_t size, std::size_t count) {
    return std::fread(buffer, size, count, stream(file));
}

std::size_t
stdio_write(void*, FileHandle* file, const void* buffer, std::size_t size, std::size_t count) {
    return std::fwrite(buffer, size, count, stream(file));
}

int64_t stdio_tell(void*, FileHandle* file) {
#if defined(_WIN32)
    return _ftelli64(stream(file));
#else
    return static_cast<int64_t>(ftello(stream(file)));
#endif
}

int32_t stdio_seek(void*, FileHandle* file, int64_t offset, SeekOrigin origin) {
    const int whence = origin == SeekOrigin::begin     ? SEEK_SET
                       : origin == SeekOrigin::current ? SEEK_CUR
                                                       : SEEK_END;
#if defined(_WIN32)
    return _fseeki64(stream(file), offset, whence);
#else
    return fseeko(stream(file), static_cast<off_t>(offset), whence);
#endif
}

void stdio_close(void*, FileHandle* file) {
    if (file) {
        std::fclose(stream(file));
    }
}

/// Holds the lock that keeps log lines whole without ever destroying it, so
/// that a thread still logging while the program exits finds it intact.
union LogLock {
    constexpr LogLock() : mutex() {}

    ~LogLock() {}

    LogLock(const LogLock&) = delete;
    LogLock& operator=(const LogLock&) = delete;
    base::threads::Mutex mutex;
};

constinit LogLock log_lock;

} // namespace

std::FILE* open_file(const char* path, const char* mode) noexcept {
#if defined(_WIN32)
    // fopen's own sharing: other opens may read and write the file.
    return _fsopen(path, mode, _SH_DENYNO);
#else
    return std::fopen(path, mode);
#endif
}

std::FILE* open_file(const std::filesystem::path& path, const char* mode) noexcept {
#if defined(_WIN32)
    // A mode is a few ASCII letters; one too long to be valid opens nothing.
    constexpr std::size_t mode_capacity = 16;
    wchar_t wide_mode[mode_capacity]{};
    for (std::size_t index = 0; mode[index] != '\0'; ++index) {
        if (index + 1 >= mode_capacity)
            return nullptr;
        wide_mode[index] = static_cast<wchar_t>(static_cast<unsigned char>(mode[index]));
    }
    return _wfsopen(path.c_str(), wide_mode, _SH_DENYNO);
#else
    return std::fopen(path.c_str(), mode);
#endif
}

Files stdio_files() noexcept {
    return {nullptr, stdio_open, stdio_read, stdio_write, stdio_tell, stdio_seek, stdio_close};
}

int log_message(const char* format, ...) noexcept {
    const base::threads::LockGuard guard(log_lock.mutex);
    va_list arguments;
    va_start(arguments, format);
    const int written = std::vfprintf(stderr, format, arguments);
    va_end(arguments);
    return written;
}

} // namespace oa::platform
