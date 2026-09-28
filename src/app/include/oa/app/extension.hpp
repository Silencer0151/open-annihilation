// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-game's extension table: the hooks through which a library linked into
// oa-game adds launch options and switches, screens, per-frame work, match
// events and checks. That library defines oa_extensions_init, which main()
// calls once before the command line is parsed; one that includes
// runtime.hpp builds against oa::extension-sdk. Every hook left null keeps
// the engine's own behaviour, which is the game without multiplayer.
//
// The engine calls every hook on the thread that runs main(), and passes
// Extension::context back unchanged; the extension owns it and keeps it
// valid until the process exits. Pointers and references a hook receives
// are valid for that call only unless its documentation says otherwise.
//
// Hooks report bad input by throwing std::runtime_error, as the engine code
// around them does, and the exception takes the path the engine's own
// errors take from the call. Mostly that ends the game: main() prints
// "open-annihilation: <message>" and exits with status 1. Two kinds of path catch it:
// - a match start the frontend falls back from: a campaign mission's
//   start, wherever it comes from, a saved game loaded from the load
//   dialog, and the in-game restart. The start is abandoned where it
//   stopped and the frontend runs on; the status line shows
//   "campaign start: <message>", "Saved game start: <message>" or
//   "Restart failed: <message>", which stderr also receives, and a failed
//   restart returns to the main menu. Other starts (a skirmish from the
//   menus or a headless run, a saved skirmish a --load run loads) are not
//   caught. frontend_game, state, match_game, match_event,
//   launched_by_service, console_host, team_panel_host, service_label,
//   load_progress, draw_loading, draw_match_hud and draw_match_overlay are
//   reached there;
// - a simulation tick of the main loop or of a headless run (--match-ticks,
//   a --campaign mission, a --save-after or --load run), which reports the
//   error as a simulation error and runs the match on: simulation_step,
//   player_gone and message_hooks. A --benchmark run instead prints
//   "benchmark tick stopped: <message>" and ticks that match no further.
// A hook reached on such a path says so.
#pragma once

#include <cstddef>
#include <cstdint>

/// The version of the extension table's contract an extension is built
/// against. An extension checks oa::app::extension_api_version, its typed
/// copy below, with static_assert; the macro serves an extension that builds
/// against more than one engine and tests the version with #if.
///
/// Raise it by one in the change that alters the contract: a hook added,
/// removed or renamed; a hook's parameters, return value or the meaning of
/// leaving it null; when or in which order the engine calls it; what it may
/// keep or must free; its error behaviour; or a type, enumerator or bit the
/// table uses. A change to wording alone keeps it. Version 2 is the first
/// with the layered layout's names: this header is oa/app/extension.hpp and
/// the types the table names are in their modules' namespaces. Version 3
/// replaces offers_multiplayer with select_multiplayer, which MULTI calls
/// and through which the extension may take the game over. Version 4 gives
/// ConsoleHost::post_message the line's sender, calls console_host as each
/// match starts, so that the console's host is filled before the match's
/// first tick, and adds draw_match_overlay, which draws over the
/// battlefield. Version 5 adds pause_changed, load_progress and
/// team_panel_host; no simulation step is offered while the running match's
/// pause bit is set, whoever set it; a shared match's clock keeps running
/// while its in-game menu, or the preferences that menu opens, are up
/// (Runtime::match_running) and while outcome_ready holds it on its
/// outcome. Version 6 adds close_requested and service_label,
/// FrontendEntry::nickname, TeamPanelHost::tournament_game, the
/// ScreenServices entries quit, stop_sounds, play_sound_alternate and
/// run_frontend, and query_register; launched_by_service is also asked as
/// each match starts, and a ScreenContext's host and services may be kept
/// while the runtime lives.
#define OA_EXTENSION_API_VERSION 6

namespace oa {
struct Game;
struct Player;
struct Surface;
struct World;
} // namespace oa

namespace oa::app::command_line {
struct SwitchHandler;
}

namespace oa::ui::frontend_state {
struct StateHandler;
}

namespace oa::present {
struct GafSprites;
}

namespace oa::sim::messages {
struct Hooks;
}

namespace oa::ui::console {
struct ConsoleHost;
}

