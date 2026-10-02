// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Connection screens: provider selection (SELPROV.GUI), TCP/IP address
// (TCP.GUI), the game list (SELGAME.GUI) and game creation (NEWMULTI.GUI).
#pragma once

#include "oa/ui/frontend_multiplayer/lobby.hpp"

#include <cstdint>

namespace oa::ui::frontend_multiplayer {

// Persisted TCP/IP address (the game's "tcpaddr" application setting).
struct ConnectSettings {
    void* context{};
    bool (*read_address)(void* context, char* out, std::size_t capacity){};
    void (*write_address)(void* context, const char* address){};
    // Account name offered when no nickname is stored.
    bool (*user_name)(void* context, char* out, std::size_t capacity){};
};

/// Game.connection_flags bit set when the connection dialog closes with OK
/// or Enter, and cleared by JOIN.
inline constexpr uint8_t kConnectFromOk = 0x01;
/// Game.connection_flags bit set when the connection dialog closes with an
/// address.
inline constexpr uint8_t kConnectAddressSet = 0x02;

// Frontend error text, bounded as the game copies it.
inline constexpr std::size_t kErrorTextBytes = 0xf9;
inline constexpr const char* kServiceErrorText = "An error occurred trying to use this service";
// Frontend state the service error returns to: the provider list.
inline constexpr uint8_t kStateConnectionSelection = 0x10;
// Frontend state a launched game goes back to: the main menu.
inline constexpr uint8_t kStateMainMenu = 2;

// The wait for the host that a launched joiner's game list runs: the text
// it shows with the seconds left, how long it waits and how often it polls.
inline constexpr const char* kWaitingForHostText = "Waiting for host...";
inline constexpr uint32_t kHostWaitMilliseconds = 20000;
inline constexpr uint32_t kHostPollMilliseconds = 500;
inline constexpr const char* kHostNotFoundText =
    "Host not found - Host may have left the game before you arrived.";
// What a joiner whose launch was not active when the wait started shows when
// its host is not found, for kHostNotFoundExitMilliseconds before the game
// leaves and ends.
inline constexpr const char* kHostNotFoundExitingText = "Host not found.  Exiting...";
inline constexpr uint32_t kHostNotFoundExitMilliseconds = 4000;
// The application mode the returns from a launched game set: the frontend.
inline constexpr int32_t kAppModeFrontend = 1;

struct ConnectState {
    Provider providers[kMaxProviders];
    int32_t provider_count{};
    int32_t provider{}; // chosen provider, -1 none
    SessionEntry sessions[kMaxSessions];
    int32_t session_count{};
    SessionEntry chosen; // game accepted for JOINGAME/WATCH
    char address[128]{};
    bool
        join_pending{}; // a launched joiner joins the launch address's game as soon as it is listed
    bool update_requested{}; // a launch that hosts goes on to NEWMULTI at once
    ConnectSettings settings;
    char error_text[kErrorTextBytes]{}; // shown once the game list next opens
    // TCP.GUI took the launch's address, which is accepted at once.
    bool launch_address_used{};
    // The wait a pending join runs: whether it runs, when it gives up, when
    // it polls next and the clock at its last frame.
    bool host_waiting{};
    uint32_t host_wait_deadline_ms{};
    uint32_t host_wait_next_poll_ms{};
    uint32_t host_wait_last_ms{};
    // Whether a launch was active when the wait started; it chooses what a
    // host not found leads to.
    bool host_wait_launch{};
    // kHostNotFoundExitingText shows until the clock reaches
    // host_not_found_exit_ms, and the game then leaves.
    bool host_not_found_exiting{};
    uint32_t host_not_found_exit_ms{};
    // NEWMULTI goes on to host at once, without waiting for OK.
    bool host_at_once{};
};

enum class ConnectAction : uint8_t {
    none,
    main_menu,  // PREVMENU on the provider list, or on the game list of a launched game
    options,    // SETTINGS
    provider,   // a provider was chosen
    providers,  // back to the provider list
    game_list,  // address accepted / back from a new game
    new_game,   // STARTNEW
    host,       // NEWMULTI accepted: create the session
    join,       // JOINGAME accepted
    watch,      // WATCH accepted
    leave_game, // the host of the join was not found: leave and end the program
};

/// Returns the provider kind of a stored 16-byte connection GUID.
///
/// @param guid Provider GUID in memory order.
/// @return modem, tcpip, ipx or serial for the stock DirectPlay providers, else other.
[[nodiscard]] ProviderKind provider_kind(const uint8_t guid[16]) noexcept;

/// Closes any open provider and loads the provider list into DPLAY, its scroll bar shown when it does not fit.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
void providers_open(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Takes the provider a launch's connection type names, as the connection selection does first.
///
/// Type 1 is TCP/IP, 2 IPX, 3 modem and 4 serial. The type is cleared
/// whatever it was. When a listed provider matches, its GUID is stored in
/// the game block as a choice would, and while a launch is active
/// the launch's password, cut to 10 characters, becomes the game's.
///
/// @param[in,out] lobby Lobby state, its game block and its launch.
/// @param[in,out] state Connection screens' state; provider is set.
/// @return True when the launch named a listed provider.
bool providers_take_launch(Lobby& lobby, ConnectState& state) noexcept;

/// Handles a click on SELPROV: choosing a provider, PREVMENU or SETTINGS.
///
/// Choosing a provider stores its GUID in the game block and signals the frontend.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
/// @return provider, main_menu, options, or none.
ConnectAction providers_handle_event(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Prepares TCP.GUI with the launch's address when it holds one, else the stored address.
///
/// Taking the launch's address sets ConnectState::launch_address_used; the
/// screen then accepts it at once (tcp_accept_launch_address).
///
/// @param lobby Lobby state and its launch.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
void tcp_open(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Accepts the launch's address TCP.GUI took, as OK would, and opens the service.
///
/// The connection flags take the address and OK, SmlButton plays and the
/// address is saved, both only unless a launch is active; then the flags' OK
/// bit takes the launch's hosting flag instead.
///
/// @param[in,out] lobby Lobby state, its game block, network table and launch.
/// @param[in,out] state Connection screens' state; launch_address_used is cleared.
/// @param panel TCP.GUI's panel.
/// @return game_list when the service opened, providers when it failed.
ConnectAction tcp_accept_launch_address(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Handles a click on TCP.GUI: OK, Enter or JOIN store the address and open the provider; PREV goes back.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
/// @return game_list when the service opened, providers when it failed or on PREV, or none.
ConnectAction tcp_handle_event(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Opens the chosen service's session and marks the game live.
///
/// On failure the service error is left for the game list and the frontend
/// returns to the provider list with both signals at initialize.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
/// @param address TCP/IP host address, or empty to search.
/// @return True when the service opened.
bool connect_open_service(Lobby& lobby, ConnectState& state, const char* address) noexcept;

/// Shows the pending error text in a message box and clears it.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
void connect_show_error_text(Lobby& lobby, ConnectState& state) noexcept;

/// Polls the session list and fills the SELGAME columns, then refreshes the join controls.
///
/// Each row shows the game and map names, players, status (Open, VER!, Lock,
/// Play or BY), metal, energy, ping, commander rule, map and line of sight.
/// The scroll bar shows when the games do not all fit.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
/// @return False, listing nothing, when the transport could not enumerate.
/// @quirk Every refresh selects the first game again and shows the list
///        from its start, as in 3.1c.
bool game_list_update(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Sets WATCH and JOINGAME availability and shows the password field for the selected game.
///
/// @param lobby Lobby state supplying the local version.
/// @param state Connection screens' state holding the listed sessions.
/// @param[in,out] panel The screen's panel.
void game_list_refresh_join(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Prepares SELGAME.GUI: the launch's requests, names, password, the first session poll and any pending
/// reject reason.
///
/// Unless a lobby program launched the game, a launch address asks
/// to host at once (update_requested) when the launch hosts, and an active
/// launch that joins asks to join the game at that address (join_pending);
/// the launch address is then cleared.
///
/// @param[in,out] lobby Lobby state, its game block, network table and launch.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
/// @return False when the address was rejected ("Invalid TCP/IP Address").
bool game_list_open(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Runs the game list's launch requests once a frame.
///
/// A request to host goes on to NEWMULTI (BigButton only when no launch is
/// active). A pending join starts the wait for the host: it polls the
/// session list at once and every 500 ms for 20 s, showing "Waiting for
/// host... (n)" with the seconds left rounded down to even ones, and joins
/// the first game listed as JOINGAME would. When none is listed in time, a
/// joiner whose launch was active when the wait started has the launch's
/// join fail with "Host not found - Host may have left the game before you
/// arrived." (LaunchLink::join_failed), and the frontend returns to
/// the main menu. Any other joiner shows "Host not found.  Exiting..."
/// for kHostNotFoundExitMilliseconds, then leaves the game.
///
/// @param[in,out] lobby Lobby state, its game block, network table, services and launch.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
/// @return new_game, join, main_menu, leave_game, or none.
/// @quirk The wait's first frame shows 0 seconds; the count shows from the
///        next frame on, as in 3.1c.
ConnectAction game_list_tick(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Handles a click on SELGAME: UPDATE, PREVMENU, STARTNEW, JOINGAME, WATCH or a list row.
///
/// Joining checks the launch-only flag (only a launched game may join a
/// launch-only game), version compatibility, the closed and started flags
/// and the nickname before choosing the session. A launched game plays no
/// BigButton, and its PREVMENU goes back to the main menu.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
/// @return new_game, providers, main_menu, join, watch, or none.
ConnectAction game_list_handle_event(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Copies one list entry control into a new control with its own text (the SERVICEx rows).
///
/// @param[in,out] panel Panel to extend.
/// @param source Control to copy.
/// @param y Row of the new control in panel coordinates.
/// @param text Text of the new control.
/// @param name Name of the new control.
/// @return The new index, or kNoControl for a bad source or a full panel.
int32_t panel_add_entry(
    Panel& panel, int32_t source, int16_t y, const char* text, const char* name
) noexcept;

/// Prepares NEWMULTI.GUI with the game name, nickname (the account name when none is stored) and password.
///
/// While a launch is active its user name is the nickname. A
/// launched game goes on to host at once (ConnectState::host_at_once and the
/// host signal), and its password entry is left as loaded.
///
/// @param[in,out] lobby Lobby state, its game block, network table and launch.
/// @param[in,out] state Connection screens' state.
/// @param[in,out] panel The screen's panel.
void new_game_open(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

/// Handles a click on NEWMULTI: Enter moves between the fields; OK validates the names before hosting.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param state Connection screens' state (unused).
/// @param[in,out] panel The screen's panel.
/// @return host once both names are set, game_list on CANCEL, or none.
ConnectAction new_game_handle_event(Lobby& lobby, ConnectState& state, Panel& panel) noexcept;

// Session creation and joining through LobbyNet once a screen accepted.

/// Creates the session and seats the local player as host in slot 0, then publishes the session.
///
/// The session takes 10 players; while a launch is active a player
/// limit above 1 takes its place, at most 10.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param state Connection screens' state (unused).
/// @return False when the network cannot host.
bool connect_host(Lobby& lobby, ConnectState& state) noexcept;

/// Joins the chosen session and seats the local player without a colour.
///
/// Its info goes out and colour 0 is asked of the host. The players already
/// there arrive through LobbyNet's receive.
///
/// @param[in,out] lobby Lobby state, its game block and network table.
/// @param state Connection screens' state holding the chosen session.
/// @param watch Join as a watcher.
/// @return False when the join fails; the reject reason's text is shown.
bool connect_join(Lobby& lobby, ConnectState& state, bool watch) noexcept;

/// Returns the text shown for a reject reason (Player.reject_reason) on return to the game list.
///
/// @param reason Reject reason.
/// @return A static message; "You were rejected from the game" for reasons without their own text.
[[nodiscard]] const char* reject_reason_text(uint8_t reason) noexcept;

} // namespace oa::ui::frontend_multiplayer
