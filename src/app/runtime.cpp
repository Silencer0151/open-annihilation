// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Runtime construction, main loop and screen loading.
#include "oa/app/runtime.hpp"
#include "oa/app/asset_files.hpp"
#include "oa/app/game_directory.hpp"
#include "match_clock.hpp"
#include "oa/data/defs/version.hpp"
#include "oa/platform/app_loop.hpp"
#include "oa/platform/log_files.hpp"
#include "oa/ui/frontend_dialogs.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::app {

namespace {

// Names a built-in screen in the headless check's output.
[[nodiscard]] std::string_view screen_label(Screen screen) {
    switch (screen) {
    case Screen::main_menu:
        return "main menu";
    case Screen::single_player:
        return "single player";
    case Screen::skirmish:
        return "skirmish";
    case Screen::map_selection:
        return "map selection";
    case Screen::loading:
        return "loading";
    case Screen::match:
        return "match";
    case Screen::options:
        return "options";
    case Screen::sound:
        return "sound";
    case Screen::visuals:
        return "visuals";
    case Screen::speeds:
        return "speeds";
    case Screen::music:
        return "music";
    case Screen::new_campaign:
        return "new campaign";
    case Screen::any_mission:
        return "any mission";
    case Screen::load_game:
        return "load game";
    case Screen::campaign_end:
        return "campaign end";
    case Screen::briefing:
        return "briefing";
    }
    return "unnamed screen";
}

} // namespace

Runtime::Runtime(
    Options options,
    oa::AssetStore& assets,
    const Extension& extension,
    SDL_Window* window,
    SDL_Renderer* renderer
)
    : options_(std::move(options)), assets_(assets), extension_(extension),
      unit_sound_catalog_(oa::audio::game_audio::UnitSoundCatalog::load(assets)),
      audio_player_(assets), offline_effects_(effect_boundary_, effect_boundary_) {
    if (extension_.frontend_game == nullptr)
        frontend_game_ = std::make_unique<oa::Game>();
    if (window != nullptr && renderer != nullptr) {
        sdl_.window = window;
        sdl_.renderer = renderer;
        sdl_.borrowed = true;
    }
    start_session_display();
    choose_web_links();
    if (extension_.startup != nullptr)
        extension_.startup(extension_.context, *this);
    load_all_sounds();
    register_screens();
    if (extension_.ready != nullptr)
        extension_.ready(extension_.context, *this);
    load_preference_file();
    // Stored after the one-time legacy import, which runs only while no
    // preferences file exists. An unwritable preferences file only costs the
    // next start the dialog.
    if (!options_.remember_game_dir.empty()) {
        remember_game_directory(preference_values_, options_.remember_game_dir);
        try {
            oa::platform::preferences::save(preference_path_, preference_values_);
        } catch (const std::exception& error) {
            std::cerr << "open-annihilation: the chosen game directory is not remembered: "
                      << error.what() << '\n';
        }
    }
    load_logo_textures();
    discover_first_map();
    load_side_table();
    load_translations(oa::app::command_line::launch_language(options_.launch));
    load_common_fonts();
    init::reset_player_slots(state_, player_storage_, false);
    init::load_preferences(state_, skirmish_settings_, preferences_, *this);
    // Session start sets the display gamma from the saved Gamma.
    apply_saved_gamma();
    audio_player_.set_volume(wave_volume_, preferences_.fx_volume);
    state_.state = frontend::state_id::main_menu;
    state_.signal = frontend::signal_id::initialize;
    state_.pending_signal = frontend::signal_id::initialize;
    state_.video_context_flags = frontend::flags::fullscreen_mode;
    environment_.messages_enabled = 1;
    environment_.message_target.value = kMessageTargetHandle;
    // Fields the game switches set before the dispatcher first runs; the
    // extension then names the states it runs in place of the engine's.
    state_.skip_intro = options_.launch.skip_intro;
    if (extension_.frontend_states != nullptr)
        extension_.frontend_states(extension_.context, frontend_states_);
    step(frontend::Step::reload_unit_overrides, state_);
    frontend::dispatch(state_, *this, frontend_states_);
}

