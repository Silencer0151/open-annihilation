// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// oa-game's extension table: the hooks through which libraries linked into
// oa-game add launch options and switches, screens, per-frame work, match
// events and checks. Each such library is an extension: the project that
// builds the game registers it (oa_add_extension, cmake/OaExtensions.cmake)
// with its init function, void <init>(oa::app::Extension* table), which
// fills a zeroed table of its own. main() calls every registered init once,
// before the command line is parsed, and combines the tables into the one
// the engine calls (ExtensionList, extension_list.hpp). A library that
// includes runtime.hpp builds against oa::extension-sdk. Network play is
// one of them: the engine registers it in every build (oa-app-netgame,
// src/app/netgame), and other extensions build on it or beside it. Every
// hook no extension fills keeps the engine's own behaviour.
//
// The extensions are listed in dependency order: one whose library links
// another registered extension comes after it, and the rest keep the order
// they were registered in. An extension later in the list builds on those
// before it. When several extensions fill a hook, the combined table calls
// them by these rules:
// - every extension, in list order: check_options, startup,
//   register_screens, ready, check_multiplayer_menu, frame, match_game,
//   match_event, check_console, draw_loading, draw_match_hud,
//   draw_match_overlay, pause_changed, load_progress, speed_changed and
//   app_mode_set; also message_hooks, console_host and team_panel_host,
//   whose entries are single owners (below);
// - every extension, in reverse list order: shutdown;
// - the first that takes it, asking the last extension in the list first:
//   take_option (the option goes to the first that takes it, with that
//   extension's effects), the switch handlers (a letter goes to the first
//   handler that takes it; every handler's reset runs), run_mode (for each
//   phase), start_scene, simulation_step, give_resources, player_gone,
//   close_requested, open_recording (each extension asked is given replay
//   and info all zero, and only the one that takes the recording fills the
//   caller's) and select_multiplayer (the first answer other than
//   unavailable);
// - the first answer, asking the last extension in the list first: the
//   first that is not null for disconnect_text and for text's usage_note
//   and register_switch (an empty text is an answer), and the first that
//   is neither null nor empty for return_label and for each field of
//   frontend_entry;
// - joined in list order: text's usage_checks, usage_runs and
//   usage_switches;
// - every extension asked, the answers combined: state (their bits OR'd),
//   keep_stored_password (true when any answers true) and outcome_ready
//   (true when every one answers true);
// - at most one extension: frontend_game and frontend_states. A second
//   extension that fills either stops the start, before the command line is
//   parsed, with a message that names both. Each entry of the hosts that
//   message_hooks, console_host and team_panel_host fill is likewise one
//   extension's: an extension that changes an entry another extension has
//   set, in that call or an earlier one, stops the call with a message
//   that names both.
// With one extension the combined table behaves as that extension's own.
//
// A check an extension runs from run_mode or check_multiplayer_menu drives
// the running game through the check host (check_host.hpp), which is not
// part of this table.
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
//   caught. frontend_game, state, match_game, match_event, console_host,
//   team_panel_host, return_label, load_progress, draw_loading,
//   draw_match_hud and draw_match_overlay are reached there;
// - a simulation tick of the main loop or of a headless run (--match-ticks,
//   a --campaign mission, a --save-after or --load run), which reports the
//   error as a simulation error and runs the match on: simulation_step,
//   player_gone and message_hooks. A --benchmark run instead prints
//   "benchmark tick stopped: <message>" and ticks that match no further.
// A hook reached on such a path says so. speed_changed and app_mode_set
// must not throw. docs/development/conventions.md states this rule for the
// extension table beside the rules of each layer.
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
/// outcome. Version 6 adds close_requested and the hook that named the
/// match's return, FrontendEntry::nickname, TeamPanelHost::tournament_game,
/// the ScreenServices entries quit, stop_sounds, play_sound_alternate and
/// run_frontend, and query_register; the hook that told whether the stored
/// password is kept is also asked as each match starts, and a
/// ScreenContext's host and services may be kept while the runtime lives.
/// Version 7 adds speed_changed, which the speed keys and the GAME slider
/// call, and app_mode_set, which every application mode the frontend sets
/// calls. Version 8 lets several extensions fill tables of their own, each
/// through the init function its registration names in place of the one
/// global init, and combines their hooks by the rules above; replaces those
/// two version 6 hooks with return_label, whose label alone now decides
/// what the match's menus show, and keep_stored_password, which only the
/// preferences write asks; and refuses a reserved game switch no extension
/// takes with "-<switch> is not handled by this build". Version 9 adds
/// open_recording, which hands an extension a recording's bytes to replay
/// into a match the engine steps one tick at a time, with the types
/// RecordingInput, RecordingInfo, RecordingStatus and ReplayHooks.
#define OA_EXTENSION_API_VERSION 9

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
    unavailable, // no answer: the next extension is asked; with none, MULTI does nothing
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

