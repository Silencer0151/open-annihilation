// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings: read at start, put in effect, saved, and
// the dialog's session that both hosts share.

#include "engine_settings_state.hpp"

#include "oa/app/acceleration_status.hpp"
#include "oa/app/runtime.hpp"
#include "render_host.hpp"
#include "render_run.hpp"
#include "screen_size.hpp"
#include "oa/platform/machine.hpp"
#include "oa/sim/ground_orders/search_worker.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <tuple>

#ifndef OA_ENGINE_VERSION
#error "OA_ENGINE_VERSION names the engine's version for the dialog's header"
#endif

namespace oa::app {

namespace settings = oa::ui::engine_settings;

static_assert(settings::base_path_search_nodes == oa::sim::ground_orders::search_tick_credit);
static_assert(settings::lowest_frame_rate == kLowestMaxFramesPerSecond);
static_assert(settings::highest_frame_rate == kDefaultMaxFramesPerSecond);
static_assert(settings::default_unit_limit == kSkirmishUnitsPerPlayer);
static_assert(
    static_cast<uint32_t>(settings::AntiAliasing::x16) ==
    oa::present::model::supersampling_factor(oa::present::model::UnitSupersampling::x16)
);

namespace {

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

/// SDL's name of the renderer that resets its device at each change of the
/// wait for the display. The game does not yet recover a device such a reset
/// leaves lost, so Vertical sync is out of reach there.
constexpr std::string_view kDeviceResettingRenderer = "direct3d";

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
    }
    return inputs;
}

