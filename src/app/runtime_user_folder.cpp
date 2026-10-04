// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The player's own folder: chosen at start, the saved games moved into it
// once, the paths the game names placed in it, its folders shown in the
// system's file manager, and the main menu's notice of the move, drawn over
// the darkened main menu in the settings dialog's look; the mod's warning
// (runtime_mod_warning.cpp) is drawn and driven as the same notice.

#include "engine_settings_state.hpp"
#include "user_folder_state.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/ui/engine_settings/dialog.hpp"
#include "oa/ui/engine_settings/notice.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"
#include "oa/ui/frontend_state/app_modes.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <exception>
#include <iostream>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace artless = oa::ui::frontend_renderer;

namespace {

/// The notice overlay's z: over the settings dialog's overlay, which never
/// shows with it, and the extensions' overlays.
constexpr int16_t kNoticeOverlayZ = 101;
/// Frames in a row the main menu shows before the notice: a start that
/// passes the main menu at its first update shows none.
constexpr uint32_t kNoticeMenuFrames = 2;
/// The sound OK plays as it closes the notice.
constexpr std::string_view kCloseSound = "Options";
/// The name of the folder of the player's own folder that holds
/// screenshots, as 3.1c names it below the Image Output Directory.
constexpr std::string_view kGameScreenshotsName = "SCREENSHOTS";
/// What the name of a film's folder below the Image Output Directory starts
/// with, a number after it.
constexpr std::string_view kGameFilmPrefix = "MOVIE";

/// Returns a text with its ASCII letters raised.
///
/// @param text the text
/// @return the text in capitals
std::string capitals(std::string_view text) {
    std::string raised(text);
    for (auto& character : raised)
        character = static_cast<char>(std::toupper(static_cast<unsigned char>(character)));
    return raised;
}

/// Tells whether a folder's name is a film's: MOVIE, matched without case,
/// then digits, or the '*' of a search for them.
///
/// @param name the name
/// @return true for a film's folder
bool film_folder_name(std::string_view name) {
    const std::string raised = capitals(name);
    if (!raised.starts_with(kGameFilmPrefix))
        return false;
    const std::string_view rest = std::string_view(raised).substr(kGameFilmPrefix.size());
    return std::all_of(rest.begin(), rest.end(), [](char character) {
        return (character >= '0' && character <= '9') || character == '*';
    });
}

/// Tells whether the run is one nobody watches: unattended, on CI, or on a
/// video driver that shows no window.
///
/// @param unattended the run is unattended (Options::unattended)
/// @return true when nobody watches
bool unwatched_run(bool unattended) {
    const char* driver = SDL_GetCurrentVideoDriver();
    if (driver == nullptr)
        driver = SDL_GetHint(SDL_HINT_VIDEO_DRIVER);
    const char* ci = SDL_getenv("CI");
    return unattended || unattended_environment(
                             ci == nullptr ? std::string_view{} : std::string_view(ci),
                             driver == nullptr ? std::string_view{} : std::string_view(driver)
                         );
}

/// Returns the source pixel an input's pointer is over: on the main menu
/// the pointer is in the picture's pixels.
///
/// @param input the input
/// @param[out] x the pixel's column
/// @param[out] y the pixel's row
void pointer_pixel(const ScreenInput& input, int32_t& x, int32_t& y) {
    x = static_cast<int32_t>(std::floor(input.x));
    y = static_cast<int32_t>(std::floor(input.y));
}

} // namespace

void Runtime::start_user_folder() {
    namespace platform_preferences = oa::platform::preferences;
    // The folder: --user-folder, the preferences' key, else beside a named
    // preferences file or in the Documents folder.
    std::error_code error;
    const fs::path preference_folder =
        fs::absolute(preference_path_, error).lexically_normal().parent_path();
    fs::path fallback;
    if (options_.preferences_file) {
        fallback = user_folder_beside(*options_.preferences_file);
    } else {
        try {
            fallback = platform_preferences::default_user_folder();
        } catch (const std::exception& failure) {
            fallback = preference_folder / std::string(platform_preferences::user_folder_name);
            std::cerr << "open-annihilation: no Documents folder (" << failure.what()
                      << "); saved games, screenshots, films and mods go in "
                      << path_to_utf8(fallback) << '\n';
        }
    }
    user_folder_ = choose_user_folder(options_.user_folder, preference_values_, fallback);
    // Only the player's own preferences file had the saved games the game
    // kept beside it; a named file's folder may hold anything, which stays.
    // They move only into the folder every start uses: a folder named for
    // one start would take them from every later one, and the dialogs list
    // them where they are instead.
    const bool one_start_folder = options_.user_folder && !options_.user_folder->empty();
    if (!options_.preferences_file && !one_start_folder)
        move_saves_once();
}