int Runtime::run() {
    int exit_code = 0;
    const auto extension_run = [&](RunPhase phase) {
        return extension_.run_mode != nullptr &&
               extension_.run_mode(extension_.context, *this, phase, exit_code);
    };
    if (extension_run(RunPhase::start))
        return exit_code;
    if (options_.headless_check) {
        // The director's runs come before the extension's headless runs,
        // which never see them; an extension takes part only through
        // open_recording.
        if (!options_.generate_script.empty()) {
            const int status = run_generate_script();
            flush_preferences();
            return status;
        }
        if (!options_.render_script.empty()) {
            const int status = run_render_script();
            flush_preferences();
            return status;
        }
        if (options_.check_director_view) {
            check_director_view();
            flush_preferences();
            return 0;
        }
        if (options_.check_director_render) {
            check_director_render();
            flush_preferences();
            return 0;
        }
        if (options_.check_interpolation) {
            check_interpolation();
            flush_preferences();
            return 0;
        }
        if (extension_run(RunPhase::headless_first))
            return exit_code;
        if (options_.check_navigation)
            check_navigation();
        if (extension_run(RunPhase::headless))
            return exit_code;
        if (options_.save_after || !options_.load_file.empty()) {
            run_headless_saveload();
            flush_preferences();
            return 0;
        }
        if (options_.campaign_mission) {
            const auto result =
                run_headless_campaign(options_.match_ticks.value_or(kDefaultCampaignTicks));
            flush_preferences();
            return result;
        }
        if (options_.match_ticks) {
            if (options_.frame_rate)
                run_headless_frames(*options_.match_ticks, *options_.frame_rate);
            else
                run_headless_match(*options_.match_ticks);
            flush_preferences();
            return 0;
        }
        if (options_.skip_intro && !options_.snapshot.empty())
            write_ppm(options_.snapshot, surface_);
        std::cout << "native check: " << surface_.width << 'x' << surface_.height << ", "
                  << resources_.layout.gadgets.size() << " GUI gadgets, " << audio_registry_.size()
                  << " registered sounds\n";
        std::cout << "native check: " << screen_label(screen_) << " open, "
                  << ui::frontend_dialogs::dialog_count() << " dialogs over it\n";
        std::cout << "native check: the game offers " << (offers_movies() ? "" : "no ")
                  << "movies\n";
        flush_preferences();
        return 0;
    }
    initialize_sdl();
    if (options_.check_match_dialogs) {
        check_match_dialogs();
        flush_preferences();
        return 0;
    }
    if (options_.check_load_save) {
        check_load_save();
        flush_preferences();
        return 0;
    }
    if (options_.check_frontend_controls) {
        check_frontend_controls();
        flush_preferences();
        return 0;
    }
    if (options_.check_scroll_bars) {
        check_scroll_bars();
        flush_preferences();
        return 0;
    }
    if (options_.check_briefing_narration) {
        check_briefing_narration();
        flush_preferences();
        return 0;
    }
    if (options_.check_match_layers) {
        check_match_layers();
        flush_preferences();
        return 0;
    }
    if (options_.check_match_orders) {
        check_match_orders();
        flush_preferences();
        return 0;
    }
    if (options_.check_factory_orders) {
        check_factory_orders();
        flush_preferences();
        return 0;
    }
    if (options_.check_download_builds) {
        check_download_builds();
        flush_preferences();
        return 0;
    }
    if (options_.check_kill_board) {
        check_kill_board();
        flush_preferences();
        return 0;
    }
    if (options_.check_patrol_reclaim) {
        check_patrol_reclaim();
        flush_preferences();
        return 0;
    }
    if (options_.check_reclaim_cursor) {
        check_reclaim_cursor();
        flush_preferences();
        return 0;
    }
    if (options_.check_pointer_interfaces) {
        check_pointer_interfaces();
        flush_preferences();
        return 0;
    }
    if (options_.check_multiplayer_menu) {
        check_multiplayer_menu();
        flush_preferences();
        return 0;
    }
    if (options_.benchmark_frames) {
        run_benchmark(*options_.benchmark_frames);
        flush_preferences();
        return 0;
    }
    // The main menu checks Revision.GPF once, as it first opens.
    const oa::data::defs::Files files = asset_files(assets_);
    if (oa::data::defs::revision_gpf_mismatch(&files, nullptr))
        std::cerr << "warning: gamedata/version.tdf does not name Revision.GPF "
                  << oa::data::defs::expected_gpf_version
                  << "; the game data may not match this build\n";
    if (extension_.start_scene == nullptr || !extension_.start_scene(extension_.context, *this))
        start_menu_music();
    if (options_.showcase != Showcase::none)
        run_showcase();
    std::size_t frames = 0;
    // A showcase has played the whole run; the loop does not start.
    bool running = options_.showcase == Showcase::none;
    while (running && !exit_requested_ &&
           (!options_.frame_limit || frames < *options_.frame_limit)) {
        park_music_while_inactive();
        SDL_Event event{};
        // A frame-limited run is scripted and must finish without focus.
        const bool live = keeps_running_inactive();
        if (!options_.frame_limit &&
            oa::platform::application_waits_for_events(application_active_, live, false)) {
            if (SDL_WaitEvent(&event))
                dispatch_event(event, running);
            oa::platform::log_files::maintain();
            continue;
        }
        run_frame(running);
        ++frames;
        pace_next_frame(running);
    }
    if (video_capture_) {
        video_capture_->finish();
        video_capture_.reset();
    }
    if (extension_.shutdown != nullptr)
        extension_.shutdown(extension_.context, *this);
    flush_preferences();
    return exit_status_;
}