// A recording the engine asks an extension to replay: a file a director
// script names, handed over as bytes so that a bundle's entries are never
// written out.
struct RecordingInput {
    const char* name{};     // the recording's file name as the script gives it, UTF-8; read at once
    const uint8_t* bytes{}; // the recording's contents; valid for the call only
    size_t byte_count{};    // bytes at `bytes`
    bool strict{};          // refuse a recording this installation cannot replay exactly
};

// What an extension says about a recording it opened.
struct RecordingInfo {
    uint32_t expected_end_tick{}; // the tick after the recording's last; 0 when not known
    uint64_t duration_ms{};       // the recording's length by its own clock; 0 when not known
    uint8_t viewer_player{};      // the player index of the slot the replay is watched from
    uint8_t player_count{};       // players the recording holds, the viewer's slot not counted
    bool content_differs{};       // the installation's unit definitions differ from the recording's
};

// Where a replay stands (ReplayHooks::status).
struct RecordingStatus {
    uint32_t tick{};          // the running match's game tick
    bool finished{};          // everything recorded has been replayed
    bool clean{};             // no error so far: every record applied and no tick failed
    bool paced{};             // the recording's periodic records came at the spacing it states
    uint32_t errors{};        // errors so far: records refused or failed, and ticks that failed
    const char* last_error{}; // the last error's text, or null; valid until the next call
};

// A recording an extension replays into the running match, which the engine
// steps one tick at a time. The extension fills every member in
// open_recording, so none is null once it returns true, and keeps what its
// context names until close.
struct ReplayHooks {
    void* context{}; // passed back to step, status and close; owned by the extension
    /// Runs the running match's next tick from the recording.
    ///
    /// A failed tick or a record that cannot be applied does not throw: it
    /// counts in RecordingStatus::errors and the replay goes on.
    ///
    /// @param context ReplayHooks::context
    /// @return true when a tick ran; false when the replay can go no further
    bool (*step)(void* context){};
    /// Tells where the replay stands; it must not change the runtime.
    ///
    /// @param context ReplayHooks::context
    /// @param[out] status the replay's state, all zero and false on entry
    void (*status)(void* context, RecordingStatus& status){};
    /// Ends the replay and frees what the extension kept for it. The match
    /// stays the engine's to tear down. Called once; the hooks are not used
    /// again.
    ///
    /// @param context ReplayHooks::context
    void (*close)(void* context){};
};

struct Extension {
    // Passed back to every hook; owned by the extension.
    void* context{};

    /// Takes a long option the engine does not know.
    ///
    /// Called while the command line is parsed, before the game directory
    /// is looked up, once for each argument that starts with "--" and is not
    /// the engine's, in command-line order, until an extension takes it. An
    /// extension that does not take the option leaves its values untaken. An
    /// option no extension takes stops the start with "unknown option: <name>".
    ///
    /// @param context Extension::context
    /// @param name the option as given ("--name"); lives as long as the process
    /// @param values takes the arguments that follow the option; valid for this call
    /// @param[out] effects option_effect bits the option implies; 0 on entry
    /// @return true when the extension took the option; false when it is not
    ///         the extension's
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

