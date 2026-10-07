// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Registration of the multiplayer screens with the application.
//
// Screen ids 0x100..0x104. The main menu reaches the flow by requesting
// kScreenProviders. Dialogs (TCP address, reject confirmation, map view and
// selection, unit restrictions, timeout, message boxes) stack inside these
// screens rather than taking ids of their own.
#pragma once

#include "oa/ui/frontend_multiplayer/connect.hpp"
#include "oa/ui/frontend_multiplayer/dialogs.hpp"
#include "oa/ui/frontend_multiplayer/lobby.hpp"
#include "oa/ui/frontend_multiplayer/restrict.hpp"
#include "oa/ui/screen_registry.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace oa::ui::frontend_multiplayer {

inline constexpr app::ScreenId kScreenProviders = 0x100;  // SELPROV.GUI
inline constexpr app::ScreenId kScreenTcp = 0x101;        // TCP.GUI over SELPROV
inline constexpr app::ScreenId kScreenGameList = 0x102;   // SELGAME.GUI
inline constexpr app::ScreenId kScreenNewGame = 0x103;    // NEWMULTI.GUI
inline constexpr app::ScreenId kScreenBattleroom = 0x104; // LOUNGE2.GUI
inline constexpr app::ScreenId kScreenMainMenu = 0;       // app::Screen::main_menu
inline constexpr app::ScreenId kScreenOptions = 6;        // app::Screen::options

enum class ModalKind : uint8_t {
    none,
    tcp,
    confirm,
    viewmap,
    selmap,
    restrict,
    timeout,
    exit_confirm
};

/// Replaces the loopback network with a real session binding; call before the first multiplayer screen is
/// entered.
///
/// The binding survives multiplayer_reset.
///
/// @param net Network table the lobby uses from now on.
void multiplayer_bind_net(const LobbyNet& net) noexcept;

/// A clock the multiplayer screens' timers read in place of the steady clock.
struct LobbyClock {
    void* context = nullptr;
    /// Returns the clock in milliseconds; null means the steady clock.
    uint32_t (*now_ms)(void* context) = nullptr;
};

/// Binds the clock the multiplayer screens' timers read.
///
/// The binding survives multiplayer_reset.
///
/// @param clock The clock; a null now_ms restores the steady clock.
void multiplayer_bind_clock(const LobbyClock& clock) noexcept;

/// The screen sizes the battle room's RES column offers, and the size it
/// starts at.
struct LobbyDisplayModes {
    void* context{};
    /// Writes the sizes the display offers, the narrower first and of two
    /// as wide the shorter, at most `capacity` of them, and returns how many
    /// it wrote; null, or none written, offers 640x480, 800x600, 1024x768,
    /// 1152x864, 1280x1024 and 1600x1200.
    int32_t (*modes)(void* context, DisplayMode* out, int32_t capacity){};
    /// Returns the size the game plays at, one of those `modes` writes, which
    /// the local player's column starts at; null, or a size of 0 by 0,
    /// starts it at 640x480.
    DisplayMode (*screen_size)(void* context){};
};

/// Binds the screen sizes the battle room's RES column offers and starts at.
///
/// The binding survives multiplayer_reset.
///
/// @param display_modes The sizes; a null member keeps its default.
void multiplayer_bind_display_modes(const LobbyDisplayModes& display_modes) noexcept;

/// Sets the player timeout of the battle room's game (the -t switch), which its stall scan reads.
///
/// The value survives multiplayer_reset.
///
/// @param seconds Player timeout in seconds; 0 or less gives kDefaultPlayerTimeoutSeconds, which
///        also applies until one is set.
void multiplayer_bind_player_timeout(int32_t seconds) noexcept;

/// Sets the battle room buttons a mod's display rules add (lobby_button
/// bits), which the battle room offers its host when its GUI has them.
///
/// The value survives multiplayer_reset.
///
/// @param buttons the buttons; 0, as in 3.1c, for none
void multiplayer_bind_lobby_buttons(uint8_t buttons) noexcept;

/// Binds the network rules the battle room plays by: the version bytes it presents and how it compares
/// them, the private chat channel and the recorder.
///
/// The binding survives multiplayer_reset; until one is bound the rules are 3.1c's.
///
/// @param rules The rules.
/// @param program The line this machine's recorder answers .report with, such as "Program 1.0"; null
///        for none.
void multiplayer_bind_wire_rules(const netgame::WireRules& rules, const char* program) noexcept;

/// Binds whether this machine sends and reads chat as UTF-8 (Lobby::unicode_chat).
///
/// The binding survives multiplayer_reset; until one is bound it is off.
///
/// @param on Unicode chat is on.
void multiplayer_bind_unicode_chat(bool on) noexcept;

/// Binds the launch the connection screens, the battle room and the session read.
///
/// The binding survives multiplayer_reset.
///
/// @param link The launch block and the launch's answers; a null block reads as empty.
void multiplayer_bind_launch_link(const LaunchLink& link) noexcept;