void Runtime::take_video_capture(std::unique_ptr<VideoCapture> capture) {
    video_capture_ = std::move(capture);
}

void Runtime::run_frame(bool& running) {
    begin_loop_frame();
    SDL_Event event{};
    while (SDL_PollEvent(&event))
        dispatch_event(event, running);
    idle_tick();
    const auto now_ms = static_cast<uint32_t>(SDL_GetTicks());
    if (oa::platform::finished_stream_sweep_due(now_ms, last_stream_sweep_ms_)) {
        audio_player_.collect_finished();
        last_stream_sweep_ms_ = now_ms;
    }
    oa::platform::log_files::maintain();
}

void Runtime::dispatch_event(SDL_Event& event, bool& running) {
    note_window_activation(event);
    note_input_activity(event);
    // Alt+Enter switches between full screen and a window on every screen,
    // before the screen or a screen package sees the key; its repeats reach
    // no screen either, so a held Alt+Enter never opens the chat line or
    // presses a dialog's default button.
    if (take_full_screen_event(event))
        return;
    if (!dispatch_screen_input(event))
        handle_sdl_event(event, running);
    apply_screen_request();
}

void Runtime::idle_tick() {
    take_frame_time();
    camera_moved_ = false;
    tick_screen_packages();
    step_music();
    present_unit_announcements();
    if (exit_requested_)
        return;
    if (screen_ == Screen::briefing && !briefing_from_pause_)
        tick_mission_briefing();
    move_match_camera();
    // Each game frame opens a profile window, and the pump, the
    // ticks and the drawing are charged as they end.
    const bool profiled = screen_ == Screen::match && match_;
    if (profiled)
        begin_profile_window();
    if (extension_.frame != nullptr)
        extension_.frame(extension_.context, *this, FrameStage::pump);
    if (profiled)
        mark_profile(OA_PROFILE_SYNC);
    if (extension_.frame != nullptr)
        extension_.frame(extension_.context, *this, FrameStage::after_pump);
    // The extension may have asked to end the run (ScreenServices::quit):
    // it ends here, leaving the match first, and nothing more of the frame
    // runs.
    finish_quit_request();
    if (exit_requested_)
        return;
    // The frame's pointer pass picks the unit under the still pointer too,
    // before the ticks, so a unit that moves under it becomes the cursor unit.
    if (screen_ == Screen::match && match_ && !match_paused_ && !match_finished_)
        pick_cursor_unit(false);
    // A held scroll bar or arrow moves its knob in the frame's update.
    tick_scroll_bars();
    step_match_frame();
    if (screen_ == Screen::match && match_)
        present_match_outcome();
    // The frame is drawn between the ticks at the fraction the clock step
    // chose; whatever draws after it shows whole ticks.
    frame_draws_.units_drawn = 0;
    frame_draws_.units_between_ticks = 0;
    frame_draws_.probe_drawn = false;
    render();
    presentation_alpha_ = 1.0F;
    capture_film_frame();
    if (profiled && match_)
        mark_profile(OA_PROFILE_RENDER_STATIC);
    // The frame just drawn showed the outcome's title; the end screen follows
    // on its own.
    if (screen_ == Screen::match && match_finished_)
        finish_match_outcome();
}

