// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-game entry point: display setup, the Game files screen where the game
// folder is missing and the platform brings game files in, intro playback
// and runtime launch.
#include "oa/app/runtime.hpp"
#include "game_files_check.hpp"
#include "game_files_screen.hpp"
#include "render_host.hpp"
#include "screen_size.hpp"
#include "oa/app/extension_list.hpp"
#include "oa/app/full_screen.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/game_files_hooks.hpp"
#include "oa/app/game_files_import.hpp"
#include "oa/app/input_hints.hpp"
#include "oa/app/mod_profile_loader.hpp"
#include "oa/app/platform_hooks.hpp"
#include "oa/app/video_capture.hpp"
#include "oa/app/window_icon.hpp"
#include "oa/base/float_precision.hpp"
#include "oa/base/threads.hpp"
#include "oa/data/defs/layout.hpp"
#include "oa/media/intro_player.hpp"
#include "oa/platform/log_files.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/platform/system.hpp"
#include "oa/ui/engine_settings.hpp"
#include <SDL3/SDL.h>
#include <cinttypes>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <tuple>
#include <utility>
#include <SDL3/SDL_main.h>

#ifndef OA_ENGINE_VERSION
#error "OA_ENGINE_VERSION names the engine's version, which the renderer records are written under"
#endif
#ifndef OA_NATIVE_DENSITY_WINDOWS
#error "OA_NATIVE_DENSITY_WINDOWS (0 or 1) says whether the window opens at native density"
#endif
#ifndef OA_TOUCH_FIRST
#error "OA_TOUCH_FIRST (0 or 1) says whether the touch controls are on from the start"
#endif

namespace oa::app {
namespace {

// The platform the game is built for opens its windows at the display's own
// pixel density, where it would otherwise scale a lower-density window
// softly (the OA_NATIVE_DENSITY_WINDOWS build option; off on the desktop).
constexpr bool kNativeDensityWindows = OA_NATIVE_DENSITY_WINDOWS != 0;
// The touch controls are on from the start (the OA_TOUCH_FIRST build option;
// off on the desktop, where the first finger or --touch-controls switches
// them on and the runtime sets their input hints then).
constexpr bool kTouchFirst = OA_TOUCH_FIRST != 0;

// Process exit status after an out-of-memory report.
constexpr int kOutOfMemoryExitStatus = 3;

// The folder ErrorLog.txt goes in, found once at start-up so that the
// out-of-memory report does not have to look for it.
std::string error_log_folder;

/// Reports running out of memory and ends the process.
///
/// Appends the out-of-memory message to ErrorLog.txt beside the application
/// (error_log_folder), shows it in an "Open Annihilation" error box, then
/// exits at once with status 3 (kOutOfMemoryExitStatus).
[[noreturn]] void handle_out_of_memory() {
    // The process ends whatever happens: a log that cannot be written still
    // leaves the box, and a box that cannot be shown the log.
    std::ignore = oa::platform::append_error_log(
        error_log_folder.c_str(), oa::platform::out_of_memory_message
    );
    // The box needs the pointer, which full screen keeps on the window.
    release_pointer(SDL_GetGrabbedWindow());
    std::ignore = SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, "Open Annihilation", oa::platform::out_of_memory_message, nullptr
    );
    std::_Exit(kOutOfMemoryExitStatus);
}

/// Logs that the floating-point settings had changed and have been put back.
///
/// The settings decide how the simulation's arithmetic rounds, so the game
/// puts back any that changed while it ran (a graphics driver may change
/// them); this reports the first such change of the run.
///
/// @param found the settings found
/// @param saved the settings the game started with, now set again
void report_float_control_change(
    void*,
    const oa::base::float_precision::FloatControl& found,
    const oa::base::float_precision::FloatControl& saved
) {
    std::fprintf(
        stderr,
        "open-annihilation: the floating-point settings had changed and have been put back "
        "(found %" PRIx32 " %" PRIx32 " %" PRIx64 ", started with %" PRIx32 " %" PRIx32 " %" PRIx64
        ")\n",
        found.older_unit,
        found.vector_unit,
        found.arm_unit,
        saved.older_unit,
        saved.vector_unit,
        saved.arm_unit
    );
}

/// Gives the window the game's icon (window_icon.hpp).
///
/// A failure to decode the icon is reported on stderr, and a video driver
/// that has no window icons, such as the dummy one, refuses it silently;
/// either way the game starts without it.
///
/// @param window the game's window
void set_window_icon(SDL_Window* window) {
    WindowIcon icon;
    std::string error;
    if (!decode_window_icon(window_icon_png(), icon, error)) {
        std::cerr << "open-annihilation: window icon: " << error << '\n';
        return;
    }
    SDL_Surface* surface = SDL_CreateSurfaceFrom(
        static_cast<int>(icon.width),
        static_cast<int>(icon.height),
        SDL_PIXELFORMAT_RGBA32,
        icon.pixels.data(),
        static_cast<int>(icon.width * window_icon_pixel_bytes)
    );
    if (surface == nullptr) {
        std::cerr << "open-annihilation: window icon: " << SDL_GetError() << '\n';
        return;
    }
    std::ignore = SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
}

/// Refuses every render driver but SDL's software renderer, for
/// --render-fault create.
///
/// @param driver SDL's name for the render driver
/// @return true for every driver but software
bool refuse_all_but_software(void*, std::string_view driver) {
    return driver != oa::platform::render_probe::software_renderer;
}

/// Returns what --check-renderer-ladder forces of the renderer from the
/// start: with --render-fault create, every render driver but SDL's
/// software renderer refuses; otherwise nothing.
///
/// @param options the parsed command line
/// @return the faults; empty in a player's run
RenderFaultHooks start_faults(const Options& options) {
    RenderFaultHooks faults;
    if (options.check_renderer_ladder && options.render_fault &&
        options.render_fault->point == RenderFaultPoint::create)
        faults.refuse_driver = refuse_all_but_software;
    return faults;
}

/// Returns where the start keeps the renderer records: beside the player's
/// own preferences file, or in memory for the run with a named
/// --preferences-file, or where the player's folder cannot be found.
///
/// @param options the parsed command line
/// @return the place
RecordsPlace records_place(const Options& options) {
    RecordsPlace place;
    place.engine_version = OA_ENGINE_VERSION;
    if (options.preferences_file)
        return place;
    try {
        place.folder = preference_file(std::nullopt).parent_path();
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: the renderer records are kept in memory for this run: "
                  << error.what() << '\n';
    }
    return place;
}

struct HostDisplay {
    SDL_Window* window = nullptr;
    // The window's renderer, made by walking SDL's render drivers, and what
    // the probe found of it.
    RendererHost renderer_host{};
    bool active = false;
    /// initialize has run: the window opens once, early for the Game files
    /// screen or where the game opens it.
    bool initialized = false;
    // The mode Alt+Enter last asked for while the intro movies play.
    FullScreenSwitch full_screen{};

