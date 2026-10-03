// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The game directory host over SDL's native folder dialog and message boxes.
#include "oa/app/app.hpp"
#include "oa/app/game_directory.hpp"
#include <SDL3/SDL.h>
#include <atomic>
#include <iostream>
#include <string>
#include <tuple>

namespace oa::app {
namespace {

constexpr const char* kDialogTitle = "Choose your Total Annihilation folder";
constexpr const char* kMessageTitle = "Open Annihilation";
constexpr Uint32 kDialogPollMs = 50;

// Video runs only while the dialogs are up, and the hints set for them are
// dropped with it; HostDisplay starts its own.
struct NativeDialogs {
    bool video_started = false;
    bool video_failed = false;
    std::string video_error;

    ~NativeDialogs() {
        if (video_started)
            SDL_QuitSubSystem(SDL_INIT_VIDEO);
        // SDL refuses only a hint it does not hold, which has nothing to
        // reset.
        if (video_started || video_failed) {
            std::ignore = SDL_ResetHint(SDL_HINT_MAC_BACKGROUND_APP);
            std::ignore = SDL_ResetHint(SDL_HINT_NO_SIGNAL_HANDLERS);
        }
    }
};

// SDL answers inside the call on macOS, on a worker thread on Windows and for
// zenity, and from event pumping for the desktop portal: the fields are
// written before `done` is released and read after it is acquired.
struct DialogWait {
    std::atomic<bool> done{};
    bool failed = false;
    std::string path;
    std::string error;
};

void SDLCALL folder_chosen(void* userdata, const char* const* filelist, int) {
    auto* wait = static_cast<DialogWait*>(userdata);
    if (filelist == nullptr) {
        wait->failed = true;
        wait->error = SDL_GetError();
    } else if (filelist[0] != nullptr) {
        // zenity reports a cancel as one empty path.
        wait->path = filelist[0];
    }
    wait->done.store(true, std::memory_order_release);
}

bool start_video(NativeDialogs& dialogs) {
    if (dialogs.video_started || dialogs.video_failed)
        return dialogs.video_started;
    // Both hints are comforts: refused, the dialogs still open with SDL's
    // own behaviour.
    // From macOS 14 SDL leaves a terminal-launched process in the background,
    // and the panel with it.
    std::ignore = SDL_SetHint(SDL_HINT_MAC_BACKGROUND_APP, "0");
    // Nothing reads SDL's quit event while a dialog is open, so Ctrl+C keeps
    // ending the process.
    std::ignore = SDL_SetHint(SDL_HINT_NO_SIGNAL_HANDLERS, "1");
    if (!SDL_InitSubSystem(SDL_INIT_VIDEO)) {
        dialogs.video_failed = true;
        dialogs.video_error = SDL_GetError();
        return false;
    }
    dialogs.video_started = true;
    // macOS finishes launching, and brings the app forward, in the first
    // event loop; the panel must not be that loop.
    SDL_PumpEvents();
    return true;
}

FolderPick pick_folder(void* context, const fs::path& start, fs::path* chosen, std::string* error) {
    auto& dialogs = *static_cast<NativeDialogs*>(context);
    if (!start_video(dialogs)) {
        *error = dialogs.video_error;
        return FolderPick::unavailable;
    }
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver != nullptr && unattended_environment({}, driver)) {
        *error = std::string("the ") + driver + " video driver has no display";
        return FolderPick::unavailable;
    }
    const SDL_PropertiesID properties = SDL_CreateProperties();
    if (properties == 0) {
        *error = SDL_GetError();
        return FolderPick::unavailable;
    }
    // A property SDL cannot store leaves the dialog with SDL's own title,
    // starting folder or choice of one folder; it still opens.
    std::ignore =
        SDL_SetStringProperty(properties, SDL_PROP_FILE_DIALOG_TITLE_STRING, kDialogTitle);
    const auto location = dialog_location(start);
    if (!location.empty()) {
        std::ignore = SDL_SetStringProperty(
            properties, SDL_PROP_FILE_DIALOG_LOCATION_STRING, location.c_str()
        );
    }
    std::ignore = SDL_SetBooleanProperty(properties, SDL_PROP_FILE_DIALOG_MANY_BOOLEAN, false);
    DialogWait wait;
    SDL_ShowFileDialogWithProperties(SDL_FILEDIALOG_OPENFOLDER, folder_chosen, &wait, properties);
    SDL_DestroyProperties(properties);
    // A worker thread may still hold `wait`, so the loop cannot end early.
    // Pumping delivers the desktop portal's answer.
    while (!wait.done.load(std::memory_order_acquire)) {
        SDL_PumpEvents();
        SDL_Delay(kDialogPollMs);
    }
    if (wait.failed) {
        *error = wait.error;
#if !defined(SDL_PLATFORM_WINDOWS) && !defined(SDL_PLATFORM_APPLE)
        *error += "\nThe folder dialog needs xdg-desktop-portal with a GTK or KDE backend, or "
                  "zenity.";
#endif
        return FolderPick::unavailable;
    }
    if (wait.path.empty())
        return FolderPick::cancelled;
    *chosen = path_from_utf8(wait.path);
    return FolderPick::chosen;
}

void tell_user(void* context, Notice kind, std::string_view text) {
    const std::string message(text);
    std::cerr << message << '\n';
    // Without video the box below fails, and that is reported.
    std::ignore = start_video(*static_cast<NativeDialogs*>(context));
    const SDL_MessageBoxFlags flags =
        kind == Notice::information ? SDL_MESSAGEBOX_INFORMATION : SDL_MESSAGEBOX_WARNING;
    if (!SDL_ShowSimpleMessageBox(flags, kMessageTitle, message.c_str(), nullptr))
        std::cerr << "open-annihilation: message box: " << SDL_GetError() << '\n';
}

// The dialogs, and where the demo's archive is unpacked.
struct NativeHost {
    NativeDialogs dialogs;
    // Empty when none is known; `data_folder_problem` then says why.
    fs::path data_folder;
    std::string data_folder_problem;
    // The mod folder and profile each candidate folder is inspected with.
    ModChoice mod;
};

FolderPick
pick_native_folder(void* context, const fs::path& start, fs::path* chosen, std::string* error) {
    return pick_folder(&static_cast<NativeHost*>(context)->dialogs, start, chosen, error);
}

void tell_native_user(void* context, Notice kind, std::string_view text) {
    tell_user(&static_cast<NativeHost*>(context)->dialogs, kind, text);
}

GameInstall inspect(void* context, const fs::path& folder) {
    const auto& host = *static_cast<NativeHost*>(context);
    auto install = inspect_game_install(folder, host.data_folder, demo_1997, host.mod);
    if (install.demo.outcome == DemoOutcome::unpack_failed && host.data_folder.empty())
        install.demo.problem += " (" + host.data_folder_problem + ")";
    return install;
}

[[nodiscard]] std::string_view text_or_empty(const char* text) {
    return text == nullptr ? std::string_view{} : std::string_view(text);
}

} // namespace

