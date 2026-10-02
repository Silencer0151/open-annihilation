// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-game entry point: display setup, intro playback and runtime launch.
#include "oa/app/runtime.hpp"
#include "screen_size.hpp"
#include "oa/app/extension_list.hpp"
#include "oa/app/full_screen.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/video_capture.hpp"
#include "oa/app/window_icon.hpp"
#include "oa/media/intro_player.hpp"
#include "oa/platform/log_files.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/platform/system.hpp"
#include <SDL3/SDL.h>
#include <cstdio>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <memory>
#include <new>
#include <stdexcept>
#include <string>
#include <utility>
#include <SDL3/SDL_main.h>

namespace oa::app {
namespace {

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
    (void)oa::platform::append_error_log(
        error_log_folder.c_str(), oa::platform::out_of_memory_message
    );
    // The box needs the pointer, which full screen keeps on the window.
    release_pointer(SDL_GetGrabbedWindow());
    (void)SDL_ShowSimpleMessageBox(
        SDL_MESSAGEBOX_ERROR, "Open Annihilation", oa::platform::out_of_memory_message, nullptr
    );
    std::_Exit(kOutOfMemoryExitStatus);
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
    (void)SDL_SetWindowIcon(window, surface);
    SDL_DestroySurface(surface);
}

struct HostDisplay {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    bool active = false;
    // The mode Alt+Enter last asked for while the intro movies play.
    FullScreenSwitch full_screen{};

    /// Starts SDL's video and sound and opens the window, at the size
    /// --resolution gives when it is given, else at the Screen size setting's
    /// (starting_screen_size), and its renderer. A window of a set screen
    /// size takes the display mode nearest it in full screen.
    ///
    /// Throws std::runtime_error when SDL, the window or the renderer fails.
    ///
    /// @param options the parsed command line
    void initialize(const Options& options) {
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
        if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_AUDIO))
            throw std::runtime_error(std::string("SDL_Init: ") + SDL_GetError());
        active = true;
        const auto screen = starting_screen_size(options, desktop_size());
        const bool sized = screen != oa::ui::engine_settings::desktop_screen_size;
        window = SDL_CreateWindow(
            "Open Annihilation",
            options.window_resolution ? options.match_width
            : sized                   ? screen.width
                                      : kDefaultWindowWidth,
            options.window_resolution ? options.match_height
            : sized                   ? screen.height
                                      : kDefaultWindowHeight,
            game_window_flags(options.start_full_screen && !sized)
        );
        if (window == nullptr)
            throw std::runtime_error(std::string("SDL_CreateWindow: ") + SDL_GetError());
        if (sized)
            take_screen_size(window, screen, options.start_full_screen);
        set_window_icon(window);
        renderer = SDL_CreateRenderer(window, nullptr);
        if (renderer == nullptr)
            throw std::runtime_error(std::string("SDL_CreateRenderer: ") + SDL_GetError());
    }

    ~HostDisplay() {
        release_pointer(window);
        if (renderer != nullptr)
            SDL_DestroyRenderer(renderer);
        if (window != nullptr)
            SDL_DestroyWindow(window);
        renderer = nullptr;
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
        playback.renderer = host->renderer;
        // Alt+Enter switches full screen during the movies as it does in the
        // game.
        playback.hooks.context = host;
        playback.hooks.window_event = [](void* context, const SDL_Event& event) {
            auto& display = *static_cast<HostDisplay*>(context);
            (void)take_full_screen_event(display.window, display.full_screen, event);
        };
    }
    if (snapshot)
        playback.snapshot_path = options.snapshot;
    auto result = opened.player->play(playback);
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
    // plays Data/2.zrb (game intro), then state 2 loads MAINMENU.GUI.
    play_intro_file(options, options.game_dir / "Data" / "1.zrb", false, host);
    play_intro_file(options, options.game_dir / "Data" / "2.zrb", true, host);
}

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

// A fatal error goes to the log, and to the terminal the game was started
// from; a game started from the desktop has no terminal, so it shows in an
// error box instead.
void report_fatal(const std::string& message) {
    const auto line = "open-annihilation: " + message + "\n";
    if (!oa::platform::log_files::current_file().empty())
        std::fputs(line.c_str(), stderr);
    if (!oa::platform::log_files::write_to_terminal(line))
        (void)SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_ERROR, "Open Annihilation", message.c_str(), nullptr
        );
}

} // namespace
} // namespace oa::app

using namespace oa::app;

int main(int argc, char** argv) {
    error_log_folder = oa::platform::error_log_directory(SDL_GetBasePath());
    std::set_new_handler(handle_out_of_memory);
    try {
        // Every registered extension fills its table before the command
        // line is parsed; the runtime gets the table that combines them.
        const ExtensionList extensions(registered_extensions());
        const Extension& extension = extensions.combined();
        auto options = parse_options(argc, argv, extension);
        // A game started for play, from a terminal or the desktop, logs to the
        // logs folder. Checks, benchmarks and other scripted runs keep their
        // output where it goes, and so does a run whose output another
        // program captures, such as a test or a script.
        if (!options.headless_check && !options.unattended &&
            !oa::platform::log_files::output_captured())
            start_log();
        auto game_directory = find_game_directory(options);
        if (!game_directory)
            return 1;
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
        HostDisplay display;
        if (!options.headless_check)
            display.initialize(options);
        play_intro(options, options.headless_check ? nullptr : &display);
        oa::AssetStore assets(options.game_dir);
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
        // The runtime holds hundreds of kilobytes of game state, so it lives
        // on the heap: the main thread's stack is 1 MiB on Windows.
        const auto runtime = std::make_unique<Runtime>(
            std::move(options), assets, extension, display.window, display.renderer
        );
        runtime->take_video_capture(std::move(capture));
        runtime->take_full_screen_switch(display.full_screen);
        return runtime->run();
    } catch (const std::exception& error) {
        report_fatal(error.what());
        return 1;
    }
}
