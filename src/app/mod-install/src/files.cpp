// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The file system's own operations the installer needs beyond the standard
// library's: renames that never replace, files made anew and synced, locks
// no child inherits, links found and removed without being entered, and
// folders synced and flushed to storage.

#include "files.hpp"

#include "oa/app/mod_install.hpp"
#include "oa/base/threads.hpp"

#include <algorithm>
#include <map>
#include <memory>
#include <string>
#include <system_error>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <cerrno>
#include <cstdio>
#include <fcntl.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <unistd.h>
#endif

namespace oa::app::mod_install {

namespace fs = std::filesystem;

namespace detail {

#ifdef _WIN32

namespace {

/// Returns the error Windows gave last, as a system error.
///
/// @return the error
std::error_code last_error() noexcept {
    return {static_cast<int>(GetLastError()), std::system_category()};
}

/// Tells whether an entry with these attributes is a link: a reparse point
/// whose tag, read from the entry's directory listing, is a name surrogate
/// (reparse_tag_is_link). A folder a file sync keeps in the cloud carries a
/// tag of its own and is a folder. A tag that cannot be read counts as a
/// link, which is never entered.
///
/// @param wide the entry's extended path
/// @param attributes its attributes
/// @return true for a link
bool names_another_place(const std::wstring& wide, DWORD attributes) {
    if ((attributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        return false;
    // A listing names the entry itself, and gives its tag where the
    // attributes say it is a reparse point.
    WIN32_FIND_DATAW found{};
    const HANDLE search = FindFirstFileW(wide.c_str(), &found);
    if (search == INVALID_HANDLE_VALUE)
        return true;
    FindClose(search);
    // Without the attribute in the listing, no tag is given.
    if ((found.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT) == 0)
        return true;
    return reparse_tag_is_link(static_cast<uint32_t>(found.dwReserved0));
}

} // namespace

std::wstring extended_path(const fs::path& path) {
    std::error_code error;
    fs::path whole = path.is_absolute() ? path : fs::absolute(path, error);
    if (error)
        whole = path;
    std::wstring text = whole.lexically_normal().native();
    for (wchar_t& character : text)
        if (character == L'/')
            character = L'\\';
    if (text.starts_with(L"\\\\?\\"))
        return text;
    if (text.starts_with(L"\\\\"))
        return L"\\\\?\\UNC\\" + text.substr(2);
    return L"\\\\?\\" + text;
}

#endif

bool retryable(const std::error_code& error) noexcept {
#ifdef _WIN32
    if (error.category() != std::system_category())
        return false;
    const auto code = static_cast<DWORD>(error.value());
    return code == ERROR_ACCESS_DENIED || code == ERROR_SHARING_VIOLATION ||
           code == ERROR_LOCK_VIOLATION;
#else
    return error == std::errc::device_or_resource_busy;
#endif
}

bool remove_entry(const fs::path& path, std::error_code& error) noexcept {
    error.clear();
#ifdef _WIN32
    const std::wstring wide = extended_path(path);
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES) {
        error = last_error();
        return false;
    }
    if ((attributes & FILE_ATTRIBUTE_READONLY) != 0)
        SetFileAttributesW(wide.c_str(), attributes & ~DWORD{FILE_ATTRIBUTE_READONLY});
    // Whatever its reparse tag, an entry is removed itself, never entered:
    // a junction or a folder link (names_another_place) as a folder, which
    // drops only the link, and any other folder, one a file sync keeps in
    // the cloud included, only once it is empty.
    const bool folder = (attributes & FILE_ATTRIBUTE_DIRECTORY) != 0;
    if (!(folder ? RemoveDirectoryW(wide.c_str()) : DeleteFileW(wide.c_str()))) {
        error = last_error();
        return false;
    }
    return true;
#else
    if (::unlink(path.c_str()) == 0)
        return true;
    if (errno == EISDIR || errno == EPERM) {
        struct stat status{};
        if (::lstat(path.c_str(), &status) == 0 && S_ISDIR(status.st_mode)) {
            if (::rmdir(path.c_str()) == 0)
                return true;
        }
    }
    error = {errno, std::generic_category()};
    return false;
#endif
}

void sync_folder(const fs::path& folder) noexcept {
#ifndef _WIN32
    const int descriptor = ::open(folder.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor >= 0) {
        (void)::fsync(descriptor);
        (void)::close(descriptor);
    }
#else
    (void)folder;
#endif
}

void flush_to_storage(const fs::path& folder) noexcept {
#if defined(__APPLE__)
    const int descriptor = ::open(folder.c_str(), O_RDONLY | O_CLOEXEC);
    if (descriptor >= 0) {
        // A file system that keeps no drive cache refuses it: a sync is all
        // it needs.
        if (::fcntl(descriptor, F_FULLFSYNC) != 0)
            (void)::fsync(descriptor);
        (void)::close(descriptor);
    }
#else
    sync_folder(folder);
#endif
}

} // namespace detail

void rename_exclusively(const fs::path& from, const fs::path& to, std::error_code& error) noexcept {
    error.clear();
#ifdef _WIN32
    // Without MOVEFILE_REPLACE_EXISTING, a name that exists is refused.
    if (!MoveFileExW(from.c_str(), to.c_str(), 0))
        error = detail::last_error();
#else
    int result = -1;
#if defined(__APPLE__)
    result = ::renamex_np(from.c_str(), to.c_str(), RENAME_EXCL);
    const bool unsupported = result != 0 && (errno == ENOTSUP || errno == EINVAL);
#elif defined(__linux__) && defined(RENAME_NOREPLACE)
    result = ::renameat2(AT_FDCWD, from.c_str(), AT_FDCWD, to.c_str(), RENAME_NOREPLACE);
    const bool unsupported = result != 0 && (errno == EINVAL || errno == ENOSYS);
#else
    const bool unsupported = true;
#endif
    if (result == 0)
        return;
    if (!unsupported) {
        error = {errno, std::generic_category()};
        return;
    }
    // A file system without an exclusive rename: refuse a name that exists,
    // then rename.
    struct stat status{};
    if (::lstat(to.c_str(), &status) == 0) {
        error = std::make_error_code(std::errc::file_exists);
        return;
    }
    if (::rename(from.c_str(), to.c_str()) != 0)
        error = {errno, std::generic_category()};
#endif
}

bool is_link_or_junction(const fs::path& path) {
#ifdef _WIN32
    const std::wstring wide = detail::extended_path(path);
    const DWORD attributes = GetFileAttributesW(wide.c_str());
    return attributes != INVALID_FILE_ATTRIBUTES && detail::names_another_place(wide, attributes);
#else
    std::error_code error;
    return fs::is_symlink(fs::symlink_status(path, error));
#endif
}

std::unique_ptr<FileLock> FileLock::take(const fs::path& file) {
    std::unique_ptr<FileLock> lock(new FileLock());
#ifdef _WIN32
    SECURITY_ATTRIBUTES attributes{};
    attributes.nLength = sizeof attributes;
    attributes.bInheritHandle = FALSE;
    // Others may read the file, as a file sync or a virus scanner does, but
    // no other open for writing succeeds while it is held.
    HANDLE handle = CreateFileW(
        file.c_str(),
        GENERIC_READ | GENERIC_WRITE,
        FILE_SHARE_READ,
        &attributes,
        OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,
        nullptr
    );
    if (handle == INVALID_HANDLE_VALUE)
        return nullptr;
    lock->handle_ = handle;
#else
    const int descriptor = ::open(file.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0644);
    if (descriptor < 0)
        return nullptr;
    if (::flock(descriptor, LOCK_EX | LOCK_NB) != 0) {
        (void)::close(descriptor);
        return nullptr;
    }
    lock->descriptor_ = descriptor;
#endif
    return lock;
}

FileLock::~FileLock() {
#ifdef _WIN32
    if (handle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(handle_));
#else
    if (descriptor_ >= 0) {
        (void)::flock(descriptor_, LOCK_UN);
        (void)::close(descriptor_);
    }
#endif
}

ModsHold hold_mods_folder(const fs::path& mods) {
    // Every hold this process takes on one folder shares one lock.
    static base::threads::Mutex guard;
    static std::map<fs::path, std::weak_ptr<const FileLock>> held;
    std::error_code error;
    const fs::path absolute = fs::absolute(mods, error).lexically_normal();
    const fs::path key = error ? mods.lexically_normal() : absolute;
    const base::threads::LockGuard locked(guard);
    if (auto shared = held[key].lock())
        return shared;
    std::shared_ptr<const FileLock> taken = FileLock::take(key / std::string(lock_file_name));
    if (!taken)
        return nullptr;
    held[key] = taken;
    return taken;
}

NewFile::~NewFile() {
    close();
}

void NewFile::close() noexcept {
#ifdef _WIN32
    if (handle_ != nullptr)
        CloseHandle(static_cast<HANDLE>(handle_));
    handle_ = nullptr;
#else
    if (descriptor_ >= 0)
        (void)::close(descriptor_);
    descriptor_ = -1;
#endif
}

bool NewFile::create(const fs::path& file, std::error_code& error) noexcept {
    error.clear();
#ifdef _WIN32
    HANDLE handle = CreateFileW(
        file.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (handle == INVALID_HANDLE_VALUE) {
        const DWORD code = GetLastError();
        error = code == ERROR_FILE_EXISTS || code == ERROR_ALREADY_EXISTS
                    ? std::make_error_code(std::errc::file_exists)
                    : std::error_code(static_cast<int>(code), std::system_category());
        return false;
    }
    handle_ = handle;
#else
    const int descriptor = ::open(file.c_str(), O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0644);
    if (descriptor < 0) {
        error = errno == EEXIST ? std::make_error_code(std::errc::file_exists)
                                : std::error_code(errno, std::generic_category());
        return false;
    }
    descriptor_ = descriptor;
#endif
    return true;
}

bool NewFile::write(std::span<const uint8_t> bytes) noexcept {
#ifdef _WIN32
    if (handle_ == nullptr)
        return false;
    while (!bytes.empty()) {
        const auto piece = static_cast<DWORD>(std::min<std::size_t>(bytes.size(), 1U << 30));
        DWORD written = 0;
        if (!WriteFile(static_cast<HANDLE>(handle_), bytes.data(), piece, &written, nullptr) ||
            written == 0)
            return false;
        bytes = bytes.subspan(written);
    }
    return true;
#else
    if (descriptor_ < 0)
        return false;
    while (!bytes.empty()) {
        const auto written = ::write(descriptor_, bytes.data(), bytes.size());
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0)
            return false;
        bytes = bytes.subspan(static_cast<std::size_t>(written));
    }
    return true;
#endif
}

bool NewFile::finish() noexcept {
#ifdef _WIN32
    if (handle_ == nullptr)
        return false;
    const bool flushed = FlushFileBuffers(static_cast<HANDLE>(handle_)) != 0;
    const bool closed = CloseHandle(static_cast<HANDLE>(handle_)) != 0;
    handle_ = nullptr;
    return flushed && closed;
#else
    if (descriptor_ < 0)
        return false;
    const bool synced = ::fsync(descriptor_) == 0;
    const bool closed = ::close(descriptor_) == 0;
    descriptor_ = -1;
    return synced && closed;
#endif
}

} // namespace oa::app::mod_install