namespace oa::ui::hud {
struct TeamPanelHost;
}

namespace oa::app {

// OA_EXTENSION_API_VERSION, typed.
inline constexpr uint32_t extension_api_version = OA_EXTENSION_API_VERSION;

class Runtime;
struct ScreenRegistry;

// The arguments that follow a long option.
struct OptionValues {
    void* arguments{};
    /// Takes the next argument of the command line.
    ///
    /// @param arguments OptionValues::arguments
    /// @return the argument, which lives as long as the process; throws
    ///         "<option> requires a value" when none is left or it is empty
    const char* (*next)(void* arguments){};
};

// Engine options an extension option implies (Extension::take_option).
namespace option_effect {
inline constexpr uint32_t headless_check = 1;
inline constexpr uint32_t skip_intro = 2;
inline constexpr uint32_t unattended = 4; // a scripted run: nobody answers a dialog
} // namespace option_effect

// Text an extension may word in place of the engine's (Extension::text).
enum class ExtensionText : uint8_t {
    usage_checks,    // --help: its check options, after the engine's
    usage_runs,      // --help: its run options, after the engine's
    usage_switches,  // --help: the game switches it takes, ahead of "-s"
    usage_note,      // --help: the line after the usage ("" for none)
    register_switch, // why -r stops the start
};

// Where Runtime::run offers the extension a run of its own.
enum class RunPhase : uint8_t {
    start,          // before anything else
    headless_first, // first of the headless runs, ahead of --check-navigation
    headless,       // after --check-navigation, ahead of the engine's headless runs
};

// What MULTI on the main menu does (Extension::select_multiplayer).
enum class MultiplayerSelection : uint8_t {
    unavailable, // the engine's message box says multiplayer is not available
    frontend,    // the main menu's own MULTI step into the frontend's multiplayer states
    taken,       // the extension has taken the game over; the engine does nothing more
};

// The two points of each frame where the extension works.
enum class FrameStage : uint8_t {
    pump,       // charged to the frame profile's pump bucket
    after_pump, // after that bucket closes, before the match clock runs
};

// Extension::state bits; all clear without an extension.
namespace extension_state {
inline constexpr uint32_t multiplayer = 1;   // a multiplayer session is open
inline constexpr uint32_t shared_match = 2;  // the running match is played with other machines
inline constexpr uint32_t replay = 4;        // the running match replays a recording
inline constexpr uint32_t local_watcher = 8; // the local player only watches the shared match
} // namespace extension_state

// What happened to a match (Extension::match_event).
enum class MatchEvent : uint8_t {
    finished,         // the finished match moves to the end-of-game screen
    torn_down,        // the match, if any, is about to be destroyed
    left,             // the player leaves the running match
    results_reported, // the end-of-game screen reports the game's end
    results_released, // the end-of-game screen lets the finished match go
    watching_kept,    // a defeated local player chose to keep watching
};

// The frontend's launch values (Extension::frontend_entry).
struct FrontendEntry {
    const char* game_name{}; // preferred to the stored game name; null for none
    const char* nickname{};  // preferred to the stored nickname; null or empty for none
};

// The fonts MatchOverlay draws text in.
enum class OverlayFont : uint8_t {
    side_panel,  // the font of the side panel's readouts (SIDEDATA.TDF's font)
    message_log, // the font of the message log over the battlefield
};

// The battlefield of a drawn match frame and a painter over it
// (Extension::draw_match_overlay). Positions and sizes are in the painter's
// pixels; `scale` of them make up one pixel of the game's 640x480 screen:
// text is drawn `scale` times larger, and a readout the game draws n pixels
// from an edge of the battlefield belongs n times `scale` from that edge
// here.
struct MatchOverlay {
    void* painter{};        // passed back to font_height, draw_text and fill_rect
    const oa::Game* game{}; // the drawn match's Game block
    int left{};             // the battlefield's left edge
    int top{};              // the battlefield's top edge
    int bottom{};           // the battlefield's bottom edge, where the bottom bar starts
    int scale{};            // painter pixels to one 640x480 pixel; at least 1
    /// Returns a font's height: the step from one line of it to the next,
    /// as the font's own header gives it.
    ///
    /// @param painter MatchOverlay::painter
    /// @param font the font
    /// @return the height in 640x480 pixels; 0 when the font is not loaded
    uint8_t (*font_height)(void* painter, OverlayFont font){};
    /// Draws a line of text in one palette colour, clipped to the battlefield.
    ///
    /// Nothing is drawn when the font is not loaded or the colour is outside
    /// the palette.
    ///
    /// @param painter MatchOverlay::painter
    /// @param font the font to draw in
    /// @param x the text's left edge
    /// @param y the top of its line
    /// @param text the text; read at once
    /// @param palette_index the colour, a palette index
    void (*draw_text)(
        void* painter, OverlayFont font, int x, int y, const char* text, uint8_t palette_index
    ){};
    /// Fills a rectangle in one palette colour, clipped to the battlefield.
    ///
    /// @param painter MatchOverlay::painter
    /// @param x the rectangle's left edge
    /// @param y its top edge
    /// @param width its width; 0 or less fills nothing
    /// @param height its height; 0 or less fills nothing
    /// @param palette_index the colour, a palette index
    void (*fill_rect)(void* painter, int x, int y, int width, int height, uint8_t palette_index){};
};

struct Extension {
    // Passed back to every hook; owned by the extension.
    void* context{};