void Runtime::move_saves_once() {
    // The saved games move once; a recorded move is not made again.
    if (preference_values_.contains(std::string(saves_moved_preference)))
        return;
    std::error_code error;
    const fs::path preference_folder =
        fs::absolute(preference_path_, error).lexically_normal().parent_path();
    if (error)
        return;
    const SavesMove move = move_earlier_saves(preference_folder, user_folder_);
    for (const auto& line : move.lines)
        std::cerr << "open-annihilation: " << line << '\n';
    if (move.moved == 0 && move.left == 0 && move.other_files == 0)
        return;
    std::cerr << "open-annihilation: " << move.moved
              << (move.moved == 1 ? " saved game" : " saved games") << " moved to "
              << path_to_utf8(oa::app::saves_folder(user_folder_, {}))
              << (move.left != 0 ? ", " : "")
              << (move.left != 0 ? std::to_string(move.left) + " left where they were" : "")
              << '\n';
    record_saves_move(preference_values_, move);
    preferences_dirty_ = true;
    try {
        flush_preferences();
    } catch (const std::exception& failure) {
        std::cerr << "open-annihilation: the move of the saved games is not recorded: "
                  << failure.what() << '\n';
    }
}

const fs::path& Runtime::user_folder() const noexcept {
    return user_folder_;
}

fs::path Runtime::saves_folder() const {
    return oa::app::saves_folder(
        user_folder_, options_.mod_profile ? options_.mod_profile->id : std::string_view{}
    );
}

ui::frontend::SaveRoots Runtime::save_roots() const {
    ui::frontend::SaveRoots roots{save_game_root(), saves_folder(), {}};
    // The folder that held saved games before, while it is there: what
    // could not move, or what an earlier version saved since.
    std::error_code absolute_error;
    const fs::path root = fs::absolute(roots.root, absolute_error).lexically_normal();
    if (const auto earlier =
            entry_without_case(absolute_error ? roots.root : root, earlier_saves_folder_name)) {
        std::error_code error;
        if (fs::is_directory(*earlier, error))
            roots.earlier = *earlier;
    }
    return roots;
}

fs::path Runtime::game_file_path(std::string_view path, ui::frontend::SavePathUse use) const {
    // Only a saved game read looks for the earlier folder.
    const ui::frontend::SaveRoots roots =
        use == ui::frontend::SavePathUse::read
            ? save_roots()
            : ui::frontend::SaveRoots{save_game_root(), saves_folder(), {}};
    const fs::path host = ui::frontend::savegame_host_path(roots, path, use);
    if (user_folder_.empty())
        return host;
    // Within the player's own folder, which stands for the Image Output
    // Directory by default, its screenshots folder is Screenshots and each
    // MOVIE folder lies in Films.
    const fs::path relative = host.lexically_normal().lexically_relative(user_folder_);
    if (relative.empty())
        return host;
    auto part = relative.begin();
    const std::string first = path_to_utf8(*part);
    if (first == "." || first == "..")
        return host;
    fs::path placed;
    if (capitals(first) == kGameScreenshotsName)
        placed = user_folder_ / std::string(screenshots_folder_name);
    else if (film_folder_name(first))
        placed = user_folder_ / std::string(films_folder_name) / path_from_utf8(first);
    else
        return host;
    for (++part; part != relative.end(); ++part)
        placed /= *part;
    return placed;
}

std::string Runtime::own_image_output_directory() {
    return user_folder_.empty() ? std::string() : path_to_utf8(user_folder_);
}

void Runtime::destroy_user_folder_state(UserFolderState* state) noexcept {
    delete state;
}

Runtime::UserFolderState& Runtime::user_folder_state() {
    if (!user_folder_state_) {
        user_folder_state_.reset(new UserFolderState{});
        auto& state = *user_folder_state_;
        state.opener = unwatched_run(options_.unattended) ? recorded_folder_opener(state.opened)
                                                          : system_folder_opener();
    }
    return *user_folder_state_;
}

FolderOpening Runtime::open_player_folder(const fs::path& folder) {
    const FolderOpening opening = open_folder(user_folder_state().opener, folder);
    if (!opening.detail.empty())
        std::cerr << "open-annihilation: " << opening.detail << '\n';
    if (!opening.opened)
        std::cerr << "open-annihilation: cannot show " << path_to_utf8(folder) << ": "
                  << opening.reason << '\n';
    return opening;
}

bool Runtime::UserFolderState::unwatched(bool unattended) {
    return unwatched_run(unattended);
}

artless::Placement Runtime::UserFolderState::notice_placement(int32_t height) {
    return {(kCanvasWidth - settings::notice_width) / 2, (kCanvasHeight - height) / 2, 1};
}

