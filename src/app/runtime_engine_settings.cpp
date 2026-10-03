// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings: read at start, put in effect, saved, and
// the dialog's session that both hosts share.

#include "engine_settings_state.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/app/mod_profile_loader.hpp"

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

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cctype>
#include <exception>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
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
    }
    inputs.units_per_player = runtime.limits_.units_per_player;
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
    // The mods the game folder offers: the game folder is the last of the
    // folders, below any mod folder layered over it.
    state.mod_folders.clear();
    state.mod_names.clear();
    const auto& game_folder =
        options_.game_folders.empty() ? options_.game_dir : options_.game_folders.back();
    for (const auto& folder : list_mod_folders(game_folder)) {
        state.mod_folders.push_back(path_to_utf8(fs::absolute(folder).lexically_normal()));
        state.mod_names.push_back(path_to_utf8(folder.filename()));
    }
    state.physical_memory = oa::platform::read_machine_traits().memory;
    // The overrides are read under the profile's id, and laid over it as
    // the settings are put in effect.
    load_profile_layers();
    const bool switch_alt = (preferences_.graphics_flags & init::preference_flags::switch_alt) != 0;
    const auto read =
        settings::read_settings(preference_values_, EngineSettingsState::inputs(*this), switch_alt);
    apply_engine_settings(read);
    // The layers start afresh: the overrides read are laid over the profile
    // even when the settings in effect held them already.
    apply_hack_overrides();
    // The run's unit limit starts at the setting.
    frontend_game().max_units_setting = read.unit_limit;
    show_frame_stats(read.frame_stats);
}

oa::present::TextStyle Runtime::text_style() const {
    if (!engine_settings_)
        return settings::text_style(settings::default_settings(EngineSettingsState::inputs(*this)));
    return settings::text_style(engine_settings_->current);
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
    // In Full the row's level asks the graphics card's world target for its
    // factor instead, which the next Full frame takes (ensure_full_world_target).
    if (chosen.frame_stats != before.frame_stats)
        show_frame_stats(chosen.frame_stats);
    // Off applies at once; so do Basic and Full, but in a shared game or a
    // replay, which keeps the tier it began with until it ends.
    if (chosen.hardware_acceleration != before.hardware_acceleration)
        update_render_tier();
    apply_vertical_sync();
    if (chosen.developer_mode != before.developer_mode ||
        chosen.hack_overrides != before.hack_overrides)
        apply_hack_overrides();
    // The language shows at once in what is drawn each frame; screens and
    // panels show it once they are opened again.
    set_language_choice(chosen.language);
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
    state.profile_id =
        options_.mod_profile ? options_.mod_profile->id : std::string(profiles::base_game_id);
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
    settings::write_settings(
        preference_values_,
        opened,
        chosen,
        settings::default_settings(EngineSettingsState::inputs(*this)),
        restored,
        engine_settings_state().mod_folders,
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
    settings::open_dialog(
        dialog,
        state.current,
        settings::default_settings(EngineSettingsState::inputs(*this)),
        engine_settings_locks(),
        kVersionText,
        state.last_page,
        acceleration_report().status,
        settings::highest_offered_unit_limit(limits_.units_per_player),
        state.mod_names,
        state.profile_hacks,
        &system_language()
    );
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
    case settings::DialogAction::changed:
        if (dialog->kind == settings::DialogKind::mod_options) {
            view_rules::apply_dialog_options(
                dialog->chosen.mod_options, ui_rules(), view_settings_
            );
            return false;
        }
        take_renderer_retry(*dialog);
        apply_engine_settings(dialog->chosen);
        return false;
    case settings::DialogAction::accepted: {
        if (dialog->kind == settings::DialogKind::mod_options) {
            view_rules::apply_dialog_options(
                dialog->chosen.mod_options, ui_rules(), view_settings_
            );
            save_view_settings();
            state.dialog.reset();
            return true;
        }
        take_renderer_retry(*dialog);
        keep_renderer_records();
        apply_engine_settings(dialog->chosen);
        const auto failure = save_engine_settings(dialog->opened, dialog->chosen, dialog->restored);
        state.last_page = dialog->page;
        state.last_developer_list = dialog->developer;
        state.dialog.reset();
        if (failure)
            EngineSettingsState::report_failed_save(*this, *failure);
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