    /// Takes a long option the engine does not know.
    ///
    /// Called while the command line is parsed, before the game directory
    /// is looked up, once for each argument that starts with "--" and is not
    /// the engine's, in command-line order. Null rejects every such option.
    ///
    /// @param context Extension::context
    /// @param name the option as given ("--name"); lives as long as the process
    /// @param values takes the arguments that follow the option; valid for this call
    /// @param[out] effects option_effect bits the option implies; 0 on entry
    /// @return false when the option is not the extension's, which stops the
    ///         start with "unknown option: <name>"
    bool (*take_option)(
        void* context, const char* name, const OptionValues& values, uint32_t& effects
    ){};

    /// Checks the options take_option took, as a whole.
    ///
    /// Called once, when every argument and game switch is parsed and the
    /// engine has checked which of its options go together (all but
    /// --choose-game-dir's checks, which follow), before the game directory
    /// is looked up. Not called when --help, a refused option or switch, or
    /// options that do not go together end the parse first. Throws to
    /// refuse the options.
    ///
    /// @param context Extension::context
    void (*check_options)(void* context){};

    /// Returns the handler of the game switches the engine reserves.
    ///
    /// Called once, after the long options, while the game switches are
    /// parsed. Null, or a null handler, refuses those switches with
    /// "-<switch> is a multiplayer switch, which this release does not include".
    ///
    /// @param context Extension::context
    /// @return the handler, kept by the extension; valid until the switches
    ///         are parsed, or null
    const oa::app::command_line::SwitchHandler* (*switch_handler)(void* context){};

    /// Returns the extension's wording of a text the engine prints.
    ///
    /// Called while --help prints the usage (usage_checks, usage_runs,
    /// usage_switches, usage_note in that order) and when the game switch -r
    /// stops the start (register_switch), before the runtime exists. Null,
    /// or a null text, keeps the engine's.
    ///
    /// @param context Extension::context
    /// @param which the text asked for
    /// @return the text, kept by the extension and read at once, or null
    const char* (*text)(void* context, ExtensionText which){};

    /// Starts the extension's part of the runtime.
    ///
    /// Called once while the runtime is built: after the session display
    /// starts, before the sounds load and the screens register. The runtime
    /// lives until main() returns, so the extension may keep the reference.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the runtime being built
    void (*startup)(void* context, Runtime& runtime){};

    /// Registers the extension's screens, overlays and dispatcher steps.
    ///
    /// Called once while the runtime is built, after the engine's own
    /// screen packages (screens.inc) register. A registration the registry
    /// rejects stops the start once this hook returns.
    ///
    /// @param context Extension::context
    /// @param[in,out] registry the runtime's registry; the state a
    ///        registration names must live as long as the runtime
    void (*register_screens)(void* context, ScreenRegistry* registry){};

    /// Finishes the extension's start once every screen exists.
    ///
    /// Called once while the runtime is built, after every screen is
    /// registered and before the preferences file loads.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the runtime being built
    void (*ready)(void* context, Runtime& runtime){};

