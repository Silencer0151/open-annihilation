// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings: read at start, put in effect, saved, and
// the dialog's session that both hosts share.

#include "engine_settings_state.hpp"
#include "oa/app/mod_install.hpp"
#include "oa/app/mod_install/prompts.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/game_files_hooks.hpp"
#include "oa/app/mod_profile_loader.hpp"
#include "oa/app/mod_summary.hpp"
#include "oa/app/user_folder.hpp"

#include "oa/app/acceleration_status.hpp"
#include "oa/app/runtime.hpp"
#include "oa/app/view_rules.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/mod_profile/overrides.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "screen_size.hpp"
#include "oa/platform/machine.hpp"
#include "oa/platform/render_probe.hpp"
#include "oa/sim/ground_orders/search_worker.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/ui/frontend/savegame_dialogs.hpp"
#include "oa/ui/frontend_multiplayer/screens.hpp"
#include "oa/ui/touch_hud.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>
#include <utility>
#include <vector>

#ifndef OA_ENGINE_VERSION
#error "OA_ENGINE_VERSION names the engine's version for the dialog's header"
#endif

namespace oa::app {

namespace settings = oa::ui::engine_settings;
namespace render_probe = oa::platform::render_probe;

static_assert(settings::base_path_search_nodes == oa::sim::ground_orders::search_tick_credit);
static_assert(settings::lowest_frame_rate == kLowestMaxFramesPerSecond);
static_assert(settings::highest_frame_rate == kDefaultMaxFramesPerSecond);
static_assert(settings::default_unit_limit == kSkirmishUnitsPerPlayer);
static_assert(
    static_cast<uint32_t>(settings::AntiAliasing::x16) ==
    oa::present::model::supersampling_factor(oa::present::model::UnitSupersampling::x16)
);

// The settings dialog keeps the mod folder the game starts with.
static_assert(mod_directory_preference == settings::key::mod_directory);

namespace {

/// Returns the size of the desktop of the display a window is on, as a
/// screen size keeps it.
///
/// @param options the parsed command line (Options::display_modes)
/// @param window the game's window; null for none
/// @param start_desktop the primary display's desktop as read at start
/// @return the desktop's size; start_desktop without a window, or where the
///     display does not report one a screen size can keep
settings::ScreenSize
window_desktop(const Options& options, SDL_Window* window, settings::ScreenSize start_desktop) {
    const SDL_DisplayID display = window != nullptr ? SDL_GetDisplayForWindow(window) : 0;
    if (display == 0)
        return start_desktop;
    // A screen size keeps each side in 16 bits.
    constexpr int32_t widest = std::numeric_limits<uint16_t>::max();
    const auto desktop = display_report(options, display).desktop.size;
    if (desktop.width <= 0 || desktop.height <= 0 || desktop.width > widest ||
        desktop.height > widest)
        return start_desktop;
    return {static_cast<uint16_t>(desktop.width), static_cast<uint16_t>(desktop.height)};
}

/// The version the dialog's header shows.
constexpr const char* kVersionText = "v" OA_ENGINE_VERSION;

/// What the player is told when the preferences file could not be written.
constexpr std::string_view kSaveFailedText = "Settings were not saved.";

/// The widest the main menu's message box about a failed save is, in source pixels.
constexpr int32_t kSaveFailedMessageWidth = 300;

/// The installation's options file, whose UnitLimit the unit limit defaults to.
constexpr std::string_view kInstallationIniName = "totala.ini";

/// SDL's names of the video drivers that draw no window.
constexpr std::array<std::string_view, 2> kWindowlessVideoDrivers{"dummy", "offscreen"};

/// Returns a text in lower case, ASCII letters only.
///
/// @param text the text
/// @return the text with A to Z lowered
std::string ascii_lower(std::string_view text) {
    std::string lowered(text);
    for (auto& character : lowered)
        character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
    return lowered;
}

} // namespace

settings::Inputs Runtime::EngineSettingsState::inputs(const Runtime& runtime) {
    settings::Inputs inputs{};
    inputs.players_own_profile = !runtime.options_.preferences_file.has_value();
#if defined(SDL_PLATFORM_MACOS)
    inputs.macos = true;
#endif
    if (runtime.engine_settings_) {
        inputs.installation_ini = runtime.engine_settings_->installation_ini;
        inputs.raspberry_pi = runtime.engine_settings_->raspberry_pi;
        inputs.light_machine = runtime.engine_settings_->light_machine;
        inputs.desktop = runtime.engine_settings_->desktop;
        inputs.steam_deck_panel_hz = runtime.engine_settings_->steam_deck_panel_hz;
    }
    inputs.units_per_player = runtime.limits_.units_per_player;
    inputs.native_density_windows = runtime.options_.native_density_windows;
    if (runtime.engine_settings_) {
        inputs.mod_folders = runtime.engine_settings_->mod_folders;
        inputs.profile_id = runtime.engine_settings_->profile_id;
    }
    return inputs;
}

std::string Runtime::EngineSettingsState::read_installation_ini(const fs::path& game_directory) {
    std::error_code error;
    for (fs::directory_iterator entry(game_directory, error), end; !error && entry != end;
         entry.increment(error)) {
        if (ascii_lower(path_to_utf8(entry->path().filename())) != kInstallationIniName ||
            !entry->is_regular_file(error))
            continue;
        std::ifstream input(entry->path(), std::ios::binary);
        std::string text(settings::installation_ini_limit, '\0');
        input.read(text.data(), static_cast<std::streamsize>(text.size()));
        text.resize(static_cast<std::size_t>(std::max<std::streamsize>(input.gcount(), 0)));
        return text;
    }
    return {};
}

bool Runtime::EngineSettingsState::live_switch_alt(const Runtime& runtime) {
    const uint16_t flags = runtime.match_ ? runtime.match_->state().game.graphics_flags
                                          : runtime.preferences_.graphics_flags;
    return (flags & init::preference_flags::switch_alt) != 0;
}

void Runtime::EngineSettingsState::take_live_settings(Runtime& runtime) {
    auto& current = runtime.engine_settings_state().current;
    current.switch_alt = live_switch_alt(runtime);
    current.frame_stats = runtime.frame_stats_shown_;
}

std::optional<std::string> Runtime::EngineSettingsState::flush(Runtime& runtime) {
    try {
        runtime.flush_preferences();
    } catch (const std::exception& error) {
        return std::string(error.what());
    }
    return std::nullopt;
}

std::vector<settings::ScreenSize>
Runtime::EngineSettingsState::offered_screen_sizes(const Runtime& runtime) {
    const int32_t minimum_height = view_rules::minimum_mode_height(runtime.ui_rules());
    if (runtime.sdl_.window == nullptr)
        return oa::app::offered_screen_sizes(
            runtime.options_, SDL_GetPrimaryDisplay(), start_use(runtime.options_), minimum_height
        );
    SDL_DisplayID display = SDL_GetDisplayForWindow(runtime.sdl_.window);
    if (display == 0)
        display = SDL_GetPrimaryDisplay();
    const bool full_screen = (SDL_GetWindowFlags(runtime.sdl_.window) & SDL_WINDOW_FULLSCREEN) != 0;
    const auto use = full_screen && run_full_screen_method() == FullScreenMethod::switch_mode
                         ? oa::platform::display_modes::Use::full_screen
                         : oa::platform::display_modes::Use::window;
    return oa::app::offered_screen_sizes(runtime.options_, display, use, minimum_height);
}

settings::ScreenSize Runtime::EngineSettingsState::screen_size_now(const Runtime& runtime) {
    // A screen size keeps each side in 16 bits.
    constexpr int widest = std::numeric_limits<uint16_t>::max();
    const SDL_Point now = runtime.screen_size_now();
    if (now.x <= 0 || now.y <= 0 || now.x > widest || now.y > widest)
        return settings::desktop_screen_size;
    return {static_cast<uint16_t>(now.x), static_cast<uint16_t>(now.y)};
}

settings::ScreenSize Runtime::EngineSettingsState::screen_size_after(
    const Runtime& runtime, settings::ScreenSize setting
) {
    if (setting != settings::desktop_screen_size)
        return setting;
    // Desktop leaves a window as it is, and shows full screen at the
    // desktop's own size.
    if (runtime.sdl_.window != nullptr &&
        (SDL_GetWindowFlags(runtime.sdl_.window) & SDL_WINDOW_FULLSCREEN) == 0)
        return screen_size_now(runtime);
    const settings::ScreenSize desktop = window_desktop(
        runtime.options_,
        runtime.sdl_.window,
        runtime.engine_settings_ ? runtime.engine_settings_->desktop : settings::desktop_screen_size
    );
    return desktop != settings::desktop_screen_size ? desktop : screen_size_now(runtime);
}

settings::ScreenSize Runtime::EngineSettingsState::screen_size_in_effect(
    const Runtime& runtime, const std::vector<settings::ScreenSize>& offered
) {
    return screen_size_shown(
        runtime,
        runtime.engine_settings_ ? runtime.engine_settings_->current.screen_size
                                 : settings::desktop_screen_size,
        offered
    );
}

settings::ScreenSize Runtime::EngineSettingsState::screen_size_shown(
    const Runtime& runtime,
    settings::ScreenSize setting,
    const std::vector<settings::ScreenSize>& offered
) {
    settings::ScreenSize size = setting;
    // A screen size keeps each side in 16 bits.
    constexpr int widest = std::numeric_limits<uint16_t>::max();
    int width = 0;
    int height = 0;
    if (size == settings::desktop_screen_size && runtime.sdl_.window != nullptr &&
        SDL_GetWindowSize(runtime.sdl_.window, &width, &height) && width > 0 && height > 0 &&
        width <= widest && height <= widest)
        size = {static_cast<uint16_t>(width), static_cast<uint16_t>(height)};
    if (size == settings::desktop_screen_size && runtime.engine_settings_)
        size = runtime.engine_settings_->desktop;
    std::vector<oa::platform::display_modes::Size> sizes;
    sizes.reserve(offered.size());
    for (const settings::ScreenSize each : offered)
        sizes.push_back({each.width, each.height});
    const auto nearest =
        oa::platform::display_modes::nearest_offered(sizes, {size.width, size.height});
    return nearest ? offered[*nearest] : size;
}

settings::ScreenSize Runtime::EngineSettingsState::stored_screen_size(
    const Runtime& runtime, settings::ScreenSize size
) {
    const bool full_screen =
        runtime.sdl_.window != nullptr
            ? (SDL_GetWindowFlags(runtime.sdl_.window) & SDL_WINDOW_FULLSCREEN) != 0
            : start_use(runtime.options_) == oa::platform::display_modes::Use::full_screen;
    const settings::ScreenSize desktop = window_desktop(
        runtime.options_,
        runtime.sdl_.window,
        runtime.engine_settings_ ? runtime.engine_settings_->desktop : settings::desktop_screen_size
    );
    return full_screen && size == desktop ? settings::desktop_screen_size : size;
}

void Runtime::EngineSettingsState::choose_screen_size(Runtime& runtime, settings::ScreenSize size) {
    auto& state = runtime.engine_settings_state();
    const settings::ScreenSize chosen = stored_screen_size(runtime, size);
    state.current.screen_size = chosen;
    runtime.preference_values_[std::string(settings::key::screen_size)] =
        settings::screen_size_text(chosen);
    runtime.preferences_dirty_ = true;
}

void Runtime::EngineSettingsState::save_frame_stats(Runtime& runtime, bool shown) {
    runtime.show_frame_stats(shown);
    runtime.engine_settings_state().current.frame_stats = shown;
    runtime.preference_values_[std::string(settings::key::frame_stats)] =
        std::string(shown ? "1" : "0");
    runtime.preferences_dirty_ = true;
    if (const auto failure = flush(runtime))
        report_failed_save(runtime, *failure);
}

void Runtime::EngineSettingsState::report_failed_save(Runtime& runtime, const std::string& reason) {
    std::cerr << "open-annihilation: settings were not saved: " << reason << '\n';
    // The engine's own words, in the player's language when the interface
    // catalogue has them.
    const std::string_view text = oa::data::languages::interface_text(kSaveFailedText);
    if (runtime.screen_ == Screen::match && runtime.match_)
        runtime.post_match_message(text, oa::sim::messages::kind_status);
    else if (runtime.screen_ == Screen::main_menu)
        runtime.show_frontend_message(
            std::string(text),
            kSaveFailedMessageWidth,
            entry::message_show_ok,
            entry::message_fit_width
        );
}

uint16_t Runtime::EngineSettingsState::run_unit_limit(Runtime& runtime) {
    const uint16_t run = runtime.frontend_game().max_units_setting;
    return run != 0 ? run : runtime.engine_settings_state().current.unit_limit;
}

void Runtime::EngineSettingsState::keep_run_unit_limit(Runtime& runtime) {
    auto& game = runtime.frontend_game();
    if (game.max_units_setting == 0)
        game.max_units_setting = runtime.engine_settings_state().current.unit_limit;
}

void Runtime::EngineSettingsState::start_skirmish(
    Runtime& runtime, uint16_t units_per_player, bool run_unit_limit
) {
    runtime.bootstrap_match(
        {.units_per_player = units_per_player,
         .seat_roster = true,
         .run_unit_limit = run_unit_limit}
    );
}

uint16_t Runtime::EngineSettingsState::restart_unit_limit(Runtime& runtime) {
    if (runtime.match_ && runtime.match_->state().game.max_units_setting != 0)
        return runtime.match_->state().game.max_units_setting;
    return run_unit_limit(runtime);
}

void Runtime::EngineSettingsState::start_path_credit(Runtime& runtime, bool shared_or_replay) {
    if (!runtime.match_)
        return;
    auto& state = runtime.engine_settings_state();
    state.path_credit_held = shared_or_replay;
    runtime.match_->path_search_jobs().tick_credit = settings::match_path_search_nodes(
        state.current, shared_or_replay, runtime.limits_.path_search.nodes
    );
}

void Runtime::EngineSettingsState::hold_path_credit(Runtime& runtime, uint32_t extension_bits) {
    auto& state = runtime.engine_settings_state();
    if (!runtime.match_ || state.path_credit_held ||
        (extension_bits & (extension_state::shared_match | extension_state::replay)) == 0)
        return;
    state.path_credit_held = true;
    runtime.match_->path_search_jobs().tick_credit = runtime.limits_.path_search.nodes;
}

bool Runtime::EngineSettingsState::escape_opens_menu(Runtime& runtime) {
    return runtime.engine_settings_state().current.escape_opens_menu;
}

void Runtime::EngineSettingsState::note_pointer_source(Runtime& runtime, const SDL_Event& event) {
    switch (event.type) {
    case SDL_EVENT_MOUSE_MOTION:
        runtime.engine_settings_state().finger_pointer = event.motion.which == SDL_TOUCH_MOUSEID;
        return;
    case SDL_EVENT_MOUSE_BUTTON_DOWN:
    case SDL_EVENT_MOUSE_BUTTON_UP:
        runtime.engine_settings_state().finger_pointer = event.button.which == SDL_TOUCH_MOUSEID;
        return;
    case SDL_EVENT_MOUSE_WHEEL:
        runtime.engine_settings_state().finger_pointer = false;
        return;
    default:
        return;
    }
}

int32_t
Runtime::EngineSettingsState::finger_reach(const Runtime& runtime, double canvas_per_pixel) {
    // The pick distance in window points, as canvas pixels of the screen
    // shown, then as pixels of what is drawn at the scale.
    const double canvas = static_cast<double>(oa::ui::touch_hud::gadget_pick_points) *
                          static_cast<double>(runtime.touch_px_per_point());
    const double per_pixel = canvas_per_pixel > 0.0 ? canvas_per_pixel : 1.0;
    return std::max(int32_t{1}, static_cast<int32_t>(std::lround(canvas / per_pixel)));
}

void Runtime::destroy_engine_settings_state(EngineSettingsState* state) noexcept {
    // A retry the player never confirmed is put back before the run ends,
    // so that its clean exit writes the records the dialog opened with.
    if (state != nullptr && state->records_host != nullptr)
        state->records_host->restore_records();
    delete state;
}

Runtime::EngineSettingsState& Runtime::engine_settings_state() {
    if (!engine_settings_)
        engine_settings_.reset(new EngineSettingsState{});
    return *engine_settings_;
}

void Runtime::load_engine_settings() {
    auto& state = engine_settings_state();
    // An explicit preferences file plays as the game does everywhere: the
    // installation's options are not read for it.
    state.installation_ini = options_.preferences_file
                                 ? std::string{}
                                 : EngineSettingsState::read_installation_ini(options_.game_dir);
    // The machine as the window was opened for it (starting_screen_size).
    const auto start = start_inputs(options_, desktop_size(options_));
    state.raspberry_pi = start.raspberry_pi;
    state.light_machine = start.light_machine;
    state.desktop = start.desktop;
    state.steam_deck_panel_hz = start.steam_deck_panel_hz;
    // A picked folder that is no longer the mod stored is forgotten; the
    // file loses its key with the next save.
    if (settings::remembered_picked_folder(preference_values_).empty() &&
        preference_values_.erase(std::string(settings::key::picked_mod_directory)) > 0)
        preferences_dirty_ = true;
    list_offered_mods();
    state.physical_memory = oa::platform::read_machine_traits().memory;
    // The overrides are read under the profile's id, and laid over it as
    // the settings are put in effect.
    load_profile_layers();
    const bool switch_alt = (preferences_.graphics_flags & init::preference_flags::switch_alt) != 0;
    auto read =
        settings::read_settings(preference_values_, EngineSettingsState::inputs(*this), switch_alt);
    state.dropped_mod_folder.clear();
    if (!options_.mod_dir.empty() || options_.base_game) {
        // --mod-dir and --base-game set the Mod setting aside for the run,
        // which plays it again only from a start without them.
        state.playing_mod_folder = read.mod_folder;
    } else if (!read.mod_folder.empty() && read.mod_folder != state.playing_mod_folder) {
        // The start dropped a stored mod folder that is gone and plays the
        // game folder as it is: the setting shows what plays.
        std::error_code missing;
        if (!fs::is_directory(path_from_utf8(read.mod_folder), missing)) {
            state.dropped_mod_folder = std::move(read.mod_folder);
            read.mod_folder.clear();
        }
    }
    apply_engine_settings(read);
    // The layers start afresh: the overrides read are laid over the profile
    // even when the settings in effect held them already.
    apply_hack_overrides();
    // The run's unit limit starts at the setting.
    frontend_game().max_units_setting = read.unit_limit;
    show_frame_stats(read.frame_stats);
    // The battle room's RES column offers the display's sizes and starts
    // at the size the game plays at; a 3.1c machine shows either as sent.
    namespace mp = oa::ui::frontend_multiplayer;
    constexpr int32_t game_bits = 8;
    mp::multiplayer_bind_display_modes(
        {this,
         [](void* context, mp::DisplayMode* out, int32_t capacity) {
             const auto sizes =
                 EngineSettingsState::offered_screen_sizes(*static_cast<const Runtime*>(context));
             int32_t count = 0;
             for (const auto size : sizes) {
                 if (count >= capacity)
                     break;
                 out[count++] = {size.width, size.height, game_bits};
             }
             return count;
         },
         [](void* context) {
             const auto& runtime = *static_cast<const Runtime*>(context);
             const auto size = EngineSettingsState::screen_size_in_effect(
                 runtime, EngineSettingsState::offered_screen_sizes(runtime)
             );
             return mp::DisplayMode{size.width, size.height, game_bits};
         }}
    );
}

void Runtime::list_offered_mods() {
    auto& state = engine_settings_state();
    // The game folder is the last of the folders, below any mod folder
    // layered over it.
    state.mod_folders.clear();
    state.mod_names.clear();
    state.mod_details.clear();
    const auto& game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    std::vector<fs::path> offered = list_mod_folders(game_folder);
    if (!user_folder_.empty()) {
        const auto own = list_mods_in(user_folder_ / std::string(user_mods_folder_name));
        offered.insert(offered.end(), own.begin(), own.end());
    }
    // The picked folder is listed only while it is the mod stored.
    if (const std::string picked = settings::remembered_picked_folder(preference_values_);
        !picked.empty()) {
        std::error_code missing;
        const fs::path folder = path_from_utf8(picked);
        if (fs::is_directory(folder, missing))
            offered.push_back(folder);
    }
    // The mod folder played is the one layered over the game folder.
    state.playing_mod_folder =
        options_.game_folders.size() > 1
            ? path_to_utf8(fs::absolute(options_.game_folders.front()).lexically_normal())
            : std::string{};
    if (options_.game_folders.size() > 1)
        offered.push_back(options_.game_folders.front());
    // A folder of the player's own Mods folder offers ROLL BACK while its
    // .backup keeps an earlier version of its mod that the game can play,
    // checked as a pick is.
    std::error_code own_error;
    const fs::path own_mods =
        user_folder_.empty()
            ? fs::path()
            : fs::absolute(user_folder_ / std::string(user_mods_folder_name), own_error)
                  .lexically_normal();
    const auto playable = [&](const fs::path& kept) {
        const ModChoice choice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_};
        return check_picked_mod_folder(kept, game_folder, choice).refusal.empty();
    };
    for (const auto& folder : offered) {
        std::error_code error;
        const fs::path absolute = fs::absolute(folder, error);
        const std::string path = path_to_utf8((error ? folder : absolute).lexically_normal());
        // A folder offered twice, as when the Mods folder is the game
        // folder's, is listed once.
        if (std::find(state.mod_folders.begin(), state.mod_folders.end(), path) !=
            state.mod_folders.end())
            continue;
        ModSummary summary = read_mod_summary(folder);
        settings::ModDetails details;
        details.version = std::move(summary.version);
        details.description = std::move(summary.description);
        details.has_profile = summary.has_profile;
        details.badge_width = summary.badge.width;
        details.badge_height = summary.badge.height;
        details.badge_pixels = std::move(summary.badge.pixels);
        if (!own_mods.empty() && path_from_utf8(path).parent_path() == own_mods)
            if (const auto backup = mod_install::read_backup(folder);
                backup && backup->kind == mod_install::FolderKind::mod &&
                playable(entry_without_case(folder, mod_install::backup_folder_name)
                             .value_or(folder / std::string(mod_install::backup_folder_name)))) {
                const auto now = mod_install::read_installed_mod(folder);
                const bool same_version = now.version == backup->version;
                details.roll_back_from =
                    mod_install::version_label(now.version, now.revision, same_version);
                details.roll_back_to =
                    mod_install::version_label(backup->version, backup->revision, same_version);
            }
        state.mod_folders.push_back(path);
        state.mod_names.push_back(std::move(summary.title));
        state.mod_details.push_back(std::move(details));
    }
}

