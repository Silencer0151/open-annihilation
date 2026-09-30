// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// SDL event dispatch, menu audio and movie playback.
#include "oa/app/runtime.hpp"
#include "oa/ui/frontend/main_menu.hpp"
#include "oa/media/intro_player.hpp"
#include <SDL3/SDL.h>
#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <variant>

namespace oa::app {

void Runtime::handle_sdl_event(SDL_Event& event, bool& running) {
    if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED) {
        // Closing the window, or the system's quit while there is one, in a
        // running match asks whether to surrender first, as in 3.1c; every
        // other screen ends the run at once.
        const bool asked = event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED || sdl_.window != nullptr;
        // The extension answers first; one that declines leaves the request
        // to the engine.
        if (asked && extension_.close_requested != nullptr &&
            extension_.close_requested(extension_.context, *this))
            return;
        // A page opened over the running match (its load and save pages, its
        // briefing) goes back to the match to ask there; the preferences a
        // match opens stay on it.
        if (asked && match_ && !match_finished_ &&
            (screen_ == Screen::match || return_to_match_for_close())) {
            request_match_close();
            return;
        }
        running = false;
        return;
    }
    // Entering or leaving full screen lays the screen out again at the size
    // the window ends at, which some window systems report only then.
    if (event.type == SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED ||
        event.type == SDL_EVENT_WINDOW_RESIZED || event.type == SDL_EVENT_WINDOW_ENTER_FULLSCREEN ||
        event.type == SDL_EVENT_WINDOW_LEAVE_FULLSCREEN) {
        apply_output_mode();
        if (screen_ == Screen::match && match_ && selected_tnt_)
            render_match_surface();
        return;
    }
    if (event.type == SDL_EVENT_TEXT_INPUT && chat_composing_) {
        chat_buffer_ += event.text.text;
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && handle_match_hotkey(event.key))
        return;
    if (event.type == SDL_EVENT_KEY_DOWN && typed_key_hook_ != TypedKeyHook::none) {
        const auto key = SDL_GetKeyFromScancode(event.key.scancode, event.key.mod, false);
        if (key >= 0x20 && key < 0x7f)
            record_typed_key(static_cast<uint8_t>(std::toupper(static_cast<int>(key))));
    }
    if (event.type == SDL_EVENT_KEY_DOWN && screen_ == Screen::campaign_end &&
        (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER)) {
        activate_end_panel_default();
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && screen_ == Screen::briefing &&
        (event.key.key == SDLK_RETURN || event.key.key == SDLK_KP_ENTER ||
         event.key.key == SDLK_ESCAPE) &&
        press_briefing_default(event.key.key == SDLK_ESCAPE))
        return;
    // Up and Down move the selection of NEWGAME.GUI's focused list.
    if (event.type == SDL_EVENT_KEY_DOWN &&
        (screen_ == Screen::new_campaign || screen_ == Screen::any_mission) &&
        (event.key.key == SDLK_UP || event.key.key == SDLK_DOWN)) {
        step_campaign_list(event.key.key == SDLK_DOWN);
        return;
    }
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_ESCAPE) {
        if (screen_ == Screen::main_menu) {
            // In state 7 escape belongs to the package that owns the frame.
            if (!frame_owned_by_package())
                running = false;
        } else if (screen_ == Screen::map_selection)
            close_map_modal();
        else if (screen_ == Screen::match) {
            // A finished match leaves for the end screen on its own. Escape
            // closes an open menu; it never opens one (F2 and MENU do), and a
            // held key's repeats do nothing more.
            if (!match_finished_ && match_paused_)
                escape_match_menu();
        } else if (
            screen_ == Screen::options || screen_ == Screen::sound || screen_ == Screen::visuals ||
            screen_ == Screen::speeds || screen_ == Screen::music
        ) {
            leave_options_screen();
        } else if (
            screen_ == Screen::new_campaign || screen_ == Screen::any_mission ||
            screen_ == Screen::load_game
        )
            load(Screen::single_player);
        else if (screen_ == Screen::campaign_end)
            load(Screen::main_menu);
        else
            load(Screen::main_menu);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && screen_ == Screen::match) {
        if (!SDL_ConvertEventToRenderCoordinates(sdl_.renderer, &event))
            return;
        handle_match_zoom(event.wheel.y, event.wheel.mouse_x, event.wheel.mouse_y);
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && screen_ == Screen::map_selection &&
        !bound_map_names_.empty()) {
        const auto direction = event.wheel.y > 0.0F ? -1 : event.wheel.y < 0.0F ? 1 : 0;
        const auto next = std::clamp<int32_t>(
            static_cast<int32_t>(modal_map_index_) + direction,
            0,
            static_cast<int32_t>(bound_map_names_.size() - 1U)
        );
        preview_map_index(static_cast<std::size_t>(next));
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_WHEEL && screen_ == Screen::any_mission &&
        !campaign_mission_files_.empty()) {
        const auto direction = event.wheel.y > 0.0F ? -1 : event.wheel.y < 0.0F ? 1 : 0;
        const auto next = std::clamp<int32_t>(
            static_cast<int32_t>(selected_mission_index_) + direction,
            0,
            static_cast<int32_t>(campaign_mission_files_.size() - 1U)
        );
        selected_mission_index_ = static_cast<std::size_t>(next);
        if (selected_mission_index_ < campaign_mission_first_visible_)
            campaign_mission_first_visible_ = selected_mission_index_;
        select_frontend_list_row("Missions", selected_mission_index_);
        rebuild_surface();
        return;
    }
    if (event.type == SDL_EVENT_MOUSE_MOTION || event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
        // Headless checks have no renderer and send canvas coordinates.
        if (sdl_.renderer != nullptr &&
            !SDL_ConvertEventToRenderCoordinates(sdl_.renderer, &event)) {
            if (options_.trace_input)
                std::cerr << "input coordinate conversion failed: " << SDL_GetError() << '\n';
            return;
        }
        const float x = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.x : event.button.x;
        const float y = event.type == SDL_EVENT_MOUSE_MOTION ? event.motion.y : event.button.y;
        update_pointer(x, y);
        // A press on a HUD button holds it until either button comes up, on
        // whatever screen; the release acts on a HUD button only when the
        // press was on it.
        std::optional<std::size_t> released_hud;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP)
            released_hud = std::exchange(match_hud_held_, std::nullopt);
        else if (
            screen_ == Screen::match && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)
        )
            match_hud_held_ = hovered_;
        // A press on a scroll bar or its arrow, and the release of a held
        // one, belong to the bar alone.
        if (route_scroll_pointer(event, x, y))
            return;
        if (screen_ == Screen::match && !match_paused_ && !match_finished_) {
            record_pointer_event(event);
            if (follow_pointer_modes(event))
                return;
        }
        if (screen_ == Screen::match && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            !match_paused_ && !match_finished_ && !hovered_ && match_) {
            if (event.button.button == SDL_BUTTON_RIGHT) {
                handle_match_right_press(x, y);
                return;
            }
            // In the right-click interface a left press over the
            // radar scrolls the view with it until the button comes up.
            namespace input = oa::sim::gameplay_input;
            if (event.button.button == SDL_BUTTON_LEFT) {
                (void)pick_match_cursor();
                if (input::start_left_radar_scroll(match_->state().game)) {
                    select_game_cursor(static_cast<uint8_t>(input::OrderCursor::normal));
                    return;
                }
            }
        }
        if (screen_ == Screen::match && event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            event.button.button == SDL_BUTTON_LEFT && !match_paused_ && !match_finished_ &&
            match_command_ == MatchCommand::none && !hovered_ && !radar_contains(x, y) &&
            x >= static_cast<float>(match_layout_.left) &&
            y >= static_cast<float>(match_layout_.top) &&
            x < static_cast<float>(match_layout_.left + match_layout_.battlefield_width()) &&
            y < static_cast<float>(match_layout_.top + match_layout_.battlefield_height())) {
            // The press starts the box on the terrain under the
            // pointer, and it is drawn from this frame on.
            if (const auto ground = match_pointer_ground(x, y))
                match_drag_ = MatchDragBox{*ground, *ground, frontend_tick()};
        }
        if (screen_ == Screen::match && event.type == SDL_EVENT_MOUSE_MOTION && match_drag_)
            track_match_drag();
        if (screen_ == Screen::match && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
            event.button.button == SDL_BUTTON_LEFT) {
            if (match_finished_) {
                match_drag_.reset();
                return;
            }
            if (match_drag_ && !match_drag_is_click()) {
                const bool add = (SDL_GetModState() & SDL_KMOD_SHIFT) != 0;
                const auto [from, to] = match_drag_corners(live_viewport(
                    static_cast<uint32_t>(match_camera_x_), static_cast<uint32_t>(match_camera_z_)
                ));
                match_drag_.reset();
                if (match_command_ == MatchCommand::attack || match_command_ == MatchCommand::dgun)
                    area_order_units(from.x, from.y, to.x, to.y, "attack");
                else if (match_command_ == MatchCommand::reclaim)
                    area_order_units(from.x, from.y, to.x, to.y, "reclaim");
                else if (match_command_ == MatchCommand::repair)
                    area_order_units(from.x, from.y, to.x, to.y, "repair");
                else
                    box_select_units(from.x, from.y, to.x, to.y, add);
                return;
            }
            match_drag_.reset();
            if (hovered_ && match_hud_) {
                if (released_hud == hovered_)
                    activate_match_hud(*hovered_);
                return;
            }
            if (match_paused_)
                return;
            handle_match_left_click(x, y, event.button.clicks);
            return;
        }
        if (screen_ == Screen::match && event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
            event.button.button == SDL_BUTTON_RIGHT) {
            if (match_paused_ || match_finished_)
                return;
            // The right button on a unit or weapon build button is the same
            // click with the last message 2: it takes one off
            // the queue.
            if (hovered_ && released_hud == hovered_ && match_hud_ &&
                *hovered_ < match_hud_->layout.gadgets.size()) {
                constexpr auto build_buttons =
                    oa::ui::hud::kCommonUnitButton | oa::ui::hud::kCommonWeaponButton;
                const auto attributes = static_cast<uint8_t>(
                    match_hud_->layout.gadgets[*hovered_].common.common_attributes
                );
                if ((attributes & build_buttons) != 0) {
                    activate_match_hud(*hovered_, false);
                    return;
                }
            }
            return;
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN &&
            (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
            event_button_ = event.button.button == SDL_BUTTON_LEFT ? 1 : 2;
            selected_ = hovered_ && frontend_gadget_pressable(*hovered_)
                            ? static_cast<int32_t>(*hovered_)
                            : -1;
            if (options_.trace_input)
                std::cerr << "input down button=" << static_cast<int>(event.button.button)
                          << " selected=" << selected_ << '\n';
        }
        if (event.type == SDL_EVENT_MOUSE_BUTTON_UP &&
            (event.button.button == SDL_BUTTON_LEFT || event.button.button == SDL_BUTTON_RIGHT)) {
            const auto released = hovered_;
            if (options_.trace_input)
                std::cerr << "input up button=" << static_cast<int>(event.button.button)
                          << " released=" << (released ? std::to_string(*released) : "none")
                          << " selected=" << selected_ << '\n';
            if (released && selected_ == static_cast<int32_t>(*released)) {
                const auto& released_name = resources_.layout.gadgets[*released].common.name;
                if (screen_ == Screen::map_selection && released_name == "MAPNAMES") {
                    select_map_row_at(y);
                    if (event.button.clicks >= 2) {
                        activate();
                        close_map_modal();
                    }
                } else if (
                    (screen_ == Screen::new_campaign || screen_ == Screen::any_mission) &&
                    (released_name == "Campaign" || released_name == "Missions")
                ) {
                    // A press on a NEWGAME.GUI list selects the row under it
                    // and gives the list the focus. A double-click on a row
                    // of the list that stands for Start, Campaign for a new
                    // campaign and Missions for any mission, starts it.
                    campaign_setup_focus_ = released_name;
                    const bool picked = select_campaign_list_row(released_name, y);
                    const auto* start_list =
                        screen_ == Screen::new_campaign ? "Campaign" : "Missions";
                    if (picked && released_name == start_list &&
                        event.button.button == SDL_BUTTON_LEFT && event.button.clicks >= 2)
                        start_campaign_setup();
                } else if (screen_ == Screen::campaign_end && released_name == "Missions") {
                    // ENDMSN starts the clicked mission, as Start does.
                    select_campaign_list_row(released_name, y);
                    activate();
                } else {
                    const bool close_after =
                        screen_ == Screen::map_selection &&
                        (released_name == "LOAD" || released_name == "PREVMENU");
                    activate();
                    if (close_after)
                        close_map_modal();
                }
            }
            selected_ = -1;
        }
    }
    if (exit_requested_)
        running = false;
}