    /// Gives the frontend's launch values.
    ///
    /// Called each time the frontend loads the preferences, the first time
    /// while the runtime is built. Null leaves every value to the
    /// preferences.
    ///
    /// @param context Extension::context
    /// @param[out] entry the values, all null on entry; the engine copies
    ///        game_name and nickname (up to 16 characters each) as soon as
    ///        the hook returns
    void (*frontend_entry)(void* context, FrontendEntry& entry){};

    /// Names the frontend states the extension runs in place of the engine's.
    ///
    /// Called once while the runtime is built, after the game switches'
    /// fields are set and before the frontend's first dispatch. Null leaves
    /// every state to the engine.
    ///
    /// @param context Extension::context
    /// @param[out] handler the runtime's handler, empty on entry, which the
    ///        runtime keeps; what its context names must live as long as the
    ///        runtime
    void (*frontend_states)(void* context, oa::ui::frontend_state::StateHandler& handler){};

    /// Offers the extension a run of its own at a point of Runtime::run.
    ///
    /// Called with RunPhase::start first, and with headless_first then
    /// headless when --headless-check was given or implied. A run the hook
    /// takes replaces the rest of Runtime::run: shutdown is not called and
    /// the preferences are not written unless the hook writes them. Null
    /// runs nothing of the extension's.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param phase the point of Runtime::run
    /// @param[out] exit_code oa-game's exit status when the hook ran something
    /// @return true when the hook ran something, which ends Runtime::run
    bool (*run_mode)(void* context, Runtime& runtime, RunPhase phase, int& exit_code){};

    /// Starts the first scene of an interactive run.
    ///
    /// Called once, when no headless run, check or benchmark took the run,
    /// just before the main loop starts.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @return true when the extension started the scene; false, or a null
    ///         hook, starts the menu music
    bool (*start_scene)(void* context, Runtime& runtime){};

    /// Stops the extension's work once the main loop ends.
    ///
    /// Called once, after the interactive main loop ends and before the
    /// preferences are written; not after a headless run, a check or a run
    /// of run_mode's, nor when an exception ended the run.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    void (*shutdown)(void* context, Runtime& runtime){};

    /// Chooses what MULTI on the main menu does, and may take the game over.
    ///
    /// Called each time MULTI is activated on the main menu, by pointer or
    /// keyboard, before the engine does anything for it. The extension may
    /// take the game over here (open its own screens, start a session) and
    /// answer taken. With a null hook, or an answer the enum does not hold,
    /// the engine shows its message box as for unavailable.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app, on the main menu
    /// @return unavailable to show the engine's "not available" message box
    ///         over the main menu; frontend to run the main menu's own MULTI
    ///         step, which plays the button's sound and moves the frontend to
    ///         its multiplayer states (the extension's frontend_states handler
    ///         and screens drive them); taken when the extension has taken
    ///         the click over, after which the engine does nothing more for it
    MultiplayerSelection (*select_multiplayer)(void* context, Runtime& runtime){};

    /// Returns the frontend's Game block, which the extension keeps itself.
    ///
    /// Called each time the engine uses the frontend's block; it asks again
    /// for every use, so the extension may replace the block between calls.
    /// The engine keeps a block of its own only while this hook is null. An
    /// exception it throws takes the path of the code that asked: a match
    /// start the file header lists abandons the start, the in-game options
    /// panel reports "options panel unavailable: <message>", and elsewhere
    /// it ends oa-game.
    ///
    /// @param context Extension::context
    /// @return the block; never null
    oa::Game* (*frontend_game)(void* context){};

    /// Tells whether a launcher the extension recognises started the game.
    ///
    /// Called when the frontend writes the preferences, and as each match
    /// starts: a game such a launcher started keeps the stored password, its
    /// in-game menus and end-of-game screen take the launcher's label
    /// (service_label), and the end-of-game screen's MAIN MENU leaves the
    /// pointer's picture as it is. An exception it throws during a match
    /// start the file header lists abandons the start; elsewhere it ends
    /// oa-game.
    ///
    /// @param context Extension::context
    /// @return true when such a launcher started the game; a null hook means false
    bool (*launched_by_service)(void* context){};

