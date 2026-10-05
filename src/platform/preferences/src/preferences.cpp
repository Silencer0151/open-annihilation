// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/preferences.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>
#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>
#else
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#endif

namespace oa::platform::preferences {
void restore_music_defaults(MusicOptions& options) noexcept {
    options.music_volume = default_music_volume;
    options.cd_mode = default_cd_mode;
    // The word is stored only when bit 0 is clear, and setting the bit keeps
    // the rest of the word.
    if ((options.music_flags & music_flag::mode) == 0) {
        options.music_flags = static_cast<uint16_t>(options.music_flags | music_flag::mode);
    }
}

namespace {
constexpr std::size_t maximum_file_bytes = 1024U * 1024U;
constexpr std::size_t maximum_entries = 4096;
constexpr std::size_t maximum_value_bytes = 64U * 1024U;
constexpr std::string_view format_header = "open-annihilation-preferences 1";
std::atomic<unsigned long> temporary_sequence{};

void validate(const Values& values) {
    if (values.size() > maximum_entries)
        throw std::runtime_error("too many game preferences");
    for (const auto& [key, value] : values)
        if (key.empty() || key.size() > maximum_value_bytes || value.size() > maximum_value_bytes)
            throw std::runtime_error("game preference exceeds size limit");
}

/// Returns a path in UTF-8, for messages: a user folder named outside the
/// system's code page has no narrow spelling on Windows.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string path_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

std::runtime_error io_error(const char* operation, const std::filesystem::path& file) {
    return std::runtime_error(std::string(operation) + ": " + path_text(file));
}

/// The engine's folder in Application Support, named after the project's domain.
constexpr std::string_view apple_folder_name = "net.coreprime.open-annihilation";
/// The name earlier versions gave that folder.
constexpr std::string_view earlier_apple_folder_name = "com.coreprime.open-annihilation";
} // namespace

std::filesystem::path apple_data_directory(const std::filesystem::path& application_support) {
    const auto folder = application_support / apple_folder_name;
    const auto earlier = application_support / earlier_apple_folder_name;
    std::error_code error;
    // A folder whose existence cannot be told is treated as present, so the
    // earlier folder is moved only when the new one is known to be absent.
    const bool folder_absent = !std::filesystem::exists(folder, error) && !error;
    if (!folder_absent || !std::filesystem::is_directory(earlier, error))
        return folder;
    std::filesystem::rename(earlier, folder, error);
    // The folder may also have appeared from another instance starting at the
    // same time; either way, it is the one to use.
    std::error_code ignored;
    if (!error || std::filesystem::exists(folder, ignored))
        return folder;
    std::cerr << "cannot rename " << path_text(earlier) << " to " << path_text(folder) << ": "
              << error.message() << "; using it under its earlier name\n";
    return earlier;
}

#ifndef __APPLE__
#ifdef _WIN32
namespace {
/// Returns Local AppData/CorePrime/Open Annihilation, which holds the
/// preferences file and the data the engine keeps.
std::filesystem::path application_folder() {
    // The folder lookup every Windows release since XP has; on Vista and
    // later it names the same folder as the Local AppData known folder.
    std::array<wchar_t, MAX_PATH> directory{};
    const HRESULT result = SHGetFolderPathW(
        nullptr, CSIDL_LOCAL_APPDATA, nullptr, SHGFP_TYPE_CURRENT, directory.data()
    );
    if (FAILED(result))
        throw std::runtime_error("Local AppData directory unavailable");
    const std::filesystem::path root(directory.data());
    return root / "CorePrime" / "Open Annihilation";
}
} // namespace

std::filesystem::path default_file() {
    return application_folder() / "preferences.conf";
}

std::filesystem::path data_directory() {
    return application_folder();
}

std::filesystem::path documents_directory() {
    // The Documents folder, My Documents on Windows XP, where the player or
    // an administrator has redirected it; the lookup every release since XP
    // has.
    std::array<wchar_t, MAX_PATH> directory{};
    const HRESULT result =
        SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, directory.data());
    if (FAILED(result))
        throw std::runtime_error("Documents directory unavailable");
    return std::filesystem::path(directory.data());
}
#else
namespace {
/// The most bytes of user-dirs.dirs read; the file the system writes holds a
/// few hundred.
constexpr std::size_t maximum_user_dirs_bytes = 64U * 1024U;

/// Returns the folder an XDG base directory variable names, or `fallback`
/// under the home directory when the variable is unset or relative.
std::filesystem::path xdg_folder(const char* variable, const char* fallback, const char* purpose) {
    const char* xdg = std::getenv(variable);
    // XDG explicitly rejects relative environment-variable paths.
    if (xdg != nullptr && std::filesystem::path(xdg).is_absolute())
        return xdg;
    const char* home = std::getenv("HOME");
    if (home == nullptr || !std::filesystem::path(home).is_absolute())
        throw std::runtime_error(std::string("user home directory unavailable for ") + purpose);
    return std::filesystem::path(home) / fallback;
}
} // namespace