void Runtime::request_soft_restart() {
    soft_restart_requested_ = true;
    exit_requested_ = true;
}

oa::present::TextStyle Runtime::text_style() const {
    oa::present::TextStyle style =
        engine_settings_
            ? settings::text_style(engine_settings_->current)
            : settings::text_style(settings::default_settings(EngineSettingsState::inputs(*this)));
    // A language drawn in the modern fonts has them on whatever the setting.
    if (language_needs_modern_fonts())
        style.modern_fonts = true;
    return style;
}

GameTextPreferences game_text_preferences(const Runtime& runtime) noexcept {
    // The settings may not exist yet, and building their defaults allocates:
    // a failure gives the defaults rather than ending the program, and says
    // why once a run, so that a broken setting is not silent.
    static std::atomic<bool> reported{false};
    const auto report = [](const char* why) noexcept {
        if (!reported.exchange(true))
            std::fprintf(
                stderr,
                "open-annihilation: the text settings could not be read, so extensions draw "
                "at the defaults: %s\n",
                why
            );
    };
    try {
        const oa::present::TextStyle style = runtime.text_style();
        return game_text_preferences_of(style.modern_fonts, style.size);
    } catch (const std::exception& error) {
        report(error.what());
    } catch (...) {
        report("an unknown error");
    }
    return GameTextPreferences{};
}