    /// Runs --check-multiplayer-menu in place of the engine's check.
    ///
    /// Called once, with the SDL renderer up, when --check-multiplayer-menu
    /// was given. Throws to fail the check. Null runs the engine's check,
    /// which clicks MULTI and requires the "not available" message box.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    void (*check_multiplayer_menu)(void* context, Runtime& runtime){};

    /// Returns the extension_state bits of the running game.
    ///
    /// Called whenever the engine needs to know what kind of session it
    /// runs, often several times a frame; it must not change the runtime.
    /// An exception it throws during a match start the file header lists
    /// abandons the start; elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param runtime the running app
    /// @return extension_state bits; a null hook means 0
    uint32_t (*state)(void* context, const Runtime& runtime){};

    /// Does the extension's work of one frame stage.
    ///
    /// Called every frame of the main loop and of the checks that step it,
    /// including every frame of a pause, once with FrameStage::pump and then
    /// once with after_pump, before the match clock runs and the frame is
    /// drawn.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param stage the point of the frame
    void (*frame)(void* context, Runtime& runtime, FrameStage stage){};

    /// Runs one pending simulation step of the running match itself.
    ///
    /// Called for each step the match clock owes when the main loop, or a
    /// check that steps it, advances the match, ahead of the engine's tick;
    /// headless runs tick the match without it. Not called while the pause
    /// bit of Game.sim_run_flags is set, nor while the in-game menu, or the
    /// preferences it opens, hold a match played on this machine alone; they
    /// hold no shared match (extension_state::shared_match), which goes on
    /// beneath them (Runtime::match_running). An exception it throws is
    /// reported as a simulation error on standard error (the game's log when
    /// it plays) and on the console, the frame's remaining steps are dropped
    /// and the match runs on.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @return true when the extension ran the step; false, or a null hook,
    ///         lets the engine tick the match
    bool (*simulation_step)(void* context, Runtime& runtime){};

    /// Tells whether a finished match may leave its outcome for the end-of-game screen.
    ///
    /// Called each time the engine would move the match on: after every frame
    /// drawn with the victory or defeat outcome, and when the disc check a
    /// campaign asks for closes while the match is still on its outcome.
    /// While it returns false for a shared match the match clock keeps
    /// running and its steps go to simulation_step as usual; a match played
    /// on this machine alone stays held on its outcome.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @return false to keep the match on its outcome until the engine asks
    ///         again, after the next frame or disc check; true, or a null
    ///         hook, moves on
    bool (*outcome_ready)(void* context, Runtime& runtime){};

    /// Writes the launch values the extension owns into a new match's Game block.
    ///
    /// Called once as each match starts (a skirmish, a campaign mission or a
    /// loaded game), after the engine writes its defaults and before the game
    /// switches' options and the player records are applied. An exception
    /// it throws abandons a match start the file header lists; elsewhere it
    /// ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] game the new match's Game block
    void (*match_game)(void* context, oa::Game& game){};

    /// Follows the life of a match.
    ///
    /// Called at each MatchEvent: finished as the match moves to the
    /// end-of-game screen, torn_down before the engine destroys the match
    /// (also when a new match starts over none, and after left), left when
    /// the player leaves a running match, results_reported when the
    /// end-of-game screen of a shared match (extension_state::shared_match)
    /// opens, results_released whenever the end-of-game screen lets the
    /// finished match go, shared or not (when the player leaves the screen
    /// for good, or a new match starts while it keeps one), and
    /// watching_kept when a defeated local player chooses to keep watching.
    /// Only results_reported is limited to shared matches. An exception it
    /// throws for torn_down, left or results_released during a match start
    /// the file header lists abandons the start; elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param event what happened
    void (*match_event)(void* context, Runtime& runtime, MatchEvent event){};

    /// Returns the end-of-game screen's text for the local player's disconnect reason.
    ///
    /// Called when the end-of-game screen of a shared match opens and the
    /// local player's record holds a reject reason other than watching; the
    /// text is shown translated in a message box.
    ///
    /// @param context Extension::context
    /// @param reason the reject reason of the local player's record
    /// @return the text, kept by the extension and read at once; null, or a
    ///         null hook, shows nothing
    const char* (*disconnect_text)(void* context, uint8_t reason){};