    /// Starts SDL's video and sound and opens the window, at the size
    /// --resolution gives when it is given, else at the Screen size setting's
    /// (start_settings, starting_screen_size), at the display's own pixel
    /// density only where decide_window_density allows it, with the
    /// renderer records read first (records_place,
    /// RendererHost::open_records), and its renderer
    /// (RendererHost::create), which it describes and logs with the tier its
    /// first frame is drawn in, from the flags and the Hardware acceleration
    /// setting read before the window opens (RendererHost::decide_start_tier).
    /// A window of a set screen size takes the display mode nearest it in
    /// full screen. A build whose touch controls are on from the start sets
    /// their input hints before SDL starts (set_input_hints), and the
    /// platform's window_ready hook, when there is one, is told once the
    /// window and its renderer are made.
    ///
    /// Throws std::runtime_error when SDL, the window or the renderer fails.
    /// It runs at most once (initialized).
    ///
    /// @param options the parsed command line
    void initialize(const Options& options) {
        initialized = true;
        if (!SDL_SetHint(SDL_HINT_MOUSE_FOCUS_CLICKTHROUGH, "1"))
            throw std::runtime_error("SDL mouse focus click-through hint was rejected");
        // Closing the window reaches the game as a close request, which a
        // running match answers with its surrender confirmation, rather than
        // as a quit SDL adds on its own.
        if (!SDL_SetHint(SDL_HINT_QUIT_ON_LAST_WINDOW_CLOSE, "0"))
            throw std::runtime_error("SDL last-window quit hint was rejected");
        // A capture takes the game's sound for itself before SDL starts it.
        if (!options.capture_video.empty())
            prepare_capture_audio(options.capture_video);
        // Touch controls on from the start read fingers and the pen with
        // their own hints from the first event; a desktop build keeps SDL's.
        if constexpr (kTouchFirst)
            set_input_hints();
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
        active = true;
        // The settings the window and its renderer start with, read before
        // either exists.
        const auto start = start_settings(options, desktop_size());
        const auto screen = starting_screen_size(options, start);
        const bool sized = screen != oa::ui::engine_settings::desktop_screen_size;
        // The renderer records, read before the window opens so that their
        // native-density key reaches the window's density, and kept for the
        // walk of the render drivers.
        const RecordsPlace place = records_place(options);
        renderer_host.open_records(place);
        // The window's pixel density is fixed once it opens: the display's
        // own only where the rule allows it (decide_window_density).
        DensityRequest density;
        density.flag = options.hardware_acceleration;
        density.platform_native = kNativeDensityWindows;
        density.asked = options.native_density;
        density.setting = start.hardware_acceleration;
        density.unattended = options.unattended;
        density.capture = !options.capture_video.empty();
        // The driver the native-density record names under this engine's
        // version. The game writes no scale-level key, so no rung is
        // remembered for it.
        if (const auto driver = renderer_state::native_density_driver(
                renderer_host.records().records(), place.engine_version
            ))
            density.record_driver = std::string(*driver);
        window = SDL_CreateWindow(
            "Open Annihilation",
            options.window_resolution ? options.match_width
            : sized                   ? screen.width
                                      : kDefaultWindowWidth,
            options.window_resolution ? options.match_height
            : sized                   ? screen.height
                                      : kDefaultWindowHeight,
            game_window_flags(
                options.start_full_screen && !sized, decide_window_density(density).native
            )
        );
        if (window == nullptr)
            throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
        if (sized)
            take_screen_size(window, screen, options.start_full_screen);
        set_window_icon(window);
        renderer_host.create(window, start_faults(options));
        TierRequest request;
        request.flag = options.hardware_acceleration;
        request.force_capable = options.force_capable;
        request.players_own_profile = !options.preferences_file.has_value();
        request.setting = start.hardware_acceleration;
        renderer_host.decide_start_tier(request);
        // The platform finishes the window once it shows the renderer's
        // view; the desktop has no such hook.
        if (const PlatformHooks& hooks = platform_hooks(); hooks.window_ready != nullptr)
            hooks.window_ready(hooks.context, window);
    }