bool Runtime::unicode_chat_on() const {
    // A mod profile's Unicode text, and a language shown in UTF-8 or whose
    // pack asks for it, turn it on whatever the setting.
    const auto& text = ui_rules().text_rendering;
    if ((text.enabled && text.unicode) || oa::data::languages::turns_unicode_chat_on(
                                              shown_language(), language_unicode_chat() != nullptr
                                          ))
        return true;
    return engine_settings_ && engine_settings_->current.unicode_chat;
}

render_policy::MenuScaling Runtime::menu_scaling() const noexcept {
    return engine_settings_ ? engine_settings_->current.menu_scaling
                            : render_policy::MenuScaling::sharp;
}

settings::ExplosionFlash Runtime::explosion_flash() const noexcept {
    return engine_settings_ ? engine_settings_->current.explosion_flash
                            : settings::ExplosionFlash::full;
}

double Runtime::past_map_edge_share() const noexcept {
    return settings::past_map_edge_share(
        engine_settings_ ? engine_settings_->current.view_past_map_edge
                         : settings::ViewPastMapEdge::one_half
    );
}

double Runtime::match_chrome_most_scale() const noexcept {
    namespace layout = oa::ui::display_layout;
    return !engine_settings_ || engine_settings_->current.hud_scaling ? layout::kMaxChromeScale
                                                                      : 1.0;
}