    /// Gives resources between players for a console gift.
    ///
    /// Called when a console command gives resources in a running match,
    /// before the engine moves them.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param from the giving player's index
    /// @param to the receiving player's index
    /// @param amount the amount given
    /// @param metal true for metal, false for energy
    /// @return true when the extension dealt with the gift; false, or a null
    ///         hook, lets the engine move the resources
    bool (*give_resources)(
        void* context, Runtime& runtime, uint8_t from, uint8_t to, float amount, bool metal
    ){};

    /// Adds the extension's hooks to the message log's.
    ///
    /// Called each time the engine builds the message log's hooks, after it
    /// fills its own: to post to the running match's message log (console
    /// lines, chat, unit reports, simulation errors, an elimination during
    /// a tick), to cycle the reported units or to change the game speed.
    /// Their context is the runtime. An exception it throws for an
    /// elimination takes the tick's path, as player_gone's does; elsewhere
    /// it ends oa-game. The hook cannot tell the two apart, and where the
    /// tick's path reports a simulation error it posts that line through the
    /// message log, which calls the hook again: a hook that throws there too
    /// ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param[in,out] hooks the hooks being built; functions the extension
    ///        sets must accept the runtime as their context
    void (*message_hooks)(void* context, Runtime& runtime, oa::sim::messages::Hooks& hooks){};

    /// Announces a player who lost its last unit.
    ///
    /// Called during a simulation tick of the running match, in a campaign
    /// too. An exception it throws takes the tick's path: where the main
    /// loop or a headless run ticks the match it is reported as a simulation
    /// error, as simulation_step's is; a --benchmark run prints it and ticks
    /// that match no further; where a check ticks the match itself it ends
    /// oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param[in,out] world the match world the announcement goes to
    /// @param player the player who lost its last unit
    /// @return true when the extension announced it; false, or a null hook,
    ///         posts the engine's elimination message outside a campaign
    bool (*player_gone)(
        void* context, Runtime& runtime, oa::World& world, const oa::Player& player
    ){};

    /// Fills the extension's part of the in-game console's host.
    ///
    /// Called as each match starts, before its first tick, when the console
    /// binds to the match's world and before the console starts; and again
    /// should the console later be used for another world. The engine fills
    /// its own callbacks first, except those for group missions, path search
    /// and posters, which it sets after this hook and so keeps; the host's
    /// context is the runtime. The host keeps its address for as long as the
    /// runtime lives, so the extension may keep it and call its callbacks
    /// while a match runs: post_message, for one, posts to the running
    /// match's message log. An exception it throws during a match start the
    /// file header lists abandons the start; elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param[in,out] host the console's host, which the runtime keeps at this
    ///        address while it lives; functions the extension sets must accept
    ///        the runtime as their context
    void (*console_host)(void* context, Runtime& runtime, oa::ui::console::ConsoleHost& host){};

    /// Checks the extension's console commands in --check-navigation's console check.
    ///
    /// Called once in that check, after the engine's option, cursor, debug,
    /// sound and display commands and before its unit commands. Throws to
    /// fail the check.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param enter_line types one line into the console and presses Enter;
    ///        valid for this call
    /// @param user the first argument of enter_line
    void (*check_console)(
        void* context,
        Runtime& runtime,
        void (*enter_line)(void* user, const char* line),
        void* user
    ){};

    /// Draws over the loading screen.
    ///
    /// Called each time the loading screen is drawn while a match loads,
    /// after the engine's progress bars, with the display surface locked.
    /// An exception it throws during a match start the file header lists
    /// abandons the start; elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param[in,out] target the locked display surface; valid for this call
    /// @param font the GUI font, or null while it is not loaded
    void (*draw_loading)(
        void* context, Runtime& runtime, oa::Surface& target, const oa::present::GafSprites* font
    ){};

    /// Draws the extension's readouts in the match HUD pass.
    ///
    /// Called each time the match HUD is drawn, after the resource readout
    /// and build captions and before the unit information and chat entry.
    /// What it draws shows only over the side panel and the top and bottom
    /// bars; draw_match_overlay draws over the battlefield. An exception it
    /// throws during a match start the file header lists abandons the start;
    /// elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    void (*draw_match_hud)(void* context, Runtime& runtime){};