/// A TCP/IP game the multiplayer screens host or join at once, as a player
/// would through them: the address typed into TCP.GUI and accepted, the
/// names and password typed into the game list and NEWMULTI, and OK or the
/// listed game's join.
struct DirectGame {
    enum class Kind : uint8_t {
        none,
        host, // create a game on NEWMULTI
        join, // join a game the game list shows
    };
    Kind kind = Kind::none;
    std::string address;     // join: typed into TCP.GUI's ADDRESS; host: the box keeps its own
    std::string player_name; // typed into the name boxes; empty keeps the name they show
    // host: typed into NEWMULTI's GAMENAME, empty keeping the name it shows;
    // join: the listed game joined, empty for the first listed.
    std::string game_name;
    // host: typed into NEWMULTI's PASSWORD; join: into the game list's;
    // empty keeps the password the box shows.
    std::string password;
};

/// How far the direct game has gone (multiplayer_direct_game_progress).
enum class DirectGameStep : uint8_t {
    none,        // no direct game was asked for
    connecting,  // on its way through the connection screens
    battle_room, // its battle room opened
    failed,      // a screen refused it, with the notice DirectGameProgress::notice
};

struct DirectGameProgress {
    DirectGameStep reached = DirectGameStep::none; // how far it has gone
    std::string notice;                            // failed: the notice the screen showed, as shown
    std::string game_name;   // battle_room: the game's name, as the game list shows it
    std::string player_name; // battle_room: the local player's name, as the battle room shows it
};

/// Asks the multiplayer screens to host or join a game as they are next entered.
///
/// The provider list takes TCP/IP when the launch block's connection type
/// names it ("-n1" writes it). Each screen then goes on at once: TCP.GUI
/// accepts the address without storing it, the game list types the player's
/// name and, to join, the password, then goes on to NEWMULTI or waits for
/// the game to be listed as a launched join does, and NEWMULTI types the
/// game's name and password and takes OK. Each screen does its part once:
/// a refusal leaves the player on that screen with its notice, to go on by
/// hand. The binding survives multiplayer_reset.
///
/// @param game The game; kind none asks for nothing.
void multiplayer_bind_direct_game(const DirectGame& game);

/// Returns how far the game multiplayer_bind_direct_game asked for has gone.
///
/// @return The step and, once it failed, the notice that ended it.
[[nodiscard]] const DirectGameProgress& multiplayer_direct_game_progress() noexcept;

/// Binds the mod profile's rules the battle room keeps (Lobby::rules).
///
/// The binding survives multiplayer_reset.
///
/// @param rules The rules, or null for 3.1c's.
void multiplayer_bind_rules(const data::match_rules::MatchRules* rules) noexcept;

// The game's translation of interface text, which the screens' texts go
// through (LobbyServices::translate).
struct TextTranslation {
    void* context{};
    /// Translates `text` into the game's language; null translates nothing.
    std::string (*translate)(void* context, std::string_view text){};
};

/// Binds the translation of the screens' interface texts, such as the map
/// summary's "Players".
///
/// The binding survives multiplayer_reset. Until one is bound, and when it
/// fails, the texts show as they are.
///
/// @param translation The game's translation.
void multiplayer_bind_translation(const TextTranslation& translation) noexcept;

// The line this machine says of its engine in the battle room's chat, which
// the engine gives (multiplayer_bind_engine_banner).
struct EngineBanner {
    void* context{};
    /// Returns the line, such as "[Engine: OpenAnnihilation v0.6.2]"; null,
    /// or an empty line, says nothing.
    std::string (*line)(void* context){};
};

/// Binds the line this machine says of its engine in the battle room: once
/// as the battle room is entered, hosting or joining, and again each time the
/// line changes while it shows. The line goes out as the local player's chat
/// line through the battle room's own chat, so every player sees it,
/// whatever game client they play.
///
/// The binding survives multiplayer_reset; until one is bound nothing is said.
///
/// @param banner The line's source; a null line says nothing.
void multiplayer_bind_engine_banner(const EngineBanner& banner) noexcept;

/// Returns the line an engine says of itself in the battle room's chat.
///
/// @param version The engine's version as the engine shows it, such as "v0.6.2".
/// @param developer_mode Developer Mode is on.
/// @param remote_controlled A program on the machine may control the game
///        (oa::app::Options::remote_controlled).
/// @return "[Engine: OpenAnnihilation <version>]", with " DEV MODE" before
///         the closing bracket while Developer Mode is on, and then
///         " REMOTED" while the game may be remote-controlled
[[nodiscard]] std::string
engine_banner_line(std::string_view version, bool developer_mode, bool remote_controlled);

// Receives the lobby when the battle room starts a game (the host's START,
// or the host's 0x08 on a client) and takes over from the frontend. Without
// one START only reports that the match loader is not connected.
using StartHandler = void (*)(void* context, Lobby& lobby);

/// Tells whether one of the multiplayer screens is on screen.
///
/// @return true from a screen's entry until it is left for a screen that is not one of them, or
///         until the battle room hands its lobby to the match loader
[[nodiscard]] bool multiplayer_showing() noexcept;