int Runtime::UserFolderState::notice_event(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!runtime.user_folder_state_)
        return 0;
    auto& state = *runtime.user_folder_state_;
    const auto& input = *context->input;
    if (state.latched_key != 0 && input.key == state.latched_key) {
        if (input.kind == ScreenInputKind::key_up)
            state.latched_key = 0;
        if (input.kind == ScreenInputKind::key_down || input.kind == ScreenInputKind::key_up)
            return 1;
    }
    if (!state.notice || runtime.screen_ != state.notice_screen)
        return 0;
    auto& notice = *state.notice;
    const auto* fonts = runtime.engine_settings_fonts();
    const int32_t height = settings::notice_height(notice, fonts);
    const auto placement = notice_placement(height);
    int32_t x = 0;
    int32_t y = 0;
    pointer_pixel(input, x, y);
    x -= placement.x;
    y -= placement.y;
    auto action = settings::NoticeAction::none;
    uint32_t key_down = 0;
    switch (input.kind) {
    case ScreenInputKind::pointer_move:
        action = settings::notice_pointer_move(notice, x, y, height);
        break;
    case ScreenInputKind::pointer_down:
        // A finger's press takes the nearer button within reach.
        if (input.button == SDL_BUTTON_LEFT)
            action = runtime.engine_settings_state().finger_pointer
                         ? settings::notice_finger_down(
                               notice, x, y, height, EngineSettingsState::finger_reach(runtime, 1.0)
                           )
                         : settings::notice_pointer_down(notice, x, y, height);
        break;
    case ScreenInputKind::pointer_up:
        if (input.button == SDL_BUTTON_LEFT)
            action = settings::notice_pointer_up(notice, x, y, height);
        break;
    case ScreenInputKind::key_down:
        if (const auto key = engine_settings_dialog_key(input.key, input.modifiers)) {
            action = settings::notice_key(notice, *key);
            key_down = input.key;
        }
        break;
    default:
        break;
    }
    if (action == settings::NoticeAction::open_folder) {
        const FolderOpening opening = runtime.open_player_folder(state.notice_folder);
        notice.failure = opening.opened ? std::string() : opening.reason;
    } else if (action == settings::NoticeAction::closed) {
        runtime.play_ui_sound(kCloseSound, 0);
        state.notice.reset();
        state.latched_key = key_down;
    }
    // The notice is modal: nothing under it sees any input while it shows.
    return 1;
}

void Runtime::UserFolderState::notice_tick(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (runtime.user_folder_state_ && runtime.user_folder_state_->notice &&
        runtime.screen_ != runtime.user_folder_state_->notice_screen)
        runtime.user_folder_state_->notice.reset();
}

void Runtime::UserFolderState::notice_draw(ScreenContext* context, void*) {
    auto& runtime = *static_cast<Runtime*>(context->host);
    if (!runtime.user_folder_state_ || !runtime.user_folder_state_->notice ||
        runtime.screen_ != runtime.user_folder_state_->notice_screen || context->surface == nullptr)
        return;
    const auto* fonts = runtime.engine_settings_fonts();
    if (fonts == nullptr)
        return;
    const auto& notice = *runtime.user_folder_state_->notice;
    auto& frame = *context->surface;
    artless::blend_source_rect(
        frame,
        {0, 0, 1},
        {0, 0, static_cast<int32_t>(frame.width), static_cast<int32_t>(frame.height)},
        settings::backdrop_color,
        settings::menu_backdrop_opacity
    );
    settings::draw_notice(
        frame,
        notice_placement(settings::notice_height(notice, fonts)),
        notice,
        *fonts,
        runtime.engine_settings_icon()
    );
}

bool Runtime::saves_notice_shown() const noexcept {
    return user_folder_state_ && user_folder_state_->notice;
}

void Runtime::register_saves_notice_overlay() {
    // On every screen, so that its tick closes the notice when another
    // screen replaces the one it shows over; it takes input and draws on
    // that screen only.
    OverlayDesc notice{};
    notice.name = "saves_moved_notice";
    notice.screen = kScreenAny;
    notice.z = kNoticeOverlayZ;
    notice.event = UserFolderState::notice_event;
    notice.tick = UserFolderState::notice_tick;
    notice.draw = UserFolderState::notice_draw;
    overlay_register(&screens_, &notice);
}

void Runtime::tell_saves_moved() {
    // Most frames have nothing to tell, and cost no more than this.
    if (!saves_notice_due_in(preference_values_))
        return;
    auto& state = user_folder_state();
    if (state.notice)
        return;
    namespace frontend_state = oa::ui::frontend_state;
    const bool settled = screen_ == Screen::main_menu && !frame_owned_by_package() &&
                         state_.state == frontend_state::state_id::main_menu &&
                         state_.pending_signal != frontend_state::signal_id::multiplayer;
    if (!settled) {
        state.main_menu_frames = 0;
        return;
    }
    if (++state.main_menu_frames < kNoticeMenuFrames ||
        oa::ui::frontend_dialogs::dialog_count() != 0 || engine_settings_dialog() != nullptr ||
        engine_settings_fonts() == nullptr)
        return;
    // A run nobody watches leaves it due for one someone does.
    if (unwatched_run(options_.unattended) && !state.check_shows_notice)
        return;
    if (const auto move = recorded_saves_move(preference_values_)) {
        state.notice = saves_moved_notice(*move, oa::app::saves_folder(user_folder_, {}));
        state.notice_folder = oa::app::saves_folder(user_folder_, {});
        state.notice_screen = Screen::main_menu;
        ++state.notices_shown;
    }
    // Told from now on: no later start shows it again.
    preference_values_[std::string(saves_notice_preference)] = std::string(saves_notice_told);
    preferences_dirty_ = true;
    try {
        flush_preferences();
    } catch (const std::exception& failure) {
        std::cerr << "open-annihilation: the notice of the saved games' move is not recorded: "
                  << failure.what() << '\n';
    }
}

} // namespace oa::app
