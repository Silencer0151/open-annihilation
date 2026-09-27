// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/game_entry.hpp"
#include <algorithm>
#include <span>
#include <stdexcept>

namespace oa::ui::frontend_state::game_entry {
namespace {
void show_message(Services& host, Message message, int32_t width) {
    const auto translated = host.translate(message);
    host.show_frontend_message(translated, width, message_show_ok, message_fit_width);
}

std::span<const SkirmishSlot> active_slots(const SkirmishSettings& settings) {
    if (settings.slot_count < 0 ||
        static_cast<std::size_t>(settings.slot_count) > settings.slots.size())
        throw std::invalid_argument("skirmish slot count is outside the settings storage");
    return {settings.slots.data(), static_cast<std::size_t>(settings.slot_count)};
}

int32_t count_controller(const SkirmishSettings& settings, int32_t controller_value) {
    const auto slots = active_slots(settings);
    return static_cast<int32_t>(
        std::count_if(slots.begin(), slots.end(), [controller_value](const SkirmishSlot& slot) {
            return slot.controller == controller_value;
        })
    );
}
} // namespace

void handle_single_player_event(State& state, const Event& event, SinglePlayerHost& host) {
    if (event.code == main_menu::destroy_event)
        return;
    const auto sound = [&](Sound value) { host.play_sound(value, sound_argument); };
    const auto select_cursor = [&] { host.select_cursor_animation(menu_cursor_animation_index); };
    Message missing_disc = Message::campaign_disc;
    if (host.button_result(event.menu, Button::new_campaign) != 0) {
        if (static_cast<uint8_t>(host.find_disc(Disc::campaign)) != 0) {
            host.refresh_disc_archives();
            sound(Sound::big_button);
            state.pending_signal = signal_id::new_campaign;
            select_cursor();
            return;
        }
    } else if (host.button_result(event.menu, Button::skirmish) != 0) {
        if (static_cast<uint8_t>(host.find_disc(Disc::multiplayer)) != 0) {
            host.refresh_disc_archives();
            sound(Sound::skirmish);
            state.pending_signal = signal_id::skirmish_menu;
            select_cursor();
            return;
        }
        missing_disc = Message::multiplayer_disc;
    } else if (host.button_result(event.menu, Button::load_game) != 0) {
        sound(Sound::big_button);
        select_cursor();
        host.open_load_game();
        host.clear_event_selection(event.menu);
        return;
    } else if (host.button_result(event.menu, Button::options) != 0) {
        sound(Sound::options);
        host.clear_event_selection(event.menu);
        select_cursor();
        host.open_options();
        return;
    } else if (host.button_result(event.menu, Button::previous_menu) != 0) {
        sound(Sound::previous);
        select_cursor();
        state.pending_signal = signal_id::back;
        return;
    } else if (host.button_result(event.menu, Button::any_mission) != 0) {
        if (static_cast<uint8_t>(host.find_disc(Disc::campaign)) != 0) {
            host.refresh_disc_archives();
            sound(Sound::any_mission);
            state.pending_signal = signal_id::any_mission;
            select_cursor();
            return;
        }
    } else {
        host.clear_event_selection(event.menu);
        return;
    }
    show_message(host, missing_disc, disc_message_width);
    host.clear_frontend_selection();
}

int32_t computer_count(const SkirmishSettings& settings) {
    return count_controller(settings, controller::computer);
}

int32_t human_count(const SkirmishSettings& settings) {
    return count_controller(settings, controller::human);
}

bool all_enabled_players_allied(const SkirmishSettings& settings) {
    const auto slots = active_slots(settings);
    const auto first = std::find_if(slots.begin(), slots.end(), [](const SkirmishSlot& slot) {
        return slot.controller != controller::disabled && slot.alliance != unassigned_alliance;
    });
    if (first == slots.end())
        return false;
    return std::none_of(
        slots.begin(), slots.end(), [alliance = first->alliance](const SkirmishSlot& slot) {
            return slot.controller != controller::disabled && slot.alliance != alliance;
        }
    );
}

bool start_selected_skirmish(
    State& state, SkirmishSettings& settings, MenuHandle menu, SkirmishHost& host
) {
    host.play_sound(Sound::big_button, sound_argument);
    if (static_cast<uint8_t>(host.find_disc(Disc::multiplayer)) == 0) {
        show_message(host, Message::multiplayer_disc, disc_message_width);
        host.clear_frontend_selection();
    }
    host.refresh_disc_archives();
    state.player_count = static_cast<uint16_t>(computer_count(settings) + 1);
    Message failure = Message::missing_terrain;
    if (host.select_map(settings.map_name) != 0) {
        failure = Message::opponents_required;
        if (computer_count(settings) >= 1 && human_count(settings) >= 1) {
            failure = Message::map_capacity;
            if (host.map_player_capacity() >= static_cast<int32_t>(state.player_count)) {
                failure = Message::same_alliance;
                if (!all_enabled_players_allied(settings)) {
                    // Humans are counted first, then computers.
                    const auto humans = human_count(settings);
                    state.player_count = static_cast<uint16_t>(humans + computer_count(settings));
                    host.apply_skirmish_players();
                    reset_mission_results(state);
                    host.save_preferences();
                    state.pending_signal = signal_id::proceed;
                    host.select_cursor_animation(menu_cursor_animation_index);
                    return true;
                }
            }
        }
    }
    show_message(host, failure, validation_message_width);
    host.clear_event_selection(menu);
    return false;
}

void reset_mission_results(State& state) {
    std::fill_n(state.mission_results.begin(), start_pattern_bytes, start_pattern_value);
    state.mission_results[start_pattern_bytes] = 0;
}

} // namespace oa::ui::frontend_state::game_entry