std::filesystem::path default_file() {
    return xdg_folder("XDG_CONFIG_HOME", ".config", "preferences") / "open-annihilation" /
           "preferences.conf";
}

std::filesystem::path data_directory() {
    return xdg_folder("XDG_DATA_HOME", ".local/share", "game data") / "open-annihilation";
}

std::filesystem::path documents_directory() {
    const char* home = std::getenv("HOME");
    if (home == nullptr || !std::filesystem::path(home).is_absolute())
        throw std::runtime_error("user home directory unavailable for the Documents folder");
    const auto user_dirs =
        xdg_folder("XDG_CONFIG_HOME", ".config", "the Documents folder") / "user-dirs.dirs";
    std::ifstream input(user_dirs, std::ios::binary);
    if (input) {
        std::string text(maximum_user_dirs_bytes, '\0');
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
        if (auto documents = xdg_documents_directory(text, home))
            return *documents;
    }
    return std::filesystem::path(home) / "Documents";
}
#endif
#endif

std::optional<std::filesystem::path>
xdg_documents_directory(std::string_view text, const std::filesystem::path& home) {
    constexpr std::string_view name = "XDG_DOCUMENTS_DIR";
    constexpr std::string_view home_prefix = "$HOME/";
    std::optional<std::filesystem::path> found;
    const auto skip_blanks = [](std::string_view& rest) {
        while (!rest.empty() && (rest.front() == ' ' || rest.front() == '\t'))
            rest.remove_prefix(1);
    };
    std::size_t at = 0;
    while (at < text.size()) {
        const auto end = std::min(text.find('\n', at), text.size());
        std::string_view line = text.substr(at, end - at);
        at = end + 1;
        if (!line.empty() && line.back() == '\r')
            line.remove_suffix(1);
        skip_blanks(line);
        if (!line.starts_with(name))
            continue;
        line.remove_prefix(name.size());
        skip_blanks(line);
        if (!line.starts_with('='))
            continue;
        line.remove_prefix(1);
        skip_blanks(line);
        if (!line.starts_with('"'))
            continue;
        line.remove_prefix(1);
        bool relative = false;
        if (line.starts_with(home_prefix)) {
            line.remove_prefix(home_prefix.size());
            relative = true;
        } else if (!line.starts_with('/')) {
            continue;
        }
        std::string value;
        bool closed = false;
        for (std::size_t index = 0; index < line.size(); ++index) {
            if (line[index] == '"') {
                closed = true;
                break;
            }
            if (line[index] == '\\' && index + 1 < line.size())
                ++index;
            value += line[index];
        }
        if (!closed)
            continue;
        if (!relative)
            found = std::filesystem::path(value);
        else
            found = value.empty() ? home : home / value;
    }
    return found;
}

std::filesystem::path default_user_folder() {
    return documents_directory() / std::string(user_folder_name);
}

Values load(const std::filesystem::path& file) {
    if (!std::filesystem::exists(file))
        return {};
    if (std::filesystem::file_size(file) > maximum_file_bytes)
        throw std::runtime_error("game preferences file exceeds size limit");
    std::ifstream input(file, std::ios::binary);
    if (!input)
        throw io_error("cannot read preferences", file);
    std::string data;
    data.resize(maximum_file_bytes + 1);
    input.read(data.data(), static_cast<std::streamsize>(data.size()));
    data.resize(static_cast<std::size_t>(input.gcount()));
    if (input.bad())
        throw io_error("cannot read preferences", file);
    if (data.size() > maximum_file_bytes)
        throw std::runtime_error("game preferences grew beyond size limit");
    std::istringstream stream(data);
    std::string header;
    std::getline(stream, header);
    // Lines may end in CR LF, as a file edited on Windows keeps them; the
    // entries' line ends are blanks between them.
    if (!header.empty() && header.back() == '\r')
        header.pop_back();
    if (header != format_header)
        throw std::runtime_error("unrecognized game preferences format");
    Values result;
    while (stream >> std::ws && !stream.eof()) {
        std::string key, value;
        if (stream.peek() != '"' || !(stream >> std::quoted(key) >> std::ws) ||
            stream.peek() != '"' || !(stream >> std::quoted(value)))
            throw std::runtime_error("malformed game preference entry");
        if (!result.emplace(std::move(key), std::move(value)).second)
            throw std::runtime_error("duplicate game preference entry");
        validate(result);
    }
    return result;
}