    /// Returns the handler of the game switches the extension takes.
    ///
    /// Called once, after the long options, while the game switches are
    /// parsed. Each letter the engine does not handle itself is offered to
    /// the handlers until one takes it; one of the game's reserved letters
    /// (command_line::kReservedSwitches) that no handler takes stops the
    /// start with "-<switch> is not handled by this build". Null, or a null
    /// handler, takes no switch.
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
    /// every state to the engine. At most one extension may fill it.
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
    /// keyboard, before the engine does anything for it; not over game data
    /// with no multiplayer map, where MULTI shows the engine's
    /// missing-content notice. The extension may take the game over here
    /// (open its own screens, start a session) and answer taken. A null
    /// hook, or an answer the enum does not hold, counts as unavailable;
    /// when no extension answers otherwise, MULTI does nothing and the main
    /// menu stays up. Network play answers frontend.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app, on the main menu
    /// @return unavailable when the extension leads MULTI nowhere, leaving it
    ///         to the other extensions; frontend to run the main menu's own MULTI
    ///         step, which plays the button's sound and moves the frontend to
    ///         its multiplayer states (the extension's frontend_states handler
    ///         and screens drive them); taken when the extension has taken
    ///         the click over, after which the engine does nothing more for it
    MultiplayerSelection (*select_multiplayer)(void* context, Runtime& runtime){};

    /// Returns the frontend's Game block, which the extension keeps itself.
    ///
    /// Called each time the engine uses the frontend's block; it asks again
    /// for every use, so the extension may replace the block between calls.
    /// The engine keeps a block of its own only while this hook is null. At
    /// most one extension may fill it. An
    /// exception it throws takes the path of the code that asked: a match
    /// start the file header lists abandons the start, the in-game options
    /// panel reports "options panel unavailable: <message>", and elsewhere
    /// it ends oa-game.
    ///
    /// @param context Extension::context
    /// @return the block; never null
    oa::Game* (*frontend_game)(void* context){};

    /// Tells whether the preferences write keeps the stored password.
    ///
    /// Called each time the frontend writes the preferences. While it
    /// answers true the write leaves the password the preferences file
    /// holds as it is; otherwise it writes the frontend's password.
    ///
    /// @param context Extension::context
    /// @return true to keep the stored password; a null hook means false
    bool (*keep_stored_password)(void* context){};

    /// Runs --check-multiplayer-menu in place of the engine's check.
    ///
    /// Called once, with the SDL renderer up, when --check-multiplayer-menu
    /// was given over game data with a multiplayer map. Throws to fail the
    /// check. Over data with no multiplayer map the engine's check runs
    /// instead, which clicks MULTI and requires the missing-content notice;
    /// over other data, when no extension fills it, the check fails.
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
    /// ends oa-game. Each of the hooks is one extension's: an extension that
    /// changes one an earlier extension set stops the call.
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
    /// match's message log. Each entry of the host, such as extend and
    /// player_info_changed, is one extension's: an extension that changes
    /// one an earlier extension set stops the call. An exception it throws
    /// during a match start the file header lists abandons the start;
    /// elsewhere it ends oa-game.
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
    /// its address for as long as the runtime lives. Each entry of the host
    /// is one extension's: an extension that changes one an earlier
    /// extension set stops the call. An exception it throws during a match
    /// start the file header lists abandons the start; elsewhere it ends
    /// oa-game.
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
    /// ScreenServices::quit. When every extension declines, the engine
    /// handles it: in a running match, or a page opened over one, the surrender confirmation
    /// (YESORNO.GUI) with its second choice preselected; anywhere else the
    /// run ends at once.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @return true when the extension answered the request; false, or a null
    ///         hook, keeps the engine's handling
    bool (*close_requested)(void* context, Runtime& runtime){};