void Runtime::move_match_camera() {
    if (screen_ == Screen::match && selected_tnt_)
        step_match_zoom();
    if (screen_ != Screen::match || match_paused_)
        return;
    pan_match_camera();
    if (match_)
        follow_match_camera_unit();
    if (match_tracking_) {
        if (!match_unit_present(tracked_match_unit_))
            stop_match_tracking();
        else
            center_camera_on_unit(tracked_match_unit_);
    }
}

uint32_t Runtime::clock_milliseconds() const {
    if (options_.fixed_clock)
        return match_timing_.tick * kFixedClockMsPerTick;
    return static_cast<uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
                                     std::chrono::steady_clock::now().time_since_epoch()
    )
                                     .count());
}

bool Runtime::match_running() const {
    return match_ && screen_ == Screen::match;
}

bool Runtime::match_clock_steps() const {
    if (!match_running() || match_tick_blocked_)
        return false;
    const bool shared = (current_extension_state() & extension_state::shared_match) != 0;
    return match_clock_runs(shared, match_paused_ && !match_finished_, match_finished_);
}

oa::base::game_loop::Timing Runtime::saved_match_timing() const {
    auto timing = match_timing_;
    if (match_)
        timing.flags = clock_flags_with_pause(timing.flags, match_->state().game.sim_run_flags);
    return timing;
}

void Runtime::advance_match_clock(uint32_t now_ms) {
    match_timing_.flags =
        clock_flags_with_pause(match_timing_.flags, match_->state().game.sim_run_flags);
    oa::base::game_loop::update_timing(
        match_timing_, oa::base::game_loop::scaled_clock(now_ms, match_clock_scale())
    );
    try {
        for (int32_t step = 0; step < match_timing_.pending_steps; ++step) {
            // Each step that runs a tick is timed for the frame statistics,
            // on the real clock; a step the extension holds runs none.
            const auto step_start = std::chrono::steady_clock::now();
            const uint32_t tick_before = match_timing_.tick;
            if (extension_.simulation_step == nullptr ||
                !extension_.simulation_step(extension_.context, *this)) {
                ++match_timing_.tick;
                match_->simulation().tick = match_timing_.tick;
                match_->tick();
            }
            if (match_timing_.tick != tick_before)
                frame_pacing::note_frame_measure(
                    frame_stats_,
                    frame_pacing::FrameMeasure::tick,
                    static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
                                              std::chrono::steady_clock::now() - step_start
                    )
                                              .count())
                );
        }
    } catch (const std::exception& error) {
        report_match_tick_error(error.what());
    }
    if (match_timing_.pending_steps != 0)
        oa::sim::messages::expire_oldest_message(match_->state().game);
}

void Runtime::report_match_tick_error(std::string_view message) {
    status_ = "match tick " + std::to_string(match_timing_.tick) + ": " + std::string(message);
    if (message == last_tick_error_) {
        ++tick_error_repeats_;
        if ((tick_error_repeats_ & (tick_error_repeats_ - 1)) != 0)
            return;
    } else {
        last_tick_error_ = message;
        tick_error_repeats_ = 1;
    }
    const auto line =
        status_ + (tick_error_repeats_ > 1 ? " (x" + std::to_string(tick_error_repeats_) + ")"
                                           : std::string());
    std::cerr << "simulation error: " << line << '\n';
    console_post_message("Simulation error: " + line);
}

void Runtime::present_unit_announcements() {
    for (auto& event : offline_services_.pump_announcements()) {
        if (event.sound_resource)
            play_wave_file(*event.sound_resource);
        if (event.text)
            post_unit_report(event.unit_index, *event.text);
    }
}