std::optional<GameDirectory> find_game_directory(const Options& options) {
    GameDirectoryRequest request;
    request.argument = options.game_dir;
    request.choose = options.choose_game_dir;
    request.archives_named = !options.archives.empty();
    request.unattended = options.unattended || unattended_environment(
                                                   text_or_empty(SDL_getenv("CI")),
                                                   text_or_empty(SDL_GetHint(SDL_HINT_VIDEO_DRIVER))
                                               );
    // An unattended run reads stored choices only from a preferences file it
    // was given, never from the player's own.
    oa::platform::preferences::Values values;
    if (!request.unattended || options.preferences_file)
        values = oa::platform::preferences::load(preference_file(options.preferences_file));
    if (request.argument.empty())
        request.stored = stored_game_directory(values);
    NativeHost native;
    native.mod.folder = chosen_mod_directory(options.mod_dir, options.base_game, values);
    // A mod folder chosen earlier that is gone is dropped with a notice, and
    // the game folder plays as it is; one named with --mod-dir must exist.
    std::error_code missing;
    if (options.mod_dir.empty() && !native.mod.folder.empty() &&
        !fs::is_directory(native.mod.folder, missing)) {
        const auto text = "The mod folder chosen earlier can no longer be found:\n\n" +
                          path_to_utf8(native.mod.folder) +
                          "\n\nThe game starts without it; choose a mod again in the "
                          "Open Annihilation settings.";
        if (request.unattended)
            std::cerr << "open-annihilation: " << text << '\n';
        else
            tell_user(&native.dialogs, Notice::information, text);
        native.mod.folder.clear();
    }
    native.mod.profile_file = options.mod_file;
    native.mod.accept_unimplemented_hacks = options.accept_unimplemented_hacks;
    native.mod.preferences = &values;
    if (options.data_dir) {
        native.data_folder = *options.data_dir;
    } else {
        try {
            native.data_folder = oa::platform::preferences::data_directory();
        } catch (const std::exception& error) {
            native.data_folder_problem = error.what();
        }
    }
    const GameDirectoryHost host{&native, pick_native_folder, tell_native_user, inspect};
    return resolve_game_directory(request, host);
}

} // namespace oa::app