    /// Returns the label the match's return names, in place of the main menu.
    ///
    /// Called as each match starts; the runtime copies up to 31 characters
    /// at once and keeps them for that match's in-game menus and end-of-game
    /// screen. While the match has a label: the in-game exit menu hides EXIT
    /// GAME; the end-of-game screen's MAIN MENU leaves the pointer's picture
    /// as it is; and when the label holds 1 to 9 characters, the exit menu's
    /// and the end-of-game screen's MAIN MENU entries read it, and the exit
    /// confirmation asks "Surrender this battle and return to <label>?". A
    /// longer label leaves the exit menu's entry and the confirmation as they
    /// are and gives the end-of-game screen's entry "OK". An exception it
    /// throws during a match start the file header lists abandons the start;
    /// elsewhere it ends oa-game.
    ///
    /// @param context Extension::context
    /// @return the label, kept by the extension and read at once; null, an
    ///         empty label or a null hook means none
    const char* (*return_label)(void* context){};

    /// Reports a game speed the local player set with the speed keys or the
    /// GAME slider.
    ///
    /// Called during a running match each time '+' sets the speed, which it
    /// does below the fastest speed, 20, and each time '-' sets it, above
    /// the slowest, 1; and each time the in-game preferences' GAME slider
    /// sets it, whatever its value. Neither works in a watcher's game
    /// (extension_state::local_watcher), which calls it for neither. Called
    /// after the speed is clamped to 1..20, posted to the message log when
    /// it changed and set as the match's speed. The preferences' Cancel and
    /// UNDO, which put back the speed they opened with, and RESTORE, which
    /// puts back the normal speed, do not call it, nor does anything else
    /// that sets the speed. It must not throw. Null keeps the speed on this
    /// machine.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param speed the match's game speed now, 1 to 20; 10 is normal
    void (*speed_changed)(void* context, Runtime& runtime, uint16_t speed){};

    /// Reports an application mode the frontend set.
    ///
    /// Called each time the engine sets the frontend Game's application mode
    /// (Game.mode, an oa::ui::frontend_state::mode_id value), after writing
    /// it, also when the mode is the one it already holds: as the frontend's
    /// states and screens move between menus, as a match is set up or a
    /// saved game loads, as the end-of-game screen opens and as its buttons
    /// leave it. A running match sets no mode here. It must not throw.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app
    /// @param mode the mode set
    void (*app_mode_set)(void* context, Runtime& runtime, int32_t mode){};

    /// Opens a recording to replay into a new match, which the engine then steps.
    ///
    /// Called when --render-script or --generate-script needs the recording a
    /// director script names, in a headless run before any match starts. An
    /// extension that replays recordings of that kind starts the recording's
    /// match through the engine's own match start, with the engine's match
    /// hooks installed, fills `replay` and `info` and returns true. The
    /// engine then runs no tick of that match itself: it calls replay.step
    /// once for each tick, replay.status whenever it needs to know where the
    /// replay stands, and replay.close once, before it tears the match down.
    /// An extension that does not recognise the recording returns false and
    /// the next one is asked; when none takes it the run stops with "no
    /// extension of this build replays <name>". Throws std::runtime_error
    /// for a recording it recognises but cannot replay: one it cannot read,
    /// whose map is not installed, with no free slot to watch from, or, when
    /// input.strict is set, whose unit definitions differ from the
    /// installation's; the error ends oa-game.
    ///
    /// @param context Extension::context
    /// @param[in,out] runtime the running app, headless, with no match running
    /// @param input the recording's name and bytes, valid for this call only
    /// @param[out] replay the replay's step, status and close, all null on entry
    /// @param[out] info what the recording holds, all zero on entry
    /// @return true when the extension opened the recording
    bool (*open_recording)(
        void* context,
        Runtime& runtime,
        const RecordingInput& input,
        ReplayHooks& replay,
        RecordingInfo& info
    ){};
};

} // namespace oa::app