uint32_t Runtime::match_clock_scale() const {
    // 30 clock units per real second. Game speed 10 * 0.1 rate = 30 Hz.
    // +/- changes actual_rate only; folding speed into the clock scale as well
    // made speed 20 run at 4x instead of 2x.
    return 30u;
}

uint32_t Runtime::frontend_tick() const {
    if (fake_frontend_tick_)
        return *fake_frontend_tick_;
    return oa::base::game_loop::scaled_clock(
        static_cast<uint32_t>(SDL_GetTicks()), match_clock_scale()
    );
}

bool Runtime::frame_owned_by_package() const {
    return screen_ == Screen::main_menu && state_.state == frontend::state_id::pump_only;
}

uint32_t Runtime::current_extension_state() const {
    return extension_.state != nullptr ? extension_.state(extension_.context, *this) : 0;
}

void Runtime::load_all_sounds() {
    const oa::data::defs::Files files = asset_files(assets_);
    const oa::data::defs::SoundCache cache{
        &audio_registry_,
        [](void* context) { static_cast<oa::audio::game_audio::Registry*>(context)->clear(); },
        [](void* context, const char* name, const char* sound) {
            static_cast<oa::audio::game_audio::Registry*>(context)->add(name, sound);
        },
    };
    oa::data::defs::sound_category_table_free(&unit_table_.sound_categories);
    oa::data::defs::load_all_sounds(&files, nullptr, cache, &unit_table_.sound_categories);
}

void Runtime::load(Screen screen) {
    // The load and save dialogs are drawn over the screen they open from.
    if (screen == Screen::load_game && screen_ != Screen::load_game)
        capture_load_game_parent();
    // Screens over or after the match read the options the match changed.
    if (screen_ == Screen::match && match_)
        take_match_options(match_->state().game);
    if (const auto* previous = screen_find(&screens_, screen_id(screen_));
        previous != nullptr && previous->leave != nullptr) {
        auto context = screen_context();
        previous->leave(&context, previous->state);
    }
    screen_ = screen;
    widget_gaf_frames_.clear();
    widget_text_stages_.clear();
    widget_sprites_.clear();
    typed_key_hook_ = TypedKeyHook::none;
    typed_keys_.fill(0);
    const auto* desc = screen_find(&screens_, screen_id(screen));
    if (desc == nullptr)
        desc = screen_find(&screens_, screen_id(Screen::map_selection));
    // A screen without a panel of its own has no scroll bars.
    frontend_scrolls_layout_ = nullptr;
    auto context = screen_context();
    if (desc->assets.layout != nullptr) {
        const char* background = desc->background != nullptr
                                     ? desc->background(&context, desc->state)
                                     : desc->assets.background;
        const renderer::ScreenAssetNames names{
            desc->assets.layout,
            "",
            desc->assets.palette,
            desc->assets.sprites,
            desc->assets.shared_sprites
        };
        resources_ = renderer::load_screen(assets_, names);
        // A panel's first draw binds its scroll bars: at once for a panel
        // drawn as it loads, after its setup for NEWGAME.GUI, which loads
        // undrawn and is set up first.
        if (!first_draw_after_setup(screen))
            bind_frontend_scrolls(names.layout, names.sprites);
        // The panel is up first; its setup then asks for the named background.
        if (background != nullptr)
            (void)load_named_background(background, false, false, false);
    }
    if (desc->enter != nullptr)
        desc->enter(&context, desc->state);
    if (desc->assets.layout != nullptr && first_draw_after_setup(screen))
        bind_set_up_frontend_scrolls(desc->assets.layout, desc->assets.sprites);
    selected_ = -1;
    hovered_.reset();
    apply_output_mode();
    rebuild_surface();
}

