// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/netgame/frontend/multiplayer_states.hpp"

#include <stdexcept>

namespace oa::netgame::frontend {
namespace {
namespace fs = oa::ui::frontend_state;

constexpr uint32_t low_byte_mask = 0xff;
constexpr uint16_t role_host = 1;        // Descriptor.role: the player hosts the session
constexpr unsigned session_host_bit = 1; // bit of MultiplayerState::session_object_flags
// Moves flags::skip_battleroom onto flags::joined_without_battleroom.
constexpr unsigned options_to_player_shift = 3;

namespace map_list {
constexpr int32_t main_menu = 0, multiplayer = 3;
}

namespace argument {
constexpr int32_t enabled = 1;
constexpr int32_t saved_provider = -1; // resolve the persisted provider selection
// Arguments these states pass to the session's routines (MultiplayerHost).
// The outcome events' first arguments are report events: the battle room
// opened, its roster changed, the session closed. The meaning of the other
// values to the session is unresolved (?).
constexpr int32_t add_player_failure_value = 1;
constexpr int32_t closed_slot_state = 0; // set for each closed slot as loading starts
constexpr int32_t session_channels_first = 2, session_channels_second = 100;
constexpr int32_t outcome_battleroom_opened = 1, outcome_roster_changed = 2,
                  outcome_session_closed = 8,
                  outcome_second = 3; // the second argument of every outcome event
} // namespace argument

namespace player_status {
constexpr uint8_t local = 1, computer = 2, closed = 4; // OA_PLAYER_STATUS_*
}

/// Returns the local player's slot.
///
/// @param[in,out] s Dispatcher state.
/// @return The slot at the local player index.
/// @throws std::out_of_range when the index is past the ten slots.
fs::PlayerSlot& local_player(fs::State& s) {
    if (s.local_player_index >= s.players.size())
        throw std::out_of_range("frontend local player index");
    return s.players[s.local_player_index];
}

/// Returns the local player's descriptor.
///
/// @param[in,out] s Dispatcher state.
/// @return The descriptor of the local player's slot.
/// @throws std::out_of_range for a bad local player index, std::invalid_argument
///         when the slot has no descriptor.
fs::Descriptor& descriptor(fs::State& s) {
    auto& p = local_player(s);
    if (!p.descriptor)
        throw std::invalid_argument("frontend player descriptor is absent");
    return *p.descriptor;
}

/// Runs a session routine that answers, or answers 0 when the host leaves it null.
///
/// @param routine MultiplayerHost entry.
/// @param context MultiplayerHost::context.
/// @param arguments The routine's arguments after the context.
/// @return The routine's answer, or 0.
template <typename... Parameters, typename... Arguments>
uint32_t ask(uint32_t (*routine)(void*, Parameters...), void* context, Arguments... arguments) {
    return routine != nullptr ? routine(context, arguments...) : 0;
}

/// Runs a session routine unless the host leaves it null.
///
/// @param routine MultiplayerHost entry.
/// @param context MultiplayerHost::context.
/// @param arguments The routine's arguments after the context.
template <typename... Parameters, typename... Arguments>
void tell(void (*routine)(void*, Parameters...), void* context, Arguments... arguments) {
    if (routine != nullptr)
        routine(context, arguments...);
}

/// Runs StateHandler::run on the MultiplayerFrontend in `context`.
///
/// @param context The MultiplayerFrontend.
/// @param[in,out] state Dispatcher state.
/// @param[in,out] host Routines the dispatcher calls.
/// @return run_state's result.
bool run_handler(void* context, fs::State& state, fs::Host& host) {
    return run_state(*static_cast<MultiplayerFrontend*>(context), state, host);
}

/// Runs StateHandler::handover_mode on the MultiplayerFrontend in `context`.
///
/// @param context The MultiplayerFrontend.
/// @param state Dispatcher state.
/// @return handover_mode's result.
int32_t handover_handler(void* context, const fs::State& state) {
    return handover_mode(*static_cast<const MultiplayerFrontend*>(context), state);
}
} // namespace

bool run_state(MultiplayerFrontend& frontend, fs::State& s, fs::Host& h) {
    auto& m = frontend.state;
    const auto& session = frontend.host;
    const auto call = [&](fs::Step f) { h.step(f, s); };
    const auto query = [&](fs::Query f) { return h.query(f, s); };
    const auto connection_type = [&] {
        return session.connection_type != nullptr ? session.connection_type(session.context)
                                                  : m.connection_type;
    };
    const auto signal = [&](uint8_t value) { fs::set_frontend_signal(s, h, value); };
    const auto transition = [&](uint8_t value) { fs::set_frontend_state(s, h, value); };
    const auto pump = [&] { call(fs::Step::present_frame); };
    if (m.pending_app_mode != 0) {
        const int32_t mode = m.pending_app_mode;
        m.pending_app_mode = 0;
        h.set_app_mode(s, mode);
    }
    switch (s.state) {
    case fs::state_id::main_menu:
        call(fs::Step::pop_input_event);
        switch (s.signal) {
        case fs::signal_id::initialize:
            if (query(query::generate_default_game_name) != 0) {
                s.session_flags |= fs::flags::live_game;
                m.gui_flags |= flags::direct_session;
                transition(state_id::session_setup);
                signal(signal_id::join);
            } else {
                if (connection_type() == 0 && (query(query::context_flags) & low_byte_mask) != 0 &&
                    (query(query::adapter_lookup_needed) & low_byte_mask) != 0) {
                    call(step::return_after_launch);
                    return true;
                }
                h.select_map_list(s, map_list::main_menu);
                m.gui_flags &= static_cast<uint16_t>(~flags::direct_session);
                call(fs::Step::setup_main_menu);
                if (connection_type() == 0)
                    call(fs::Step::return_to_main_menu);
                else
                    signal(fs::signal_id::multiplayer);
            }
            h.set_cursor_visible(s, argument::enabled);
            return true;
        case fs::signal_id::update:
            pump();
            if (m.setup_request != 0) {
                m.setup_request = 0;
                call(step::setup_requested);
            }
            return true;
        case fs::signal_id::single_player:
            s.session_flags |= fs::flags::single_player;
            transition(fs::state_id::single_player_menu);
            call(fs::Step::shut_down_resource);
            return true;
        case fs::signal_id::multiplayer:
            h.select_map_list(s, map_list::multiplayer);
            s.session_flags &= static_cast<uint16_t>(~fs::flags::single_player);
            transition(state_id::connection_selection);
            call(fs::Step::shut_down_resource);
            return true;
        case fs::signal_id::restart_intro:
            transition(fs::state_id::intro_gate);
            return true;
        case fs::signal_id::exit_application:
            call(fs::Step::draw_current_frame);
            call(fs::Step::shut_down_resource);
            h.shut_down(s);
            return true;
        case fs::signal_id::movie_5:
            transition(fs::state_id::movie_5);
            return true;
        default:
            return true;
        }
    case state_id::connection_selection:
        call(fs::Step::pop_input_event);
        switch (s.signal) {
        case fs::signal_id::initialize:
            if (m.launch_pending != 0) {
                m.launch_pending = 0;
                h.select_map_list(s, map_list::multiplayer);
                s.session_flags &= static_cast<uint16_t>(~fs::flags::single_player);
            }
            m.connection_flags &= static_cast<uint16_t>(~flags::connection_address_set);
            m.connection_flags &= static_cast<uint16_t>(~flags::connection_from_ok);
            if (query(query::generate_default_game_name) != 0) {
                s.session_flags |= fs::flags::live_game;
                transition(state_id::session_setup);
                signal(signal_id::join);
                return true;
            }
            call(step::setup_service_select);
            signal(
                ask(session.resolve_connection_info, session.context, argument::saved_provider) != 0
                    ? fs::signal_id::proceed
                    : fs::signal_id::update
            );
            return true;
        case fs::signal_id::update:
            pump();
            return true;
        case fs::signal_id::proceed:
            if (m.provider_guid == modem_provider) {
                transition(state_id::connection_dialog);
                signal(fs::signal_id::update);
                call(step::setup_modem_connect);
                return true;
            }
            if (m.provider_guid == serial_provider) {
                transition(state_id::connection_dialog);
                signal(fs::signal_id::update);
                call(step::setup_serial_connect);
                return true;
            }
            if (m.provider_guid == internet_provider) {
                transition(state_id::connection_dialog);
                signal(fs::signal_id::update);
                call(step::setup_internet_connect);
                return true;
            }
            if (query(query::open_service_session) != 0) {
                transition(state_id::session_setup);
                signal(fs::signal_id::initialize);
            }
            return true;
        case fs::signal_id::back:
            transition(fs::state_id::main_menu);
            return true;
        case fs::signal_id::options:
            transition(fs::state_id::options);
            call(fs::Step::open_options);
            signal(fs::signal_id::update);
            return true;
        default:
            return true;
        }
    case state_id::session_setup:
        call(fs::Step::pop_input_event);
        switch (s.signal) {
        case fs::signal_id::initialize:
            tell(session.set_guaranteed_delivery, session.context, argument::enabled);
            signal(fs::signal_id::update);
            call(fs::Step::reset_player_slots);
            if (m.provider_guid == modem_provider || m.provider_guid == serial_provider) {
                if ((m.connection_flags & flags::connection_from_ok) != 0) {
                    signal(signal_id::proceed);
                    return true;
                }
                m.session_guid = {}; // the game's all-zero identifier
            }
            call(step::setup_game_select);
            call(step::show_flag_message);
            return true;
        case fs::signal_id::update:
            pump();
            call(step::show_flag_message);
            return true;
        case fs::signal_id::back:
            call(step::finish_reload_sync);
            transition(state_id::connection_selection);
            return true;
        case signal_id::proceed:
            call(step::broadcast_player_status);
            if (ask(session.handle_add_player_failure,
                    session.context,
                    s.local_player_index,
                    argument::add_player_failure_value) != 0)
                tell(session.add_player_slot, session.context, local_player(s).player_id);
            transition(state_id::loading);
            return true;
        case signal_id::join:
        case signal_id::join_as_watcher:
            if (s.signal == signal_id::join_as_watcher)
                descriptor(s).options |= flags::watcher;
            if (ask(session.start_player_session,
                    session.context,
                    m.session_guid,
                    s.local_player_index) == 0) {
                signal(fs::signal_id::initialize);
                return true;
            }
            if (s.signal == signal_id::join) {
                call(fs::Step::draw_current_frame);
                call(step::update_offscreen_surface);
                if (query(query::init_score_reporting) != 0) {
                    call(step::clear_control_selection);
                    signal(signal_id::await_reporting);
                    return true;
                }
            }
            signal(signal_id::finish_join);
            return true;
        case signal_id::await_reporting:
            pump();
            return true;
        case signal_id::finish_join: {
            auto& d = descriptor(s);
            const auto bit = m.session_object_flags
                                 ? ((*m.session_object_flags >> session_host_bit) & role_host)
                                 : 0u;
            d.role = static_cast<uint16_t>((d.role & static_cast<uint16_t>(~role_host)) | bit);
            // The signal is finish_join here, so this never sets the watcher
            // option; join_as_watcher has set it already.
            if (s.signal == signal_id::join_as_watcher)
                descriptor(s).options |= flags::watcher;
            auto& p = local_player(s);
            p.machine_flags = static_cast<uint8_t>(
                (p.machine_flags & static_cast<uint8_t>(~flags::joined_without_battleroom)) |
                ((m.setup_options >> options_to_player_shift) & flags::joined_without_battleroom)
            );
            if ((m.setup_options & flags::skip_battleroom) != 0) {
                call(step::select_mission_language);
                for (std::size_t i = 0; i < fs::limits::player_count; ++i) {
                    if (s.players[i].present && (s.players[i].status == player_status::local ||
                                                 s.players[i].status == player_status::computer))
                        tell(session.add_player_slot, session.context, s.players[i].player_id);
                }
                s.session_flags |= fs::flags::loading;
                return true;
            }
            transition(state_id::loading);
            return true;
        }
        default:
            return true;
        }
    case state_id::loading:
        switch (s.signal) {
        case fs::signal_id::initialize:
            if ((s.session_flags & fs::flags::loading) == 0) {
                call(step::setup_battleroom);
                tell(
                    session.handle_game_outcome_event,
                    session.context,
                    argument::outcome_battleroom_opened,
                    argument::outcome_second
                );
                tell(
                    session.handle_game_outcome_event,
                    session.context,
                    argument::outcome_roster_changed,
                    argument::outcome_second
                );
                signal(fs::signal_id::update);
            } else {
                call(step::sync_then_free_battleroom);
                signal(signal_id::proceed);
            }
            call(step::reset_player_pings);
            return true;
        case fs::signal_id::update:
            call(step::tick_battleroom);
            pump();
            if ((s.session_flags & fs::flags::loading) != 0) {
                call(step::sync_then_free_battleroom);
                call(step::update_gadgets);
                signal(signal_id::proceed);
            }
            return true;
        case fs::signal_id::back:
            s.session_flags |= fs::flags::live_game;
            call(step::quit_game_session);
            tell(
                session.init_session_channels,
                session.context,
                argument::session_channels_first,
                argument::session_channels_second
            );
            call(step::free_battleroom);
            tell(
                session.handle_game_outcome_event,
                session.context,
                argument::outcome_session_closed,
                argument::outcome_second
            );
            call(step::shut_down_score_buffers);
            if ((query(query::context_flags) & low_byte_mask) != 0) {
                transition(fs::state_id::main_menu);
                h.set_app_mode(s, fs::mode_id::loading_return);
                return true;
            }
            if ((m.gui_flags & flags::direct_session) != 0) {
                call(step::leave_game_with_flush);
                return true;
            }
            {
                // A modem or serial game goes back to the connection
                // selection; any other returns to the game list.
                const auto kind = query(query::transport_kind);
                transition(
                    kind == transport_kind::modem || kind == transport_kind::serial
                        ? state_id::connection_selection
                        : state_id::session_setup
                );
            }
            return true;
        case signal_id::proceed:
            for (uint8_t i = 0; i < fs::limits::player_count; ++i)
                if (s.players[i].status == player_status::closed)
                    tell(
                        session.set_player_slot_state,
                        session.context,
                        i,
                        argument::closed_slot_state
                    );
            call(step::sync_then_free_battleroom);
            s.session_flags |= fs::flags::loading;
            return true;
        default:
            return true;
        }
    case state_id::connection_dialog:
        if (s.signal != fs::signal_id::update)
            return true;
        if ((m.connection_flags & flags::connection_address_set) == 0) {
            pump();
            return true;
        }
        if (query(query::open_service_session) == 0) {
            m.connection_flags &= static_cast<uint16_t>(~flags::connection_from_ok);
            m.connection_flags &= static_cast<uint16_t>(~flags::connection_address_set);
            transition(state_id::connection_selection);
        } else
            transition(state_id::session_setup);
        signal(fs::signal_id::initialize);
        return true; // 3.1c repeats the integrity check after clearing the pair
    default:
        return false;
    }
}

int32_t handover_mode(const MultiplayerFrontend& frontend, const fs::State& s) {
    if (s.state == state_id::loading)
        return (s.session_flags & fs::flags::loading) != 0 ? app_mode::multiplayer_idle : 0;
    if (s.state == state_id::session_setup &&
        (frontend.state.setup_options & flags::skip_battleroom) != 0 &&
        (s.signal == signal_id::join || s.signal == signal_id::join_as_watcher))
        return app_mode::multiplayer_idle;
    return 0;
}

fs::StateHandler state_handler(MultiplayerFrontend& frontend) {
    fs::StateHandler handler;
    handler.context = &frontend;
    handler.run = run_handler;
    handler.handover_mode = handover_handler;
    return handler;
}

void reset_frontend_ui(MultiplayerFrontend& frontend, fs::State& s, fs::Host& h) {
    fs::set_frontend_state(s, h, fs::state_id::intro_gate);
    frontend.state.chat_mode = 0;
    frontend.state.connection_flags &= static_cast<uint16_t>(~flags::connection_from_ok);
    frontend.state.error_text[0] = '\0';
}

} // namespace oa::netgame::frontend
