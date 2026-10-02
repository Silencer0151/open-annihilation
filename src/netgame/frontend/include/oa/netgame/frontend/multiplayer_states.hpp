// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The multiplayer half of the frontend state machine: the main menu with its
// lobby-launch, launch and multiplayer entries, the connection selection,
// the session setup, the battle room and the connection dialog. The frontend
// dispatcher runs them through its StateHandler; the steps and queries they
// add use the labels the dispatcher's Step and Query leave unnamed.
#pragma once

#include "oa/ui/frontend_state/app_modes.hpp"
#include "oa/ui/frontend_state/dispatcher.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace oa::netgame::frontend {

// The frontend states network play runs, besides the main menu.
namespace state_id {
inline constexpr uint8_t connection_selection = 16;
inline constexpr uint8_t session_setup = 17;
inline constexpr uint8_t loading = 18; // the battle room, until the game loads
// States 19 and 20 have no handler: the dispatcher only
// runs its checksum step in them.
inline constexpr uint8_t unused_19 = 19;
inline constexpr uint8_t unused_20 = 20;
inline constexpr uint8_t connection_dialog = 21;
} // namespace state_id

// Signals whose meaning depends on the state that receives them.
namespace signal_id {
// The session setup seats the local player and opens the battle room; the
// battle room tells the session the state of each closed slot and starts
// loading the game.
inline constexpr uint8_t proceed = 17;
inline constexpr uint8_t join = 18;            // join the session
inline constexpr uint8_t join_as_watcher = 19; // join the session as a watcher
// ? Waits, presenting frames, while score reporting starts after a join
// (query::init_score_reporting answered nonzero).
inline constexpr uint8_t await_reporting = 20;
inline constexpr uint8_t finish_join = 21; // mark the local player's descriptor and go on
} // namespace signal_id

namespace flags {
// Meanings below are limited to the tests and updates these states make.
inline constexpr uint16_t direct_session = 0x0010; // Game.gui_flags: a lobby launch joined directly
// Game.connection_flags: the connection dialog closed with OK or Enter rather
// than JOIN; a modem or serial session then skips the game list and proceeds
// to the battle room (signal_id::proceed).
inline constexpr uint16_t connection_from_ok = 0x0001;
// Game.connection_flags: the connection dialog closed with an address; the
// dialog state waits for it.
inline constexpr uint16_t connection_address_set = 0x0002;
// MultiplayerState::setup_options: a join loads the game at once, without the
// battle room.
inline constexpr uint8_t skip_battleroom = 0x10;
inline constexpr uint8_t watcher = 0x40; // PlayerSetupInfo.options (OA_SETUP_OPTION_WATCHER)
// Player.machine_flags: the player joined without the battle room, copied from
// skip_battleroom.
inline constexpr uint8_t joined_without_battleroom = 0x02;
} // namespace flags

// The application mode the battle room and a joined session hand over to.
namespace app_mode {
inline constexpr int32_t multiplayer_idle = 3;
}

// A DirectPlay GUID as its 16 stored bytes.
using Identifier = std::array<uint8_t, 16>;
// The DirectPlay service providers the connection selection tells apart.
inline constexpr Identifier modem_provider{
    0x60, 0xa7, 0xea, 0x44, 0x68, 0xcb, 0xcf, 0x11, 0x9c, 0x4e, 0, 0xa0, 0xc9, 5, 0x42, 0x5e
};
inline constexpr Identifier serial_provider{
    0x60, 0x68, 0x1d, 0x0f, 0xd9, 0x88, 0xcf, 0x11, 0x9c, 0x4e, 0, 0xa0, 0xc9, 5, 0x42, 0x5e
};
inline constexpr Identifier internet_provider{
    0xe0, 0x5e, 0xe9, 0x36, 0x77, 0x85, 0xcf, 0x11, 0x96, 0x0c, 0, 0x80, 0xc7, 0x53, 0x4e, 0x82
};

inline constexpr std::size_t error_text_bytes = 0xf9;