void Runtime::rebuild_surface() {
    if (screen_ == Screen::match) {
        refresh_filtered_terrain();
        render_match_surface();
        draw_screen_packages();
        tick_and_draw_cursor();
        return;
    }
    if (screen_ == Screen::loading) {
        draw_loading_screen();
        draw_screen_packages();
        tick_and_draw_cursor();
        return;
    }
    if (frame_owned_by_package()) {
        if (surface_.width != kCanvasWidth || surface_.height != kCanvasHeight) {
            surface_.width = kCanvasWidth;
            surface_.height = kCanvasHeight;
            surface_.rgb.assign(static_cast<std::size_t>(kCanvasWidth) * kCanvasHeight * 3U, 0);
        }
        draw_screen_packages();
        tick_and_draw_cursor();
        return;
    }
    if (draw_end_screen_battlefield()) {
        draw_screen_packages();
        return;
    }
    // A panel whose setup has not yet asked for its named background keeps
    // the frame shown until it does.
    if (resources_.background.width == 0 || resources_.background.height == 0)
        return;
    std::vector<renderer::ButtonPresentation> presentation;
    presentation.reserve(resources_.layout.gadgets.size());
    for (std::size_t index = 0; index < resources_.layout.gadgets.size(); ++index) {
        auto condition = renderer::ButtonCondition::normal;
        // A button whose status is set (the chosen member of its group) shows
        // pressed like one pressed and held with the pointer still over it.
        // The pointer over a button without a press leaves it as it is.
        const auto* button =
            std::get_if<oa::ui::gui_layout::ButtonFields>(&resources_.layout.gadgets[index].fields);
        if (button != nullptr && button->grayed_out)
            condition = renderer::ButtonCondition::disabled;
        else if (
            (selected_ == static_cast<int32_t>(index) && hovered_ == index) ||
            (button != nullptr && button->status != 0)
        )
            condition = renderer::ButtonCondition::pressed;
        const auto& name = resources_.layout.gadgets[index].common.name;
        const auto frame = widget_gaf_frames_.find(name);
        const auto text_stage = widget_text_stages_.find(name);
        const auto sprite = widget_sprites_.find(name);
        presentation.push_back(
            {name,
             condition,
             frame == widget_gaf_frames_.end() ? std::nullopt
                                               : std::optional<std::size_t>(frame->second),
             text_stage == widget_text_stages_.end()
                 ? std::nullopt
                 : std::optional<std::size_t>(text_stage->second),
             sprite == widget_sprites_.end()
                 ? std::nullopt
                 : std::optional<renderer::SpriteOverride>(sprite->second)}
        );
    }
    std::vector<renderer::ListPresentation> lists;
    if (screen_ == Screen::map_selection && !bound_map_names_.empty()) {
        const auto selected = static_cast<std::size_t>(std::max<int16_t>(0, modal_map_index_));
        lists.push_back({"MAPNAMES", bound_map_names_, map_first_visible(), selected});
    }
    // A list its scroll bar scrolls shows the rows the bar brings into view.
    if (const auto first = frontend_list_first("Campaign"))
        campaign_first_visible_ = *first;
    if (const auto first = frontend_list_first("Missions"))
        campaign_mission_first_visible_ = *first;
    if (screen_ == Screen::any_mission || screen_ == Screen::new_campaign) {
        if (!campaign_labels_.empty())
            lists.push_back(
                {"Campaign", campaign_labels_, campaign_first_visible_, selected_campaign_index_}
            );
        if (screen_ == Screen::any_mission && !campaign_mission_labels_.empty())
            lists.push_back(
                {"Missions",
                 campaign_mission_labels_,
                 campaign_mission_first_visible_,
                 selected_mission_index_}
            );
    }
    if (screen_ == Screen::campaign_end && !end_mission_rows_.empty()) {
        lists.push_back(
            {"Missions",
             end_mission_rows_,
             campaign_mission_first_visible_,
             selected_mission_index_}
        );
    }
    if (screen_ == Screen::load_game)
        present_load_game_panel(lists);
    surface_ = renderer::render_screen(resources_, presentation, lists);
    if (auto* scrolls = frontend_scrolls()) {
        renderer::refresh_layout_scrolls(*scrolls, resources_.layout);
        renderer::draw_layout_scrolls(
            surface_,
            renderer::grayed_paint(resources_, frontend_gray_table_),
            frontend_scrolls_own_art_ ? &resources_.sprites : nullptr,
            resources_.shared_sprites,
            *scrolls,
            0,
            0
        );
    }
    if (screen_ == Screen::main_menu) {
        if (menu_sparks_.dest.empty())
            renderer::reset_menu_sparks(menu_sparks_, resources_.background);
        renderer::step_menu_sparks(menu_sparks_, surface_, resources_.background);
    } else {
        menu_sparks_ = {};
    }
    if (screen_ == Screen::briefing)
        draw_briefing_overlays();
    if (screen_ == Screen::map_selection && !preview_rgb_.empty() && preview_width_ > 0 &&
        preview_height_ > 0) {
        const auto* target = widget("MAPPIC");
        if (target != nullptr && preview_destination_width_ > 0 &&
            preview_destination_height_ > 0) {
            // The picture drawer clears the gadget-sized picture to index 0 before the
            // fitted map is drawn into it.
            for (int y = 0; y < target->common.height; ++y)
                for (int x = 0; x < target->common.width; ++x) {
                    const int destination_x = target->common.x + x;
                    const int destination_y = target->common.y + y;
                    if (destination_x < 0 || destination_y < 0 ||
                        destination_x >= static_cast<int>(surface_.width) ||
                        destination_y >= static_cast<int>(surface_.height))
                        continue;
                    std::copy_n(
                        preview_clear_rgb_.begin(),
                        3,
                        surface_.rgb.begin() +
                            static_cast<std::ptrdiff_t>(
                                (static_cast<std::size_t>(destination_y) * surface_.width +
                                 static_cast<std::size_t>(destination_x)) *
                                3U
                            )
                    );
                }
            for (int y = 0; y < preview_destination_height_; ++y) {
                const auto source_y = static_cast<std::size_t>(y) * preview_source_height_ /
                                      static_cast<std::size_t>(preview_destination_height_);
                for (int x = 0; x < preview_destination_width_; ++x) {
                    const int destination_x = target->common.x + preview_destination_x_ + x;
                    const int destination_y = target->common.y + preview_destination_y_ + y;
                    if (destination_x < 0 || destination_y < 0 ||
                        destination_x >= static_cast<int>(surface_.width) ||
                        destination_y >= static_cast<int>(surface_.height))
                        continue;
                    const auto source_x = static_cast<std::size_t>(x) * preview_source_width_ /
                                          static_cast<std::size_t>(preview_destination_width_);
                    const auto source = (source_y * preview_width_ + source_x) * 3U;
                    const auto destination =
                        (static_cast<std::size_t>(destination_y) * surface_.width +
                         static_cast<std::size_t>(destination_x)) *
                        3U;
                    std::copy_n(
                        preview_rgb_.begin() + static_cast<std::ptrdiff_t>(source),
                        3,
                        surface_.rgb.begin() + static_cast<std::ptrdiff_t>(destination)
                    );
                }
            }
        }
    }
    if (screen_ == Screen::map_selection && modal_parent_surface_.width == kCanvasWidth &&
        modal_parent_surface_.height == kCanvasHeight) {
        auto composed = modal_parent_surface_;
        const auto& modal_root = resources_.layout.gadgets.front().common;
        const auto modal_width = static_cast<uint32_t>(modal_root.width);
        const auto modal_height = static_cast<uint32_t>(modal_root.height);
        const int offset_x = (kCanvasWidth - static_cast<int>(modal_width)) / 2;
        const int offset_y = (kCanvasHeight - static_cast<int>(modal_height)) / 2;
        for (uint32_t y = 0; y < modal_height; ++y) {
            for (uint32_t x = 0; x < modal_width; ++x) {
                const auto source = (static_cast<std::size_t>(y) * surface_.width + x) * 3U;
                const auto destination =
                    (static_cast<std::size_t>(offset_y + static_cast<int>(y)) * composed.width +
                     static_cast<std::size_t>(offset_x + static_cast<int>(x))) *
                    3U;
                std::copy_n(
                    surface_.rgb.begin() + static_cast<std::ptrdiff_t>(source),
                    3,
                    composed.rgb.begin() + static_cast<std::ptrdiff_t>(destination)
                );
            }
        }
        surface_ = std::move(composed);
    }
    if (screen_ == Screen::campaign_end)
        draw_campaign_end_title();
    if (panel_over_screen())
        compose_panel_over_parent();
    draw_screen_packages();
    if (!end_screen_hides_cursor())
        tick_and_draw_cursor();
}

} // namespace oa::app