    /// Draws the extension's readouts over the battlefield.
    ///
    /// Called each time a match frame is drawn, after the HUD pass, the kills
    /// board and the message log, and before the profile bars and the paused
    /// or finished title; what it draws shows wherever the battlefield does.
    /// An exception it throws during a match start the file header lists
    /// abandons the start; elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param overlay the battlefield and its painter; valid for this call
    void (*draw_match_overlay)(void* context, Runtime& runtime, const MatchOverlay& overlay){};

    /// Reports that the Pause key toggled the running match's pause.
    ///
    /// Called during a running match each time the Pause key flips the pause
    /// bit of Game.sim_run_flags, after the flip, in any kind of game; the
    /// Pause key of a finished match flips nothing. While the bit is set,
    /// whoever set it, the match clock steps no simulation and
    /// simulation_step is not called; frame still is, every frame. Null
    /// keeps the pause on this machine.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param paused the pause bit after the flip
    void (*pause_changed)(void* context, Runtime& runtime, bool paused){};

    /// Reports the progress of a match's loading.
    ///
    /// Called each time the loading sets a row of the loading screen while a
    /// match loads, the building of its world included, before the loading
    /// screen is drawn. No frame runs while the world is built, so an extension that
    /// must keep working through a long load does it here. An exception it
    /// throws during a match start the file header lists abandons the start;
    /// elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param rows the loading screen's rows, each 0 to 100 percent; valid for
    ///        this call
    /// @param row_count how many rows `rows` holds
    void (*load_progress)(void* context, Runtime& runtime, const uint8_t* rows, size_t row_count){};

    /// Fills the extension's part of the in-game team panels' host.
    ///
    /// Called as each match starts, before its first tick, beside
    /// console_host, with every entry of the host null and its context the
    /// runtime. The team panels (TABMENU.GUI, SHARE.GUI, ALLIES.GUI and
    /// CONTROL.GUI) open only in a multiplayer game
    /// (extension_state::multiplayer); the engine makes their changes here
    /// itself and the host tells the other players' machines. The host keeps
    /// its address for as long as the runtime lives. An exception it throws
    /// during a match start the file header lists abandons the start;
    /// elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param[in,out] host the panels' host, which the runtime keeps at this
    ///        address while it lives; functions the extension sets must
    ///        accept the runtime as their context
    void (*team_panel_host)(void* context, Runtime& runtime, oa::ui::hud::TeamPanelHost& host){};

    /// Answers a request to end the program: the window's close button, or
    /// the system's quit while the window is open.
    ///
    /// Called for each such request the main loop receives, before the engine
    /// does anything for it; a request that arrives while a match loads is
    /// not offered, and the load stops at once. The extension may answer
    /// with a box of its own, or leave its session and end the run through
    /// ScreenServices::quit. Declining keeps the engine's handling: in a
    /// running match, or a page opened over one, the surrender confirmation
    /// (YESORNO.GUI) with its second choice preselected; anywhere else the
    /// run ends at once.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @return true when the extension answered the request; false, or a null
    ///         hook, keeps the engine's handling
    bool (*close_requested)(void* context, Runtime& runtime){};

    /// Returns the label of the launcher that started the game.
    ///
    /// Called as each match starts, beside launched_by_service; the runtime
    /// copies up to 31 characters at once and keeps them for that match's
    /// in-game menus and end-of-game screen. While launched_by_service answers
    /// true and the label holds 1 to 9 characters, the end-of-game screen's
    /// MAIN MENU entry and the in-game exit menu's MAIN MENU entry read it
    /// (the exit menu then hides EXIT GAME), and the exit confirmation asks
    /// "Surrender this battle and return to <label>?". An exception it throws
    /// during a match start the file header lists abandons the start;
    /// elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @return the label, kept by the extension and read at once; null, an
    ///         empty label or a null hook means none
    const char* (*service_label)(void* context){};
};

} // namespace oa::app

/// Fills oa-game's extension table.
///
/// Defined by the one extension library oa-game links; main() calls it once,
/// before the command line is parsed.
///
/// @param[out] table the table, zeroed on entry, which main() keeps until it returns
void oa_extensions_init(oa::app::Extension* table);