const settings::EngineSettings& Runtime::engine_settings() {
    EngineSettingsState::take_live_settings(*this);
    return engine_settings_state().current;
}

void Runtime::apply_engine_settings(const settings::EngineSettings& chosen) {
    auto& state = engine_settings_state();
    const settings::EngineSettings before = state.current;
    state.current = chosen;
    // Turning the wheel zoom off brings the battlefield back to its own scale.
    if (before.wheel_zoom && !chosen.wheel_zoom && match_)
        EngineSettingsState::ease_zoom_about_centre(*this, kDefaultBattlefieldZoom);
    // A view past the zoom's new limits comes back within them about the
    // battlefield's centre, at the next frame (step_match_zoom).
    else if (
        (before.max_zoom_out != chosen.max_zoom_out || before.max_zoom_in != chosen.max_zoom_in) &&
        match_
    ) {
        const float held = std::clamp(match_zoom_target_, least_match_zoom(), most_match_zoom());
        if (held != match_zoom_target_ || match_zoom_ < least_match_zoom() ||
            match_zoom_ > most_match_zoom())
            EngineSettingsState::ease_zoom_about_centre(*this, held);
    }
    // A view past the new limits of View past the map's edge is held within
    // them from the next frame, not from where it lies now.
    if (before.view_past_map_edge != chosen.view_past_map_edge)
        view_hold_ = {};
    // ui.megamap acts only while the wheel zoom is off.
    if (before.wheel_zoom != chosen.wheel_zoom && match_)
        megamap_wheel_zoom_changed();
    // SwitchAlt, where the keys read it: the match's options and the
    // frontend's preferences, which the match's options go back into.
    const auto with_switch_alt = [&](uint16_t flags) {
        return static_cast<uint16_t>(
            (flags & ~init::preference_flags::switch_alt) |
            (chosen.switch_alt ? init::preference_flags::switch_alt : 0)
        );
    };
    preferences_.graphics_flags = with_switch_alt(preferences_.graphics_flags);
    if (match_)
        match_->state().game.graphics_flags = with_switch_alt(match_->state().game.graphics_flags);
    // The next new game plays at a changed limit; a running game keeps its own.
    if (chosen.unit_limit != before.unit_limit)
        frontend_game().max_units_setting = chosen.unit_limit;
    // --max-fps holds for the run.
    if (!options_.max_frames_per_second_given)
        options_.max_frames_per_second = chosen.max_frame_rate;
    unit_supersampling_ = oa::present::model::unit_supersampling_from_factor(
                              static_cast<uint32_t>(chosen.anti_aliasing)
    )
                              .value_or(oa::present::model::UnitSupersampling::off);
    // In Full the row's level asks the graphics card's world target for its
    // factor instead, which the next Full frame takes (ensure_full_world_target).
    if (chosen.frame_stats != before.frame_stats)
        show_frame_stats(chosen.frame_stats);
    // Off applies at once; so do Basic and Full, but in a shared game or a
    // replay, which keeps the tier it began with until it ends.
    if (chosen.hardware_acceleration != before.hardware_acceleration)
        update_render_tier();
    apply_vertical_sync();
    // Menu scaling applies at once to a screen drawn as one frame, and to a
    // match drawn at a screen size and scaled to the screen: whole steps
    // set the window's presentation, and every frame takes its filter.
    if (chosen.menu_scaling != before.menu_scaling &&
        (screen_ != Screen::match || scaled_frame_width_ > 0))
        apply_output_mode();
    // HUD scaling lays a game out again at once.
    else if (chosen.hud_scaling != before.hud_scaling && screen_ == Screen::match)
        apply_output_mode();
    if (chosen.developer_mode != before.developer_mode ||
        chosen.hack_overrides != before.hack_overrides)
        apply_hack_overrides();
    // The language shows at once, on the screen under the settings too.
    set_language_choice(chosen.language);
    // The Touch section takes effect at once too: the touch controls read
    // the settings in effect at every finger and every frame.
    // So does Include in device backups, on the game files themselves.
    if (chosen.game_files_backed_up != before.game_files_backed_up)
        EngineSettingsState::apply_game_files_backups(*this, chosen.game_files_backed_up);
}