// Calls with no scalar arguments these states make through the dispatcher
// Host's step. The host keeps the object each one acts on: the state of
// whatever registered return_after_launch and setup_requested, the GUI
// context that begins in Game.gui_context_block for clear_control_selection
// and update_gadgets, Game.session_description for quit_game_session, and
// Game.game_options and the value in Game.block_after_connection_flags for
// select_mission_language. The values only identify the steps.
namespace step {
using oa::ui::frontend_state::Step;
inline constexpr Step show_flag_message{0x103};
// The main menu's initialize asks for this instead of opening itself when
// the frontend's queries report a launched game to return to.
inline constexpr Step return_after_launch{0x109};
// The first main-menu update after a setup request ("-y") asks for this.
inline constexpr Step setup_requested{0x10b};
inline constexpr Step setup_service_select{0x115};
inline constexpr Step setup_modem_connect{0x116};
inline constexpr Step setup_serial_connect{0x117};
inline constexpr Step setup_internet_connect{0x118};
inline constexpr Step setup_game_select{0x119};
inline constexpr Step finish_reload_sync{0x11a};
inline constexpr Step broadcast_player_status{0x11b};
inline constexpr Step select_mission_language{0x11c};
inline constexpr Step update_offscreen_surface{0x11d};
inline constexpr Step clear_control_selection{0x11e};
inline constexpr Step setup_battleroom{0x11f};
inline constexpr Step sync_then_free_battleroom{0x120};
inline constexpr Step reset_player_pings{0x121};
inline constexpr Step tick_battleroom{0x122};
inline constexpr Step update_gadgets{0x123};
inline constexpr Step quit_game_session{0x124};
inline constexpr Step free_battleroom{0x125};
inline constexpr Step shut_down_score_buffers{0x126};
inline constexpr Step leave_game_with_flush{0x127};
} // namespace step

// Queries these states ask through the dispatcher Host.
namespace query {
using oa::ui::frontend_state::Query;
inline constexpr Query generate_default_game_name{0x200};
inline constexpr Query context_flags{0x201};         // tested in its low byte only
inline constexpr Query adapter_lookup_needed{0x202}; // tested in its low byte only
// Opens the chosen service provider's session; nonzero when it opened. On
// failure the answer itself puts "An error occurred trying to use this
// service" in the error text and returns to the connection selection, whose
// initialize then runs; the states only go on to the session setup when it
// opened.
inline constexpr Query open_service_session{0x203};
// Starts score reporting as a join goes on; nonzero has the join wait,
// presenting frames, for the reporter (signal_id::await_reporting).
inline constexpr Query init_score_reporting{0x204};
// The chosen provider's kind (transport_kind values).
inline constexpr Query transport_kind{0x205};
} // namespace query

// query::transport_kind's answers.
namespace transport_kind {
inline constexpr uint32_t modem = 0, tcpip = 1, ipx = 2, serial = 3, other = 4;
}

// The application fields these states keep beside the dispatcher's State.
struct MultiplayerState {
    uint16_t connection_flags{}, gui_flags{}; // Game.connection_flags, Game.gui_flags
    // Setup option bits of this session, apart from Game.setup_options and
    // PlayerSetupInfo.options; the canonical record holds them inside
    // Game.block_after_connection_flags. See flags::skip_battleroom.
    uint8_t setup_options{};
    Identifier provider_guid{}, session_guid{};
    // ? A flags word of the object a reference in
    // Game.block_after_session_description names; bit 1 marks this machine as
    // the session's host. None while that reference is null.
    std::optional<uint32_t> session_object_flags;
    int32_t connection_type{}; // "-n": the main menu opens multiplayer at once
    uint8_t setup_request{};   // "-y": the main menu asks for step::setup_requested
    // A launch waits for the connection selection's initialize to
    // select the multiplayer map list and clear session_flags' single player
    // bit, before anything reads them.
    uint8_t launch_pending{};
    // The application mode a return from a launched game asks for (1, the
    // frontend), which the next pass of these states sets through the dispatcher
    // Host before it runs its state; 0 asks for none.
    int32_t pending_app_mode{};
    uint8_t chat_mode{};                             // Game.chat_mode
    std::array<char, error_text_bytes> error_text{}; // shared error message buffer
};