bool Runtime::frontend_gadget_pressable(std::size_t index) const {
    if (index >= resources_.layout.gadgets.size())
        return false;
    const auto* button =
        std::get_if<oa::ui::gui_layout::ButtonFields>(&resources_.layout.gadgets[index].fields);
    return button == nullptr || !button->grayed_out;
}

void Runtime::record_typed_key(uint8_t key) {
    std::copy(typed_keys_.begin() + 1, typed_keys_.end(), typed_keys_.begin());
    typed_keys_.back() = key;
    switch (typed_key_hook_) {
    case TypedKeyHook::skirmish_players:
        skirmish::handle_player_count_code(
            state_, skirmish_settings_, preferences_, skirmish_ui_, typed_keys_, *this, *this
        );
        break;
    case TypedKeyHook::single_player_code:
        check_single_player_code();
        break;
    case TypedKeyHook::none:
        return;
    }
    rebuild_surface();
}

void Runtime::show_unsupported(std::string_view message) {
    status_ = std::string(message);
    std::cerr << "unsupported operation: " << message << '\n';
    if (sdl_.window != nullptr)
        SDL_ShowSimpleMessageBox(
            SDL_MESSAGEBOX_INFORMATION, "Open Annihilation", status_.c_str(), sdl_.window
        );
}