const oa::data::mod_profile::ModProfile* Runtime::mod_profile() const noexcept {
    if (engine_settings_ && engine_settings_->layered)
        return engine_settings_->plays_base_rules ? nullptr : engine_settings_->played.get();
    return options_.mod_profile.get();
}

const oa::data::mod_profile::ModProfile* Runtime::next_match_profile() const noexcept {
    if (!engine_settings_ || !engine_settings_->layered)
        return options_.mod_profile.get();
    const auto& state = *engine_settings_;
    // A game without a mod plays 3.1c's rules, and no profile, while its
    // overrides change none of them.
    if (!options_.mod_profile && state.latest && state.latest->sim_hash == state.base_sim_hash)
        return nullptr;
    return state.latest.get();
}

void Runtime::play_latest_profile() noexcept {
    if (!engine_settings_ || !engine_settings_->layered || !engine_settings_->played)
        return;
    auto& state = *engine_settings_;
    const auto* next = next_match_profile();
    state.plays_base_rules = next == nullptr;
    // Copied into the one object the run plays by, which keeps its place.
    if (next != nullptr && next != state.played.get()) {
        try {
            *state.played = *next;
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: developer mode: the rules cannot be put in play: "
                      << error.what() << '\n';
        }
    }
}

const oa::data::mod_profile::UiRules& Runtime::ui_rules() const noexcept {
    static const oa::data::mod_profile::UiRules base{};
    if (engine_settings_ && engine_settings_->layered)
        return engine_settings_->latest ? engine_settings_->latest->ui : base;
    const auto* profile = options_.mod_profile.get();
    return profile != nullptr ? profile->ui : base;
}

bool Runtime::developer_mode() const noexcept {
    return engine_settings_ && engine_settings_->current.developer_mode;
}

void Runtime::load_profile_layers() {
    namespace profiles = oa::data::mod_profile;
    auto& state = engine_settings_state();
    state.layered = true;
    state.latest = options_.mod_profile;
    state.played = std::make_shared<profiles::ModProfile>(
        options_.mod_profile ? *options_.mod_profile : profiles::ModProfile{}
    );
    state.plays_base_rules = !options_.mod_profile;
    state.refused.clear();
    // A mod folder without a profile keeps its overrides under its own id,
    // apart from the game's without a mod.
    if (options_.mod_profile)
        state.profile_id = options_.mod_profile->id;
    else if (options_.game_folders.size() > 1)
        state.profile_id = folder_overrides_id(options_.game_folders.front());
    else
        state.profile_id = std::string(profiles::base_game_id);
    state.profile_hacks = profiles::base_hack_states();
    // The plain 3.1c baseline, which a game without a mod lays its
    // overrides over; its sim hash is the one 3.1c's rules give.
    ProfileSource base{};
    const std::string base_text = profiles::base_game_profile_text();
    base.text.assign(base_text.begin(), base_text.end());
    base.name = std::string(profiles::base_game_id);
    base.options.accept_unimplemented_hacks = options_.accept_unimplemented_hacks;
    const auto base_result = profiles::resolve_profile(base.text, base.name, base.options);
    if (base_result.resolution)
        state.base_sim_hash = base_result.resolution->profile.sim_hash;
    if (!options_.mod_profile) {
        state.profile_source = std::move(base);
        return;
    }
    // A mod's profile is read again as the game folder's was, with the
    // settings it binds.
    state.profile_source = folder_profile_source(
        options_.game_folders.empty() ? std::vector<fs::path>{options_.game_dir}
                                      : options_.game_folders,
        ModChoice{{}, options_.mod_file, options_.accept_unimplemented_hacks, &preference_values_}
    );
    if (!state.profile_source)
        return;
    const auto shipped = profiles::resolve_profile(
        state.profile_source->text, state.profile_source->name, state.profile_source->options
    );
    if (shipped.resolution)
        state.profile_hacks = profiles::hack_states(shipped.resolution->effective);
    else
        state.profile_source.reset();
}

void Runtime::apply_hack_overrides() {
    namespace profiles = oa::data::mod_profile;
    if (!engine_settings_ || !engine_settings_->layered)
        return;
    auto& state = *engine_settings_;
    const auto& settings = state.current;
    std::shared_ptr<const profiles::ModProfile> latest = options_.mod_profile;
    std::vector<std::string> refused;
    if (settings.developer_mode && !settings.hack_overrides.empty()) {
        if (!state.profile_source) {
            refused.emplace_back("the profile cannot be read again; no override applies");
        } else {
            auto options = state.profile_source->options;
            options.overrides = settings.hack_overrides;
            auto result = profiles::resolve_profile(
                state.profile_source->text, state.profile_source->name, options
            );
            // The profile's own warnings were reported as the game started;
            // those of the overrides are reported here.
            constexpr std::string_view left_out = "the override is left out";
            for (const auto& warning : result.warnings)
                if (warning.message.find(left_out) != std::string::npos)
                    refused.push_back(profiles::format_diagnostic(warning));
            for (const auto& error : result.errors)
                refused.push_back(profiles::format_diagnostic(error));
            if (result.resolution)
                latest = std::make_shared<const profiles::ModProfile>(
                    std::move(result.resolution->profile)
                );
        }
    }
    if (refused != state.refused) {
        for (const auto& line : refused)
            std::cerr << "open-annihilation: developer mode: " << line << '\n';
        state.refused = std::move(refused);
    }
    static const profiles::UiRules base_rules{};
    const auto& display_before = state.latest ? state.latest->ui : base_rules;
    const auto& display_after = latest ? latest->ui : base_rules;
    const bool display_changed = !(display_before == display_after);
    state.latest = std::move(latest);
    // The rules wait for the running match to end; without one they apply now.
    if (!match_)
        play_latest_profile();
    if (!display_changed)
        return;
    // The display rules apply at once, a running match's voices and
    // explosions and the player's view settings included.
    if (match_)
        match_->set_display_rules(view_rules::match_display_rules(ui_rules()));
    load_view_settings();
}

