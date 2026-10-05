// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Windows registration: the .oamod extension, its program identifier, the program's icon
// and its open command under HKEY_CURRENT_USER, through calls Windows XP has.

#include "oa/platform/file_types.hpp"

#include "utf8.hpp"

#include <exception>
#include <string>
#include <system_error>
#include <vector>

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <shlobj.h>

namespace oa::platform::file_types {

namespace fs = std::filesystem;

namespace {

/// The Windows name of the key HKEY_CURRENT_USER, for the log.
constexpr std::string_view current_user_name = "HKEY_CURRENT_USER";
/// The value of a key that the registry calls (Default), for the log.
constexpr std::string_view default_value_name = "(Default)";

/// One string value the registration keeps.
struct RegistryValue {
    std::wstring key{};  ///< under HKEY_CURRENT_USER
    std::wstring name{}; ///< empty for the key's default value
    std::wstring data{};
};

/// Converts UTF-8 text to UTF-16.
///
/// @param text the text, UTF-8
/// @return the text, UTF-16; empty when it is not UTF-8
std::wstring wide_of(std::string_view text) {
    if (text.empty())
        return {};
    const int length = MultiByteToWideChar(
        CP_UTF8, MB_ERR_INVALID_CHARS, text.data(), static_cast<int>(text.size()), nullptr, 0
    );
    if (length <= 0)
        return {};
    std::wstring wide(static_cast<std::size_t>(length), L'\0');
    MultiByteToWideChar(
        CP_UTF8,
        MB_ERR_INVALID_CHARS,
        text.data(),
        static_cast<int>(text.size()),
        wide.data(),
        length
    );
    return wide;
}

/// Converts UTF-16 text to UTF-8, for the log.
///
/// @param text the text, UTF-16
/// @return the text, UTF-8
std::string narrow_of(const std::wstring& text) {
    if (text.empty())
        return {};
    const int length = WideCharToMultiByte(
        CP_UTF8, 0, text.data(), static_cast<int>(text.size()), nullptr, 0, nullptr, nullptr
    );
    if (length <= 0)
        return {};
    std::string narrow(static_cast<std::size_t>(length), '\0');
    WideCharToMultiByte(
        CP_UTF8,
        0,
        text.data(),
        static_cast<int>(text.size()),
        narrow.data(),
        length,
        nullptr,
        nullptr
    );
    return narrow;
}

/// Returns a value's place, for the log: its key under HKEY_CURRENT_USER, then its name.
///
/// @param value the value
/// @return the place
std::string place_of(const RegistryValue& value) {
    return std::string(current_user_name) + "\\" + narrow_of(value.key) + " " +
           (value.name.empty() ? std::string(default_value_name) : narrow_of(value.name));
}

/// Returns a system error's description.
///
/// @param code the error, as the registry calls return it
/// @return its description
std::string error_text(LONG code) {
    return std::system_category().message(static_cast<int>(code));
}

/// Reports whether a value is a string holding `value.data`.
///
/// @param value the value
/// @return true when it is there with that text
bool holds(const RegistryValue& value) {
    HKEY key = nullptr;
    if (RegOpenKeyExW(HKEY_CURRENT_USER, value.key.c_str(), 0, KEY_QUERY_VALUE, &key) !=
        ERROR_SUCCESS)
        return false;
    const wchar_t* name = value.name.empty() ? nullptr : value.name.c_str();
    DWORD type = 0;
    DWORD size = 0;
    bool same = false;
    if (RegQueryValueExW(key, name, nullptr, &type, nullptr, &size) == ERROR_SUCCESS &&
        type == REG_SZ) {
        // Room for the text and its terminating zero, which a value may lack.
        std::vector<wchar_t> held(size / sizeof(wchar_t) + 2, L'\0');
        DWORD held_size = static_cast<DWORD>((held.size() - 1) * sizeof(wchar_t));
        if (RegQueryValueExW(
                key, name, nullptr, &type, reinterpret_cast<BYTE*>(held.data()), &held_size
            ) == ERROR_SUCCESS &&
            type == REG_SZ) {
            std::size_t length = held_size / sizeof(wchar_t);
            while (length > 0 && held[length - 1] == L'\0')
                --length;
            same = std::wstring(held.data(), length) == value.data;
        }
    }
    RegCloseKey(key);
    return same;
}

/// Writes a string value, making its key when it is missing.
///
/// @param value the value
/// @return ERROR_SUCCESS, or the registry's error
LONG write(const RegistryValue& value) {
    HKEY key = nullptr;
    LONG result = RegCreateKeyExW(
        HKEY_CURRENT_USER,
        value.key.c_str(),
        0,
        nullptr,
        REG_OPTION_NON_VOLATILE,
        KEY_SET_VALUE,
        nullptr,
        &key,
        nullptr
    );
    if (result != ERROR_SUCCESS)
        return result;
    const wchar_t* name = value.name.empty() ? nullptr : value.name.c_str();
    const auto size = static_cast<DWORD>((value.data.size() + 1) * sizeof(wchar_t));
    result = RegSetValueExW(
        key, name, 0, REG_SZ, reinterpret_cast<const BYTE*>(value.data.c_str()), size
    );
    RegCloseKey(key);
    return result;
}

/// Returns a path's long form, upper-cased without regard to the user's language, for
/// comparing paths as Windows does.
///
/// @param path the path
/// @return its long form upper-cased; the path upper-cased when it has no long form
std::wstring folded(std::wstring path) {
    std::wstring long_form(MAX_PATH, L'\0');
    DWORD length =
        GetLongPathNameW(path.c_str(), long_form.data(), static_cast<DWORD>(long_form.size()));
    if (length >= long_form.size()) {
        long_form.resize(length);
        length =
            GetLongPathNameW(path.c_str(), long_form.data(), static_cast<DWORD>(long_form.size()));
    }
    if (length > 0 && length < long_form.size()) {
        long_form.resize(length);
        path = long_form;
    }
    if (path.empty())
        return path;
    std::wstring upper(path.size(), L'\0');
    const int mapped = LCMapStringW(
        LOCALE_INVARIANT,
        LCMAP_UPPERCASE,
        path.c_str(),
        static_cast<int>(path.size()),
        upper.data(),
        static_cast<int>(upper.size())
    );
    return mapped == static_cast<int>(path.size()) ? upper : path;
}

/// Returns the temporary folder when it holds `executable`, as it does for a copy Explorer
/// started from inside a zip.
///
/// @param executable the program, absolute
/// @return the folder; empty when the program lies outside it
std::wstring temporary_folder_of(const std::wstring& executable) {
    std::wstring folder(MAX_PATH + 1, L'\0');
    const DWORD length = GetTempPathW(static_cast<DWORD>(folder.size()), folder.data());
    if (length == 0 || length > folder.size())
        return {};
    folder.resize(length);
    std::wstring folder_key = folded(folder);
    if (folder_key.empty())
        return {};
    if (folder_key.back() != L'\\')
        folder_key += L'\\';
    const std::wstring executable_key = folded(executable);
    if (executable_key.size() > folder_key.size() &&
        executable_key.compare(0, folder_key.size(), folder_key) == 0)
        return folder;
    return {};
}

/// The registration itself; register_windows catches what it throws.
///
/// @param executable the program, absolute
/// @param places where it writes
/// @return what was done
Registration register_in(const fs::path& executable, const Places& places) {
    Registration done{};
    done.supported = true;
    if (!executable.is_absolute()) {
        done.error = "the program's path " + detail::utf8_of(executable) + " is not absolute";
        return done;
    }
    const std::wstring program = executable.wstring();
    if (const std::wstring temporary = temporary_folder_of(program); !temporary.empty()) {
        done.lines.push_back(
            "skipped: " + narrow_of(program) + " lies in the temporary folder " +
            narrow_of(temporary)
        );
        return done;
    }
    const std::wstring classes = wide_of(places.classes_key);
    const std::wstring extension = classes + L"\\." + wide_of(mod_extension);
    const std::wstring program_id = wide_of(mod_program_id);
    const std::wstring program_key = classes + L"\\" + program_id;
    const RegistryValue values[] = {
        {extension, L"", program_id},
        {extension, L"Content Type", wide_of(mod_mime_type)},
        {extension + L"\\OpenWithProgids", program_id, L""},
        {program_key, L"", wide_of(mod_type_name)},
        {program_key + L"\\DefaultIcon", L"", wide_of(default_icon(executable))},
        {program_key + L"\\shell\\open\\command", L"", wide_of(open_command(executable))},
    };
    for (const RegistryValue& value : values) {
        if (holds(value))
            continue;
        const LONG result = write(value);
        if (result != ERROR_SUCCESS) {
            done.error = "cannot write " + place_of(value) + ": " + error_text(result);
            break;
        }
        done.changed = true;
        done.lines.push_back("wrote " + place_of(value));
    }
    if (done.changed && places.notify_shell) {
        SHChangeNotify(SHCNE_ASSOCCHANGED, SHCNF_IDLIST, nullptr, nullptr);
        done.lines.push_back("told the shell the file types changed");
    }
    return done;
}

} // namespace

Registration register_windows(const fs::path& executable, const Places& places) {
    try {
        return register_in(executable, places);
    } catch (const std::exception& error) {
        Registration failed{};
        failed.supported = true;
        failed.error = std::string("the file types could not be registered: ") + error.what();
        return failed;
    }
}

} // namespace oa::platform::file_types