std::string Runtime::EngineSettingsState::read_installation_ini(const fs::path& game_directory) {
    std::error_code error;
    for (fs::directory_iterator entry(game_directory, error), end; !error && entry != end;
         entry.increment(error)) {
        if (ascii_lower(entry->path().filename().string()) != kInstallationIniName ||
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
    if (runtime.screen_ == Screen::match && runtime.match_)
        runtime.post_match_message(kSaveFailedText, oa::sim::messages::kind_status);
    else if (runtime.screen_ == Screen::main_menu)
        runtime.show_frontend_message(
            runtime.translate_ui(std::string(kSaveFailedText)),
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

void Runtime::EngineSettingsState::start_skirmish(Runtime& runtime, uint16_t units_per_player) {
    runtime.bootstrap_match({.units_per_player = units_per_player, .seat_roster = true});
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
    runtime.match_->path_search_jobs().tick_credit =
        settings::match_path_search_nodes(state.current, shared_or_replay);
}

void Runtime::EngineSettingsState::hold_path_credit(Runtime& runtime, uint32_t extension_bits) {
    auto& state = runtime.engine_settings_state();
    if (!runtime.match_ || state.path_credit_held ||
        (extension_bits & (extension_state::shared_match | extension_state::replay)) == 0)
        return;
    state.path_credit_held = true;
    runtime.match_->path_search_jobs().tick_credit = settings::base_path_search_nodes;
}

bool Runtime::EngineSettingsState::escape_opens_menu(Runtime& runtime) {
    return runtime.engine_settings_state().current.escape_opens_menu;
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
    const auto start = start_inputs(options_, desktop_size());
    state.raspberry_pi = start.raspberry_pi;
    state.light_machine = start.light_machine;
    state.desktop = start.desktop;
    state.physical_memory = oa::platform::read_machine_traits().memory;
    const bool switch_alt = (preferences_.graphics_flags & init::preference_flags::switch_alt) != 0;
    const auto read =
        settings::read_settings(preference_values_, EngineSettingsState::inputs(*this), switch_alt);
    apply_engine_settings(read);
    // The run's unit limit starts at the setting.
    frontend_game().max_units_setting = read.unit_limit;
    show_frame_stats(read.frame_stats);
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
    if (chosen.frame_stats != before.frame_stats)
        show_frame_stats(chosen.frame_stats);
    // Off applies at once; so does On, but in a shared game or a replay,
    // which keeps the tier it began with until it ends.
    if (chosen.hardware_acceleration != before.hardware_acceleration)
        update_render_tier();
    apply_vertical_sync();
}

AccelerationFacts Runtime::acceleration_facts() const {
    AccelerationFacts facts{};
    const bool setting = engine_settings_ && engine_settings_->current.hardware_acceleration;
    if (render_run_ && render_run_->host != nullptr) {
        // The facts the tier is decided from, the function test's result
        // and the presentation drawing now among them.
        const bool drawing = accelerated_presentation();
        facts = tier_acceleration_facts(
            render_run_->host->tier_inputs(),
            drawing ? accelerated_.rung : render_tier_rung(),
            drawing
        );
        facts.asked = hardware_acceleration_asked(options_, setting);
        facts.flag = options_.hardware_acceleration;
        facts.force_capable = options_.force_capable;
        render_run_->host->fill_record_facts(facts);
        const std::string_view renderer_name = render_run_->host->facts().renderer;
        facts.vertical_sync_resets_device = renderer_name == kDeviceResettingRenderer;
        facts.vertical_sync_refused = vertical_sync_refused_;
        facts.slow_frames_stepped = render_run_->watch && render_run_->watch->slowed;
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
    facts.vertical_sync_resets_device = renderer_name == kDeviceResettingRenderer;
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
    settings::write_settings(
        preference_values_,
        opened,
        chosen,
        settings::default_settings(EngineSettingsState::inputs(*this)),
        restored
    );
    // SwitchAlt keeps 3.1c's own key, written only when it changed.
    if (chosen.switch_alt != opened.switch_alt || restored)
        write_number(init::general_section, "SwitchAlt", chosen.switch_alt ? 1U : 0U);
    preferences_dirty_ = true;
    return EngineSettingsState::flush(*this);
}

settings::Dialog& Runtime::open_engine_settings_dialog() {
    EngineSettingsState::take_live_settings(*this);
    auto& state = engine_settings_state();
    state.opened_zoom_target = match_zoom_target_;
    state.opened_run_unit_limit = frontend_game().max_units_setting;
    auto& dialog = state.dialog.emplace();
    state.retries_taken = 0;
    state.records_host = nullptr;
    settings::open_dialog(
        dialog,
        state.current,
        settings::default_settings(EngineSettingsState::inputs(*this)),
        engine_settings_locks(),
        kVersionText,
        state.last_page,
        acceleration_report().status
    );
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
    case settings::DialogAction::changed:
        take_renderer_retry(*dialog);
        apply_engine_settings(dialog->chosen);
        return false;
    case settings::DialogAction::accepted: {
        take_renderer_retry(*dialog);
        keep_renderer_records();
        apply_engine_settings(dialog->chosen);
        const auto failure = save_engine_settings(dialog->opened, dialog->chosen, dialog->restored);
        state.last_page = dialog->page;
        state.dialog.reset();
        if (failure)
            EngineSettingsState::report_failed_save(*this, *failure);
        return true;
    }
    case settings::DialogAction::cancelled: {
        const bool zoom_changed = match_ && match_zoom_target_ != state.opened_zoom_target;
        restore_renderer_records();
        apply_engine_settings(dialog->opened);
        // The battlefield's zoom and the next game's unit limit go back to
        // what they were, a loaded game's limit included.
        if (zoom_changed)
            EngineSettingsState::ease_zoom_about_centre(*this, state.opened_zoom_target);
        frontend_game().max_units_setting = state.opened_run_unit_limit;
        state.last_page = dialog->page;
        state.dialog.reset();
        return true;
    }
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

std::optional<settings::DialogKey>
Runtime::engine_settings_dialog_key(uint32_t key, uint16_t modifiers) noexcept {
    switch (key) {
    case SDLK_RETURN:
    case SDLK_KP_ENTER:
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
    default:
        return std::nullopt;
    }
}

} // namespace oa::app