    /// Ends the run's renderer records cleanly, deleting the sentinel, and
    /// closes the window and SDL. It runs at every exit through main, the
    /// fatal error's among them, as the error unwinds.
    ~HostDisplay() {
        release_pointer(window);
        renderer_host.finish_records();
        renderer_host.destroy();
        if (window != nullptr)
            SDL_DestroyWindow(window);
        window = nullptr;
        if (active)
            SDL_Quit();
    }
};

void play_intro_file(
    const Options& options, const fs::path& path, bool snapshot, HostDisplay* host
) {
    if (!fs::exists(path)) {
        std::cerr << "intro missing: " << path << '\n';
        return;
    }
    auto opened = oa::media::IntroPlayer::open(path);
    if (!opened) {
        std::cerr << "intro skip " << path.filename().string() << ": " << opened.error << '\n';
        return;
    }
    oa::media::PlaybackOptions playback;
    playback.headless_check = options.headless_check;
    playback.frame_limit = options.frame_limit.value_or(0);
    playback.play_audio = !options.headless_check && !options.mute;
    if (host != nullptr) {
        playback.window = host->window;
        playback.renderer = host->renderer_host.renderer();
        // Alt+Enter switches full screen during the movies as it does in the
        // game.
        playback.hooks.context = host;
        // The movie player does not hand its events on, so whether Alt+Enter
        // took one does not matter. A lost device is noted for after the
        // movie.
        playback.hooks.window_event = [](void* context, const SDL_Event& event) {
            auto& display = *static_cast<HostDisplay*>(context);
            if (display.renderer_host.take_event(event))
                return;
            std::ignore = take_full_screen_event(display.window, display.full_screen, event);
        };
    }
    if (snapshot)
        playback.snapshot_path = options.snapshot;
    auto result = opened.player->play(playback);
    // A device lost while the movie played is made again before what follows.
    if (host != nullptr)
        host->renderer_host.service();
    if (!result.ok()) {
        std::cerr << "intro " << path.filename().string() << ": " << result.error << '\n';
        return;
    }
    std::cout << "intro " << path.filename().string() << ": decoded " << result.decoded_frames
              << " frame(s)" << (result.skipped ? ", skipped\n" : "\n");
}

void play_intro(const Options& options, HostDisplay* host) {
    // The game's -c, -n and -y switches skip the movies too. This startup
    // path runs before the preferences load, so the PlayMovie preference
    // does not bring them back; in 3.1c the movies still play under those
    // switches while PlayMovie is set.
    if (options.skip_intro || options.launch.skip_intro != 0)
        return;
    // Frontend state 0 plays Data/1.zrb (the publisher's logo), then state 1
    // plays Data/2.zrb (game intro), then state 2 loads MAINMENU.GUI. A mod
    // folder's movie replaces the game folder's.
    const oa::AssetStore folders(options.game_folders);
    const auto movie = [&](std::string_view name) {
        const auto found = folders.loose_file(std::string("Data/") + std::string(name));
        return found ? *found : options.game_dir / "Data" / std::string(name);
    };
    play_intro_file(options, movie("1.zrb"), false, host);
    play_intro_file(options, movie("2.zrb"), true, host);
}

/// Writes each game file and listing a run looks up to a file, one a line,
/// from whichever thread looks it up (--trace-lookups).
class LookupLog {
  public:

    /// Opens the log.
    ///
    /// @param file the log file, replaced
    explicit LookupLog(const fs::path& file) : out_(file, std::ios::binary | std::ios::trunc) {
        if (!out_)
            throw std::runtime_error("cannot write the lookup log " + path_to_utf8(file));
    }

    /// The observer that writes to this log.
    ///
    /// @return the observer
    oa::LookupObserver observer() {
        return {this, [](void* context, std::string_view name) {
                    auto& self = *static_cast<LookupLog*>(context);
                    const oa::base::threads::LockGuard guard(self.lock_);
                    self.out_ << name << '\n';
                }};
    }

  private:

    oa::base::threads::Mutex lock_;
    std::ofstream out_;
};

// Sends the game's standard output and standard error to the logs folder in
// the per-user folder. Without a per-user folder, or when the log cannot be
// opened, they stay where they were, and standard error says why.
void start_log() {
    try {
        const auto folder = oa::platform::preferences::data_directory() / "logs";
        if (!oa::platform::log_files::begin(folder))
            std::cerr << "open-annihilation: cannot open a log in " << folder.string()
                      << "; the output stays here\n";
    } catch (const std::exception& error) {
        std::cerr << "open-annihilation: no log: " << error.what() << '\n';
    }
}

/// What the start needs where the platform brings game files in: the
/// import's folders, the backups setting, what the start's recovery found
/// and whether the Game files screen may open.
struct GameFilesStart {
    /// The platform's hooks are installed, and the game folder and the data
    /// folder are known: what an import left is taken up at the start and
    /// the backups setting is applied after resolution.
    bool installed{};
    /// The screen may be offered as well: the player did not ask for the
    /// notice instead (--no-game-files-screen).
    bool offered{};
    game_files::ImportPaths paths{};            ///< the game folder, staging and state
    oa::platform::preferences::Values values{}; ///< the preferences, for the backups and the mod
    bool backed_up{};                           ///< the game files are kept in device backups
    game_files::RecoveryResult recovery{};      ///< what the start's recovery found
};

/// Prepares the start where the platform brings game files in: the
/// import's folders, the backups setting from the preferences, and the
/// recovery of an import a stop or a change for this start left (renames
/// only), with or without the screen (--no-game-files-screen). Without the
/// hooks nothing is done.
///
/// @param options the parsed command line
/// @return what the start needs; nothing installed without the hooks
GameFilesStart start_game_files(const Options& options) {
    GameFilesStart start;
    const GameFilesHooks& hooks = game_files_hooks();
    if (!game_files_import_offered(hooks))
        return start;
    std::string game_folder;
    if (!hooks.game_folder(hooks.context, &game_folder) || game_folder.empty())
        return start;
    fs::path data_folder;
    if (options.data_dir) {
        data_folder = *options.data_dir;
    } else {
        try {
            data_folder = oa::platform::preferences::data_directory();
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: the game files cannot be brought in: " << error.what()
                      << '\n';
            return start;
        }
    }
    start.installed = true;
    start.offered = !options.no_game_files_screen;
    start.paths = game_files::import_paths(path_from_utf8(game_folder), data_folder);
    // An unattended run reads only a preferences file it was given.
    if (!options.unattended || options.preferences_file)
        start.values = oa::platform::preferences::load(preference_file(options.preferences_file));
    start.backed_up =
        oa::ui::engine_settings::read_settings(start.values, {}, false).game_files_backed_up;
    start.recovery = game_files::recover_import(hooks, start.paths, start.backed_up);
    return start;
}

/// Runs the Game files screen until the game folder resolves: the window
/// opens first, once, and stays for the game. Each PLAY resolves the folder
/// again; a refused folder opens the screen again with the reason.
///
/// @param options the parsed command line
/// @param display the game's display, initialised here when it is not yet
/// @param start the import's folders, the preferences and the recovery
/// @param[in,out] needed why there is no folder; resolution fills it again
/// @param[out] game_directory the folder, once it resolves
/// @return true to go on with the start; false when the screen was closed
bool run_game_files_until_resolved(
    const Options& options,
    HostDisplay& display,
    GameFilesStart& start,
    GameFilesNeeded& needed,
    std::optional<GameDirectory>& game_directory
) {
    while (!game_directory && needed.needed) {
        if (!display.initialized)
            display.initialize(options);
        GameFilesScreenRequest request;
        request.window = display.window;
        request.renderer = display.renderer_host.renderer();
        request.host = &display.renderer_host;
        request.entry = GameFilesEntry::first_run;
        request.paths = start.paths;
        request.recovery = start.recovery;
        request.needed = needed;
        request.mod.folder = chosen_mod_directory(options.mod_dir, options.base_game, start.values);
        request.mod.profile_file = options.mod_file;
        request.mod.accept_unimplemented_hacks = options.accept_unimplemented_hacks;
        request.mod.preferences = &start.values;
        request.version = std::string("v") + OA_ENGINE_VERSION;
        request.preferences_file = options.preferences_file;
        request.players_own_profile = !options.preferences_file.has_value();
        if (options.check_game_files)
            request.check = game_files_check_hooks(options);
        if (run_game_files_screen(request) == GameFilesEnd::quit)
            return false;
        // The continue banner shows once.
        start.recovery = {};
        game_directory = find_game_directory(options, &needed);
    }
    return true;
}

// A fatal error goes to the log, and to the terminal the game was started
// from; a game started from the desktop has no terminal, so it shows in an
// error box instead.
void report_fatal(const std::string& message) {
    const auto line = "open-annihilation: " + message + "\n";
    if (!oa::platform::log_files::current_file().empty())
        std::fputs(line.c_str(), stderr);
    // With neither a terminal nor a box, the log holds the message.
    if (!oa::platform::log_files::write_to_terminal(line))
        std::ignore = SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Open Annihilation", message.c_str(), nullptr
        );
}

} // namespace
} // namespace oa::app

