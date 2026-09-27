// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/ui/frontend_state/app_modes.hpp"

namespace oa::ui::frontend_state {
namespace {
constexpr int32_t cursor_normal = 0x13;
constexpr int32_t map_list_main_menu = 0;
constexpr uint16_t outcome_flags_cleared_on_entry = 0x10 | 0x04;

namespace app_mode {
constexpr int32_t skirmish_setup = 4, game_load = 5;
}

namespace map_list_state {
constexpr uint32_t skirmish_ready = 1, game_ready = 2;
}

/// Clears the in-game session flags and outcome bits and selects the main-menu map list.
///
/// Ends by emptying the key queue.
///
/// @param[in,out] s Dispatcher state.
/// @param[in,out] h Host that runs the map-list and key-queue calls.
void leave_game_flags(State& s, Host& h) {
    s.session_flags &= static_cast<uint16_t>(~flags::loading);
    s.session_flags &= static_cast<uint16_t>(~flags::single_player);
    s.session_flags &= static_cast<uint16_t>(~flags::live_game);
    h.select_map_list(s, map_list_main_menu);
    s.outcome_flags &= static_cast<uint16_t>(~outcome_flags_cleared_on_entry);
    h.step(Step::reset_key_queue, s);
}
} // namespace

void set_frontend_state(State& s, Host& h, uint8_t state) {
    h.step(Step::check_state_checksum, s);
    s.state = state;
    h.step(Step::check_state_checksum, s);
    s.signal = signal_id::initialize;
    s.pending_signal = signal_id::initialize;
}

void set_frontend_signal(State& s, Host& h, uint8_t signal) {
    h.step(Step::check_state_checksum, s);
    s.signal = signal;
    s.pending_signal = signal;
}

void reset_to_main_menu(State& s, Host& h) {
    set_frontend_state(s, h, state_id::main_menu);
    h.step(Step::load_default_palette, s);
}

void enter_frontend_mode(State& s, Host& h) {
    h.set_cursor(s, cursor_normal);
    h.step(Step::draw_cursor, s);
    leave_game_flags(s, h);
    h.set_app_mode(s, mode_id::frontend);
}

void reset_to_frontend_mode(State& s, Host& h) {
    reset_to_main_menu(s, h);
    leave_game_flags(s, h);
    h.step(Step::enable_panel_keyboard, s);
    h.set_app_mode(s, mode_id::frontend);
}

void tick_frontend_mode(State& s, Host& h, const StateHandler& extension) {
    h.step(Step::reload_unit_overrides, s);
    dispatch(s, h, extension);
    int32_t next = 0;
    if ((s.session_flags & flags::loading) != 0) {
        if (h.query(Query::map_list_object_state, s) == map_list_state::skirmish_ready)
            next = app_mode::skirmish_setup;
        else if (
            (s.session_flags & flags::loading) != 0 &&
            h.query(Query::map_list_object_state, s) == map_list_state::game_ready
        )
            next = app_mode::game_load;
    }
    if (next == 0 && extension.handover_mode != nullptr)
        next = extension.handover_mode(extension.context, s);
    if (next != 0) {
        h.step(Step::get_cursor_context, s);
        h.set_app_mode(s, next);
    }
    h.step(Step::get_cursor_context, s);
    h.step(Step::draw_panel_gadgets, s);
    h.step(Step::draw_cursor, s);
}

} // namespace oa::ui::frontend_state