AccelerationFacts Runtime::acceleration_facts() const {
    AccelerationFacts facts{};
    const auto setting = engine_settings_ ? engine_settings_->current.hardware_acceleration
                                          : oa::ui::engine_settings::HardwareAcceleration::off;
    if (render_run_ && render_run_->host != nullptr) {
        // The facts the tier is decided from, the function test's result
        // and the presentation drawing now among them.
        const bool drawing = accelerated_presentation();
        facts = tier_acceleration_facts(
            render_run_->host->tier_inputs(),
            drawing ? accelerated_.rung : render_tier_rung(),
            full_presentation() ? render_policy::RenderTier::full
            : drawing           ? render_policy::RenderTier::accelerated
                                : render_policy::RenderTier::standard
        );
        facts.asked = hardware_acceleration_asked(options_, setting);
        facts.flag = options_.hardware_acceleration;
        facts.force_capable = options_.force_capable;
        render_run_->host->fill_record_facts(facts);
        const std::string_view renderer_name = render_run_->host->facts().renderer;
        facts.vertical_sync_resets_device =
            render_probe::vertical_sync_resets_device(renderer_name);
        facts.vertical_sync_refused = vertical_sync_refused_;
        facts.full_supersample_drawn = static_cast<uint8_t>(full_supersample());
        facts.full_supersample = std::max<uint8_t>(1, facts.full_supersample_drawn);
        if (match_) {
            const uint32_t extension = current_extension_state();
            facts.shared_game =
                facts.shared_game || (extension & extension_state::shared_match) != 0;
            facts.replay = facts.replay || (extension & extension_state::replay) != 0;
        }
        return facts;
    }
    facts.asked = hardware_acceleration_asked(options_, setting);
    facts.flag = options_.hardware_acceleration;
    facts.force_capable = options_.force_capable;
    const char* named_driver = SDL_GetHint(SDL_HINT_RENDER_DRIVER);
    const char* video_driver = SDL_GetCurrentVideoDriver();
    facts.environment_driver =
        (named_driver != nullptr && *named_driver != '\0') ||
        (video_driver != nullptr &&
         std::find(kWindowlessVideoDrivers.begin(), kWindowlessVideoDrivers.end(), video_driver) !=
             kWindowlessVideoDrivers.end());
    facts.physical_memory = engine_settings_ ? engine_settings_->physical_memory : 0;
    // A renderer the runtime made itself is not looked at, so whether it is
    // able stays unknown (renderer_capable empty); only SDL's software
    // renderer is known unable.
    const char* renderer = sdl_.renderer != nullptr ? SDL_GetRendererName(sdl_.renderer) : nullptr;
    const std::string_view renderer_name = renderer != nullptr ? renderer : "";
    facts.software_renderer = renderer_name == SDL_SOFTWARE_RENDERER;
    facts.vertical_sync_resets_device = render_probe::vertical_sync_resets_device(renderer_name);
    facts.vertical_sync_refused = vertical_sync_refused_;
    if (match_) {
        const uint32_t extension = current_extension_state();
        facts.shared_game = (extension & extension_state::shared_match) != 0;
        facts.replay = (extension & extension_state::replay) != 0;
    }
    return facts;
}

void Runtime::take_renderer_retry(const settings::Dialog& dialog) {
    auto& state = engine_settings_state();
    if (dialog.forget_renderer_failures == state.retries_taken)
        return;
    state.retries_taken = dialog.forget_renderer_failures;
    if (render_run_ && render_run_->host != nullptr) {
        // The host keeps what the dialog opened with for Cancel, not what an
        // earlier retry in it left.
        state.records_host = render_run_->host;
        std::ignore = state.records_host->clear_records();
    }
    forget_render_failures();
}

void Runtime::keep_renderer_records() {
    auto& state = engine_settings_state();
    if (state.records_host != nullptr)
        state.records_host->keep_cleared_records();
    state.records_host = nullptr;
}

void Runtime::restore_renderer_records() {
    auto& state = engine_settings_state();
    if (state.records_host != nullptr)
        state.records_host->restore_records();
    state.records_host = nullptr;
}

AccelerationReport Runtime::acceleration_report() const {
    return report_acceleration(acceleration_facts());
}

void Runtime::apply_vertical_sync() {
    if (sdl_.renderer == nullptr || !engine_settings_)
        return;
    const bool wanted =
        engine_settings_->current.vertical_sync && !acceleration_report().vertical_sync_unavailable;
    // SDL makes every renderer without it, so while the setting stays Off
    // the renderer is never asked.
    if (wanted == vertical_sync_in_effect_)
        return;
    if (!SDL_SetRenderVSync(sdl_.renderer, wanted ? 1 : 0)) {
        std::cerr << "open-annihilation: the renderer does not wait for the display: "
                  << SDL_GetError() << '\n';
        if (wanted)
            vertical_sync_refused_ = true;
    }
    int vsync = 0;
    vertical_sync_in_effect_ = SDL_GetRenderVSync(sdl_.renderer, &vsync) && vsync != 0;
}

float Runtime::display_refresh_rate() const {
    if (sdl_.window == nullptr)
        return 0.0F;
    const SDL_DisplayMode* mode = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(sdl_.window));
    return mode != nullptr ? mode->refresh_rate : 0.0F;
}

std::optional<std::string> Runtime::save_engine_settings(
    const settings::EngineSettings& opened, const settings::EngineSettings& chosen, bool restored
) {
    // A mod folder the start dropped is still stored: what is chosen now
    // replaces it, No Mod included.
    auto& state = engine_settings_state();
    settings::EngineSettings stored = opened;
    if (!state.dropped_mod_folder.empty()) {
        stored.mod_folder = std::move(state.dropped_mod_folder);
        state.dropped_mod_folder.clear();
    }
    settings::write_settings(
        preference_values_,
        stored,
        chosen,
        settings::default_settings(EngineSettingsState::inputs(*this)),
        restored,
        engine_settings_state().profile_id
    );
    // SwitchAlt keeps 3.1c's own key, written only when it changed.
    if (chosen.switch_alt != opened.switch_alt || restored)
        write_number(init::general_section, "SwitchAlt", chosen.switch_alt ? 1U : 0U);
    preferences_dirty_ = true;
    return EngineSettingsState::flush(*this);
}

