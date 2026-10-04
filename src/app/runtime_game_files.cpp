// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Settings › Game files: the section's rows (what is installed, its size and
// the free space, where the files are, the backups switch), and Manage…,
// which opens the Game files screen over the main menu on the game's own
// window. The section is listed only where the platform offers the import
// (game_files_hooks.hpp).
#include "engine_settings_state.hpp"
#include "game_files_screen.hpp"
#include "oa/app/game_files_hooks.hpp"
#include "oa/app/game_files_import.hpp"
#include "oa/app/runtime.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/game_files.hpp"

#include <SDL3/SDL.h>

#include <array>
#include <cstddef>
#include <exception>
#include <filesystem>
#include <initializer_list>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>

#ifndef OA_ENGINE_VERSION
#error "OA_ENGINE_VERSION names the engine's version, which the Game files screen's header shows"
#endif

namespace oa::app {

namespace {

namespace settings = oa::ui::engine_settings;
namespace screen = oa::ui::game_files;

/// The header's version text on the Game files screen.
constexpr const char* kVersionText = "v" OA_ENGINE_VERSION;

/// Returns the folders the import uses in this run: the platform's game
/// folder and the data folder (--data-dir, else the per-user data folder).
///
/// @param options the run's options
/// @return the folders; nothing where the platform brings no game files in,
///     or where either folder is not known
std::optional<game_files::ImportPaths> import_paths_of(const Options& options) {
    const GameFilesHooks& hooks = game_files_hooks();
    if (!game_files_import_offered(hooks))
        return std::nullopt;
    std::string game_folder;
    if (!hooks.game_folder(hooks.context, &game_folder) || game_folder.empty())
        return std::nullopt;
    fs::path data_folder;
    if (options.data_dir) {
        data_folder = *options.data_dir;
    } else {
        try {
            data_folder = oa::platform::preferences::data_directory();
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: the game files' data folder is not known: "
                      << error.what() << '\n';
            return std::nullopt;
        }
    }
    return game_files::import_paths(path_from_utf8(game_folder), data_folder);
}

/// Returns one of the platform's words, or the engine's neutral word where
/// the platform gives none (oa::ui::game_files::platform_word).
///
/// @param which the word
/// @return the word, UTF-8
std::string platform_text(GameFilesText which) {
    const GameFilesHooks& hooks = game_files_hooks();
    screen::Model model;
    const auto index = static_cast<std::size_t>(which);
    if (hooks.text != nullptr && index < model.words.size())
        if (const char* word = hooks.text(hooks.context, which); word != nullptr)
            model.words[index] = word;
    return screen::platform_word(model, static_cast<uint8_t>(which));
}

/// Writes the interface catalogue's text for a pattern with its fields
/// filled: each "{name}" in it replaced.
///
/// @param pattern the pattern, in English as the source writes it
/// @param fields each field's name and text
/// @return the text
std::string filled(
    std::string_view pattern, std::initializer_list<std::pair<std::string_view, std::string>> fields
) {
    std::string text(oa::data::languages::interface_text(pattern));
    for (const auto& [name, value] : fields) {
        const std::string field = "{" + std::string(name) + "}";
        for (std::size_t at = text.find(field); at != std::string::npos;
             at = text.find(field, at + value.size()))
            text.replace(at, field.size(), value);
    }
    return text;
}

/// Returns the parts present in an installed folder, by the screen's row
/// kinds, from which the summary is written.
///
/// @param installed what is installed
/// @return one flag for each oa::ui::game_files::PartKind
std::array<bool, 11> parts_present(const game_files::InstalledSummary& installed) {
    std::array<bool, 11> present{};
    const auto found = [&installed](game_files::Part part) {
        return installed.parts[static_cast<std::size_t>(part)].found;
    };
    const auto set = [&present](screen::PartKind kind, bool on) {
        present[static_cast<std::size_t>(kind)] = on;
    };
    set(screen::PartKind::game_archives, found(game_files::Part::game_archives));
    set(screen::PartKind::update_31c, found(game_files::Part::update_31c));
    set(screen::PartKind::core_contingency, found(game_files::Part::core_contingency));
    set(screen::PartKind::battle_tactics, found(game_files::Part::battle_tactics));
    set(screen::PartKind::extra, found(game_files::Part::extra));
    set(screen::PartKind::music, found(game_files::Part::music));
    set(screen::PartKind::movies, found(game_files::Part::movies));
    set(screen::PartKind::mod, !installed.mods.empty());
    set(screen::PartKind::demo, installed.demo || found(game_files::Part::demo));
    set(screen::PartKind::demo_data, installed.demo_data_bytes != 0);
    return present;
}

/// Returns the bytes free for the game files on the game folder's volume:
/// the platform's answer, else the file system's.
///
/// @param paths the import's folders
/// @return the bytes; nothing when neither says
std::optional<uint64_t> free_bytes(const game_files::ImportPaths& paths) {
    const GameFilesHooks& hooks = game_files_hooks();
    // The game folder may be gone; its parent is on the same volume.
    std::error_code error;
    const fs::path& folder =
        fs::is_directory(paths.game_folder, error) ? paths.game_folder : paths.documents;
    if (hooks.free_space != nullptr) {
        uint64_t bytes = 0;
        if (hooks.free_space(hooks.context, path_to_utf8(folder).c_str(), &bytes))
            return bytes;
    }
    const auto space = fs::space(folder, error);
    if (error)
        return std::nullopt;
    return static_cast<uint64_t>(space.available);
}

} // namespace

void Runtime::open_game_files_manage() {
    const auto paths = import_paths_of(options_);
    if (!paths || sdl_.window == nullptr || sdl_.renderer == nullptr)
        return;
    GameFilesScreenRequest request;
    request.window = sdl_.window;
    request.renderer = sdl_.renderer;
    request.entry = GameFilesEntry::manage;
    request.paths = *paths;
    // The check of the game folder plays the mod the game plays: the mod
    // folder comes first among the game's folders when one is layered.
    if (options_.game_folders.size() > 1)
        request.mod.folder = options_.game_folders.front();
    request.mod.profile_file = options_.mod_file;
    request.mod.accept_unimplemented_hacks = options_.accept_unimplemented_hacks;
    request.mod.preferences = &preference_values_;
    request.version = kVersionText;
    request.preferences_file = options_.preferences_file;
    request.players_own_profile = !options_.preferences_file.has_value();
    int width_before = 0;
    int height_before = 0;
    std::ignore = SDL_GetWindowSize(sdl_.window, &width_before, &height_before);
    // The screen runs its own loop on the game's window; the game waits
    // under it until it closes.
    const GameFilesEnd end = run_game_files_screen(request);
    // The screen took the window's events: a size it changed to while it
    // showed reaches the game as if it had just changed.
    int width = 0;
    int height = 0;
    if (SDL_GetWindowSize(sdl_.window, &width, &height) &&
        (width != width_before || height != height_before)) {
        SDL_Event resized{};
        resized.type = SDL_EVENT_WINDOW_RESIZED;
        resized.window.windowID = SDL_GetWindowID(sdl_.window);
        resized.window.data1 = width;
        resized.window.data2 = height;
        std::ignore = SDL_PushEvent(&resized);
        int pixel_width = 0;
        int pixel_height = 0;
        if (SDL_GetWindowSizeInPixels(sdl_.window, &pixel_width, &pixel_height)) {
            SDL_Event pixels = resized;
            pixels.type = SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED;
            pixels.window.data1 = pixel_width;
            pixels.window.data2 = pixel_height;
            std::ignore = SDL_PushEvent(&pixels);
        }
        SDL_Event safe_area = resized;
        safe_area.type = SDL_EVENT_WINDOW_SAFE_AREA_CHANGED;
        std::ignore = SDL_PushEvent(&safe_area);
    }
    // The section shows what the screen changed.
    if (auto* dialog = engine_settings_dialog())
        fill_game_files_rows(*dialog);
    // A quit the screen took is the game's to answer.
    if (end == GameFilesEnd::quit) {
        SDL_Event quit{};
        quit.type = SDL_EVENT_QUIT;
        std::ignore = SDL_PushEvent(&quit);
    }
}

void Runtime::fill_game_files_rows(oa::ui::engine_settings::Dialog& dialog) {
    if (!dialog.game_files)
        return;
    const auto paths = import_paths_of(options_);
    if (!paths)
        return;
    const auto installed = game_files::summarize_installed(game_files_hooks(), *paths);
    const std::string device = platform_text(GameFilesText::device);
    dialog.game_files_summary = screen::summary_text(
        parts_present(installed), static_cast<uint32_t>(installed.mods.size()), installed.demo
    );
    const std::string used = screen::size_text(installed.bytes);
    if (const auto free = free_bytes(*paths))
        dialog.game_files_sizes = filled(
            "{used} · {free} free on this {device}",
            {{"used", used}, {"free", screen::size_text(*free)}, {"device", device}}
        );
    else
        dialog.game_files_sizes =
            filled("{used} on this {device}", {{"used", used}, {"device", device}});
    dialog.game_files_location = platform_text(GameFilesText::folder_location);
    dialog.game_files_device = device;
}

void Runtime::EngineSettingsState::apply_game_files_backups(Runtime& runtime, bool backed_up) {
    if (const auto paths = import_paths_of(runtime.options_))
        game_files::apply_backup_setting(game_files_hooks(), *paths, backed_up);
}

} // namespace oa::app