void Runtime::start_menu_music() {
    music_main_menu();
    play_menu_voice(oa::ui::frontend::kMainMenuMusic);
}

void Runtime::play_menu_voice(std::string_view sound) {
    if (menu_music_playing_)
        return;
    (void)play_alternate_sound(sound);
}

bool Runtime::play_alternate_sound(std::string_view sound) {
    if (options_.mute || options_.headless_check)
        return false;
    const auto selection = oa::audio::game_audio::select_alternate(
        audio_registry_, sound, false, sound_playback_state()
    );
    if (selection.status != oa::audio::game_audio::SelectionStatus::selected ||
        selection.sound == nullptr)
        return false;
    // The route's loop stops as the new one starts, whether or not it does.
    std::string error;
    menu_music_playing_ = audio_player_.start_loop_resource(selection.sound->resource, error);
    if (!menu_music_playing_)
        std::cerr << "menu BGM unavailable: " << error << '\n';
    return menu_music_playing_;
}

void Runtime::stop_menu_music() {
    audio_player_.stop_loop();
    menu_music_playing_ = false;
    music_begin_match();
}

void Runtime::play_menu_sound(menu::Sound sound) {
    if (options_.mute)
        return;
    const auto selection = oa::audio::game_audio::select(
        audio_registry_, menu::resource_name(sound), false, sound_playback_state()
    );
    std::string error;
    if (selection.status == oa::audio::game_audio::SelectionStatus::selected &&
        !audio_player_.play(selection, error))
        std::cerr << "sound unavailable: " << error << '\n';
}

void Runtime::play_movie_resource(std::string_view filename) {
    const auto path = options_.game_dir / "Data" / filename;
    auto opened = oa::media::IntroPlayer::open(path);
    if (!opened) {
        status_ = "movie unavailable: " + opened.error;
    } else {
        oa::media::PlaybackOptions playback;
        playback.headless_check = options_.headless_check;
        playback.frame_limit = options_.frame_limit.value_or(0);
        playback.play_audio = !options_.mute && !options_.headless_check;
        playback.window = sdl_.window;
        playback.renderer = sdl_.renderer;
        // Alt+Enter switches full screen during the movie as it does in the
        // game.
        playback.hooks.context = this;
        playback.hooks.window_event = [](void* context, const SDL_Event& event) {
            (void)static_cast<Runtime*>(context)->take_full_screen_event(event);
        };
        const auto result = opened.player->play(playback);
        if (!result.ok())
            status_ = "movie playback failed: " + result.error;
    }
    apply_output_mode();
}

} // namespace oa::app