settings::Dialog& Runtime::open_engine_settings_dialog(settings::DialogKind kind) {
    EngineSettingsState::take_live_settings(*this);
    auto& state = engine_settings_state();
    if (kind == settings::DialogKind::mod_options) {
        const auto& ui = ui_rules();
        settings::EngineSettings current = state.current;
        current.mod_options = view_rules::dialog_options(view_settings_, ui);
        settings::EngineSettings defaults = current;
        const auto* profile = mod_profile();
        const oa::data::match_rules::OrdersConPatrolGuardOptions base_builders{};
        const auto& builders =
            profile != nullptr ? profile->rules.orders.con_patrol_guard_options : base_builders;
        defaults.mod_options =
            view_rules::dialog_options(view_rules::read_view_settings(ui, builders, {}), ui);
        auto& dialog = state.dialog.emplace();
        settings::open_mod_options_dialog(
            dialog, current, defaults, view_rules::dialog_option_locks(ui), kVersionText
        );
        return dialog;
    }
    state.opened_zoom_target = match_zoom_target_;
    state.opened_run_unit_limit = frontend_game().max_units_setting;
    auto& dialog = state.dialog.emplace();
    state.retries_taken = 0;
    state.records_host = nullptr;
    // The Game files section is the main menu's: where the platform brings
    // game files in, Manage… runs over the menu, never over a match.
    const bool game_files =
        screen_ == Screen::main_menu && game_files_import_offered(game_files_hooks());
    settings::open_dialog(
        dialog,
        state.current,
        settings::default_settings(EngineSettingsState::inputs(*this)),
        engine_settings_locks(),
        kVersionText,
        state.last_page,
        acceleration_report().status,
        settings::highest_offered_unit_limit(limits_.units_per_player),
        settings::ModOffer{
            state.mod_names,
            state.mod_folders,
            state.mod_details,
            state.playing_mod_folder,
        },
        state.profile_hacks,
        &system_language(),
        touch_controls_active(),
        game_files,
        pad_used()
    );
    // Screen size offers Desktop and the display's own sizes. In a window it
    // opens at the window's own size, shown as Custom where the display
    // offers no such size; in full screen, and without a window, at the
    // setting, listed where the display does not offer it.
    dialog.offered_screen_sizes.assign(1, settings::desktop_screen_size);
    for (const settings::ScreenSize size : EngineSettingsState::offered_screen_sizes(*this))
        dialog.offered_screen_sizes.push_back(size);
    dialog.window_screen_size.reset();
    dialog.custom_screen_size.reset();
    settings::ScreenSize shown = state.current.screen_size;
    if (sdl_.window != nullptr && (SDL_GetWindowFlags(sdl_.window) & SDL_WINDOW_FULLSCREEN) == 0)
        if (const auto now = EngineSettingsState::screen_size_now(*this);
            now != settings::desktop_screen_size) {
            shown = now;
            dialog.window_screen_size = now;
        }
    state.window_screen_size_shown = dialog.window_screen_size.has_value();
    if (std::find(dialog.offered_screen_sizes.begin(), dialog.offered_screen_sizes.end(), shown) ==
        dialog.offered_screen_sizes.end()) {
        const auto later = std::find_if(
            dialog.offered_screen_sizes.begin() + 1,
            dialog.offered_screen_sizes.end(),
            [shown](settings::ScreenSize size) {
                return size.width > shown.width ||
                       (size.width == shown.width && size.height > shown.height);
            }
        );
        dialog.offered_screen_sizes.insert(later, shown);
        if (dialog.window_screen_size)
            dialog.custom_screen_size = shown;
    }
    // Controller's Steam Input notice, and Maximum frame rate's line naming
    // a Steam Deck's screen rate.
    dialog.steam_input = pad_steam_input();
    dialog.steam_deck_panel_hz = state.steam_deck_panel_hz;
    fill_game_files_rows(dialog);
    // Your files shows the player's own folder.
    dialog.user_folder = path_to_utf8(user_folder_);
    // The languages whose packs turn Unicode chat on.
    dialog.unicode_chat_languages = unicode_chat_language_tags();
    // Developer Mode opens as it was left: its open areas and hacks and its filter.
    if (state.last_developer_list) {
        dialog.developer.areas_open = state.last_developer_list->areas_open;
        dialog.developer.hacks_open = state.last_developer_list->hacks_open;
        dialog.developer.active_only = state.last_developer_list->active_only;
    }
    return dialog;
}

settings::Dialog* Runtime::engine_settings_dialog() {
    if (!engine_settings_ || !engine_settings_->dialog)
        return nullptr;
    return &*engine_settings_->dialog;
}

bool Runtime::take_engine_settings_action(settings::DialogAction action) {
    auto* dialog = engine_settings_dialog();
    if (dialog == nullptr)
        return false;
    auto& state = engine_settings_state();
    switch (action) {
    case settings::DialogAction::none:
    case settings::DialogAction::redraw:
        return false;
    case settings::DialogAction::open_folder: {
        // Your files: Saves, which holds each mod's saved games; where
        // screenshots go, Screenshots, which holds each mod's, while no Image
        // Output Directory is set; or the player's Mods folder. The dialog
        // stays open.
        fs::path folder;
        switch (dialog->folder_to_open) {
        case settings::FolderButton::saves:
            folder = user_folder_ / std::string(saves_folder_name);
            break;
        case settings::FolderButton::screenshots:
            folder = game_file_path(
                preferences_.image_output_directory + "\\screenshots",
                ui::frontend::SavePathUse::write
            );
            if (folder == oa::app::screenshots_folder(user_folder_, files_mod_id()))
                folder = folder.parent_path();
            break;
        case settings::FolderButton::mods:
            folder = user_folder_ / std::string(user_mods_folder_name);
            break;
        }
        const FolderOpening opening = open_player_folder(folder);
        std::ignore = settings::set_folder_notice(*dialog, opening.opened ? "" : opening.reason);
        return false;
    }
    case settings::DialogAction::changed:
        if (dialog->kind == settings::DialogKind::mod_options) {
            view_rules::apply_dialog_options(
                dialog->chosen.mod_options, ui_rules(), view_settings_
            );
            return false;
        }
        take_renderer_retry(*dialog);
        {
            // A Screen size moved to applies when OK is pressed; until then
            // the setting stays as it was.
            settings::EngineSettings live = dialog->chosen;
            live.screen_size = dialog->opened.screen_size;
            apply_engine_settings(live);
        }
        return false;
    case settings::DialogAction::accepted:
    case settings::DialogAction::switch_mod: {
        if (dialog->kind == settings::DialogKind::mod_options) {
            view_rules::apply_dialog_options(
                dialog->chosen.mod_options, ui_rules(), view_settings_
            );
            save_view_settings();
            state.dialog.reset();
            return true;
        }
        // A mod folder the next start could not play is refused before
        // anything is stored: the page says why, and the dialog stays open
        // with the mod played still chosen.
        if (action == settings::DialogAction::switch_mod && !dialog->chosen.mod_folder.empty()) {
            const auto& game_folder =
                options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
            const fs::path picked = path_from_utf8(dialog->chosen.mod_folder);
            auto check = check_picked_mod_folder(
                picked,
                game_folder,
                ModChoice{{}, {}, options_.accept_unimplemented_hacks, &preference_values_}
            );
            // Without a profile, the start ends on a SIDEDATA it cannot play,
            // such as one a side's section is missing from, in the language
            // the dialog leaves chosen.
            if (check.refusal.empty() && check.without_profile)
                if (auto problem = side_data_problem_over(
                        picked, game_folder, data_word_for(dialog->chosen.language)
                    );
                    !problem.empty()) {
                    check.refusal = "That folder cannot be played; the log says why.";
                    check.errors.push_back(std::move(problem));
                }
            for (const auto& line : check.errors)
                std::cerr << "open-annihilation: the mod chosen: " << line << '\n';
            if (!check.refusal.empty()) {
                dialog->chosen.mod_folder = state.current.mod_folder;
                std::ignore = settings::set_folder_notice(
                    *dialog, oa::data::languages::interface_text(check.refusal)
                );
                return false;
            }
        }
        take_renderer_retry(*dialog);
        keep_renderer_records();
        // A Screen size moved to is stored (as Desktop for the desktop's own
        // in full screen) and applied at once, also where it is the setting
        // already, as a window sized by hand snaps back to it.
        settings::EngineSettings chosen = dialog->chosen;
        const bool screen_size_moved =
            chosen.screen_size != dialog->opened.screen_size ||
            (state.window_screen_size_shown && !dialog->window_screen_size);
        if (screen_size_moved)
            chosen.screen_size = EngineSettingsState::stored_screen_size(*this, chosen.screen_size);
        apply_engine_settings(chosen);
        const auto failure = save_engine_settings(dialog->opened, chosen, dialog->restored);
        state.last_page = dialog->page;
        state.last_developer_list = dialog->developer;
        state.dialog.reset();
        if (screen_size_moved)
            apply_screen_size(chosen.screen_size);
        if (failure) {
            // A mod that was not stored would not be the one the restart
            // plays: the game stays as it is and says so.
            EngineSettingsState::report_failed_save(*this, *failure);
            return true;
        }
        // SWITCH reloads the game for the mod now stored, back on the main
        // menu: the loop ends after this frame and main() starts afresh.
        if (action == settings::DialogAction::switch_mod)
            request_soft_restart();
        return true;
    }
    case settings::DialogAction::cancelled: {
        if (dialog->kind == settings::DialogKind::mod_options) {
            view_rules::apply_dialog_options(
                dialog->opened.mod_options, ui_rules(), view_settings_
            );
            state.dialog.reset();
            return true;
        }
        const bool zoom_changed = match_ && match_zoom_target_ != state.opened_zoom_target;
        restore_renderer_records();
        apply_engine_settings(dialog->opened);
        // The battlefield's zoom and the next game's unit limit go back to
        // what they were, a loaded game's limit included.
        if (zoom_changed)
            EngineSettingsState::ease_zoom_about_centre(*this, state.opened_zoom_target);
        frontend_game().max_units_setting = state.opened_run_unit_limit;
        state.last_page = dialog->page;
        state.last_developer_list = dialog->developer;
        state.dialog.reset();
        return true;
    }
    case settings::DialogAction::manage_game_files:
        // The dialog stays open under the Game files screen.
        open_game_files_manage();
        return false;
    case settings::DialogAction::roll_back_mod:
        // The dialog stays open, unless the mod played rolls back as the
        // run ends.
        return roll_back_mod_folder(*dialog);
    }
    return false;
}