using namespace oa::app;

int main(int argc, char** argv) {
    error_log_folder = oa::platform::error_log_directory(SDL_GetBasePath());
    std::set_new_handler(handle_out_of_memory);
    oa::base::float_precision::program_float_control().hooks.changed = report_float_control_change;
    try {
        // Every registered extension fills its table before the command
        // line is parsed; the runtime gets the table that combines them.
        const ExtensionList extensions(registered_extensions());
        const Extension& extension = extensions.combined();
        auto options = parse_options(argc, argv, extension);
        // The Game files screen's check installs its scripted platform
        // before anything reads the hooks.
        if (options.check_game_files)
            install_game_files_check(options);
        // --print-profile prints the resolved mod profile and stops.
        if (options.print_profile)
            return print_mod_profile(
                options.mod_file,
                options.mod_dir.empty() ? options.game_dir : options.mod_dir,
                options.accept_unimplemented_hacks,
                std::cout,
                std::cerr
            );
        // A game started for play, from a terminal or the desktop, logs to the
        // logs folder. Checks, benchmarks and other scripted runs keep their
        // output where it goes, and so does a run whose output another
        // program captures, such as a test or a script.
        if (!options.headless_check && !options.unattended &&
            !oa::platform::log_files::output_captured())
            start_log();
        // Where the platform brings game files in, what a stopped import or
        // a change waiting for this start left is taken up before the
        // folder is looked for.
        GameFilesStart game_files = start_game_files(options);
        // The window opens where the game opens it, or early for the Game
        // files screen, and only once.
        HostDisplay display;
        // The game folder's profile, --mod's or the mod folder's or its own,
        // is resolved while the folder is inspected, before any archive is
        // mounted, so that one the engine cannot use stops the run with its
        // errors.
        GameFilesNeeded needed;
        auto game_directory = find_game_directory(options, game_files.offered ? &needed : nullptr);
        // Without a usable folder, the Game files screen brings one in.
        if (!run_game_files_until_resolved(options, display, game_files, needed, game_directory))
            return options.check_game_files ? finish_game_files_check(options, 0) : 0;
        if (!game_directory)
            return options.check_game_files ? finish_game_files_check(options, 1) : 1;
        // The game files are kept out of device backups unless the player
        // put them back in, with or without the screen.
        if (game_files.installed)
            game_files::apply_backup_setting(
                game_files_hooks(), game_files.paths, game_files.backed_up
            );
        // A folder that held the demo's installer is played from the folder
        // its archive was unpacked to, and remembered as chosen.
        options.game_dir = game_directory->installation;
        if (game_directory->source == GameDirectorySource::chosen)
            options.remember_game_dir = game_directory->path;
        if (!fs::is_directory(options.game_dir))
            throw std::runtime_error(
                "game directory does not exist: " + path_to_utf8(options.game_dir) +
                " (name it with --game-dir PATH)"
            );
        options.game_folders = game_directory->folders;
        if (options.game_folders.empty())
            options.game_folders = {options.game_dir};
        options.mod_profile = game_directory->profile;
        // --archive names the archives and skips the inspection; a --mod
        // profile still sets the layout the data is read by.
        if (!options.archives.empty() && !options.mod_file.empty()) {
            auto resolved = resolve_folder_profile(
                options.game_folders,
                {{}, options.mod_file, options.accept_unimplemented_hacks, nullptr}
            );
            if (!resolved.errors.empty()) {
                std::string message = "the mod profile cannot be used:";
                for (const auto& error : resolved.errors)
                    message += "\n  " + error;
                throw std::runtime_error(message);
            }
            options.mod_profile = std::move(resolved.profile);
            game_directory->profile_warnings = std::move(resolved.warnings);
        }
        if (options.mod_profile) {
            // Its limits size the game's tables from the start, and its
            // rules reach every match.
            report_mod_profile(*options.mod_profile, game_directory->profile_warnings, std::cout);
        }
        // Every later read of game data uses the profile's layout.
        oa::data::defs::use_data_layout(data_layout_of(options.mod_profile.get()));
        if (!options.headless_check && !display.initialized)
            display.initialize(options);
        // The Game files check's management route runs its pass over the
        // installed folder before the game starts.
        if (options.check_game_files && options.game_files_route == GameFilesRoute::manage &&
            !run_game_files_manage_check(options, display.window, display.renderer_host.renderer()))
            return finish_game_files_check(options, 1);
        play_intro(options, options.headless_check ? nullptr : &display);
        // The movies present on the game's renderer.
        oa::base::float_precision::restore_program_float_control();
        oa::AssetStore assets(options.game_folders);
        std::unique_ptr<LookupLog> lookup_log;
        if (!options.trace_lookups.empty()) {
            lookup_log = std::make_unique<LookupLog>(options.trace_lookups);
            assets.observe_lookups(lookup_log->observer());
        }
        auto archives = options.archives;
        if (archives.empty())
            archives = std::move(game_directory->archives);
        if (archives.empty())
            throw std::runtime_error("no game archives were selected");
        for (const auto& candidate : archives) {
            const auto archive = candidate.is_absolute() ? candidate : options.game_dir / candidate;
            assets.mount(archive);
        }
        if (game_directory->demo.outcome == DemoOutcome::ready)
            std::cout << "open-annihilation: " << describe_ready(game_directory->demo) << '\n';
        // A capture starts before the runtime, which starts the menu's
        // sound, so that its own sound device opens first and sets the mix.
        std::unique_ptr<VideoCapture> capture;
        if (!options.capture_video.empty()) {
            int width = 0;
            int height = 0;
            if (!SDL_GetWindowSizeInPixels(display.window, &width, &height))
                throw std::runtime_error(std::string("SDL window size: ") + SDL_GetError());
            capture = std::make_unique<VideoCapture>(options.capture_video, width, height);
        }
        // The Game files check reads its options again for its verdict once
        // the runtime, which takes them, has run.
        std::optional<Options> checked_options;
        if (options.check_game_files)
            checked_options = options;
        // The runtime holds hundreds of kilobytes of game state, so it lives
        // on the heap: the main thread's stack is 1 MiB on Windows.
        const auto runtime = std::make_unique<Runtime>(
            std::move(options),
            assets,
            extension,
            display.window,
            display.renderer_host.renderer(),
            &display.renderer_host
        );
        runtime->take_video_capture(std::move(capture));
        runtime->take_full_screen_switch(display.full_screen);
        const int status = runtime->run();
        return checked_options ? finish_game_files_check(*checked_options, status) : status;
    } catch (const std::exception& error) {
        report_fatal(error.what());
        return 1;
    }
}
