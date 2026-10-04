// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Showing a folder in the system's file manager: the platform's own way
// where it has one (PlatformHooks::show_folder), else open on macOS, the
// shell's open verb on Windows, and xdg-open on Linux, or the desktop portal
// where xdg-open is missing.

#include "oa/app/platform_hooks.hpp"
#include "oa/app/user_folder.hpp"

#include <filesystem>
#include <string>
#include <tuple>

#ifndef OA_PROCESS_SPAWNING
#error "OA_PROCESS_SPAWNING (0 or 1) says whether the game may start other programs"
#endif

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <shellapi.h>
#else
#include <SDL3/SDL.h>
#endif

namespace oa::app {

namespace fs = std::filesystem;

namespace {

/// Converts a path to UTF-8 text.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_of(const fs::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

#if OA_PROCESS_SPAWNING
#ifdef _WIN32
/// Shows a folder through the shell's open verb, which opens it in Explorer
/// or whatever file manager the player has made the folders' default.
///
/// @param folder the folder
/// @return what came of it
FolderOpening open_with_shell(const fs::path& folder) {
    // The shell may hand the folder to an extension that needs COM; a
    // thread already in another apartment keeps it.
    const HRESULT com = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    const HINSTANCE result =
        ShellExecuteW(nullptr, L"open", folder.c_str(), nullptr, nullptr, SW_SHOWNORMAL);
    if (SUCCEEDED(com))
        CoUninitialize();
    // A value above 32 is success; the others are the shell's error codes.
    constexpr INT_PTR shell_success_above = 32;
    const auto code = reinterpret_cast<INT_PTR>(result);
    if (code > shell_success_above)
        return FolderOpening{true, {}, {}};
    return FolderOpening{
        false,
        std::string(file_manager_failed_text),
        "the shell did not open " + utf8_of(folder) + " (code " + std::to_string(code) + ")"
    };
}
#else
/// Runs a program and waits for it to end.
///
/// @param arguments the program and its arguments, null-terminated
/// @param[out] why what went wrong, when it did
/// @return true when it started and ended with status 0
bool run_to_end(const char* const* arguments, std::string& why) {
    SDL_Process* process = SDL_CreateProcess(arguments, false);
    if (process == nullptr) {
        why = std::string("cannot start ") + arguments[0] + ": " + SDL_GetError();
        return false;
    }
    int status = 0;
    const bool ended = SDL_WaitProcess(process, true, &status);
    SDL_DestroyProcess(process);
    if (!ended || status != 0) {
        why = std::string(arguments[0]) + " ended with status " + std::to_string(status);
        return false;
    }
    return true;
}

#ifndef __APPLE__
/// Starts a program in the background, without waiting for it: xdg-open may
/// wait for the file manager it starts.
///
/// @param arguments the program and its arguments, null-terminated
/// @param[out] why what went wrong, when it did
/// @return true when it started
bool start_in_background(const char* const* arguments, std::string& why) {
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0) {
        why = SDL_GetError();
        return false;
    }
    // SDL reads the arguments as a mutable array but never writes them.
    void* const argument_list = const_cast<char**>(arguments);
    std::ignore =
        SDL_SetPointerProperty(properties, SDL_PROP_PROCESS_CREATE_ARGS_POINTER, argument_list);
    std::ignore =
        SDL_SetBooleanProperty(properties, SDL_PROP_PROCESS_CREATE_BACKGROUND_BOOLEAN, true);
    SDL_Process* process = SDL_CreateProcessWithProperties(properties);
    SDL_DestroyProperties(properties);
    if (process == nullptr) {
        why = std::string("cannot start ") + arguments[0] + ": " + SDL_GetError();
        return false;
    }
    SDL_DestroyProcess(process);
    return true;
}
#endif
#endif
#endif

} // namespace

FolderOpenerHooks system_folder_opener() noexcept {
    FolderOpenerHooks hooks{};
    hooks.open = [](void*, const fs::path& folder) {
        // The platform's own way comes first.
        if (const PlatformHooks& platform = platform_hooks(); platform.show_folder != nullptr) {
            std::string why;
            if (platform.show_folder(platform.context, utf8_of(folder).c_str(), &why))
                return FolderOpening{true, {}, {}};
            return FolderOpening{false, std::string(file_manager_failed_text), why};
        }
#if !OA_PROCESS_SPAWNING
        // Without it, a build that starts no other programs shows no folder.
        return FolderOpening{
            false,
            std::string(no_file_manager_text),
            "this build starts no other programs, and the platform shows no folder"
        };
#elif defined(_WIN32)
        return open_with_shell(folder);
#else
        const std::string text = utf8_of(folder);
        std::string why;
#ifdef __APPLE__
        const char* const open[] = {"/usr/bin/open", text.c_str(), nullptr};
        if (run_to_end(open, why))
            return FolderOpening{true, {}, {}};
        return FolderOpening{false, std::string(file_manager_failed_text), why};
#else
        const char* const xdg_open[] = {"xdg-open", text.c_str(), nullptr};
        if (start_in_background(xdg_open, why))
            return FolderOpening{true, {}, {}};
        // Without xdg-open, the desktop portal is asked over the session bus.
        // Quoted, so that gdbus reads it as a string; file_uri writes no quote.
        const std::string uri = "'" + file_uri(folder) + "'";
        const char* const portal[] = {
            "gdbus",
            "call",
            "--session",
            "--timeout",
            "5",
            "--dest",
            "org.freedesktop.portal.Desktop",
            "--object-path",
            "/org/freedesktop/portal/desktop",
            "--method",
            "org.freedesktop.portal.OpenURI.OpenURI",
            "''",
            uri.c_str(),
            "{}",
            nullptr
        };
        std::string portal_why;
        if (run_to_end(portal, portal_why))
            return FolderOpening{
                true, {}, why + "; the desktop portal was asked to show " + text + " instead"
            };
        return FolderOpening{
            false,
            std::string(no_file_manager_text),
            why + "; nor did the desktop portal take " + text + ": " + portal_why +
                " (install xdg-utils, or a desktop portal and gdbus)"
        };
#endif
#endif
    };
    return hooks;
}

} // namespace oa::app