const settings::DialogFonts* Runtime::engine_settings_fonts() {
    auto& state = engine_settings_state();
    if (!state.fonts && !state.fonts_missing) {
        try {
            state.fonts = settings::load_dialog_fonts(assets_);
        } catch (const std::exception& error) {
            state.fonts_missing = true;
            std::cerr << "open-annihilation: the settings' fonts are missing: " << error.what()
                      << '\n';
        }
    }
    return state.fonts ? &*state.fonts : nullptr;
}

oa::ui::frontend_renderer::RgbaPicture Runtime::engine_settings_icon() {
    auto& state = engine_settings_state();
    if (!state.icon && !state.icon_missing) {
        WindowIcon decoded;
        std::string error;
        if (decode_window_icon(window_icon_png(), decoded, error))
            state.icon = visible_part(decoded);
        if (!state.icon || state.icon->pixels.empty()) {
            state.icon.reset();
            state.icon_missing = true;
            std::cerr << "open-annihilation: the settings' icon is missing"
                      << (error.empty() ? std::string{} : ": " + error) << '\n';
        }
    }
    if (!state.icon)
        return {};
    return {state.icon->width, state.icon->height, state.icon->pixels};
}

std::optional<settings::DialogKey>
Runtime::engine_settings_dialog_key(uint32_t key, uint16_t modifiers) noexcept {
    switch (key) {
    case SDLK_RETURN:
        return settings::DialogKey::enter;
    case SDLK_ESCAPE:
        return settings::DialogKey::escape;
    case SDLK_UP:
        return settings::DialogKey::up;
    case SDLK_DOWN:
        return settings::DialogKey::down;
    case SDLK_LEFT:
        return settings::DialogKey::left;
    case SDLK_RIGHT:
        return settings::DialogKey::right;
    case SDLK_SPACE:
        return settings::DialogKey::space;
    case SDLK_TAB:
        return (modifiers & SDL_KMOD_SHIFT) != 0 ? settings::DialogKey::back_tab
                                                 : settings::DialogKey::tab;
    case SDLK_PAGEUP:
        return settings::DialogKey::page_up;
    case SDLK_PAGEDOWN:
        return settings::DialogKey::page_down;
    case SDLK_HOME:
        return settings::DialogKey::home;
    case SDLK_END:
        return settings::DialogKey::end;
    case SDLK_Y:
        return settings::DialogKey::yes;
    case SDLK_N:
        return settings::DialogKey::no;
    default:
        return std::nullopt;
    }
}

void Runtime::release_unsaved_unit_limit() {
    if (!unsaved_unit_limit_playing_)
        return;
    unsaved_unit_limit_playing_ = false;
    if (unsaved_unit_limit_pending_)
        return;
    frontend_game().max_units_setting = engine_settings_state().current.unit_limit;
}

void set_unit_limit(Runtime& runtime, int32_t units_per_player, SettingScope scope) {
    const auto& units = runtime.limits_.units_per_player;
    const int64_t lowest = units.minimum;
    const int64_t highest = settings::highest_offered_unit_limit(units);
    const int64_t clamped = std::clamp(static_cast<int64_t>(units_per_player), lowest, highest);
    const auto kept = static_cast<uint16_t>(clamped);
    switch (scope) {
    case SettingScope::next_game:
        // The next new game plays at it, and that match's teardown puts the
        // player's setting back.
        runtime.frontend_game().max_units_setting = kept;
        runtime.unsaved_unit_limit_pending_ = true;
        return;
    case SettingScope::immediate:
        runtime.frontend_game().max_units_setting = kept;
        runtime.unsaved_unit_limit_pending_ = false;
        runtime.engine_settings_state().current.unit_limit = kept;
        break;
    case SettingScope::next_restart:
        // The setting, as Settings shows it, and the preferences take it; the
        // run's limit stays as it is until the game next starts.
        runtime.engine_settings_state().current.unit_limit = kept;
        break;
    }
    runtime.preference_values_[std::string(settings::key::unit_limit)] = std::to_string(kept);
    runtime.preferences_dirty_ = true;
    if (const auto failure = Runtime::EngineSettingsState::flush(runtime))
        Runtime::EngineSettingsState::report_failed_save(runtime, *failure);
}

} // namespace oa::app