namespace {
/// Encodes the values in the preferences format, checking the size limits.
///
/// Throws std::runtime_error when the values exceed them.
///
/// @param values key/value map to store
/// @return the file's bytes
std::string encode(const Values& values) {
    validate(values);
    // Check the encoded size before constructing it: individually valid
    // entries can otherwise expand to hundreds of MiB before the file cap is
    // checked. std::quoted adds an escape before each quote and backslash.
    std::size_t encoded_bytes = format_header.size() + 1;
    for (const auto& [key, value] : values) {
        constexpr std::size_t entry_punctuation_bytes = 6; // four quotes, space, newline
        const auto escaped_size = [](const std::string& text) {
            return text.size() +
                   static_cast<std::size_t>(std::count_if(text.begin(), text.end(), [](char ch) {
                       return ch == '"' || ch == '\\';
                   }));
        };
        const auto entry_bytes = escaped_size(key) + escaped_size(value) + entry_punctuation_bytes;
        if (entry_bytes > maximum_file_bytes - encoded_bytes)
            throw std::runtime_error("game preferences file exceeds size limit");
        encoded_bytes += entry_bytes;
    }
    std::ostringstream stream;
    stream << format_header << '\n';
    for (const auto& [key, value] : values)
        stream << std::quoted(key) << ' ' << std::quoted(value) << '\n';
    auto bytes = stream.str();
    if (bytes.size() > maximum_file_bytes)
        throw std::runtime_error("game preferences file exceeds size limit");
    return bytes;
}

/// Makes the folder that holds a file when it is missing.
///
/// @param file the file
void make_folder(const std::filesystem::path& file) {
    // A file named without a folder is written in the current directory:
    // there is no folder to make, and making an empty path fails.
    if (file.has_parent_path())
        std::filesystem::create_directories(file.parent_path());
}
} // namespace

void save(const std::filesystem::path& file, const Values& values, SyncFolder sync) {
    const auto bytes = encode(values);
    make_folder(file);
#ifdef _WIN32
    const auto process = GetCurrentProcessId();
#else
    const auto process = getpid();
#endif
    auto temporary = file;
    temporary += ".tmp-" + std::to_string(process) + "-" + std::to_string(temporary_sequence++);

    struct Cleanup {
        std::filesystem::path path;
        bool owned = false;

        ~Cleanup() {
            if (owned) {
                std::error_code ignored;
                std::filesystem::remove(path, ignored);
            }
        }
    } cleanup{temporary};
#ifdef _WIN32
    HANDLE handle = CreateFileW(
        temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW, FILE_ATTRIBUTE_NORMAL, nullptr
    );
    if (handle == INVALID_HANDLE_VALUE)
        throw io_error("cannot create preferences temporary file", temporary);
    cleanup.owned = true;
    DWORD written = 0;
    const bool complete =
        WriteFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &written, nullptr) &&
        written == bytes.size() && FlushFileBuffers(handle);
    const bool closed = CloseHandle(handle);
    if (!complete || !closed)
        throw io_error("cannot write preferences", temporary);
    // The replace is written through to the disk, so the folder needs no
    // sync of its own whatever `sync` asks.
    (void)sync;
    if (!MoveFileExW(
            temporary.c_str(), file.c_str(), MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH
        ))
        throw io_error("cannot replace preferences", file);
#else
    const int descriptor = ::open(temporary.c_str(), O_WRONLY | O_CREAT | O_EXCL, 0600);
    if (descriptor < 0)
        throw io_error("cannot create preferences temporary file", temporary);
    cleanup.owned = true;
    std::size_t position = 0;
    bool complete = true;
    while (position < bytes.size()) {
        const auto written = ::write(descriptor, bytes.data() + position, bytes.size() - position);
        if (written < 0 && errno == EINTR)
            continue;
        if (written <= 0) {
            complete = false;
            break;
        }
        position += static_cast<std::size_t>(written);
    }
    if (complete)
        complete = ::fsync(descriptor) == 0;
    const bool closed = ::close(descriptor) == 0;
    if (!complete || !closed)
        throw io_error("cannot write preferences", temporary);
    std::filesystem::rename(temporary, file);
    if (sync == SyncFolder::yes) {
        // The rename is a change to the folder, which reaches the disk only
        // when the folder is synced. A folder that cannot be opened or synced
        // keeps the replaced file all the same.
        const auto folder =
            file.has_parent_path() ? file.parent_path() : std::filesystem::path(".");
        const int folder_descriptor = ::open(folder.c_str(), O_RDONLY | O_DIRECTORY);
        if (folder_descriptor >= 0) {
            (void)::fsync(folder_descriptor);
            (void)::close(folder_descriptor);
        }
    }
#endif
    cleanup.owned = false;
}

void overwrite(const std::filesystem::path& file, const Values& values) {
    const auto bytes = encode(values);
    make_folder(file);
    // Truncating and writing the open file keeps it where it is: no
    // temporary file and no rename, and nothing waits for the disk.
    std::ofstream output(file, std::ios::binary | std::ios::trunc);
    if (!output)
        throw io_error("cannot open preferences for rewriting", file);
    output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    output.close();
    if (!output)
        throw io_error("cannot rewrite preferences", file);
}
} // namespace oa::platform::preferences
