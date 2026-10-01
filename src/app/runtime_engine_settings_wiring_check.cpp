// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// --check-engine-settings, the settings' part: each setting takes effect,
// in step with its console command and the command line.

#include "engine_settings_state.hpp"

#include "oa/app/runtime.hpp"
#include "oa/sim/messages.hpp"
#include "oa/ui/frontend_dialogs.hpp"

#include <SDL3/SDL.h>

#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <system_error>

namespace oa::app {

namespace {

namespace settings = oa::ui::engine_settings;

/// A file the check names as the preferences file's folder, so that saving fails.
constexpr std::string_view kBlockingFileName = "engine-settings-not-a-folder";

/// The zoom the check gives the battlefield before the Mouse wheel zoom setting turns off.
constexpr float kCheckZoom = 2.0F;

/// Twice the base path credit: Pathfinding cycles at 2x.
constexpr int32_t kDoubledPathNodes = settings::base_path_search_nodes * 2;

/// A unit limit above 3.1c's own highest.
constexpr uint16_t kCheckUnitLimit = 500;

/// A maximum frame rate below the default.
constexpr uint32_t kCheckFrameRate = 60;

/// The frame rate the check gives --max-fps.
constexpr uint32_t kCommandLineFrameRate = 90;

/// Throws when a condition fails.
///
/// @param ok the condition
/// @param what what failed
void require(bool ok, std::string_view what) {
    if (!ok)
        throw std::runtime_error("engine settings check: " + std::string(what));
}

/// Returns an Escape press.
///
/// @param repeat the press is a held key's repeat
/// @return the event
SDL_Event escape_press(bool repeat) {
    SDL_Event event{};
    event.type = SDL_EVENT_KEY_DOWN;
    event.key.key = SDLK_ESCAPE;
    event.key.scancode = SDL_SCANCODE_ESCAPE;
    event.key.down = true;
    event.key.repeat = repeat;
    return event;
}

} // namespace

void Runtime::check_engine_settings_wiring() {
    auto& state = engine_settings_state();
    if (state.dialog)
        (void)take_engine_settings_action(settings::DialogAction::cancelled);
    bool running = true;
    const auto send = [&](SDL_Event event) {
        handle_sdl_event(event, running);
        require(running, "an event ended the run");
    };

    // With --preferences-file, every default is the game's own behaviour on
    // every platform, and the installation's options are not read.
    const auto defaults = settings::default_settings(EngineSettingsState::inputs(*this));
    require(!defaults.escape_opens_menu, "Escape opens the menu by default with a named file");
    require(defaults.unit_limit == settings::default_unit_limit, "the unit limit's default moved");
    require(state.installation_ini.empty(), "the installation's totala.ini was read");

    // The main menu: a held Escape's repeats do not quit.
    if (screen_ == Screen::main_menu && !frame_owned_by_package()) {
        require(
            frontend_game().max_units_setting == engine_settings().unit_limit,
            "the next game's unit limit is not the setting's"
        );
        send(escape_press(true));
    }

    if (screen_ != Screen::match || !match_)
        start_benchmark_skirmish();
    if (match_paused_)
        resume_match_pause();
    auto chosen = engine_settings();
    const auto kept = chosen;

    // The locks a game played alone puts on the settings.
    auto locks = engine_settings_locks();
    require(
        locks.path_search == settings::Lock::in_game &&
            locks.unit_limit == settings::Lock::in_game && !locks.shared_game,
        "a game played alone does not lock Pathfinding cycles and Unit limit"
    );

    // Maximum frame rate: at once, unless --max-fps holds it for the run.
    if (!options_.max_frames_per_second_given) {
        chosen.max_frame_rate = kCheckFrameRate;
        apply_engine_settings(chosen);
        require(
            options_.max_frames_per_second == kCheckFrameRate &&
                frame_stats_notes().max_frames_per_second == kCheckFrameRate,
            "the maximum frame rate did not take effect"
        );
        options_.max_frames_per_second_given = true;
        options_.max_frames_per_second = kCommandLineFrameRate;
        chosen.max_frame_rate = settings::highest_frame_rate;
        apply_engine_settings(chosen);
        require(
            options_.max_frames_per_second == kCommandLineFrameRate,
            "the setting changed the frame rate --max-fps gave"
        );
        require(
            engine_settings_locks().max_frame_rate == settings::Lock::command_line,
            "--max-fps does not lock the maximum frame rate"
        );
        options_.max_frames_per_second_given = false;
        apply_engine_settings(chosen);
        require(
            options_.max_frames_per_second == settings::highest_frame_rate,
            "the maximum frame rate did not go back"
        );
    }

    // Enhanced anti-aliasing sets how finely units are drawn.
    for (const auto level : settings::anti_aliasing_levels) {
        chosen.anti_aliasing = level;
        apply_engine_settings(chosen);
        require(
            oa::present::model::supersampling_factor(unit_supersampling_) ==
                static_cast<uint32_t>(level),
            "enhanced anti-aliasing did not set the units' drawing"
        );
    }
    chosen.anti_aliasing = settings::AntiAliasing::off;
    apply_engine_settings(chosen);

    // Show performance statistics is the +stats panel.
    chosen.frame_stats = true;
    apply_engine_settings(chosen);
    require(frame_stats_shown_, "Show performance statistics did not show the panel");
    chosen.frame_stats = false;
    apply_engine_settings(chosen);
    require(!frame_stats_shown_, "Show performance statistics did not hide the panel");

    // Select groups without Alt is 3.1c's SwitchAlt, in the match and in the
    // preferences, and saved under its own key.
    const auto switch_alt_on = [&] {
        return (match_->state().game.graphics_flags & init::preference_flags::switch_alt) != 0 &&
               (preferences_.graphics_flags & init::preference_flags::switch_alt) != 0;
    };
    auto opened = engine_settings();
    chosen.switch_alt = true;
    apply_engine_settings(chosen);
    require(
        switch_alt_on() && engine_settings().switch_alt, "SwitchAlt did not follow the setting"
    );
    require(!save_engine_settings(opened, chosen, false), "the settings were not saved");
    require(saved_general_number("SwitchAlt") == 1, "SwitchAlt was not saved");
    opened = chosen;
    chosen.switch_alt = false;
    apply_engine_settings(chosen);
    require(!save_engine_settings(opened, chosen, false), "the settings were not saved");
    require(
        !switch_alt_on() && saved_general_number("SwitchAlt") == 0,
        "SwitchAlt was not cleared and saved"
    );

    // Mouse wheel zoom off: the wheel does nothing and the battlefield eases
    // back to its own scale about its centre; Cancel brings the zoom back.
    chosen.wheel_zoom = true;
    apply_engine_settings(chosen);
    match_zoom_ = kCheckZoom;
    match_zoom_target_ = kCheckZoom;
    zoom_anchored_ = false;
    {
        auto& dialog = open_engine_settings_dialog();
        dialog.chosen.wheel_zoom = false;
        (void)take_engine_settings_action(settings::DialogAction::changed);
    }
    require(
        match_zoom_target_ == kDefaultBattlefieldZoom && zoom_anchored_ &&
            zoom_anchor_sx_ == match_layout_.battlefield_width() / 2 &&
            zoom_anchor_sy_ == match_layout_.battlefield_height() / 2,
        "turning the wheel zoom off did not ease to 1x about the centre"
    );
    SDL_Event wheel{};
    wheel.type = SDL_EVENT_MOUSE_WHEEL;
    wheel.wheel.y = 1.0F;
    SDL_RenderCoordinatesToWindow(
        sdl_.renderer,
        static_cast<float>(match_layout_.left + match_layout_.battlefield_width() / 2),
        static_cast<float>(match_layout_.top + match_layout_.battlefield_height() / 2),
        &wheel.wheel.mouse_x,
        &wheel.wheel.mouse_y
    );
    send(wheel);
    require(match_zoom_target_ == kDefaultBattlefieldZoom, "the wheel zoomed with the setting off");
    require(
        take_engine_settings_action(settings::DialogAction::cancelled) && !engine_settings_dialog(),
        "Cancel did not close the dialog"
    );
    require(
        engine_settings().wheel_zoom && match_zoom_target_ == kCheckZoom,
        "Cancel did not bring the wheel zoom and the zoom back"
    );
    send(wheel);
    require(match_zoom_target_ > kCheckZoom, "the wheel did not zoom with the setting on");
    chosen = engine_settings();

    // Escape opens the game menu: off, Escape only clears the selection;
    // on, the first press cancels or clears and the next opens the menu.
    uint16_t commander = 0;
    uint16_t commander_type = 0;
    for (const auto& slot : match_->world().slots)
        if (commander == 0 && slot.unit != nullptr && slot.record.type_index != 0 &&
            slot.record.owner_index == match_local_player_) {
            commander = slot.unit_index;
            commander_type = slot.record.type_index;
        }
    require(commander != 0, "no local unit to select");
    chosen.escape_opens_menu = false;
    apply_engine_settings(chosen);
    clear_local_selection();
    send(escape_press(false));
    require(!match_paused_, "Escape opened the menu with the setting off");
    chosen.escape_opens_menu = true;
    apply_engine_settings(chosen);
    adopt_selection(commander);
    match_command_ = MatchCommand::move;
    send(escape_press(false));
    require(
        !match_paused_ && match_command_ == MatchCommand::none && has_local_selection(),
        "Escape did not first cancel the armed order"
    );
    send(escape_press(false));
    require(!match_paused_ && !has_local_selection(), "Escape did not clear the selection next");
    send(escape_press(false));
    require(ingame_menu_column_shown(), "Escape with nothing to clear did not open the menu");
    send(escape_press(true));
    require(match_paused_, "a held Escape's repeat closed the menu");
    send(escape_press(false));
    require(!match_paused_, "Escape did not close the menu it opened");
    open_chat_line();
    send(escape_press(false));
    require(!chat_composing_ && !match_paused_, "Escape did not close the chat line first");
    // The unit info panel closes first, then a pending build is cancelled,
    // and both keep the selection.
    adopt_selection(commander);
    match_->state().game.cursor_unit_id = commander;
    const bool info_opened = open_unit_info() && unit_info_panel_;
    match_->state().game.cursor_unit_id = 0;
    require(info_opened, "F1 did not open the unit info panel");
    send(escape_press(false));
    require(
        !unit_info_panel_ && has_local_selection() && !match_paused_,
        "Escape did not close the unit info panel first"
    );
    pending_build_type_ = commander_type;
    send(escape_press(false));
    require(
        pending_build_type_ == 0 && has_local_selection() && !match_paused_,
        "Escape did not cancel the pending build before the selection"
    );
    send(escape_press(false));
    require(!has_local_selection() && !match_paused_, "Escape did not clear the selection");
    // In a shared game the menu Escape opens leaves the game running.
    {
        const Extension kept_extension = extension_;
        extension_.state = [](void*, const Runtime&) -> uint32_t {
            return extension_state::multiplayer | extension_state::shared_match;
        };
        send(escape_press(false));
        const bool opened = ingame_menu_column_shown();
        const bool running = match_clock_steps();
        send(escape_press(false));
        const bool closed = !ingame_menu_column_shown();
        extension_ = kept_extension;
        require(opened, "Escape with nothing to clear did not open the menu in a shared game");
        require(running, "the menu Escape opened stopped a shared game");
        require(closed, "Escape did not close the menu in a shared game");
        if (match_paused_)
            resume_match_pause();
    }
    chosen.escape_opens_menu = false;
    apply_engine_settings(chosen);

    // A failed save says so in the message log, and the dialog closes.
    {
        const fs::path kept_path = preference_path_;
        const fs::path blocking = fs::current_path() / kBlockingFileName;
        std::ofstream(blocking) << "not a folder\n";
        preference_path_ = blocking / "preferences.conf";
        auto& game = match_->state().game;
        oa::sim::messages::clear_messages(game);
        auto& dialog = open_engine_settings_dialog();
        dialog.chosen.switch_alt = !dialog.opened.switch_alt;
        const bool closed = take_engine_settings_action(settings::DialogAction::accepted);
        preference_path_ = kept_path;
        std::error_code removed;
        fs::remove(blocking, removed);
        bool reported = false;
        for (int32_t index = 0; index < oa::sim::messages::line_capacity(game); ++index) {
            const auto* line = oa::sim::messages::message_line(game, static_cast<uint32_t>(index));
            reported = reported || (line != nullptr &&
                                    std::string_view(line->text) == "Settings were not saved.");
        }
        require(closed && !engine_settings_dialog(), "a failed save left the dialog open");
        require(reported, "a failed save was not reported in the message log");
        oa::sim::messages::clear_messages(game);
        // The kept choice is saved once the file can be written again.
        opened = engine_settings();
        chosen = opened;
        chosen.switch_alt = kept.switch_alt;
        apply_engine_settings(chosen);
        require(!save_engine_settings(opened, chosen, false), "the settings were not saved");
        require(
            saved_general_number("SwitchAlt") == (kept.switch_alt ? 1 : 0),
            "SwitchAlt was not saved back"
        );
    }

    // Pathfinding cycles and Unit limit apply from the next game: the
    // running one keeps its own.
    const int32_t running_credit = match_->path_search_jobs().tick_credit;
    chosen.path_search_nodes = kDoubledPathNodes;
    chosen.unit_limit = kCheckUnitLimit;
    apply_engine_settings(chosen);
    require(
        match_->path_search_jobs().tick_credit == running_credit,
        "Pathfinding cycles changed the running game"
    );
    require(
        frontend_game().max_units_setting == kCheckUnitLimit,
        "the unit limit did not set the next game's"
    );
    apply_skirmish_players();
    require(match_ && !altitude_sight_blocked_, "the next skirmish did not start");
    enter_match_view();
    require(
        match_->path_search_jobs().tick_credit == kDoubledPathNodes,
        "a game played alone did not take the Pathfinding cycles"
    );
    require(
        match_->state().game.max_units_setting == kCheckUnitLimit &&
            EngineSettingsState::restart_unit_limit(*this) == kCheckUnitLimit,
        "the next skirmish did not take the unit limit"
    );
    // A match that turns out to be a replay or shared plays at the base credit.
    EngineSettingsState::hold_path_credit(*this, extension_state::replay);
    require(
        match_->path_search_jobs().tick_credit == settings::base_path_search_nodes,
        "a replay did not play at the base path credit"
    );
    EngineSettingsState::hold_path_credit(*this, 0);
    require(
        match_->path_search_jobs().tick_credit == settings::base_path_search_nodes,
        "a replay left the base path credit"
    );
    EngineSettingsState::start_path_credit(*this, true);
    require(
        match_->path_search_jobs().tick_credit == settings::base_path_search_nodes,
        "a shared game did not play at the base path credit"
    );

    // Back to the settings the check found, in a skirmish at their limit.
    apply_engine_settings(kept);
    frontend_game().max_units_setting = kept.unit_limit;
    apply_skirmish_players();
    require(match_ && !altitude_sight_blocked_, "the last skirmish did not start");
    enter_match_view();
    require(
        match_->path_search_jobs().tick_credit == kept.path_search_nodes &&
            match_->state().game.max_units_setting == kept.unit_limit,
        "the skirmish did not go back to the settings"
    );
    std::cout << "engine settings check: the maximum frame rate (and --max-fps over it), "
                 "enhanced anti-aliasing, the performance statistics, SwitchAlt, the wheel zoom "
                 "with Cancel, Escape's order, a failed save, and the path credit and unit "
                 "limit of the next game take effect\n";
}

} // namespace oa::app