/// Asks for the exit confirmation (YESORNO.GUI, exit_confirm_open) over the multiplayer screen that
/// shows, as a request to close the window does after a launch.
///
/// The screen's next tick opens it over whatever is in front, which is back
/// in front once No closes it; Yes leaves the game through
/// LaunchLink::leave_game without the disconnect reason.
///
/// @return false when no multiplayer screen shows
bool multiplayer_request_exit_confirm() noexcept;

/// Installs the handler that takes over when the battle room starts a game.
///
/// @param handler Start handler, or null to leave START unconnected.
/// @param context Passed back to the handler.
void multiplayer_bind_start(StartHandler handler, void* context) noexcept;

// Inspection hooks for tests and the integrator.

/// Returns the frontend's lobby state.
[[nodiscard]] Lobby& multiplayer_lobby() noexcept;

/// Returns the loopback network used until multiplayer_bind_net.
[[nodiscard]] LoopbackNet& multiplayer_loopback() noexcept;

/// Returns the connection screens' state.
[[nodiscard]] ConnectState& multiplayer_connect() noexcept;

/// Returns the interactive panel of the current screen: the stacked dialog when one is open.
[[nodiscard]] Panel& multiplayer_panel() noexcept;

/// Where the interactive panel's controls lie on the 640x480 canvas.
struct PanelOffset {
    int16_t x{}; ///< columns added to a control's x
    int16_t y{}; ///< rows added to a control's y
};

/// Returns where the interactive panel (multiplayer_panel) lies on the canvas.
///
/// @return the offset its controls' positions take on the canvas
[[nodiscard]] PanelOffset multiplayer_panel_offset() noexcept;

/// Returns the gadget records of the current base screen as its loader left them.
[[nodiscard]] const ui::gui_layout::Layout& multiplayer_screen_layout() noexcept;

/// Returns the stacked dialog's panel, or null when none is open.
[[nodiscard]] Panel* multiplayer_modal() noexcept;

/// Returns which dialog is stacked, or none.
[[nodiscard]] ModalKind multiplayer_modal_kind() noexcept;

/// Returns the unit restriction dialog's state.
[[nodiscard]] RestrictPanel& multiplayer_restrict() noexcept;

/// Returns the open message box text; empty when none is open.
[[nodiscard]] const std::string& multiplayer_message() noexcept;

/// Returns the battle room's unit table, read from the assets on first use.
///
/// @param assets Asset store the unit headers are read from.
/// @param[out] count Table length, the reserved empty type included.
/// @return The first entry; a failed read leaves the table as it was.
[[nodiscard]] LobbyUnit* multiplayer_units(const oa::AssetStore& assets, int32_t* count) noexcept;

/// Computes the battle room's LobbyServices::unit_checksum over the installed files of the unit table.
///
/// @param context Service context.
/// @param unit Unit whose content checksum is wanted.
/// @return The checksum; the unit's stored content checksum when the files cannot be read.
[[nodiscard]] uint32_t multiplayer_unit_checksum(void* context, const LobbyUnit& unit) noexcept;

/// Clicks a named control of the front panel as the pointer would: press, gadget-engine update, handler.
///
/// An open message box is dismissed instead.
///
/// @param ctx Screen context of the running frontend.
/// @param name Control name or link.
/// @param button Mouse button, 1 left or 2 right.
/// @return False when the control is missing.
bool multiplayer_click(app::ScreenContext* ctx, const char* name, uint8_t button = 1) noexcept;

/// Types text into the focused text box, up to its length limit.
///
/// The battle room's chat line (MESSAGE) takes every character but the
/// control characters, whole, as game text: UTF-8 where game text holds it,
/// else the game's code page with '?' for a character it lacks. The other
/// boxes take printable ASCII.
///
/// @param ctx Screen context of the running frontend.
/// @param text NUL-terminated UTF-8; null types nothing.
void multiplayer_type(app::ScreenContext* ctx, const char* text) noexcept;

/// Shows the input method's composition at the end of the battle room's
/// focused chat line, in place of the one shown before; typed text replaces
/// it.
///
/// @param composition NUL-terminated UTF-8; empty or null takes the shown
///     one away.
/// @return false when none of these screens shows, or the focused box is
///     not the chat line
bool multiplayer_compose(const char* composition) noexcept;

/// Starts a new frontend session: drops lobby, connection and dialog state and resets the loopback network.
///
/// A network bound with multiplayer_bind_net is kept.
void multiplayer_reset() noexcept;

/// Returns the frontend game block the lobby reads, its unit header table included.
[[nodiscard]] Game& multiplayer_game() noexcept;

/// Runs the frontend mode tick's unit header step.
///
/// The lobby's units are read again while none are loaded or a full unit
/// load marked them stale.
///
/// @param ctx Screen context of the running frontend.
void multiplayer_reload_unit_headers(app::ScreenContext* ctx);

} // namespace oa::ui::frontend_multiplayer

namespace oa::app {
/// Registers the five multiplayer screens (ids 0x100..0x104) with the application.
///
/// @param[in,out] registry Screen registry to add to.
void register_multiplayer_screens(ScreenRegistry* registry);
} // namespace oa::app