// The session routines these states call with arguments. Every entry may be
// null: a null query answers 0 and a null call does nothing. Every callback
// may change the dispatcher's State and the MultiplayerState.
struct MultiplayerHost {
    void* context{};
    // Resolves the connection selection; `value` -1 resolves the persisted
    // provider selection. Nonzero when it resolves.
    uint32_t (*resolve_connection_info)(void* context, int32_t value){};
    // Sets the guaranteed-delivery option; `value` 1 enables it.
    void (*set_guaranteed_delivery)(void* context, int32_t value){};
    // Runs the add-player failure check for `player`; the states pass 1 as
    // `value` (meaning unresolved). Nonzero when the slot must be added.
    uint32_t (*handle_add_player_failure)(void* context, uint8_t player, int32_t value){};
    // Adds the player slot of player `id`.
    void (*add_player_slot)(void* context, uint32_t id){};
    // Joins session `session` (passed by value) as the local player `player`;
    // 0 on failure.
    uint32_t (*start_player_session)(void* context, Identifier session, uint8_t player){};
    // Sets the state of player slot `index` (0..9); the states pass 0 as
    // `value` (meaning unresolved).
    void (*set_player_slot_state)(void* context, uint8_t index, int32_t value){};
    // Sets up the session channels; the states pass 2 and 100 (meanings
    // unresolved).
    void (*init_session_channels)(void* context, int32_t first, int32_t second){};
    // Reports a game-outcome event: `first` 1 (the battle room opened) then
    // 2 (its roster changed) as the battle room opens, 8 (the session
    // closed) on exit; `second` is always 3 (meaning unresolved).
    void (*handle_game_outcome_event)(void* context, int32_t first, int32_t second){};
    // The launch's connection type, which the main menu tests; null
    // reads MultiplayerState::connection_type.
    int32_t (*connection_type)(void* context){};
};

// What the dispatcher's StateHandler hands back to these states.
struct MultiplayerFrontend {
    MultiplayerState state;
    MultiplayerHost host;
};

/// Runs the main menu and the multiplayer states for the current signal (StateHandler::run).
///
/// A pending application mode (MultiplayerState::pending_app_mode) is set
/// first, whatever the state. The main menu adds its lobby-launch entry (a
/// direct session join), the return after a launched game,
/// the "-n" and "-y" requests and the MULTI path to the frontend's main menu;
/// states 16, 17, 18 and 21 are the connection selection, session setup,
/// battle room and connection dialog. The connection selection's initialize
/// first applies a pending launch
/// (MultiplayerState::launch_pending). Queries go to the dispatcher
/// Host, which answers those the extension registered. Host callbacks may
/// change both states; values are read at the game's sequencing points. The
/// main menu's other signals repeat the frontend's own main menu step
/// for step.
///
/// @param[in,out] frontend Multiplayer state and session routines.
/// @param[in,out] state Dispatcher state, its pending signal already applied.
/// @param[in,out] host Routines the dispatcher calls.
/// @return True when the state is one of these; false leaves it to the other frontend states.
/// @throws std::out_of_range for an out-of-range local player index, and
///         std::invalid_argument for an absent descriptor, on the paths that
///         read them.
/// @quirk The main menu tests only the low byte of the context_flags and
///        adapter_lookup_needed answers before the return after a launch,
///        and the battle room's exit tests only the low byte of the first.
/// @quirk Finishing a join (signal 21) tests again for a watcher join
///        (signal 19), which cannot match there, so a watcher's descriptor
///        is marked only by the join itself.
/// @quirk When the connection dialog closes it signals initialize again after
///        the state change has already set it, so the state checksum step
///        runs a third time.
bool run_state(
    MultiplayerFrontend& frontend,
    oa::ui::frontend_state::State& state,
    oa::ui::frontend_state::Host& host
);

/// Returns the application mode the battle room or a joined session hands over to (StateHandler::handover_mode).
///
/// The battle room hands over to multiplayer idle once loading, and so does
/// the session setup once a join (signal 18 or 19) starts with
/// flags::skip_battleroom set.
///
/// @param frontend Multiplayer state.
/// @param state Dispatcher state.
/// @return app_mode::multiplayer_idle, or 0 for none.
int32_t
handover_mode(const MultiplayerFrontend& frontend, const oa::ui::frontend_state::State& state);

/// Returns the dispatcher StateHandler that runs these states on `frontend`.
///
/// @param[in,out] frontend Multiplayer state and session routines; they must
///                outlive the handler.
/// @return The handler.
oa::ui::frontend_state::StateHandler state_handler(MultiplayerFrontend& frontend);

/// Returns to the intro gate with chat mode, flags::connection_from_ok and the error text cleared.
///
/// @param[in,out] frontend Multiplayer state whose fields are cleared.
/// @param[in,out] state Dispatcher state.
/// @param[in,out] host Host that runs the checksum step.
void reset_frontend_ui(
    MultiplayerFrontend& frontend,
    oa::ui::frontend_state::State& state,
    oa::ui::frontend_state::Host& host
);

} // namespace oa::netgame::frontend
