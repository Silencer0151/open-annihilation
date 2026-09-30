// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The native frontend/match runtime that hosts the frontend and offline match.
#pragma once

#include "app.hpp"
#include "extension.hpp"
#include "match_model_draws.hpp"
#include "offline_services.hpp"
#include "video_capture.hpp"
#include "web_link.hpp"
#include "oa/ui/display_layout.hpp"
#include "oa/audio/sdl_audio.hpp"
#include "oa/base/game_loop.hpp"
#include "oa/data/defs/locale.hpp"
#include "oa/data/defs/sides.hpp"
#include "oa/data/defs/unit_catalog.hpp"
#include "oa/sim/gameplay_input/input.hpp"
#include "oa/sim/gameplay_input/order_cursor.hpp"
#include "oa/ui/gui_input/gadget_panel.hpp"
#include "oa/ui/gui_input.hpp"
#include "oa/platform/memory_status.hpp"
#include "oa/sim/map_runtime/feature_defs.hpp"
#include "oa/sim/map_runtime.hpp"
#include "oa/formats/ota.hpp"
#include "oa/present/display.hpp"
#include "oa/present/gaf_sprites.hpp"
#include "oa/present/surface.hpp"
#include "oa/present/world_renderer/unit_renderer.hpp"
#include "oa/present/world_renderer/world_fog.hpp"
#include "oa/present/world_renderer/world_overlays.hpp"
#include "oa/sim/sprite_animation.hpp"
#include "oa/ui/hud/boundary.hpp"
#include "oa/ui/hud/camera_scroll.hpp"
#include "oa/ui/hud/kill_board.hpp"
#include "oa/ui/hud/order_panel.hpp"
#include "oa/ui/hud/team_panels.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"
#include "oa/ui/frontend/resource_palette.hpp"
#include "oa/ui/frontend_renderer/scroll_bars.hpp"
#include "oa/present/world_renderer/world_radar.hpp"
#include "oa/sim/messages.hpp"
#include "oa/sim/selection.hpp"
#include <SDL3/SDL.h>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <optional>
#include <set>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::console {
struct Console;
struct ConsoleHost;
enum class CrashTest : uint8_t;
} // namespace oa::ui::console

namespace oa::present::model {
struct RgbBridge;
struct RgbFrame;
struct ModelDisplay;
} // namespace oa::present::model

namespace oa::data::persist {
struct Bank;
struct SaveContext;
} // namespace oa::data::persist

namespace oa::ui::frontend {
struct Panel;
struct OptionsContext;
struct LoadSummary;
struct GameSettingsView;
} // namespace oa::ui::frontend

namespace oa::data::campaign {
struct CampaignFile;
struct CampaignEnv;
enum class SessionKind : int32_t;
} // namespace oa::data::campaign

namespace oa::ui::campaign {
struct FrontendHost;
struct BriefingRegion;
struct ScoreLayout;
} // namespace oa::ui::campaign

namespace oa::app {

/// Loads the session palette, PALETTE.PAL, through the palette file loader.
///
/// Throws std::runtime_error naming palettes/PALETTE.PAL when it cannot be
/// loaded.
///
/// @param assets game files
/// @return the palette file's bytes
PaletteBytes load_active_palette(const AssetStore& assets);

struct MatchConsole;
struct MatchModels;

// The named-background cache and the bitmaps its handles name (index + 1).
struct NamedBackgrounds {
    oa::ui::frontend::ResourceCache cache{};
    std::vector<std::unique_ptr<Image>> bitmaps;

    NamedBackgrounds() = default;
    NamedBackgrounds(const NamedBackgrounds&) = delete;
    NamedBackgrounds& operator=(const NamedBackgrounds&) = delete;

    /// Frees the cached backgrounds.
    ~NamedBackgrounds() { oa::ui::frontend::resource_cache_free(&cache, {}); }
};

struct SdlObjects {
    SDL_Window* window = nullptr;
    SDL_Renderer* renderer = nullptr;
    SDL_Texture* texture = nullptr;
    bool borrowed = false;

    /// Destroys the texture and, unless the window and renderer are borrowed, the renderer and the
    /// window.
    void reset() noexcept {
        if (texture != nullptr)
            SDL_DestroyTexture(texture);
        texture = nullptr;
        if (borrowed)
            return;
        if (renderer != nullptr)
            SDL_DestroyRenderer(renderer);
        if (window != nullptr)
            SDL_DestroyWindow(window);
        renderer = nullptr;
        window = nullptr;
    }

    /// Resets the SDL objects and, unless they were borrowed, shuts SDL down.
    ~SdlObjects() {
        reset();
        if (!borrowed)
            SDL_Quit();
    }
};

// The session's display, bound while it lives, and the off-screen surface
// behind Game.offscreen_surface (runtime_display.cpp).
struct SessionDisplay {
    oa::present::DisplayContext context{};
    oa::present::OffscreenSurface offscreen{};

    SessionDisplay() = default;
    SessionDisplay(const SessionDisplay&) = delete;
    SessionDisplay& operator=(const SessionDisplay&) = delete;

    /// Frees the off-screen surface, shuts the display down and unbinds it when it is the bound
    /// display.
    ~SessionDisplay();
};

// The last frame the headless display sink received, with its palette.
struct CapturedFrame {
    oa::present::SurfaceBuffer frame{};
    oa::Palette palette{};
};

// The SDL display sink's texture: palette indices become XRGB texels through
// a table rebuilt whenever the frame's palette changes.
struct IndexedOutput {
    SDL_Texture* texture = nullptr;
    int width = 0;
    int height = 0;
    oa::Palette palette{};
    std::array<uint32_t, OA_PALETTE_COLORS> texels{};
    bool texels_ready = false;
};

// sim::unit_spawn::Request.state of a unit placed in the ground plot slot
// (Unit.flags occupancy kind 1).
inline constexpr uint32_t kGroundOccupancyState = 1;

// Where a mission's MoveUnitToRadius victory condition sends units.
struct MissionMoveGoal {
    oa::sim::ground_orders::Point
        point{};           // the condition's point on the terrain, 16.16 world units
    std::string type_name; // the unit type it names; empty for any type
};

class Runtime final : public menu::Host,
                      public entry::SinglePlayerHost,
                      public frontend::Host,
                      public init::PreferencesHost,
                      public init::MapListHost,
                      public skirmish::Host,
                      public map_modal::Host,
                      public oa::sim::unit_spawn::AssetReader,
                      public oa::sim::scenario::DefinitionHost,
                      public oa::sim::unit_spawn::StartHost {
  public:

    /// Starts the runtime on the main menu.
    ///
    /// In order: the session display, the extension's startup hook, the sounds,
    /// the screen packages, the extension's ready hook, the preferences file (with
    /// a chosen game directory remembered), the side logos, the first map, the
    /// side table, the common fonts, the player slots and saved preferences, the
    /// saved gamma and effect volume; then the frontend enters the main menu with
    /// the extension's entry fields and the game switches' skip-intro flag, and
    /// runs its first dispatcher pass.
    ///
    /// @param options parsed command line
    /// @param assets mounted game files; must outlive the runtime
    /// @param extension the extension hooks (ExtensionList::combined); the
    ///        runtime keeps a copy, and what its context names must outlive
    ///        the runtime
    /// @param window SDL window to borrow instead of creating one; null for none
    /// @param renderer SDL renderer of `window` to borrow; null for none
    Runtime(
        Options options,
        oa::AssetStore& assets,
        const Extension& extension,
        SDL_Window* window = nullptr,
        SDL_Renderer* renderer = nullptr
    );

    /// Runs the application: the headless check or run the options ask for, or the application
    /// loop.
    ///
    /// The extension's run phases come first. A headless check runs the
    /// navigation check, a save/load, campaign or match run, or writes the
    /// snapshot, and returns. Otherwise SDL starts and a check, the benchmark or
    /// the application instance's loop runs. A showcase plays in place of the
    /// loop, and a video capture makes its video once either ends. The loop:
    /// while active it runs an idle tick
    /// whenever no event is pending and sweeps finished sound streams once more
    /// than 99 ms passed; while inactive outside a live multiplayer game it parks
    /// the music and only waits for events. The start-up half (command line,
    /// display, archives, sound and session) is main() and the constructor.
    ///
    /// Start-up differs from 3.1c's in three ways: several copies of the game
    /// may run at once, the translate.tdf language table is read for the
    /// language the command line names (English without one, as no Language
    /// setting is kept), and no AudioCD or multiplayer settings are written.
    ///
    /// @return the exit status: 0, the campaign run's result, the one an
    ///     extension run phase set, or, after the application loop, the one
    ///     ScreenServices::quit asked for
    int run();

    /// Hands the runtime a video capture started before it, so that the capture's sound
    /// device opened first. The capture takes every frame the runtime presents from then on,
    /// and run() makes its video once the loop or the showcase ends.
    ///
    /// @param capture the capture; null for none
    void take_video_capture(std::unique_ptr<VideoCapture> capture);

    /// Starts the saved game the load dialog chose.
    ///
    /// A failure is shown on the status line and stderr.
    ///
    /// @param dialog_path the dialog's SAVEGAME\<file> path under the user directory
    /// @return true when the game started
    bool start_saved_game(std::string_view dialog_path);

    /// Saves the game the save dialog was opened over.
    ///
    /// The running match, or between missions the finished one, is saved under
    /// the typed name with the current time as the game id. A failure is shown on
    /// the status line and stderr.
    ///
    /// @param dialog_path the dialog's SAVEGAME\<file> path under the user directory
    /// @param description the typed name
    /// @return true when the save was written
    bool save_dialog_game(std::string_view dialog_path, const char* description);

    /// Closes the save dialog: stops text input and returns to the screen it was opened over.
    void close_save_dialog();

    /// Replaces the text of a label on the current screen.
    ///
    /// @param name label gadget name; a screen without it is left alone
    /// @param text new text
    void set_screen_label(std::string_view name, std::string_view text);

    /// Translates interface text into the game's language (gamedata/translate.tdf).
    ///
    /// @param text text to translate, matched exactly
    /// @return its translation, or the text itself when the language has none
    std::string translate_ui(std::string_view text) override;

    /// Returns the directory that holds SAVEGAME: the one the preferences file is in.
    ///
    /// @return that directory
    [[nodiscard]] fs::path save_game_root() const;

    /// Returns the palette the current frontend screen is drawn in.
    ///
    /// @return the background's palette, else the GUI palette
    [[nodiscard]] const oa::PaletteBytes& screen_palette() const;

    /// Returns the frame position of the root of a panel drawn over another screen: the load and
    /// save dialogs and the in-game briefing. Their records are relative to it.
    ///
    /// @return the root gadget's position on those screens, else (0, 0)
    [[nodiscard]] oa::ui::display_layout::Point panel_origin() const;

    /// Returns the first row a list of the frontend screen shows while its
    /// scroll bars are bound.
    ///
    /// @param name list gadget
    /// @return the row, or nothing for a list that is not bound
    [[nodiscard]] std::optional<std::size_t> frontend_list_first(std::string_view name);

    /// Returns the row a press on a list of the frontend screen picks, as
    /// 3.1c picks it: the row under the pointer, kept on the list's page.
    ///
    /// @param name list gadget
    /// @param canvas_y pointer row on the canvas
    /// @return the row, or nothing when the list is not bound or the press misses its rows
    [[nodiscard]] std::optional<std::size_t>
    frontend_list_row_at(std::string_view name, float canvas_y);

    /// Tells whether the running match goes on this frame.
    ///
    /// It does on the match screen, which also shows the match's menus and
    /// the preferences its in-game menu opens (PREFS.GUI in the side column);
    /// the pages opened over the match (load, save, briefing) leave it.
    ///
    /// @return false without a running match
    [[nodiscard]] bool match_running() const;

  private:

    /// Runs one pass of the idle loop.
    ///
    /// The overlay and screen packages, music mood and timers, the speech queue, then, unless the
    /// application is closing, the match camera and the active screen's frame: in
    /// a match the extension's pump, the pointer's pick of the unit under it
    /// (pick_cursor_unit), the ticks the clock is worth, the outcome, the
    /// render (which rebuilds the on-screen list) and the film step, each
    /// charged to the frame's profile window. When the extension's pump asks
    /// to end the run (ScreenServices::quit), the run ends after it and the
    /// frame stops there.
    /// The F2 and Ctrl+F9 screenshot keys reach the hotkey handlers as SDL key
    /// events. 3.1c also takes a request to re-initialise the display here;
    /// the engine has no such request.
    void idle_tick();

    /// Dispatches one SDL event: the window's focus, the screen packages or else the runtime's own
    /// handling, then any screen change requested.
    ///
    /// @param event event to dispatch
    /// @param[in,out] running loop flag; cleared when the event ends the loop
    void dispatch_event(SDL_Event& event, bool& running);

    /// Tracks the window's focus as the application-active flag.
    ///
    /// A focus gain or loss sets the flag. Any other event while inactive sets it
    /// again when the window has input focus, since the loading pump and the input
    /// drain discard events, a focus gain among them.
    ///
    /// @param event event just received
    void note_window_activation(const SDL_Event& event);

    /// Tests whether the loop keeps ticking while the window is inactive.
    ///
    /// @return true during a live multiplayer game: the extension's multiplayer
    ///     state, or the Game block's live-game bit
    [[nodiscard]] bool keeps_running_inactive() const;

    /// Returns the extension's state bits for the running game.
    ///
    /// @return Extension::state's extension_state bits; 0 without the hook
    [[nodiscard]] uint32_t current_extension_state() const;

    /// Parks the CD music while the application is inactive and resumes it on activation.
    ///
    /// Going inactive stores the disc's track kinds, remembers the music kind and
    /// closes the player; on activation the player reopens with the music options
    /// and that kind.
    void park_music_while_inactive();

    /// Tests whether a control key is held, from SDL's keyboard state.
    ///
    /// @param key control key
    /// @return true while it is down
    [[nodiscard]] bool control_key_down(oa::ui::gui_input::ControlKey key) const;

    /// Marks the application as closing so no further frame runs, shows the reason when there is
    /// one, and ends the loop.
    ///
    /// The reason shows in a message box under the window's title, or goes to
    /// standard error without a window. The desktop display mode returns when
    /// the display host destroys the window.
    ///
    /// @param message reason to show; null or empty for none
    void quit_application(const char* message);

    /// Prints the developer memory report after benchmark and headless match runs; nothing when it
    /// is empty.
    void print_memory_status();

    /// Plays and captions the unit announcements the offline services present this frame.
    void present_unit_announcements();

    /// Reports a failed match tick; the tick is skipped rather than stopping the match.
    ///
    /// The message goes to the status line, standard error (the game's log
    /// when it plays) and the match message log. Repeats of the same message are counted and reported
    /// only when the count reaches a power of two.
    ///
    /// @param message the tick's error
    void report_match_tick_error(std::string_view message);

    /// Returns the match clock's units per real second.
    ///
    /// Game speed 10 at a 0.1 rate is 30 Hz. The +/- keys change only the actual
    /// rate; folding the speed into the clock scale as well made speed 20 run at
    /// 4x instead of 2x.
    ///
    /// @return 30
    uint32_t match_clock_scale() const;

    /// Returns the frontend clock package screens step on, in game ticks (30 per second).
    ///
    /// @return the tick a check fixed, else SDL's milliseconds on the match clock
    [[nodiscard]] uint32_t frontend_tick() const;

    /// Tests whether a package overlay owns the main menu's frame and input.
    ///
    /// Frontend state 7 only pops input and presents; no TA panel draws or takes
    /// input.
    ///
    /// @return true on the main menu in that state
    [[nodiscard]] bool frame_owned_by_package() const;

    /// Returns the host clock in milliseconds.
    ///
    /// @return the steady clock; with Options::fixed_clock, the match tick times
    ///     kFixedClockMsPerTick instead
    [[nodiscard]] uint32_t clock_milliseconds() const;

    /// Runs the match ticks the clock time since the last frame is worth at the current speed.
    ///
    /// The clock first takes the pause bit of the match's Game.sim_run_flags
    /// (clock_flags_with_pause): while it is set the clock's time moves on and
    /// no tick is owed. Each tick runs unless the extension's simulation step
    /// takes it; a failing tick goes to report_match_tick_error(). After any
    /// tick at most one expired line of the message log is retired.
    ///
    /// @param now_ms clock_milliseconds() of this frame
    void advance_match_clock(uint32_t now_ms);

    /// Tells whether the running match's clock steps this frame.
    ///
    /// The match goes on (match_running), its ticks are not blocked, and
    /// match_clock_runs allows it for the kind of match (shared with other
    /// players' machines or not), an open menu (match_paused_ before the
    /// outcome) and the outcome.
    ///
    /// @return true when idle_tick advances the match clock
    [[nodiscard]] bool match_clock_steps() const;

    /// Returns the match clock as a save stores it: its run flags carry the
    /// running match's pause bit (clock_flags_with_pause), which the clock
    /// itself takes only as it steps, so a match paused while its menu holds
    /// it is saved paused.
    ///
    /// @return match_timing_ with the pause bit of Game.sim_run_flags; the
    ///     clock unchanged without a running match
    [[nodiscard]] oa::base::game_loop::Timing saved_match_timing() const;

    /// Returns how many rows the map selection list shows.
    ///
    /// @return the MAPNAMES list height over its item height (the font's line
    ///     height plus one unless the list sets one), at least 1
    [[nodiscard]] std::size_t map_visible_rows();

    /// Returns the first map row the list shows, scrolled so the selected map is the last row once
    /// it is past the first page.
    ///
    /// @return row index
    [[nodiscard]] std::size_t map_first_visible();

    /// Selects a map of the list, previews it and redraws the screen.
    ///
    /// @param index row of the bound map names; out of range does nothing
    void preview_map_index(std::size_t index);

    /// Selects the map row under a canvas row of the centred map modal.
    ///
    /// @param canvas_y pointer row in canvas pixels; above the list does nothing
    void select_map_row_at(float canvas_y);

    /// Closes the map selection modal, when open, and sets the skirmish screen up again.
    void close_map_modal();

    /// Clicks a gadget of the current screen as the navigation check does: pointer, press, redraw,
    /// release and activation at its centre.
    ///
    /// Throws std::runtime_error when the screen lacks the gadget or the press
    /// is not kept to the release.
    ///
    /// @param gadget_name gadget to click
    void exercise_click(std::string_view gadget_name);

    /// Runs the headless navigation check from the main menu.
    ///
    /// Walks SINGLE.GUI and SKIRMISH.GUI into a skirmish and through the match,
    /// map selection, campaign, save, end-game and dialog checks, writing frames
    /// to local/reports. Game data with no skirmish map runs
    /// check_navigation_without_maps() instead. Throws std::runtime_error at the
    /// first failure.
    void check_navigation();

    /// Runs the headless navigation check over game data with no skirmish map, such as the
    /// Total Annihilation demo (1997).
    ///
    /// MULTI and SINGLE.GUI's Skirmish show the notice, whose website button asks the web link
    /// hooks for project_website_address and closes it, and whose OK returns to the main menu;
    /// the entries the data cannot open are grayed out or hidden; New Campaign opens on a side
    /// with a campaign and Start opens the first mission's briefing; a victory in the last
    /// mission ends the campaign with the notice over the main menu when the game offers no
    /// movies. Frames go to local/reports. Throws std::runtime_error at the first failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_navigation_without_maps(const fs::path& report_directory);

    /// Steps the end screen of a finished mission until it leaves for the frontend.
    ///
    /// @return true when the end screen was left within the frames a check allows
    bool step_endgame_until_left();

    /// Leaves the match for the skirmish menu without the end-of-game screen.
    void return_to_skirmish_menu();

    /// Checks that a skirmish presents victory once no opponent has a unit left.
    ///
    /// From the skirmish menu, every other player's units are swept, as a
    /// commander's death sweeps its player's units under the commander rule, and
    /// the match ticks until it finishes. Writes native-match-victory.ppm and
    /// returns to the skirmish menu; throws std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frame is written to
    void check_skirmish_victory(const fs::path& report_directory);

    /// Checks MSGBOX.GUI, CDCHECK.GUI and HELP.GUI stacked over the current screen.
    ///
    /// Throws std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_dialogs(const fs::path& report_directory);

    /// Checks the dialogs and menus of a live match presented through SDL.
    ///
    /// HELP.GUI from the pause menu, read back from the renderer: the layered
    /// presenter stays in use, the panel is centred right of the drawn side
    /// column, the options panel under it is darkened, every other pixel
    /// matches the paused frame, and OK at its presented position closes it;
    /// then check_in_game_briefing(). Over a new skirmish: the window's close
    /// request and a held Escape; the preferences the in-game menu opens,
    /// PREFS.GUI in the side column with the OPTIONS lightbar sweeping over
    /// it and the battlefield beside it, each tab's sub-panel (SOUNDSRT,
    /// SPEEDSRT, VISUALRT, MUSICRT) merged beside the tabs, the FXVOL and GAME
    /// sliders, Cancel, Enter, Escape, the quick keys and F2, and a close
    /// request over them whose CHOICE2 returns to the in-game menu; a unit's
    /// speech with its order's caption; a return label in the exit menus and
    /// on ENDMSN.GUI, whose MAIN MENU then leaves the pointer's picture as it
    /// is; and the system's quit, whose CHOICE1 surrenders and ends the
    /// run. Throws std::runtime_error on a failure.
    void check_match_dialogs();

    /// Checks BRIEFING.GUI opened by MISSION on the pause menu of a campaign mission.
    ///
    /// On a 640x480 window and on the default window, each read back from the
    /// renderer and written to native-match-briefing-WxH.ppm: the panel sits at
    /// the position BRIEFING.GUI gives its root, which centres it on the 640x480
    /// window, shows its bitmap in the match palette outside its records, and
    /// every pixel outside it matches the paused match; its first row holds the
    /// first line of the mission's briefing file as the frontend briefing shows
    /// it, a click on MOREBAR turns the page and OK at its presented position
    /// returns to the paused match with the pause menu. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_in_game_briefing(const fs::path& report_directory);

    /// Checks the kills board F4 pins in a skirmish.
    ///
    /// The board slides in over the battlefield's top-right corner in 18 frames,
    /// opens no dialog and changes nothing else on the frame. Its margin is the
    /// battlefield through shade row 8, the local player's row starts with the
    /// colour logo and the "Kills" header is drawn. A second F4 slides it away
    /// again. Throws std::runtime_error on a failure.
    void check_kill_board();

    /// Checks MULTI on the main menu, twice.
    ///
    /// MULTI opens MSGBOX.GUI over the main menu with the one line saying
    /// multiplayer is not available, and the box's OK (first round) or Enter
    /// (second) closes it with the main menu still up; over game data with no
    /// multiplayer map it opens the missing-content notice instead. --snapshot
    /// takes the first round's frames as <stem>-box.ppm and <stem>-closed.ppm.
    /// An extension with a check_multiplayer_menu hook runs that instead.
    void check_multiplayer_menu();

    /// Runs the engine's own check of MULTI's "not available" message box, which
    /// check_multiplayer_menu runs without an extension check.
    void check_multiplayer_unavailable();

    /// Sends the multiplayer check a left-button pointer event at a canvas point through the SDL
    /// presenter, then runs a frame.
    ///
    /// @param type SDL mouse event type
    /// @param x canvas column
    /// @param y canvas row
    void multiplayer_check_pointer(SDL_EventType type, int32_t x, int32_t y);

    /// Clicks a canvas point for the multiplayer check: motion, press and release, then the
    /// settling frames.
    ///
    /// @param x canvas column
    /// @param y canvas row
    void multiplayer_check_click(int32_t x, int32_t y);

    /// Runs the frames a click leaves the menus to settle in the multiplayer check.
    void multiplayer_check_settle();

    /// Reads the renderer's target back into the video capture, when one runs, and into the
    /// surface a check asked for, when one is asked for. Called with each frame drawn, before it
    /// is presented.
    ///
    /// Throws std::runtime_error when the pixels cannot be read or the capture fails.
    void capture_render_target();

    /// Composes the frontend dialogs over a layered match.
    ///
    /// The panel below the bottom dialog is darkened in the HUD source, and the
    /// panels become a canvas-sized layer drawn over the world and HUD.
    ///
    /// @return false when no dialog is open
    bool compose_match_dialog_layer();

    /// Runs the benchmark: a skirmish and a campaign mission, each static and scrolling, then the
    /// memory report.
    ///
    /// @param frames frames per scene
    void run_benchmark(std::size_t frames);

    /// Starts a two-player skirmish from the main menu through SINGLE and SKIRMISH.
    ///
    /// Throws std::runtime_error when the map lacks two start positions or Start
    /// does not enter a match.
    void start_benchmark_skirmish();

    /// Places two lines of light infantry facing each other beside the local commander, close
    /// enough to engage at once.
    ///
    /// Throws std::runtime_error without the local commander or the ARMPW and
    /// CORAK types.
    ///
    /// @param per_side units in each line
    void spawn_combat_armies(std::size_t per_side);

    /// Runs a headless match and logs its timings and list sizes.
    ///
    /// Starts a two-player skirmish at the options' size, zoom and camera,
    /// optionally spawns two armies and the reclaim check, then simulates and
    /// composes every tick.
    ///
    /// @param ticks ticks to run
    void run_headless_match(std::size_t ticks);

    /// Runs the --campaign mission headless with the computer player active.
    ///
    /// Every distinct tick error is logged with the run's tick it first
    /// appeared. The run ends at the first victory or defeat unless
    /// --past-outcome keeps it ticking. With --give-orders the local player's
    /// units get give_mission_orders() at the start and every 300 ticks of the
    /// mission, and the outcome goes on to the end screen and, after a
    /// victory, into the next mission (advance_headless_campaign()).
    ///
    /// The unit counts ("campaign tick"), the outcome and the result's outcome
    /// tick count the ticks of the mission being played, from its start or
    /// from where --restart-at started it over; --restart-at, the errors and
    /// the result's tick and failure counts are the whole run's. A run that
    /// played one mission from its start counts both alike.
    ///
    /// @param ticks ticks to run at most
    /// @return the process exit status: nonzero when any tick failed
    [[nodiscard]] int run_headless_campaign(std::size_t ticks);

    /// Starts the --campaign mission --mission names through the Any Mission list, its briefing and
    /// Start.
    ///
    /// Throws std::runtime_error when no campaign has the name or the mission
    /// index is out of range.
    ///
    /// @return the mission's file
    std::string start_headless_campaign_mission();

    /// Goes from a finished campaign mission to the end screen and, after a
    /// victory, into the next mission, as a player does: dismisses the
    /// outcome, steps the end screen to its panel, then presses its Start and
    /// the next briefing's Start. Prints "campaign advance:" with the run's
    /// tick or, after a defeat or when no mission follows, "campaign end:".
    ///
    /// Throws std::runtime_error when a screen on the way does not open.
    ///
    /// @param run_tick the headless run's tick at which the mission ended
    /// @return the next mission's file, or empty after a defeat or when the
    ///     campaign has no next mission
    std::string advance_headless_campaign(std::size_t run_tick);

    /// Writes the current screen to <snapshot stem>-<stage>.ppm when --snapshot
    /// names a file.
    ///
    /// @param stage name of the screen in the file name, such as "briefing-1"
    void write_stage_snapshot(std::string_view stage);

    // Marks the unit types a match may use: sets the catalog bit of each
    // header (headers[type_id], by its FBI hash) that stays in the table.
    struct UnitFilter {
        void* context{};
        void (*mark_units)(void* context, oa::UnitDef* headers, uint32_t count){};
    };

    // The match world from the selected map and skirmish slots. A skirmish
    // places each enabled slot's commander on its marker; a multiplayer
    // launch or a replay starts empty at the session's unit limit. A replay's
    // local slot only watches and cannot be defeated. A multiplayer launch
    // takes the multiplayer outcome rules (the game-end check picks them by
    // game kind).
    struct MatchBootstrap {
        uint16_t units_per_player{kSkirmishUnitsPerPlayer};
        bool place_commanders{true};
        bool defeat_allowed{true};
        // Decides the unit table of a session that agreed on one.
        UnitFilter unit_filter{};
        // Seat the skirmish roster's players, as the Start
        // click, a resumed skirmish and Restart do.
        bool seat_roster{false};
        bool multiplayer{false};
    };

    /// Builds the match world from the selected map and skirmish slots and enters the match.
    ///
    /// A new attempt first drops the previous match and end screen, then loads,
    /// behind the loading screen, the movement classes and weapons, the terrain
    /// and map features, the unit catalog and definitions, the feature links and
    /// sight tables, applies the session's rules, binds the match's hosts, seats
    /// the players and places the commanders or the mission's units. Throws
    /// std::runtime_error when any part cannot be loaded.
    ///
    /// @param bootstrap how the players are placed and seated and which units
    ///     the session allows
    void bootstrap_match(const MatchBootstrap& bootstrap);

    /// Returns whose sight, radar and economy the match view shows: the local slot, a replay's
    /// watcher included.
    ///
    /// @return player index
    [[nodiscard]] uint8_t match_view_player() const noexcept { return match_local_player_; }

    /// Starts --reclaim-check: orders the local commander to reclaim the nearest metal-bearing
    /// feature.
    ///
    /// Throws std::runtime_error without a match, the local commander, the map
    /// plots or such a feature.
    void begin_reclaim_check();

    /// Notes each tick whether the reclaim check's credit arrived: metal produced in one tick of at
    /// least half the feature's metal.
    void tick_reclaim_check();

    /// Ends --reclaim-check: prints the feature and the player's metal before and after and whether
    /// the player was credited.
    ///
    /// Throws std::runtime_error when the check did not start.
    void finish_reclaim_check();

    /// Times a benchmark scene and prints its phase times.
    ///
    /// Each frame drains the events, scrolls the camera back and forth when asked,
    /// steps the simulation and composes the frame. Throws std::runtime_error
    /// without an active match.
    ///
    /// @param label scene name printed with the times
    /// @param frames frames to run
    /// @param scroll true to scroll the camera across the map
    void benchmark_scene(std::string_view label, std::size_t frames, bool scroll);

    /// Runs one match tick, advancing the match clock's tick first.
    void step_match_simulation();

    /// Caches the allsound.tdf sounds and loads the unit sound categories the FBI loader resolves
    /// soundcategory against, as engine start-up does.
    void load_all_sounds();

    /// Shows a screen: leaves the current one and enters the new one.
    ///
    /// The load and save dialogs remember the screen they open over, and screens
    /// after a match read the options the match changed. An unregistered screen
    /// falls back to the map selection's package.
    ///
    /// @param screen screen to show
    void load(Screen screen);

    /// Selects a named frontend background through the background cache.
    ///
    /// Bitmaps are read as PCX through the asset store; one that cannot be read
    /// is fatal (std::runtime_error "Unable to load <path>"). The backdrop and
    /// palette land on the loaded screen resources and the frontend Game block
    /// records the name, as oa::ui::frontend::load_resource_palette() describes.
    ///
    /// @param name background name without directory or extension; null selects none
    /// @param redraw whether to clear and present the frame before loading
    /// @param apply whether to apply the palette when the bitmap becomes the backdrop
    /// @param defer whether to only load and cache, leaving the backdrop alone
    /// @return 1 when a bitmap or a null name was set, 0 when deferred or nothing loaded
    int32_t load_named_background(const char* name, bool redraw, bool apply, bool defer);

    /// Returns the Game block the frontend screens share.
    ///
    /// An extension's multiplayer screens replace their block when they reset, so
    /// it is looked up on every call.
    ///
    /// @return the extension's block, else the runtime's own
    [[nodiscard]] oa::Game& frontend_game();

    /// Redraws the current screen into the frame: the match, the loading screen, a package-owned
    /// main menu or a frontend screen with its dialogs, packages and cursor.
    void rebuild_surface();

    friend struct BuiltinScreens;
    // The check host's entries (check_host.hpp, runtime_check_host.cpp).
    friend struct CheckHostAccess;
    // Defined by the one extension that adds Runtime members, whose hooks
    // reach the runtime through it; to be replaced by hooks and declared
    // headers (src/app/README.md).
    friend struct RuntimeExtension;

    /// Registers the screen packages of screens.inc and the extension's.
    ///
    /// Dispatcher steps nobody took are bound to a handler that ignores them.
    /// Throws std::runtime_error when a registration was rejected or the
    /// built-in screens are missing.
    void register_screens();

    /// Returns the context screen packages are called with.
    ///
    /// @param input the input event being dispatched; null outside dispatch
    /// @return the runtime's services, assets, frame, match and current screen
    [[nodiscard]] ScreenContext screen_context(const ScreenInput* input = nullptr);

    /// Offers an SDL event to the overlays, topmost first, then to the current screen's package.
    ///
    /// When an overlay or the screen's package takes a pointer event, the
    /// cursor sprite moves to it here, as the built-in handler never sees it.
    ///
    /// @param event event to offer
    /// @return true when a package took it
    bool dispatch_screen_input(const SDL_Event& event);

    /// Ticks the overlays and the current screen's package, then runs a pending ending and any
    /// requested screen change.
    void tick_screen_packages();

    /// Draws the current screen's package, then the overlays over it.
    void draw_screen_packages();

    /// Shows the screen a package requested, if any, then runs the frontend
    /// pass a package asked for (ScreenServices::run_frontend); when a package
    /// asked to end the run, ends it instead (finish_quit_request).
    ///
    /// The pass is the one a pointer press runs: the unit header step, then
    /// the dispatcher, and a screen the pass requested is shown after it.
    /// Requests since the last call make one pass; none runs while a match
    /// is on screen, and the request is dropped.
    void apply_screen_request();

    /// Ends the run a package asked to end (ScreenServices::quit), once the
    /// event or frame that asked has been handled: a running match is left
    /// first, then the reason shows and the main loop stops. Nothing when no
    /// package asked.
    ///
    /// apply_screen_request calls it before anything else, and the frame calls
    /// it after the extension's pump.
    void finish_quit_request();
    ScreenRegistry screens_{};
    std::optional<ScreenId> pending_screen_;
    // A package asked for a frontend pass (ScreenServices::run_frontend).
    bool frontend_pass_requested_{};
    // A package asked to end the run (ScreenServices::quit), with this reason
    // (empty for none); finish_quit_request ends it.
    bool quit_requested_{};
    std::string quit_reason_{};

    /// Returns the viewed player's side prefix for side-specific art.
    ///
    /// @return "cor" when the side's commander name starts with C, else "arm"
    std::string match_side_prefix() const;

    /// Returns the unit definition of a match unit's type.
    ///
    /// @param unit unit id
    /// @return its definition, or null for no match, no unit or a type outside
    ///     the table
    const oa::data::unit_definitions::UnitDefinition* definition_for(uint16_t unit) const;

    /// Returns the type name a match unit's GUI art is found by.
    ///
    /// @param unit unit id
    /// @return the type name, or empty for no match, no unit or an unknown type
    std::string unit_gui_name(uint16_t unit) const;

    /// Returns the name the unit readout shows for a match unit.
    ///
    /// @param unit unit id
    /// @return the side and display name, the display name alone, the unit name,
    ///     or unit_gui_name() when the definition gives none
    std::string unit_info_name(uint16_t unit) const;

    /// Returns the GUI type name of the selected match unit.
    ///
    /// @return as unit_gui_name()
    std::string selected_unit_gui_name() const;

    /// Finds a GAF sequence by name, ignoring ASCII case.
    ///
    /// @param archive archive to search
    /// @param name sequence name
    /// @return the sequence, or null when the archive lacks it
    const oa::formats::gaf::Sequence*
    gaf_sequence(const oa::formats::gaf::Archive& archive, std::string_view name) const;

    /// Returns an animation file from the cache, loading anims/<name>.gaf on first use.
    ///
    /// The lookup ignores case; "fx" and an empty name are the match FX archive.
    /// A missing file stays an empty archive; 3.1c stops with a fatal error
    /// there.
    ///
    /// @param name animation file name without extension
    /// @return the cached archive
    const oa::formats::gaf::Archive& explosion_gaf_archive(std::string_view name);

    /// Appends the sequences of a GAF file to an archive.
    ///
    /// A file that is missing or fails to parse is remembered and skipped from
    /// then on; a parse failure is reported on stderr.
    ///
    /// @param[in,out] destination archive the sequences go to
    /// @param path GAF file path
    void append_gaf_file(oa::formats::gaf::Archive& destination, std::string_view path);

    /// Blits the covered pixels of a rendered GAF frame into an RGB image through a palette,
    /// clipped to the image.
    ///
    /// @param[in,out] destination RGB image
    /// @param frame rendered frame; pixels without coverage are skipped
    /// @param destination_x image column of the frame's first column
    /// @param destination_y image row of the frame's first row
    /// @param palette 4 bytes per colour
    void blit_gaf_frame(
        oa::Image& destination,
        const oa::formats::gaf::RenderedFrame& frame,
        int destination_x,
        int destination_y,
        const oa::PaletteBytes& palette
    );

    /// Blits the first frame of a named GAF sequence into an RGB image; nothing when the sequence
    /// is missing or does not render.
    ///
    /// @param[in,out] destination RGB image
    /// @param archive archive holding the sequence
    /// @param name sequence name, matched ignoring case
    /// @param x image column of the frame's first column
    /// @param y image row of the frame's first row
    /// @param palette 4 bytes per colour
    void overlay_gaf_sequence(
        oa::Image& destination,
        const oa::formats::gaf::Archive& archive,
        std::string_view name,
        int x,
        int y,
        const oa::PaletteBytes& palette
    );

    // Cursor table index of cursornormal. The runtime keeps the cursor table
    // itself, in place of its entry in Game.sprite_and_effect_tables.
    static constexpr uint8_t kNormalCursor = 19;
    static constexpr std::array<std::string_view, 22> kCursorNames{
        "",
        "cursorattack",
        "cursorairstrike",
        "cursortoofar",
        "cursorcapture",
        "cursordefend",
        "cursorrepair",
        "cursorpatrol",
        "cursorpickup",
        "cursorteleport",
        "cursorrevive",
        "cursorreclamate",
        "cursorload",
        "cursorunload",
        "cursormove",
        "cursorselect",
        "cursorfindsite",
        "cursorred",
        "cursorgrn",
        "cursornormal",
        "cursorhourglass",
        "pathicon"
    };

    /// Loads CURSORS.GAF and PALETTE.PAL for the software cursor and starts the normal cursor, as
    /// session start does.
    ///
    /// Without cursors the platform cursor stays in use.
    void load_game_cursors();

    /// Returns the GAF sequence of a cursor table entry.
    ///
    /// @param index cursor table index (kCursorNames)
    /// @return the sequence, or null for an empty or unknown entry or one
    ///     CURSORS.GAF lacks
    const oa::formats::gaf::Sequence* cursor_sequence(uint8_t index) const;

    /// Starts a cursor unless it is the current one.
    ///
    /// A cursor without a sequence falls back to 19 (kNormalCursor).
    ///
    /// @param index cursor table index (kCursorNames)
    void select_game_cursor(uint8_t index);

    /// Picks the match pointer's cursor through the order-cursor resolution.
    ///
    /// Writes the local player, the unit under the pointer, the armed order and
    /// the ground point into the Game block first, and stores the cursor cell.
    ///
    /// @return a cursor table index: the normal cursor over a gadget or without a
    ///     match, the build cursor when the resolution fails
    uint8_t pick_match_cursor();

    /// Binds the GUI context's devices.
    ///
    /// The frontend clock, the SDL pointer as the latest move (the queue is
    /// empty; SDL events go straight to their handlers) and the software cursor's
    /// picture.
    void bind_gui_context();

    /// Tests whether the pointer is over the root of the panel on top.
    ///
    /// @return true over the in-match panel on the match canvas, or over the
    ///     frontend screen's root
    [[nodiscard]] bool pointer_over_top_panel();

    /// Steps and draws the software cursor, the cursor half of the GUI update.
    ///
    /// The elapsed ticks, the cursor step and pointer read, the match's order
    /// cursor, then the cursor for the pointer over or off the top panel; the
    /// cursor is drawn into the frame unless the match presents in layers.
    void tick_and_draw_cursor();

    /// Loads an in-match panel layout as the match HUD with the viewed side's chrome.
    ///
    /// The panel's buttons keep their states; the side's interface GAF, side tile
    /// and commongui.gaf supply the art.
    ///
    /// @param layout GUI file of the panel
    /// @return false when the panel cannot be loaded, which is reported on stderr
    bool load_match_hud_layout(const std::string& layout);

    /// Shows the side's general order page for the one selected unit, or for none, from the
    /// selection summary.
    void show_match_orders_page();

    /// Pauses the match and opens the in-game options menu (ARMOPT.GUI); nothing once the match is
    /// finished.
    void show_match_pause_menu();

    /// Resumes a paused match and shows the order panel for the selection; nothing once the match
    /// is finished.
    ///
    /// The preferences a match opens are left first, keeping what they set,
    /// as their OK leaves them.
    void resume_match_pause();

    /// Answers a request to close the window during a running match: opens the surrender
    /// confirmation (YESORNO) unless it is already up.
    void request_match_close();

    /// Brings a page opened over a running match (the load and save pages, the in-game
    /// briefing) back to the match's menu, so that a close request asks there.
    ///
    /// @return true when the match is on screen again; false when no such page is open
    bool return_to_match_for_close();

    /// Answers Escape over a paused match: the surrender confirmation takes it as its Escape
    /// default (No), the preferences as theirs (OK), any other menu resumes the match.
    void escape_match_menu();

    /// Answers Enter over a paused match: the surrender confirmation takes it as its Enter
    /// default (No).
    ///
    /// @return true when the confirmation took the key; false over any other menu
    bool enter_match_menu();

    /// Steps and draws the lightbar sweep of the preferences a match opens while it runs.
    ///
    /// Called as the match frame draws its menus; the sweep steps once a frame
    /// and is drawn over the side column and the battlefield beside it.
    void draw_options_lightbar();

    /// Binds the running match's speech with its own captions to the offline services.
    void bind_match_speech();

    /// Fills the covered pixels of a rendered GAF frame into the HUD source, one source pixel each.
    ///
    /// @param frame rendered frame; pixels without coverage are skipped
    /// @param dest_x source column of the frame's first column
    /// @param dest_y source row of the frame's first row
    void blit_gaf_source(const oa::formats::gaf::RenderedFrame& frame, int dest_x, int dest_y);

    /// Loads igtitles.gaf, the titles drawn over the battlefield and the end screen, once.
    void ensure_match_titles();

    /// Finishes the match once the outcome is decided: pauses it and drops the armed command.
    ///
    /// The frame drawn next shows the victory or defeat title over the
    /// battlefield, and the match leaves for the end-of-game screen right after
    /// it (finish_match_outcome()), with no input asked for.
    void present_match_outcome();

    /// Leaves the finished match for the end-of-game screen once the extension is ready.
    ///
    /// Every finished game, skirmish and multiplayer included, goes to the
    /// end-of-game screen, which starts by darkening the match's last frame;
    /// its step 4 asks a campaign for the disc. While the extension is not
    /// ready the match stays on its outcome and each frame asks again.
    void finish_match_outcome();

    /// Draws the victory or defeat title at the top centre of the end-of-game screen.
    ///
    /// At (screen_width / 2, 0x1c) with the GAF origin as the hotspot, as the
    /// panel set-up draws it.
    void draw_campaign_end_title();

    /// Clicks the hovered gadget of the end-of-game panel.
    void activate_campaign_end_gadget();

    /// Returns the end-of-game screen's background.
    ///
    /// @return "outcome1" while a campaign can continue, else "outcome0"
    [[nodiscard]] const char* campaign_end_background();

    /// Binds the ENDMSN.GUI panel to the finished game and runs the end-of-game screen, which opens
    /// the panel at its outcome step.
    void enter_campaign_end();

    /// Sets the end-of-game panel up over the loaded ENDMSN.GUI and fills its Missions list.
    ///
    /// @param reopened the stat bar layout to lay out again when a briefing's Back
    ///     reopens the panel; null for the first opening
    void open_end_panel(oa::ui::campaign::ScoreLayout* reopened);

    /// Makes the end-of-game panel's closing call (the click handler with no selection) as another
    /// screen replaces it.
    void close_end_panel();

    /// Closes the end-of-game panel and releases the finished game unless a screen that returns to
    /// the panel (the save dialog) keeps it.
    void leave_campaign_end();

    /// Presses the end-of-game panel's default control for Enter.
    ///
    /// The panel's Enter control, Start while a campaign can continue, otherwise
    /// the focused Main Menu; nothing when it is inactive.
    void activate_end_panel_default();

    /// Clicks a gadget of the end-of-game panel through the panel's click handler.
    ///
    /// A multi-stage button steps first. Start or a Missions row binds that
    /// mission and opens its briefing, whose Back reopens this panel; a changed
    /// difficulty is saved.
    ///
    /// @param gadget gadget index in ENDMSN.GUI
    void click_end_panel(std::size_t gadget);

    /// Returns the campaign object: the campaign file the mission screens share.
    ///
    /// @return the campaign runtime's file
    [[nodiscard]] oa::data::campaign::CampaignFile& campaign_object();

    /// Returns the environment the campaign object loads with: the game files and the campaign
    /// difficulty.
    ///
    /// @return the environment
    [[nodiscard]] oa::data::campaign::CampaignEnv campaign_object_env();

    /// Draws an igtitles.gaf title at the battlefield centre.
    ///
    /// The GAF origin is the hotspot and lands on the battlefield centre, which
    /// the live canvas and side column give ((screen_width + 0x80) / 2,
    /// screen_height / 2 on a 640x480 screen: 384, 240); the title scales with
    /// the chrome.
    ///
    /// @param name title sequence (igvictory, igdefeat, ...)
    void draw_igtitle(std::string_view name);

    /// Draws the title over a finished or paused match.
    ///
    /// A finished match shows the victory or defeat title, none for a watcher.
    /// The paused title shows while the pause bit of Game.sim_run_flags is set
    /// or an open menu holds a match played on this machine alone; the menu of
    /// a shared match draws its panel over the running game.
    void draw_end_overlay();

    /// Clicks a named control of the in-game options panels and carries out the action the panel
    /// asks for.
    ///
    /// A multi-stage button steps first. The action may open the save, briefing,
    /// game settings, help, exit or restart panels, restart the mission, return
    /// to the main menu or leave the game. Over the preferences the options
    /// handlers take the click: a slider takes none (its bar and arrows take
    /// the pointer, preferences_bar_moved), a tab
    /// merges its sub-panel into PREFS.GUI, OK and Cancel close them.
    ///
    /// @param name control name in the loaded panel
    void activate_pause_gadget(std::string_view name);

    /// Opens EXITMENU's RESTART: RESTART.GUI beside the HUD strip over its art.
    ///
    /// The dialog is set up with the name the campaign object holds (a skirmish's
    /// is its map's).
    void open_restart_dialog();

    /// Runs the game frame's restart branch over this session.
    ///
    /// leave_match() ends the game, the frontend mode's load is the campaign
    /// start or the skirmish's match view, and a restart that cannot load
    /// returns to the main menu.
    void restart_match();

    /// Opens the Game Settings sheet MISSION shows outside a campaign.
    ///
    /// GAMEOPTIONS.GUI beside the HUD strip over its art, with a label row and a
    /// value row per rule of the running game.
    void open_game_settings_sheet();

    /// Checks the Game Settings sheet as the game builds and draws it.
    ///
    /// Each row is a label in GUI font slot 1 (hattfont11) centred in its 0x6e or
    /// 0x78 pixel column, so "Commander Death:" and "Starting Locations:" show
    /// whole, with blank columns either side. Throws std::runtime_error on a
    /// failure.
    void check_game_settings_sheet();

    /// Returns the running game's rules for the Game Settings sheet.
    ///
    /// A multiplayer game's rules are the host's game options; the others come
    /// from the Game block and the skirmish roster.
    ///
    /// @return the sheet's view of the rules
    [[nodiscard]] oa::ui::frontend::GameSettingsView game_settings_view();

    /// Shows the pause menu's own panels over the battlefield.
    ///
    /// EXITMENU, YESORNO, RESTART, GAMEOPTIONS, the team panels and the
    /// preferences' sub-panels lie over the battlefield; they render into the
    /// HUD source with the side panels, and the battlefield pass shows them:
    /// RESTART.GUI and GAMEOPTIONS.GUI whole over their art, the team panels
    /// whole over the side panel's tile, PREFS.GUI's part beside the side column
    /// at the side column's scale, the others (which have no art here) control
    /// by control.
    void draw_battlefield_panel();

    // ---- Scroll bars (runtime_scroll_bars.cpp) ----

    /// Binds the frontend screen's scroll bars and readies its lists, as the
    /// first draw of its panel does in 3.1c.
    ///
    /// The bars take the panel's own SLIDERS art when `sprites` is named after
    /// `layout`, else the shared art of COMMONGUI.GAF, and the panel font's
    /// line height paces the lists.
    ///
    /// @param layout GUI file the screen's panel was loaded from
    /// @param sprites GAF file it was loaded with
    void bind_frontend_scrolls(std::string_view layout, std::string_view sprites);

    /// Binds the match HUD panel's scroll bars and readies its lists from a
    /// gadget on, as the first draw of a panel does; those bound before stay.
    ///
    /// Its own GAF is anims/<GUI name>.GAF and the shared art comes from the
    /// HUD's COMMONGUI.GAF sequences.
    ///
    /// @param first first gadget bound
    void bind_hud_scrolls(std::size_t first);

    /// Returns the frontend screen's scroll bars while they are bound to its layout.
    ///
    /// @return the bars, or null when the layout was replaced since they were bound
    [[nodiscard]] renderer::LayoutScrolls* frontend_scrolls();

    /// Returns the match HUD panel's scroll bars while they are bound to its layout.
    ///
    /// @return the bars, or null when the layout was replaced since they were bound
    [[nodiscard]] renderer::LayoutScrolls* hud_scrolls();

    /// Maps a canvas point to the frontend screen panel's coordinates, as the
    /// pointer's hit test does.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return the point in the panel's coordinates
    [[nodiscard]] oa::ui::display_layout::Point frontend_panel_point(float x, float y) const;

    /// Maps a canvas point of a match to the HUD layer's 640x480 source.
    ///
    /// The preferences' sub-panel over the battlefield takes its own rows,
    /// down to its bottom, wherever the window puts the bottom bar
    /// (preferences_panel_rows); elsewhere the chrome's mapping applies.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return the point in HUD source coordinates
    [[nodiscard]] oa::ui::display_layout::Point hud_source_point(float x, float y) const;

    /// Routes a pointer event to the scroll bars of the panel it is over.
    ///
    /// A press on a shown bar or arrow holds it and is kept from the panel's
    /// buttons; the release of a held bar is too. Moves are passed on.
    ///
    /// @param event the SDL pointer event
    /// @param x canvas column of the pointer
    /// @param y canvas row of the pointer
    /// @return true when the scroll bars took the event
    bool route_scroll_pointer(const SDL_Event& event, float x, float y);

    /// Runs the held scroll bar, or arrow, for this frame at the frontend's 30 Hz tick.
    void tick_scroll_bars();

    /// Runs the handler of a bar whose knob moved: an options slider's
    /// callback, the share panel's amounts, or a list that follows it.
    ///
    /// @param over_hud true for a bar of the match HUD panel, false for the frontend screen's
    /// @param gadget the bar's gadget
    void scroll_bar_changed(bool over_hud, int32_t gadget);

    /// Fills a list of the frontend screen with rows and shows or hides its scroll bar.
    ///
    /// @param name list gadget
    /// @param count rows
    void fill_frontend_list(std::string_view name, std::size_t count);

    /// Selects a row of a frontend list and brings it into view, moving its bar's knob.
    ///
    /// @param name list gadget
    /// @param row row selected
    void select_frontend_list_row(std::string_view name, std::size_t row);

    /// Tells whether a panel whose first draw binds its scroll bars only once
    /// its setup has run: NEWGAME.GUI, which loads undrawn.
    ///
    /// @param screen the screen loaded
    /// @return true for the new campaign and any mission screens
    [[nodiscard]] static bool first_draw_after_setup(Screen screen);

    /// Binds the scroll bars of a panel its setup has already filled, as its
    /// first draw does: each list the setup filled is filled again with its
    /// rows, and its selected row brought into view.
    ///
    /// @param layout GUI file the screen's panel was loaded from
    /// @param sprites GAF file it was loaded with
    void bind_set_up_frontend_scrolls(std::string_view layout, std::string_view sprites);

    /// Moves an options slider of the frontend screen to its bar's knob and runs its callback.
    ///
    /// @param gadget the slider's gadget
    void options_bar_moved(std::size_t gadget);

    /// Moves a slider of the match's preferences to its bar's knob and runs
    /// its callback; the match takes the options it sets.
    ///
    /// @param gadget the slider's gadget
    void preferences_bar_moved(std::size_t gadget);

    /// Takes the share panel's amount from a METAL or ENERGY bar, or the recipient list's first row.
    ///
    /// @param gadget the bar's gadget
    void share_bar_moved(std::size_t gadget);

    /// Returns the first row SHARE.GUI's recipient list shows.
    ///
    /// @return the row; 0 without the panel's scroll bars
    [[nodiscard]] std::size_t share_list_first() const;

    /// Returns the source rows the preferences' sub-panel shows over the
    /// battlefield: from its top down to its bottom, the bottom bar's rows
    /// included, as 3.1c places the panel at every resolution.
    ///
    /// @return the rows' source rectangle, or an empty one when the preferences are not open
    [[nodiscard]] oa::ui::display_layout::Rect preferences_panel_rows() const;

    /// Places the preferences' sub-panel's rows in the bottom bar's band of the HUD layer.
    ///
    /// Where the bottom bar sits apart from the chrome (a window taller than
    /// the chrome's 4:3), each of its rows under the panel shows the panel's
    /// row the chrome's scale puts at that height, or else the bar's own
    /// picture; the panel as drawn is kept (preferences_hud_) for the rows
    /// over the battlefield. Where the bar joins the chrome the layer is left
    /// as it is.
    ///
    /// @param[in,out] hud the HUD layer, in 640x480 source space
    void place_preferences_rows(renderer::Surface& hud);

    /// Tells whether the running game is a multiplayer game (extension_state::multiplayer).
    ///
    /// @return true while a multiplayer session is open
    [[nodiscard]] bool multiplayer_session() const;

    /// Opens TABMENU.GUI over a multiplayer match, or closes the team menu or
    /// panel that is open (Tab).
    ///
    /// ALLIES and SHARE show for a local player who is not a watcher; CONTROL
    /// also needs this machine to host the game (ui::hud::toggle_tab_menu).
    /// Nothing once the match is finished.
    void toggle_team_menu();

    /// Opens SHARE.GUI over a multiplayer match ('h' and the tab menu's SHARE).
    ///
    /// The recipients are the players taking part other than local players and
    /// watchers (ui::hud::open_share_panel), listed in PLYRLIST with the first
    /// chosen; the METAL and ENERGY sliders run to the local player's stores
    /// and start at 0. A watcher gets no panel, and one with no recipient
    /// closes at once.
    void open_team_share_panel();

    /// Opens ALLIES.GUI over a multiplayer match (ui::hud::open_allies_panel).
    void open_allies_team_panel();

    /// Opens CONTROL.GUI over a multiplayer match; nothing for a watching
    /// local player (ui::hud::open_control_panel).
    void open_control_team_panel();

    /// Opens YESORNO.GUI to ask whether to remove a player (CONTROL.GUI's LIVEPLYRn).
    ///
    /// @param player player index 0..9
    void open_removal_question(uint8_t player);

    /// Tells whether a team menu or panel is open over the match.
    ///
    /// @return true while TABMENU.GUI, SHARE.GUI, ALLIES.GUI, CONTROL.GUI or
    ///     the removal question is loaded as the match HUD
    [[nodiscard]] bool team_panel_open() const;

    /// Clicks a named control of the open team menu or panel.
    ///
    /// The tab menu opens the options menu, SHARE, ALLIES or CONTROL; SHARE.GUI
    /// picks a recipient row (its sliders' bars and arrows take the pointer,
    /// share_bar_moved), toggles its boxes
    /// and gives on OK (ui::hud::share_panel_click); ALLIES.GUI and CONTROL.GUI
    /// follow ui::hud::allies_panel_click and control_panel_click, and the
    /// alliance line is said in chat through the chat formatter; the removal
    /// question removes on CHOICE1. A panel that closes resumes the match's
    /// order panel.
    ///
    /// @param name control name in the loaded panel
    void click_team_panel(std::string_view name);

    /// Forgets the team menu or panel, as any in-game menu closing or
    /// replacing it does: the menu bits of Game.gui_flags and the share and
    /// allies panels' bits of Game.frame_flags are dropped.
    void forget_team_panel();

    /// Gives the team menu or panel's controls their drawn state: the players'
    /// logos, the alliance and team icons, and SHARE.GUI's recipient list.
    ///
    /// @param[in,out] presentation the match HUD's button presentation
    /// @param[out] lists receives PLYRLIST while SHARE.GUI is open
    void present_team_panel(
        std::vector<renderer::ButtonPresentation>& presentation,
        std::vector<renderer::ListPresentation>& lists
    ) const;

    /// Returns the named controls of the team menu or panel loaded as the match HUD.
    ///
    /// @return controls over match_hud_: a value is a button's stage or an
    ///     image's frame
    [[nodiscard]] oa::ui::hud::PanelControls team_panel_controls();

    /// Loads a team menu or panel as the match HUD over the running match.
    ///
    /// A panel whose root lies at a negative position is placed from the
    /// bottom or right edge of the 640x480 screen. The in-game menu counts as
    /// open (match_paused_), which blocks the battlefield's input.
    ///
    /// @param file GUI file under guis/
    /// @return false when the panel cannot be loaded
    bool load_team_panel(const char* file);

    /// Hands the local player's selected units to another player
    /// (ui::hud::give_selected_units): Match::transfer_unit for each, keeping
    /// commanders (the COMMANDER category), airborne units and units carrying
    /// or carried by another.
    ///
    /// @param recipient player index 0..9
    void give_selected_units_to(uint8_t recipient);

    /// Checks the Pause key and the menus in a skirmish.
    ///
    /// Pause sets the pause bit of Game.sim_run_flags, opens no menu, holds
    /// Game.tick over a second of frames and shows the paused title; Pause
    /// again resumes. Escape opens no menu, and F2 opens ARMOPT.GUI and holds the skirmish;
    /// 'h' opens nothing and moves no resources, and Tab opens no team menu.
    /// Throws std::runtime_error at the first failure.
    void check_pause_key();

    /// Checks the services and hooks the screens and the extension reach the
    /// runtime through, on the main menu.
    ///
    /// Probe hooks stand in for the extension's and are put back afterwards:
    /// a close request the extension answers leaves the run going and one it
    /// declines, or a null hook, ends it at once; quit ends the run with its
    /// exit status once its callback has returned; stop_sounds silences everything and play_sound_alternate
    /// plays nothing headless; a frontend pass runs once for two requests; a
    /// query binding answers
    /// Runtime::query; the preferences load takes the launch's nickname and
    /// game name. Throws std::runtime_error at the first failure.
    void check_screen_services();

    /// Checks the return label and quit in a match, over a new skirmish,
    /// which it leaves for the skirmish menu.
    ///
    /// A probe hook gives a return label: the match start keeps it, a long
    /// label is cut to kReturnLabelBytes - 1 characters, none leaves it
    /// empty, a frontend pass requested in the match does not run, and
    /// quit, once its callback has returned, leaves the match first
    /// (MatchEvent::left) and closes the preferences open over it. Throws
    /// std::runtime_error at the first failure.
    void check_launch_services();

    /// Checks the rules of a match shared with other players' machines and
    /// the team panels, over the running skirmish taken as one.
    ///
    /// The extension state is taken as a shared multiplayer match and the team
    /// panels' host records what it is told: ARMOPT.GUI holds nothing, a pause
    /// bit set elsewhere holds the clock and shows the paused title, Tab and
    /// 'h' open the tab menu and SHARE.GUI, which gives metal and a unit to the
    /// computer player, ALLIES.GUI allies with it and says so in chat,
    /// CONTROL.GUI and its removal question tell the host. Everything is put
    /// back afterwards. Throws std::runtime_error at the first failure.
    void check_team_panels();

    /// Leaves the options screens for the screen they were opened from.
    ///
    /// The preferences are saved. Opened from a match, the match takes the
    /// options back and shows the paused options menu again; the preferences a
    /// match opens (PREFS.GUI) close to the in-game menu they were opened over.
    void leave_options_screen();

    /// Tells whether the preferences a match opens (PREFS.GUI in the side
    /// column) are up.
    ///
    /// @return true while they are open over the running match
    [[nodiscard]] bool match_preferences_open() const;

    /// Tells whether the preferences a match opens show their MUSIC tab
    /// (MUSICRT.GUI beside the tabs), which the music counts as its panel.
    ///
    /// @return true while that tab is open over the running match
    [[nodiscard]] bool match_music_panel_open() const;

    /// Closes the preferences a match opened as the match goes, without
    /// showing the in-game menu: what they set is saved, their lightbar and
    /// pictures are freed and the next match opens with none. Nothing when
    /// none are open.
    void forget_match_preferences();

    /// Tells whether a paused menu's panel is on the HUD in place of the
    /// unit's pages: the in-game menu and what it opens, the team panels, the
    /// exit confirmation.
    ///
    /// The finished match is held without one; a menu open as it finished
    /// stays on the HUD.
    ///
    /// @return true while such a panel is shown
    [[nodiscard]] bool pause_menu_shown() const;

    /// Shows the VISUALS toggles' stages from the saved graphics word: SHADING, ANTI and BSHADOWS.
    void sync_visual_option_widgets();

    /// Shows the campaign screens' Difficulty button at the saved difficulty.
    void sync_campaign_option_widgets();

    /// Flips a bit of the saved graphics word, saves it under a preference key and redraws the
    /// toggles.
    ///
    /// @param mask graphics word bit
    /// @param key General preference the bit is saved under, 1 or 0
    void toggle_graphics_flag(uint16_t mask, std::string_view key);

    /// Clicks the hovered gadget of an options screen through the options panel handlers.
    ///
    /// The handlers are bound to the screen's panel on first use; sub-panels load
    /// over the tab panel, STARTOPT's records first, then the sub-panel's, offset
    /// by the difference of the two panel origins.
    void activate_options_gadget();

    /// Binds the options handlers' services over this runtime's preferences, sound and display.
    ///
    /// Opened from a running match, the options are PREFS.GUI in the side
    /// column with the in-game (*RT) sub-panels, and the GAME slider sets the
    /// running game's speed; elsewhere they are the full-screen frontend panels.
    /// A watcher's game speed slider is locked.
    void bind_options_context();

    /// Opens OPTIONS: the lightbar takes the panel below, then the tab panel loads.
    ///
    /// Outside a match that is STARTOPT.GUI, full screen. From a running match
    /// it is PREFS.GUI in the in-game menu's place in the side column, over the
    /// match, showing the match's own option fields and speed.
    void enter_options_panel();

    /// Clicks the hovered gadget of the new-campaign or any-mission screen.
    ///
    /// PrevMenu returns to Single Player; the side buttons show the chosen
    /// side's Arm/Core and emblem buttons pressed, save the side and refill the
    /// Campaign list with its campaigns (and, on Any Mission, the Missions
    /// list); Difficulty cycles and saves the difficulty; Start marks
    /// every mission unplayed and opens the chosen mission's briefing.
    void activate_campaign_gadget();

    /// Compares two TDF names ignoring ASCII case.
    ///
    /// @param left first name
    /// @param right second name
    /// @return true when they match
    static bool tdf_names_equal(std::string_view left, std::string_view right);
    // The session object's schema and its keys (runtime_campaign.cpp,
    // runtime_skirmish_host.cpp).

    /// Returns the session object's schema: the [Schema N] the mission info reads the resources,
    /// SurfaceMetal, aiprofile and storm keys from.
    ///
    /// The campaign's difficulty schema during a campaign mission, otherwise the
    /// one the selected skirmish or multiplayer map matches against its roster.
    ///
    /// @return the schema name; empty without a map or a match
    std::string session_schema();

    /// Returns the GlobalHeader's [Schema N] section session_schema_ names.
    ///
    /// @return the section, or null without a map header or schema
    const oa::data::unit_definitions::TdfSection* session_schema_section() const;

    /// Reads an integer key of the session's schema section.
    ///
    /// @param key key name
    /// @param fallback value without the section or key
    /// @return the key's value, or `fallback`
    int32_t schema_integer(std::string_view key, int32_t fallback);

    /// Reads a text key of the session's schema section.
    ///
    /// @param key key name
    /// @return the key's text, or nullopt without the section or key
    std::optional<std::string> schema_text(std::string_view key);
    // Campaign session setup the match bootstrap applies (runtime_campaign.cpp).

    /// Fills the session rules record from the campaign mission's block.
    ///
    /// The mission info replaces the Single rules with the mission's block, which
    /// mission start applies for a campaign.
    ///
    /// @param[out] record session rules record
    void campaign_session_rules(int32_t (&record)[4]);

    /// Keeps only the unit types the mission's use-only file lists as available.
    ///
    /// Mission start marks those types before the unit definitions compact the
    /// table to them. With no use-only file every type stays.
    ///
    /// @param[in,out] catalog unit catalog, compacted and renumbered
    void restrict_campaign_catalog(oa::data::unit_definitions::UnitCatalog& catalog);
    // The unit table a session that agreed on one plays
    // (runtime_skirmish_start.cpp).

    /// Keeps only the unit types a session agreed on.
    ///
    /// Every agreed type is marked before the unit definitions compact the table
    /// to the marked ones. The marking reads only the FBI hash the header load
    /// gave each type.
    ///
    /// @param[in,out] catalog unit catalog, compacted and renumbered
    /// @param filter the session's marking hook
    void restrict_marked_catalog(
        oa::data::unit_definitions::UnitCatalog& catalog, const UnitFilter& filter
    );

    /// Drops the catalog entries whose header lost its available bit.
    ///
    /// The catalog is already in its name order, so the kept entries only
    /// renumber (type ids from 1).
    ///
    /// @param[in,out] catalog unit catalog
    /// @param headers unit headers indexed by type id, entry i at i + 1
    static void keep_available_units(
        oa::data::unit_definitions::UnitCatalog& catalog, std::span<const oa::UnitDef> headers
    );

    /// Collects the session object's schema features and loads their definitions.
    ///
    /// The placement reads them for every kind: the campaign mission's, or those
    /// of the schema a skirmish or multiplayer map matches on its roster. A
    /// placement's FeatureDef the table lacks is loaded; the match copies the
    /// table, so every named one is loaded here, after the map's own and before
    /// the units add their corpses, as the game does. A resumed save
    /// (Game.saved_game) neither loads nor places them: the save's Features
    /// section places every feature, the map's own included.
    ///
    /// @param documents the parsed feature TDF set
    /// @param host feature definition loader
    void load_mission_features(
        std::span<const oa::data::unit_definitions::TdfDocument> documents,
        const oa::sim::map_runtime::FeatureDefHost& host
    );

    /// Adds the draw entries of the schema features on the plots the placement put them on.
    ///
    /// A map feature they replaced loses its draw first, which may already place
    /// the replacement's.
    void place_mission_feature_draws();

    /// Loads a campaign mission's map: its TNT terrain and OTA metadata.
    ///
    /// Throws std::runtime_error when either does not parse.
    ///
    /// @param mission_file mission file name; the extension is replaced
    /// @return false when the name is empty or either file is missing
    bool load_campaign_map(std::string_view mission_file);

    /// Seats a campaign's two players: the local player on the chosen side in slot 0 and the
    /// computer on the other in slot 1, with the mission's resources.
    void configure_campaign_players();

    /// Creates the campaign mission's units and their scripts, centres the view on the first start
    /// position and grants the mission's resources.
    ///
    /// Throws std::runtime_error when the schema counts units but lists none.
    void spawn_campaign_units();

    /// Centres the battlefield view on the schema's StartPos1 special.
    ///
    /// The camera it moves is the runtime's, so it runs over a stand-in Game that
    /// carries the match's map extents and the view size the camera clamps to.
    void place_campaign_camera();
    // Computer players at the session start and on ReloadAIProfiles
    // (runtime_campaign.cpp).

    /// Returns the session object's AI profile path (path slot 7).
    ///
    /// @return ai/<aiprofile>.txt of the mission's schema, else ai/Default.txt;
    ///     empty without a campaign mission or map
    std::string session_ai_profile_path();

    /// Reads the computer players' profile: the session's, else ai/default.txt.
    ///
    /// @return the profile text; empty when neither exists, and the computer
    ///     players then take only their types' own directives
    std::string read_computer_profile();

    /// Configures the computer players before the mission's units exist, as mission state set-up
    /// does.
    ///
    /// They take the profile and the side build lists read from
    /// gamedata/sidedata.tdf; the match applies both before its first tick's
    /// orders run. Throws std::runtime_error when either cannot be loaded or
    /// stored.
    void configure_computer_players();

    /// Reloads the computer players' profile (ReloadAIProfiles): it is read again and reapplied
    /// over reset tables.
    void reload_computer_profiles();

    /// Loads a campaign file and lists its missions and their files; a missing file is reported on
    /// stderr.
    ///
    /// @param campaign_index index into the discovered campaigns
    void load_campaign_missions(std::size_t campaign_index);

    /// Lists the chosen side's campaigns, selects the side's own campaign and loads its missions.
    void discover_campaigns();

    /// Selects the Campaign or Missions row under a canvas row.
    ///
    /// Two campaign files or fewer leave the new-campaign screen to the side
    /// alone, over newcampaign4x with no campaign list; more show the list over
    /// newcampaign4.
    ///
    /// @param gadget_name "Campaign" or the missions list
    /// @param canvas_y pointer row in canvas pixels
    void select_campaign_list_row(std::string_view gadget_name, float canvas_y);

    /// Returns the selected mission's file, listing the campaigns first when none are listed.
    ///
    /// @return the file, or empty when the selection has none
    std::string resolve_campaign_mission_file();

    /// Returns the match's map context (Game.game_options).
    ///
    /// The campaign object during a campaign mission, otherwise the selected map,
    /// bound on first use as the game binds a skirmish or multiplayer map.
    ///
    /// @return the context, or null without a map
    const oa::data::campaign::CampaignFile* match_map_context();

    /// Declared for a planet's briefing animation; no definition exists and nothing calls it.
    ///
    /// @param planet planet name
    /// @return briefing GAF name
    static std::string briefing_gaf_for_planet(std::string_view planet);

    /// Streams a briefing's narration, or the end screen's glamour sound, unless muted.
    ///
    /// The sound takes the place of any stream still playing and is heard from
    /// its beginning after the delay; a failure is reported on stderr.
    ///
    /// @param narration narration sound resource; empty plays nothing
    /// @param delay engine clock ticks (30 a second) before the sound is heard
    void play_briefing_narration(std::string_view narration, uint32_t delay);

    /// Stops the stream, the briefing's narration or the end screen's glamour sound, at once.
    void stop_briefing_audio();

    /// Opens the selected mission's briefing (MSNBRIEF.GUI) over the side's background and starts
    /// its narration unless muted.
    ///
    /// Back returns to the screen it was opened from: Any Mission, the
    /// end-of-game screen, else New Campaign. Without a bound mission the status
    /// line says why and nothing opens.
    void show_mission_briefing();

    /// Opens the in-game briefing (BRIEFING.GUI) from the pause menu over the paused match; a
    /// failure is shown on the status line.
    ///
    /// The panel sits at its authored position unless that runs past the window, drawn over the
    /// match as it stands, the options panel undimmed, and its bitmap and gadgets are shown in
    /// the match palette. No narration plays.
    void show_in_game_briefing();

    /// Clicks the hovered gadget of a briefing.
    void activate_briefing_gadget();

    /// Clicks the mission briefing's gadget for Enter or Escape.
    ///
    /// Enter clicks Start and Escape clicks PrevMenu, so either key leaves the
    /// briefing as its button does. The briefing opened from the pause menu
    /// takes neither key.
    ///
    /// @param escape true for Escape, false for Enter
    /// @return whether the key clicked a gadget
    bool press_briefing_default(bool escape);

    /// Clicks a briefing gadget by name.
    ///
    /// From the pause menu, OK returns to the paused match; otherwise the
    /// briefing's Start, PrevMenu, SHUTUP and page controls run.
    ///
    /// @param name gadget name
    void click_briefing_gadget(std::string name);

    /// Runs the mission briefing's ticker: SHUTUP turns off once the narration is over.
    ///
    /// The narration counts as playing from its request until its last sound,
    /// so SHUTUP stays on while it waits out its delay. With no sound the
    /// narration is over at once.
    void tick_mission_briefing();

    /// Draws a briefing's sprite gadgets (the planet and panorama) scaled to their gadgets, then
    /// its page of text and the MOREBAR caption, all in the screen's palette.
    ///
    /// The rows are drawn in the side's text colour and the highlighted words over them in
    /// their green, yellow or red, each word flashing in another colour for a quarter second
    /// after every second from when the page was laid out.
    void draw_briefing_overlays();

    /// Loads the FNT fonts a briefing's text is drawn in from the loaded panel's font
    /// records: the side's font for the TextRegion (the second record for ARM, the third for
    /// CORE; the first is the small font) and the font the MOREBAR names for its caption.
    /// Restarts the highlights' flashing.
    ///
    /// A font the panel does not name, or that does not load, leaves the GUI font in its place.
    void load_briefing_fonts();

    /// Returns the font a briefing's pages are wrapped, laid out and drawn in.
    ///
    /// @return the side's FNT font, or the GUI font without one
    [[nodiscard]] oa::formats::fnt::Font& briefing_text_font();

    /// Returns one row of the briefing page on show, as it is drawn.
    ///
    /// @param row row from the top of the page, from 0
    /// @return the row's text without the carriage return that ends a line of the briefing;
    ///         empty past the page's last row
    [[nodiscard]] std::string briefing_row(std::size_t row);

    /// Starts the selected mission of the loaded campaign, as the briefing's Start does.
    ///
    /// A missing mission file or map, or a failed start, is shown on the status
    /// line and the campaign mission flag is dropped.
    void start_campaign_mission();
    // The campaign object as the in-game restart reads and rebinds it.

    /// Returns the campaign object's mission name.
    ///
    /// @return the name, up to the field's size
    [[nodiscard]] std::string bound_mission_name();

    /// Returns the index of the campaign object's bound mission.
    ///
    /// @return mission index in the campaign's list
    [[nodiscard]] int32_t bound_mission_index();

    /// Reloads the campaign file under the name it was loaded with; one that was never loaded
    /// reloads as none.
    ///
    /// A failure is shown on the status line.
    void reload_campaign_file();

    /// Binds a mission of the campaign object for a restart and selects it.
    ///
    /// @param index mission index in the campaign's list
    /// @return false when it cannot be bound, which the status line shows
    bool bind_campaign_mission(int32_t index);

    /// Starts the bound mission again.
    ///
    /// start_campaign_mission() alone would restart a new campaign from its first
    /// mission.
    void restart_campaign_mission();

    /// Clicks the hovered gadget of the load-game screen.
    ///
    /// CANCEL, PREV and PREVMENU close the save dialog, return to a paused match
    /// or return to Single Player; LOAD and DELETE only report that there is no
    /// saved game to act on.
    void activate_load_game_gadget();

    /// Returns how many build pages the selected unit's type has.
    ///
    /// UnitDef.gui_page_count holds the first missing page index (from the unit
    /// definitions), raised to the highest download MENU; pages are 1..count-1.
    ///
    /// @return the page count; 0 without a selected unit of a known type
    int builder_gui_page_count() const;

    /// Tests whether a HUD gadget steps the build pages.
    ///
    /// @param name gadget name
    /// @return true for PREV, NEXT and PREVIOUS actions
    bool is_build_page_nav(std::string_view name) const;

    /// Shows a build page of the selected builder.
    ///
    /// The page is loaded through the build-orders panel, which falls back to the
    /// side's download page and links the download buttons; an unfinished
    /// builder, which has no build page, shows the order page instead. The build
    /// pages cycle as the game's page flags do; a type with one page behaves as if
    /// it had a second, missing one.
    ///
    /// @param page page number from 1; the current page plus or minus one steps
    void show_match_build_page(int page);

    /// Shows a page as the digit keys pick it, with the nextbuildmenu sound.
    ///
    /// Page 0 is the selected unit's order page; only pages below the type's
    /// first missing page are shown.
    ///
    /// @param page page number
    void show_match_page_by_key(int page);

    /// Loads the viewed side's side tile for the match chrome and shows the order page.
    void load_match_chrome();

    /// Scales a source rectangle of an RGB surface onto a destination rectangle, nearest pixel,
    /// clipped to both.
    ///
    /// Equal sizes copy through blit_rect().
    ///
    /// @param[in,out] destination RGB surface
    /// @param source RGB surface
    /// @param dx destination column
    /// @param dy destination row
    /// @param dw destination width; 0 or less draws nothing
    /// @param dh destination height; 0 or less draws nothing
    /// @param sx source column
    /// @param sy source row
    /// @param sw source width; 0 or less draws nothing
    /// @param sh source height; 0 or less draws nothing
    void scale_blit(
        renderer::Surface& destination,
        const renderer::Surface& source,
        int dx,
        int dy,
        int dw,
        int dh,
        int sx,
        int sy,
        int sw,
        int sh
    );

    /// Copies a rectangle of an RGB surface to another, clipped to both.
    ///
    /// @param[in,out] destination RGB surface
    /// @param source RGB surface
    /// @param destination_x destination column
    /// @param destination_y destination row
    /// @param source_x source column
    /// @param source_y source row
    /// @param width rectangle width
    /// @param height rectangle height
    void blit_rect(
        renderer::Surface& destination,
        const renderer::Surface& source,
        int destination_x,
        int destination_y,
        int source_x,
        int source_y,
        int width,
        int height
    );

    /// Paints text in a palette colour with the match label font.
    ///
    /// The palette is the HUD's, else the match palette; nothing is drawn without
    /// a font or for an index outside the palette.
    ///
    /// @param x paint column of the text's left edge
    /// @param y paint row of the glyph tops
    /// @param text text to paint
    /// @param palette_index palette colour
    /// @param scale pixel repeat, 1 or more
    void
    draw_match_label(int x, int y, std::string_view text, uint8_t palette_index, int scale = 1);

    /// Paints text in a palette colour with a given font.
    ///
    /// The palette is the HUD's, else the match palette; nothing is drawn without
    /// a font or for an index outside the palette.
    ///
    /// @param font font to draw with; null draws nothing
    /// @param x paint column of the text's left edge
    /// @param y paint row of the glyph tops
    /// @param text text to paint
    /// @param palette_index palette colour
    /// @param scale pixel repeat, 1 or more
    void draw_match_text(
        const oa::formats::fnt::Font* font,
        int x,
        int y,
        std::string_view text,
        uint8_t palette_index,
        int scale
    );

    /// Paints text into the paint target in one colour.
    ///
    /// Glyph tops land on y; only glyph pixels are written, each font pixel
    /// repeated as a scale x scale block.
    ///
    /// @param font font to draw with
    /// @param x paint column of the text's left edge
    /// @param y paint row of the glyph tops
    /// @param text text to paint
    /// @param color RGB colour
    /// @param scale pixel repeat; below 1 paints nothing
    void paint_text(
        const oa::formats::fnt::Font& font,
        int x,
        int y,
        std::string_view text,
        std::array<uint8_t, 3> color,
        int scale
    );

    struct HudRect {
        int x = 0, y = 0, width = 0, height = 0;
    };

    struct SideHud {
        uint8_t metal_color = 224;  // SIDEDATA metalcolor
        uint8_t energy_color = 208; // SIDEDATA energycolor
        HudRect metal_bar{};
        HudRect energy_bar{};
        int metal_num_x = 278, metal_num_y = 18;
        int metal_max_x = 341, metal_max_y = 1;
        int metal_zero_x = 215, metal_zero_y = 1;
        int metal_produced_x = 358, metal_produced_y = 5;
        int metal_consumed_x = 358, metal_consumed_y = 17;
        int energy_num_x = 529, energy_num_y = 18;
        int energy_max_x = 595, energy_max_y = 1;
        int energy_zero_x = 468, energy_zero_y = 1;
        int energy_produced_x = 609, energy_produced_y = 5;
        int energy_consumed_x = 609, energy_consumed_y = 17;
        HudRect unit_name{245, 452, 0, 8};
        HudRect damage_bar{200, 463, 90, 2};
        HudRect unit_metal_make{350, 458, 0, 8};
        HudRect unit_metal_use{350, 468, 0, 8};
        HudRect unit_energy_make{400, 458, 0, 8};
        HudRect unit_energy_use{400, 468, 0, 8};
        HudRect logo2{132, 455, 21, 21};       // SIDEDATA LOGO2: the cursor unit owner's logo
        HudRect mission_text{385, 449, 16, 2}; // MISSIONTEXT: head order status, centred on x
        HudRect unit_name2{555, 452, 1, 9};    // UNITNAME2: the second unit, centred on x
        HudRect damage_bar2{510, 463, 91, 3};  // DAMAGEBAR2: its damage or stockpile bar
        HudRect name{132, 452, 11, 9};         // NAME: build button cost or feature line
        HudRect description{132, 465, 11, 8};  // DESCRIPTION: build button description
    };

    /// Reads an integer key of a TDF section.
    ///
    /// @param section TDF section
    /// @param key key name
    /// @param fallback value for a missing, empty or malformed key
    /// @return the key's value, or `fallback`
    static int tdf_int(
        const oa::data::unit_definitions::TdfSection& section,
        std::string_view key,
        int fallback = 0
    );

    /// Declared for a SIDEDATA rectangle; no definition exists and nothing calls it.
    ///
    /// @param side side section of SIDEDATA.TDF
    /// @param name rectangle name
    /// @return the rectangle
    static HudRect
    tdf_rect(const oa::data::unit_definitions::TdfSection& side, std::string_view name);

    /// Declared for a SIDEDATA x position; no definition exists and nothing calls it.
    ///
    /// @param side side section of SIDEDATA.TDF
    /// @param name position name
    /// @param fallback value without the key
    /// @return the column
    static int
    tdf_x(const oa::data::unit_definitions::TdfSection& side, std::string_view name, int fallback);

    /// Declared for a SIDEDATA y position; no definition exists and nothing calls it.
    ///
    /// @param side side section of SIDEDATA.TDF
    /// @param name position name
    /// @param fallback value without the key
    /// @return the row
    static int
    tdf_y(const oa::data::unit_definitions::TdfSection& side, std::string_view name, int fallback);

    /// Loads the viewed side's HUD layout (bars, colours and readout positions) from SIDEDATA.TDF;
    /// the defaults stay without it.
    void load_side_hud();
    /// Layer the match overlay primitives paint on.
    enum class PaintLayer : uint8_t {
        hud,         // match_hud_cpu_, in 640x480 source coordinates
        battlefield, // match_world_cpu_, canvas pixels from the battlefield corner
    };

    /// Chooses the layer overlays paint on.
    ///
    /// The HUD layer holds the 640x480 chrome that present_match_layers scales
    /// into the side column and the bars; the world layer is the battlefield at
    /// canvas resolution, so battlefield overlays paint there, offset by its
    /// corner.
    ///
    /// @param layer layer to paint on
    void paint_on(PaintLayer layer);

    /// Returns the surface overlays paint on.
    ///
    /// @return the chosen layer, else the frame
    renderer::Surface& paint_target();

    /// Converts a canvas point to the paint target's pixels.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return the point less the paint origin
    oa::ui::display_layout::Point canvas_paint(int x, int y) const;

    /// Returns the scale of text painted over the battlefield: it grows with the chrome, in whole
    /// pixels.
    ///
    /// @return the chrome scale rounded, at least 1
    int hud_text_scale() const;

    /// Returns the font match labels paint in.
    ///
    /// @return the small font, else the HUD's font; null without either
    const oa::formats::fnt::Font* match_label_font() const;

    /// Fills a rectangle of the paint target with a match palette colour, clipped to the target.
    ///
    /// @param x paint column
    /// @param y paint row
    /// @param width rectangle width; 0 or less fills nothing
    /// @param height rectangle height; 0 or less fills nothing
    /// @param palette_index match palette colour
    void fill_hud_rect(int x, int y, int width, int height, uint8_t palette_index);

    /// Converts a 640x480 source point to the paint target's pixels.
    ///
    /// @param x source column
    /// @param y source row
    /// @return the point on the paint target
    oa::ui::display_layout::Point hud_canvas(int x, int y) const;

    /// Paints a label at a 640x480 source point.
    ///
    /// @param x source column of the text's left edge
    /// @param y source row of the glyph tops
    /// @param text text to paint
    /// @param palette_index palette colour
    void draw_hud_label(int x, int y, std::string_view text, uint8_t palette_index);

    /// Paints a label ending at a 640x480 source point.
    ///
    /// @param x source column of the text's right edge
    /// @param y source row of the glyph tops
    /// @param text text to paint
    /// @param palette_index palette colour
    void draw_match_label_right(int x, int y, std::string_view text, uint8_t palette_index);

    /// Paints a label centred on a 640x480 source point.
    ///
    /// The unit readout labels the name at (UNITNAME.x1 - measure/2,
    /// UNITNAME.y1). SIDEDATA UNITNAME is a degenerate x1==x2 point at the
    /// segment centre, not a left edge.
    ///
    /// @param x source column of the text's centre
    /// @param y source row of the glyph tops
    /// @param text text to paint
    /// @param palette_index palette colour
    void draw_hud_label_centered(int x, int y, std::string_view text, uint8_t palette_index);

    /// Fills a 640x480 source rectangle with a match palette colour, at least one pixel on each
    /// axis.
    ///
    /// @param x source column
    /// @param y source row
    /// @param width rectangle width
    /// @param height rectangle height
    /// @param palette_index match palette colour
    void fill_source_rect(int x, int y, int width, int height, uint8_t palette_index);

    /// Draws a SIDEDATA resource trough: the fill to the shown amount and the share threshold's
    /// marker.
    ///
    /// @param bar trough rectangle in source space; an empty one draws nothing
    /// @param shown the eased amount shown
    /// @param capacity storage capacity
    /// @param threshold share threshold the marker stands at
    /// @param store the stored amount
    /// @param color fill colour
    void draw_sidedata_bar(
        const HudRect& bar, float shown, float capacity, float threshold, float store, uint8_t color
    );

    /// Draws the viewed player's resource readout: troughs, stored and capacity numbers and the
    /// produced and consumed rates.
    ///
    /// The readout eases the shown stores toward the player's and re-reads the
    /// settled rates on the player's display timer.
    void draw_resource_readout();

    /// Draws a unit's energy and metal make and use as of the last economy settlement.
    ///
    /// @param unit unit the readout shows
    void draw_unit_rates(const oa::Unit& unit);

    /// Returns the overlay raster that draws rectangles and text in 640x480 source space on the
    /// HUD.
    ///
    /// @return the raster, bound to this runtime
    [[nodiscard]] oa::present::world_renderer::OverlayRaster source_overlay_raster();

    /// Grays the battlefield outside the viewer's line of sight and blacks out never-mapped ground.
    ///
    /// One 32-pixel FOG.GAF tile per edge-grid cell. Tiles are placed in map space
    /// and scaled through the terrain's zoom DDA, so the tile edges stay on the
    /// same map pixels at any zoom. Nothing is drawn with mapping and line of
    /// sight both off.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param camera_x camera column in map pixels
    /// @param camera_y camera row in map pixels
    /// @param dest_x frame column of the battlefield
    /// @param dest_y frame row of the battlefield
    /// @param dest_w battlefield width in frame pixels
    /// @param dest_h battlefield height in frame pixels
    void apply_match_fog(
        oa::present::world_renderer::Surface& destination,
        uint32_t camera_x,
        uint32_t camera_y,
        int dest_x,
        int dest_y,
        int dest_w,
        int dest_h
    );

    /// Loads the FOG.GAF tile sets and prepares native RGB fog levels, once.
    void ensure_fog_frames();

    /// Tests whether a map feature is left undrawn under fog.
    ///
    /// Only `nodrawundergray` features (dragon's teeth and fortification walls)
    /// are hidden outside line of sight, and not when the plot's feature owner is
    /// the viewer; every other feature stays drawn and the fog tiles gray or cover
    /// it. The sight test is the feature's origin cell at the plot height, then
    /// its far footprint corner.
    ///
    /// @param feature_index index into the map's feature table
    /// @param cell_x feature cell column
    /// @param cell_z feature cell row
    /// @return true when the feature is not drawn
    bool feature_hidden_by_fog(uint16_t feature_index, int32_t cell_x, int32_t cell_z);

    /// Marks the cells the viewer has mapped or covers as explored for the radar, while mapping is
    /// on.
    void absorb_radar_exploration();

    /// Blits the radar picture into the HUD's radar well.
    ///
    /// The radar picture fills the 0x7e-pixel square at the top left of the
    /// 640x480 HUD; the well's picture area is painted on the HUD layer every
    /// frame.
    void blit_match_minimap();

    /// Returns the RGB of a match palette index.
    ///
    /// @param index palette index
    /// @return its colour; pure green for kPaletteGreen and pure red for any
    ///     other index when the palette is not loaded
    std::array<uint8_t, 3> palette_rgb(uint8_t index) const;

    struct PendingBuildSite {
        oa::sim::ground_orders::Point world{};
        int32_t cell_x{};
        int32_t cell_z{};
        int16_t footprint_x{};
        int16_t footprint_z{};
        bool legal{};
    };

    // Game.ui_colors slots of the build rectangle.
    static constexpr uint8_t kBuildSiteClearColor = 10;
    static constexpr uint8_t kBuildSiteRefusedColor = 4;

    /// Returns the build ghost's site under a battlefield point.
    ///
    /// The footprint cell the snapped point falls on, whether the local player
    /// may place the pending building there, and the height it would stand at
    /// (the yard height of the site when not). A footprint of 0 counts as 2.
    ///
    /// @param world 16.16 world point
    /// @return the site, or nullopt without a match or pending building
    std::optional<PendingBuildSite> pending_build_site(oa::sim::ground_orders::Point world) const;

    /// Returns the pending building's site under a battlefield screen point: the terrain point the
    /// pointer picks there, tested as the build ghost tests it.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return the site, or nullopt off the map or without a pending building
    std::optional<PendingBuildSite> build_site_under(float x, float y) const;

    // The order overlays the battlefield draws while Shift is held
    // (runtime_order_overlays.cpp): what one pass drew.
    struct OrderOverlayPass {
        std::vector<std::string> labels;
        std::size_t lines{};
        std::size_t sprites{};
    };

    /// Draws the local player's order overlays into the battlefield frame.
    ///
    /// The overlays are those of the view record that starts at
    /// Game.follow_unit, the followed unit. The module's screen points are
    /// those of the 640x480 frame; the frame here starts at the battlefield
    /// corner and carries the engine's zoom. Labels are drawn in UI colour 15,
    /// as other battlefield text; 3.1c draws them in whatever text colour was
    /// set last. The markers are cursor sequences, loaded first when a
    /// headless run has not.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param viewport battlefield viewport
    /// @return what the pass drew
    OrderOverlayPass draw_order_overlays(
        oa::present::world_renderer::Surface& destination,
        const oa::present::world_renderer::BattlefieldViewport& viewport
    );

    /// Checks the ShowRanges console toggle against the order overlays.
    ///
    /// ShowRanges flips Game.show_ranges, and the overlays of a unit whose order
    /// draws ranges then circle and label its sight, build distance and the rest;
    /// with it off they label nothing. The local commander is selected with a
    /// Move_Ground order (overlay path and ranges). Throws std::runtime_error on a
    /// failure.
    ///
    /// @param enter_line runs one console line
    void check_console_range_overlays(const std::function<void(const char*)>& enter_line);

    /// Outlines the pending building's footprint under the pointer at the height it would stand at.
    ///
    /// Two nested rectangles, green where it may be placed and red where not.
    /// Nothing is drawn while the pointer is off the battlefield or no building
    /// is armed.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param viewport battlefield viewport
    void draw_build_ghost(
        oa::present::world_renderer::Surface& destination,
        const oa::present::world_renderer::BattlefieldViewport& viewport
    );

    /// Draws projectiles by their weapon render type.
    ///
    /// Lasers are a line from the head to the tail in UI colour `color`, with a
    /// second line one pixel off the major axis in `color2`. Model projectiles
    /// (render types 1, 3 and 6) are not drawn by this pass; a projectile out of
    /// the viewer's sight is skipped.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param viewport battlefield viewport
    void draw_match_projectiles(
        oa::present::world_renderer::Surface& destination,
        const oa::present::world_renderer::BattlefieldViewport& viewport
    );

    /// Returns the RGB of a Game UI colour slot.
    ///
    /// @param index UI colour slot
    /// @return the colour of the palette index the slot holds
    [[nodiscard]] std::array<uint8_t, 3> ui_color_rgb(uint8_t index) const;

    /// Projects a 16.16 world position onto the battlefield frame, raised by half its height at the
    /// view's scale.
    ///
    /// @param viewport battlefield viewport
    /// @param position 16.16 x, height and z
    /// @return frame point
    oa::present::world_renderer::ScreenPoint project_match_point(
        const oa::present::world_renderer::BattlefieldViewport& viewport,
        const std::array<uint32_t, 3>& position
    ) const;

    /// Writes one pixel of the battlefield frame, clipped to the visible world rectangle and the
    /// frame.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param x frame column
    /// @param y frame row
    /// @param color RGB colour
    void put_match_pixel(
        oa::present::world_renderer::Surface& destination,
        int x,
        int y,
        const std::array<uint8_t, 3>& color
    );

    /// Draws a line on the battlefield frame, clipped to the visible world rectangle first.
    ///
    /// Clipping before stepping keeps off-map endpoints (a build ghost under an
    /// off-map cursor, say) from walking billions of steps or overflowing.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param x0 first endpoint column
    /// @param y0 first endpoint row
    /// @param x1 second endpoint column
    /// @param y1 second endpoint row
    /// @param color RGB colour
    void draw_match_line(
        oa::present::world_renderer::Surface& destination,
        int x0,
        int y0,
        int x1,
        int y1,
        const std::array<uint8_t, 3>& color
    );

    /// Blits the covered pixels of a rendered GAF frame onto the battlefield frame, scaled
    /// nearest-pixel.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param frame rendered frame
    /// @param destination_x frame column of the frame's left edge
    /// @param destination_y frame row of the frame's top edge
    /// @param palette 4 bytes per colour
    /// @param scale size factor; 0 or less draws at 1
    void blit_gaf_on_world(
        oa::present::world_renderer::Surface& destination,
        const oa::formats::gaf::RenderedFrame& frame,
        int destination_x,
        int destination_y,
        const oa::PaletteBytes& palette,
        float scale = 1.0F
    );

    /// Blits a rendered GAF frame onto the battlefield frame with its origin at a screen point.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param frame rendered frame
    /// @param screen frame point the GAF origin lands on
    /// @param palette 4 bytes per colour
    /// @param scale size factor; 0 or less draws at 1
    void blit_gaf_hotspot(
        oa::present::world_renderer::Surface& destination,
        const oa::formats::gaf::RenderedFrame& frame,
        const oa::present::world_renderer::ScreenPoint& screen,
        const oa::PaletteBytes& palette,
        float scale
    );

    /// Composes the match frame: the battlefield world layer and the 640x480 HUD layer.
    ///
    /// The camera is clamped to the map, the radar surfaces and view bound, and
    /// the drag box followed. The terrain, features, units, projectiles, nano
    /// streams, order overlays, shatter fragments, effects, debris and
    /// explosions draw on the world layer, then the fog, the build ghost and the
    /// selection band; the HUD layer takes the radar, the status strip, unit
    /// labels and readouts, the resource readout, the build captions and the
    /// extension's HUD. Each part is charged to its profile category. Throws
    /// std::logic_error without a match.
    void render_match_surface();

    /// Returns the 3DO renderer state of the current match, built on first use.
    ///
    /// Building it begins every unit's and 3D feature's draw state afresh.
    ///
    /// @return the state; rebuilt when the match changed
    MatchModels& match_models();

    /// Lists the loaded primitive table the match shatters a model object from: the prepared
    /// model's order and flag words, with each primitive's colour and vertex list.
    ///
    /// A model that no loaded unit type owns lists no primitives.
    ///
    /// @param model 3DO model
    /// @param object object index in the model
    /// @param[out] out primitives, cleared first
    /// @return 0 when the prepared object skips its first primitive, else -1
    int32_t loaded_match_primitives(
        const oa::formats::objects3d::Model& model,
        uint32_t object,
        std::vector<oa::sim::effect_particles::PiecePrimitive>& out
    );

    /// Fills the terrain cache with a box-filtered picture while the battlefield is zoomed out.
    ///
    /// A presentation enhancement: the terrain cache the match renderer would
    /// fill by nearest sampling is filled here with a box average over each
    /// pixel's footprint, keyed to the same camera and zoom so the renderer
    /// reuses it. 1:1 and magnified views never enter, so their output is
    /// unchanged.
    void refresh_filtered_terrain();

    /// Loads the load screen's background, bar art and font, once; a missing part is reported on
    /// stderr.
    void ensure_loading_screen();

    /// Shows the load screen with every category at 0 and pumps its first frame.
    void begin_loading_screen();

    /// Sets a load screen category's progress and pumps a frame.
    ///
    /// Reaching 100 starts the category's flash. The extension hears of the
    /// rows (Extension::load_progress) before the frame is drawn.
    ///
    /// @param row category row, 0 through 5; out of range only pumps
    /// @param percent progress, clamped to 100
    void set_load_progress(std::size_t row, uint8_t percent);

    /// Shows the load screen's current frame while a match loads.
    ///
    /// Headless runs draw the frame for snapshots; a window shows it through
    /// render() and keeps its events flowing. Throws std::runtime_error "loading
    /// cancelled" when the window is closed.
    void pump_loading_screen();

    /// Enters the load screen's display: PALETTE.PAL becomes the display palette and the 640x480
    /// off-screen surface is kept, as on its first entry.
    void enter_loading_display();

    /// Draws the six-category load progress screen.
    ///
    /// Into the off-screen surface, then out through draw_frame. A category's
    /// label is drawn lit through the light table while its flash lasts.
    void draw_loading_screen();
    // Session display and its sinks (runtime_display.cpp).

    /// Starts the session display.
    ///
    /// The start-up display with the application's session request, then the
    /// display steps of session start: the off-screen surface at the display
    /// size, the session flag cleared, the lookup tables from PALETTE.PAL and its
    /// table files, GUIPAL as the display palette and the text transparent
    /// colour. Headless runs capture frames; others present them through SDL.
    /// Throws std::runtime_error when the display cannot start.
    void start_session_display();

    /// Presents the frame through the session sink.
    ///
    /// Headless runs keep the frame as surface_, the RGB frame snapshots write.
    void show_display_frame();

    /// Writes the last headless frame as PCX through the PCX writer: the display palette, not the
    /// frame's gamma-corrected one.
    ///
    /// Throws std::runtime_error when it cannot be encoded or written.
    ///
    /// @param path file to create or truncate
    void write_display_pcx(const fs::path& path) const;

    /// Passes a finished indexed frame from the display sink to present_indexed_frame(), reporting
    /// a failure on stderr.
    ///
    /// The sink is called from noexcept presentation code, so nothing escapes.
    ///
    /// @param user the runtime
    /// @param pixels palette indices
    /// @param pitch bytes per row
    /// @param width frame width
    /// @param height frame height
    /// @param palette frame palette; null presents nothing
    static void present_sink_frame(
        void* user,
        const uint8_t* pixels,
        int32_t pitch,
        int32_t width,
        int32_t height,
        const oa::Palette* palette
    );

    /// Presents an indexed frame through SDL.
    ///
    /// The only place frame indices become texels: through the palette's texel
    /// table into an XRGB8888 streaming texture, letterboxed as apply_output_mode
    /// sets the logical presentation, with the software cursor on top.
    ///
    /// @param pixels palette indices
    /// @param pitch bytes per row, at least `width`
    /// @param width frame width
    /// @param height frame height
    /// @param palette frame palette
    void present_indexed_frame(
        const uint8_t* pixels,
        int32_t pitch,
        int32_t width,
        int32_t height,
        const oa::Palette& palette
    );

    /// Loads PALETTE.PAL and builds the guipal -> PALETTE.PAL UI colour table, once.
    ///
    /// The load screen keeps PALETTE.PAL active while loading, so Loadgame2bg,
    /// LIGHTBAR and the font draw raw indices through it. A failure is reported
    /// on stderr and retried on the next call.
    void ensure_ui_colors();

    /// Drops the match: tells the extension, unbinds the effects and services, and frees its
    /// models, radar, selection and HUD.
    void teardown_match();

    /// Leaves the match: tells the extension and resets the match view, zoom, caches, layers, chat
    /// and selection state.
    void leave_match();
    // End-of-game screen over the finished match and the game reporter
    // (runtime_endgame.cpp).
    struct EndgameState;

    /// Frees an end-screen state.
    ///
    /// @param state state to free; null is allowed
    static void destroy_endgame_state(EndgameState* state) noexcept;

    /// Returns the end-screen state, creating it on first use.
    ///
    /// @return the state
    EndgameState& endgame_state();

    /// Returns the engine ticks the end screen's timers run on (the 30 Hz clock).
    ///
    /// @return the stepped clock a check set, else SDL's time in ticks
    [[nodiscard]] uint32_t endgame_now() const;

    /// Returns the game options the end screen scores with.
    ///
    /// @return the campaign object during a campaign mission, else the end
    ///     state's session
    oa::data::campaign::CampaignFile* game_options();

    /// Reads a skirmish or multiplayer game's score multipliers from the map: the game options'
    /// kill_multiplier and time_multiplier, from the OTA's killmul and timemul, the score per
    /// kill and per 60 ticks.
    ///
    /// A campaign's loader reads its own.
    void bind_session_options();

    /// Keeps the finished match for the end screen.
    ///
    /// The match's last frame, its outcome title included, is kept for the
    /// screen to darken (keep_battlefield_frame()). Session teardown records the
    /// score table while the match tables still exist, then the end screen is
    /// entered at its first step. The extension stops driving the match now;
    /// its reports stay up for the end-of-game event the screen sends. The Game
    /// block keeps the UI colour table the match bootstrap filled.
    void keep_finished_match();

    /// Keeps the running match's last frame for the end screen.
    ///
    /// The match is drawn once more, its outcome title included and without
    /// the cursor, and kept as indices of the match palette at the match's
    /// size, the size the display keeps until the end screen has darkened it.
    /// Without a match nothing is kept.
    void keep_battlefield_frame();

    /// Darkens the kept last frame of the match by one step of the end screen.
    ///
    /// @param level shade level the step applies to every pixel, through the display shade table
    void shade_battlefield(int32_t level);

    /// Ends the darkening: the display goes back to the frontend's 640x480.
    void finish_battlefield_shade();

    /// Returns the kept last frame of the match while the end screen shows it at its own size.
    ///
    /// @return the frame, or null once the darkening has ended or on another screen
    [[nodiscard]] const oa::Surface* end_screen_battlefield_size() const;

    /// Draws the end screen before its panel: the finished match's last frame
    /// as it darkens, then a cleared screen the outcome's picture covers.
    ///
    /// A last frame larger than 640x480 is not shown once the darkening ends and
    /// the display is the frontend's again.
    ///
    /// @return false on another screen, or once the stat bars or the panel are up
    bool draw_end_screen_battlefield();

    /// Tells whether the end screen hides the cursor: from its first step until the stat bars
    /// have counted up.
    ///
    /// @return true on the end screen before its panel takes input
    [[nodiscard]] bool end_screen_hides_cursor() const;

    /// Runs the end screen once ENDMSN.GUI is loaded.
    ///
    /// The stat bars count up from the kept Game block and the buttons appear
    /// when the panel opens. Back from a mission briefing reopens the panel.
    void start_endgame();

    /// Returns the kept finished match's world.
    ///
    /// @return the world, or null without a kept match
    [[nodiscard]] oa::World* endgame_world();

    /// Returns the kept finished match's game options.
    ///
    /// @return the options, or null without a kept match
    [[nodiscard]] oa::data::campaign::CampaignFile* endgame_game_options();

    /// Marks the end screen to reopen over the kept game when the screen it is left for returns.
    void leave_end_panel_for_briefing();

    /// Loads the outcome's glamour picture for the end screen.
    ///
    /// After a campaign victory, the mission's glamour picture, or
    /// glamour/Arm01.PCX when the named one is missing. Any other outcome keeps
    /// the Outcome palette, which is the ENDMSN background's own. A picture that
    /// cannot be read is reported on stderr.
    void load_outcome_glamour();

    /// Returns the palette of the glamour picture load_outcome_glamour() loaded.
    ///
    /// The runtime keeps the picture and its palette in place of their part of
    /// Game.endgame_pictures.
    ///
    /// @return the palette, or null without a picture
    [[nodiscard]] const uint8_t* outcome_glamour_palette() const;

    /// Leaves a final campaign victory for the frontend's ending.
    ///
    /// The end screen leaves it in app mode 2, whose dispatcher plays the side's
    /// ending movies and returns to the main menu; that dispatcher runs until
    /// MAINMENU.GUI replaces the end screen. Game data that offers no movies
    /// (offers_movies()) goes to the main menu and shows the further_missions
    /// notice over it.
    void run_pending_ending();

    /// Tells whether the game folder holds movies: a Data folder with a .zrb file, names in any
    /// case.
    ///
    /// @return true when it does
    [[nodiscard]] bool holds_movies() const;

    /// Tells whether the game offers movies from its menus and its campaign's end.
    ///
    /// A game folder that holds a game disc's archive, totala1.hpi or totala2.hpi, is the game's
    /// own installation, which keeps INTRO, Credits and the ending movies whether or not its Data
    /// folder holds them. Other game data offers movies only when its folder holds them
    /// (holds_movies()); the Total Annihilation demo (1997) holds none, so its INTRO is grayed
    /// out, its Credits hidden and a final campaign victory ends with the further_missions
    /// notice.
    ///
    /// @return true when the game folder holds a disc archive or movies
    [[nodiscard]] bool offers_movies() const;

    /// Tests whether the end panel was left for a screen that returns to it over the kept game.
    ///
    /// @return true while a kept match is marked to reopen
    [[nodiscard]] bool endgame_reopens() const;

    /// Opens LOADGAME.GUI in its save role over the in-game menu or ENDMSN.
    ///
    /// ENDMSN keeps the finished game for the save and reopens over it.
    ///
    /// @param parent screen the dialog returns to
    void open_save_dialog(Screen parent);

    /// Tests whether the load-game screen is in its save role.
    ///
    /// @return true for the save dialog
    [[nodiscard]] bool save_dialog_open() const;

    /// Returns the load-game screen's background.
    ///
    /// @return "dsavegame2" in the save role, else "dloadgame2"
    [[nodiscard]] const char* load_game_background() const;

    /// Captures the frame the load or save dialog opens over.
    ///
    /// Without the software cursor, with the screen's top panel darkened: the
    /// options panel of a paused match, else the frontend screen's root.
    void capture_load_game_parent();

    /// Places the load-game dialog over the captured frame on its first draw.
    ///
    /// The root goes on the frame below and the bitmap's indices are shown in
    /// the palette of that frame.
    void enter_load_game();

    /// Drops the frame the load-game dialog was drawn over.
    void leave_load_game();

    /// Shows the background bitmap's indices in a palette, which the screen's gadgets are then
    /// drawn in too.
    ///
    /// @param palette palette of the frame the screen is drawn over
    void show_background_in(const oa::PaletteBytes& palette);

    /// Reports whether the panel on show is drawn over another screen: the load and save
    /// dialogs, or the in-game briefing over the paused match.
    ///
    /// @return true on those screens
    [[nodiscard]] bool panel_over_screen() const;

    /// Returns the frame the panel on show is drawn over.
    ///
    /// @return the load or save dialog's frame below, or the paused match under the in-game
    ///         briefing; null on any other screen or when that frame is empty
    [[nodiscard]] const renderer::Surface* panel_parent() const;

    /// Composes the panel's face at its root's position over the frame below; its records are
    /// drawn root-relative from the bitmap's corner.
    ///
    /// Without a frame below, the panel goes over a black 640x480 frame.
    void compose_panel_over_parent();

    /// Rebuilds the current screen's frame without the software cursor.
    ///
    /// @return the frame
    [[nodiscard]] renderer::Surface frame_without_cursor();

    /// Adds the load-game dialog's records as its handlers left them.
    ///
    /// The controls they hide, and the saves listed in GAMES, drawn by the list
    /// draw with the selected row lit.
    ///
    /// @param[in,out] lists list presentations the screen draws
    void present_load_game_panel(std::vector<renderer::ListPresentation>& lists);

    /// Checks LOADGAME.GUI in both roles, placed as the game places it.
    ///
    /// The load dialog centred over Single Player, which it darkens (with no save
    /// listed, MSGBOX.GUI says so over it); then through the SDL presenter over a
    /// paused skirmish, the save dialog at its authored position and the load
    /// dialog centred, each darkening only the options panel and showing its
    /// bitmap in the match palette, with CANCEL, typed names, Return and a GAMES
    /// row reached at their drawn positions. Game data with no save and load
    /// dialog runs check_saved_games_unavailable() instead. Throws
    /// std::runtime_error on a failure.
    void check_load_save();

    /// Checks, over game data with no save and load dialog such as the Total Annihilation demo
    /// (1997), that every entry to it is grayed out and takes no press: SINGLE.GUI's Load Game,
    /// and SAVEGAME and LOADGAME on the paused first campaign mission's options panel. Frames go
    /// to local/reports. Throws std::runtime_error on a failure.
    void check_saved_games_unavailable();

    /// Checks that every option of the setup screens shows what a click set.
    ///
    /// Through the SDL presenter: on SKIRMISH.GUI each rule button, Difficulty,
    /// the first rows' player, side, colour, allegiance, metal and energy
    /// controls and a map chosen through SELMAP.GUI; on NEWGAME.GUI the
    /// difficulty and the side buttons; on the options screen the VISUALS and
    /// SPEEDS tabs and every staged button of their panels. Each click must
    /// change the setting and the caption, frame, text or pressed button shown
    /// for it, with the pixels to match, before Start; the clicked options tab
    /// must stay pressed and the others raised. HELPTEXT must show the help of
    /// the control under the pointer: the new help of a clicked rule button,
    /// the first row's allegiance, metal and energy hints, and nothing over its
    /// player, side and colour. --snapshot takes the skirmish, new-campaign and
    /// options frames as <stem>-<step>.ppm. Throws std::runtime_error listing
    /// every mismatch.
    void check_frontend_controls();

    /// Checks the engine screens' scroll bars through the SDL presenter.
    ///
    /// The options' SOUND panel binds FXVOL between its arrows with 89
    /// positions and draws its knob; a press beside the knob steps it one
    /// position and a hold one a tick, a drag moves it pixel for pixel, an
    /// arrow steps at once and repeats after 15 ticks, and each move sets the
    /// effects volume the knob stands for. SELMAP.GUI's list shows its scroll
    /// bar when the maps overflow it, with the knob 3.1c sizes, and its knob
    /// scrolls the list; so does NEWGAME.GUI's Missions list for any mission,
    /// bound once its setup has placed and filled it. Over a skirmish in a 1920x1080 window and a 1280x960
    /// one the preferences' SPEEDS sub-panel draws GAME's knob, the knob sets
    /// the game speed, and the sub-panel's last rows show over the battlefield
    /// where the bottom bar sits apart from the chrome (its own picture under
    /// them) and in the bottom bar where it joins the chrome, with the pointer
    /// over them taking the sub-panel's controls. Throws std::runtime_error on
    /// a failure.
    void check_scroll_bars();

    /// Returns the canvas point of a point of the screen's panel, or of the
    /// HUD layer's 640x480 source in a match.
    ///
    /// @param x column in the panel's (or source) coordinates
    /// @param y row in the panel's (or source) coordinates
    /// @return the canvas point the pointer is at over it
    [[nodiscard]] oa::ui::display_layout::Point scroll_canvas_point(int32_t x, int32_t y) const;

    /// Sends a pointer event at a canvas point, through the SDL presenter's
    /// coordinates when there is one.
    ///
    /// @param type SDL_EVENT_MOUSE_MOTION, SDL_EVENT_MOUSE_BUTTON_DOWN or SDL_EVENT_MOUSE_BUTTON_UP
    /// @param canvas canvas point
    /// @param button SDL button of a press or release
    void
    send_check_pointer(SDL_EventType type, oa::ui::display_layout::Point canvas, uint8_t button);

    /// Returns a bound scroll bar of the screen's panel, or of the match HUD's panel in a match.
    ///
    /// Throws std::runtime_error naming the bar when it is not bound.
    ///
    /// @param name the bar's gadget
    /// @return the bar
    [[nodiscard]] const renderer::LayoutScrolls::Bar& check_scroll_bar(std::string_view name);

    /// Drags a scroll bar's knob along the bar through pointer events, as a
    /// player does: a press on the knob, a move, one update and the release.
    ///
    /// @param name the bar's gadget
    /// @param pixels pixels along the bar, negative toward its start
    void drag_check_knob(std::string_view name, int32_t pixels);

    /// Clicks one of a scroll bar's arrows through pointer events.
    ///
    /// @param name the bar's gadget
    /// @param forward true for the arrow that steps the knob forward
    void click_check_arrow(std::string_view name, bool forward);

    /// Checks that the mission briefing's narration plays exactly while SHUTUP is on.
    ///
    /// Opens the first mission's briefing from NEWGAME.GUI with sound on: the
    /// narration plays and SHUTUP shows "Narration"; SHUTUP silences it at once
    /// and, clicked again, plays it anew; PrevMenu and Escape silence it and go
    /// back; a briefing left with SHUTUP off opens again with it on; SHUTUP
    /// turns off once the narration is over; Start and Enter silence it and
    /// start the mission. --snapshot takes the briefing's
    /// frames as <stem>-<step>.ppm. Throws std::runtime_error on a failure, and
    /// at once under --mute.
    void check_briefing_narration();

    /// Plays the showcase Options::showcase names from the main menu, with
    /// pointer and key events sent through dispatch_event() as SDL delivers
    /// the player's, on the application loop's frames and clock.
    ///
    /// arm_first_mission: SINGLE, New Campaign on the Arm side, the first
    /// mission's briefing for a while and its Start; then Ctrl+A selects the
    /// player's units, MOVE ORDERS is clicked until every mobile unit holds
    /// position, and MOVE and a click on the radar at the point of the
    /// mission's MoveUnitToRadius victory condition send them there. The
    /// camera follows the mobile unit of the group nearest that point, the T
    /// key cycling the selection to it, and changes to another when it dies
    /// or another is well ahead of it. After the victory the end screen runs
    /// on: the darkened last frame, the glamour picture, which a click
    /// advances after a while, and the score screen, held a while. Prints a
    /// "showcase:" line for each step. Throws std::runtime_error when a step
    /// does not come about in its time or the mission is lost.
    void run_showcase();

    /// Runs one pass of the application loop: the pending SDL events, the
    /// idle tick, the sweep of finished sound streams when one is due and the
    /// log files' upkeep.
    ///
    /// @param[in,out] running loop flag; cleared when an event ends the loop
    void run_frame(bool& running);

    /// Leaves the end screen: closes the report, tells the extension and releases the finished
    /// match.
    void release_endgame();

    /// Goes on after CDCHECK.GUI accepted the disc: the end screen proceeds to its outcome step, or
    /// the match outcome finishes without a kept game.
    void resume_endgame_after_disc();

    /// Checks the end screen of the finished campaign mission, stepped on its own clock.
    ///
    /// The disc check when the disc is absent, then the stat bars, drawn from the
    /// kept Game block's UI colour table, until the buttons take over. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_endgame_screen(const fs::path& report_directory);

    /// Checks that the end screen opens on the finished match's last frame and darkens it.
    ///
    /// The screen's first frame is the kept frame at the match's size, the
    /// outcome's title included, with nothing of ENDMSN.GUI drawn; over the
    /// ten shade steps it grows no brighter and ends at a tenth of its
    /// brightness or less, and then the display goes back to the frontend's
    /// size. Writes native-campaign-outcome.ppm and
    /// native-campaign-darkening.ppm (half-way) and leaves the screen at its
    /// disc check step. Throws std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_end_screen_darkening(const fs::path& report_directory);

    /// Checks through the SDL presenter that a won skirmish leaves its outcome frame for the
    /// end screen on its own.
    ///
    /// The opponents' units are swept and the match stepped until its victory
    /// is decided; then one frame of the main loop draws the VICTORY frame and
    /// leaves for the end screen, which keeps that frame (the software cursor
    /// aside) and shows it at the match's size, growing no brighter, until the
    /// darkening ends. The panel that follows shows Main Menu alone, in the
    /// single button housing of the Outcome0 background. Writes
    /// native-match-end-outcome.ppm and native-match-end-panel.ppm. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frame is written to
    void check_presented_match_end(const fs::path& report_directory);

    /// What step_end_screen_to_panel() met on the way to the end screen's panel.
    struct EndScreenSteps {
        bool disc_check{};      ///< a disc check opened and was accepted
        bool glamour_pressed{}; ///< a key was pressed at the faded-in glamour picture
        bool glamour_refused{}; ///< the screen did not take that key, and stepping stopped
        bool panel{};           ///< the panel is up
    };

    /// Steps the end screen of a finished mission on its own clock until it
    /// shows its panel, as a player who presses a key at the glamour picture
    /// and accepts the disc check sees it. A final campaign victory runs the
    /// ending it leaves for (run_pending_ending()) instead.
    ///
    /// @param at_disc_check called while a disc check is open, before it is
    ///     accepted; may be empty
    /// @param at_glamour called at the faded-in glamour picture, before the
    ///     key press; may be empty
    /// @return what the screen met; `panel` is false without a kept game,
    ///     after the ending, or when the screen did not reach its panel in time
    EndScreenSteps step_end_screen_to_panel(
        const std::function<void()>& at_disc_check = {},
        const std::function<void()>& at_glamour = {}
    );

    /// Checks that the faded-in glamour picture covers the frame in its own palette.
    ///
    /// Throws std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frame is written to
    void check_glamour_frame(const fs::path& report_directory);

    /// Checks ENDMSN after a won campaign mission.
    ///
    /// The mission is marked won and the list preselects the next one; Start
    /// opens that mission's briefing, whose Back reopens the panel over the kept
    /// game. Throws std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_campaign_advance(const fs::path& report_directory);

    /// Checks that the campaign's use-only list greys build buttons.
    ///
    /// AC01's use-only list leaves ARMCOM none of the units on its first build
    /// page: with a finished local ARMCOM selected, each of those unit buttons is
    /// greyed, drawn at its disabled frame and refuses the click. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    void check_campaign_build_page(const fs::path& report_directory);

    /// Checks the save dialog SAVEGAME opens from the pause menu or ENDMSN.
    ///
    /// The typed name and Return write the save and return to the screen it was
    /// opened over. Throws std::runtime_error on a failure.
    ///
    /// @param report_directory directory the frames are written to
    /// @param name save name to type
    /// @return the Summary the save holds
    oa::ui::frontend::LoadSummary
    check_save_dialog(const fs::path& report_directory, const std::string& name);

    /// Enters the match view once a match is built.
    ///
    /// Mission start sets the session's cheat flag; the menu music stops, the
    /// match palette, textures, FX and fog art, side HUD and chrome load, and the
    /// local commander is selected.
    void enter_match_view();

    /// Selects the local player's first active unit (the commander) as the match starts.
    void select_local_commander();
    // Side table, player records and respawn view of the offline match
    // (runtime_sides.cpp).

    /// Loads the side table once, as engine start-up does; every match copies it into Game.sides.
    ///
    /// Throws std::runtime_error when SIDEDATA.TDF fails or defines no side.
    void load_side_table();

    /// Loads the two fonts the engine keeps for its whole run: COMIX, which the message log draws
    /// in, and smlfont.
    void load_common_fonts();

    /// Loads the interface texts of gamedata/translate.tdf in a language, in place of those of
    /// the language loaded before.
    ///
    /// Each section of the file names a text as the game holds it and gives its translation
    /// under the language's key; English has none, so its texts show as they are.
    ///
    /// @param language language key, such as "German"; null is English
    void load_translations(const char* language);

    /// Fills Game.sides from the side table and every player record as a skirmish or campaign
    /// leaves it.
    ///
    /// The single-player menu clears each of the eleven records and gives it its
    /// own index as colour; Start gives an enabled slot its side, colour and
    /// controller (a campaign seats its two sides the same way); the mission
    /// start marks the local record started. No starting resources, unit limit
    /// or host role are written: those come from a multiplayer game's setup,
    /// whose launch replaces the slots' records.
    ///
    /// @param[in,out] world match world
    void bind_player_records(oa::World& world);

    /// Seats the skirmish roster's rows, as the settings block holds them, over the match's player
    /// records.
    ///
    /// @param[in,out] world match world
    void seat_skirmish_roster(oa::World& world);

    /// Seats a campaign's players: the local player in record 0 and the computer, in colour 1, in
    /// record 1.
    ///
    /// The seating half of the campaign's profile set-up; bootstrap_match owns
    /// the mode change.
    ///
    /// @param[in,out] world match world
    void seat_campaign_players(oa::World& world);

    /// Binds the respawned commander's view and the watch-mode notices to the match.
    ///
    /// A respawn rebuilds the sight grids and selects the commander (the
    /// commander finder with 1). A defeated multiplayer player who goes on
    /// watching gets the rebuilt grids too, and while computer players it hosts
    /// still play the watch-mode message box.
    void bind_respawn_view();

    /// Shows a sight rebuild in the view.
    ///
    /// The runtime's copy of the viewer's explored terrain is dropped when the
    /// mapped grid is refilled, and the radar, which every rebuild refills and
    /// composes, is at its next frame. Fog is drawn from the match's sight grids
    /// every frame.
    ///
    /// @param refill_mapped true when the mapped grid was refilled too
    void reset_sight_presentation(bool refill_mapped);

    /// Rebuilds the match's sight grids, as a launch, a console visibility command or a respawn
    /// does; nothing while altitude sight is blocked.
    ///
    /// @param refill_mapped true to refill the mapped grid too
    void reset_match_sight(bool refill_mapped);

    /// Selects the local commander through the commander finder (with 1) over the runtime's follow,
    /// camera and command state.
    ///
    /// The unit it leaves selected becomes the runtime's selection; the camera
    /// centres on it at once, where 3.1c glides it there.
    void select_side_commander();

    /// Selects the CTRL_C category and follows the local commander, as the key dispatcher runs
    /// Ctrl+C.
    ///
    /// Game.follow_unit mirrors the runtime's tracked unit around the call, since
    /// this runtime's camera follows through tracked_match_unit_.
    ///
    /// @param add true to add to the selection
    void select_and_follow_commander(bool add);

    /// Checks Game.sides and the player records of a started match.
    ///
    /// Game.sides carries the side table's commanders and every enabled slot's
    /// Player.info resolves to that slot's side, colour and controller. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param context label printed with the result
    void check_player_records(std::string_view context);

    /// Checks a deathmatch commander respawn from the skirmish menu.
    ///
    /// A skirmish under the deathmatch commander rule whose local units are all
    /// destroyed must respawn a commander that ends up selected with the camera on
    /// it. A skirmish seats no host (the active slot search finds none), so the
    /// respawn reads the no-player record's player-info block and the player
    /// keeps only the start storage floor. Returns to the skirmish menu; throws
    /// std::runtime_error on a failure.
    void check_deathmatch_respawn();

    /// Creates the RGB24 streaming output texture at a size, unless it already has it.
    ///
    /// Throws std::runtime_error when SDL cannot create it.
    ///
    /// @param width texture width
    /// @param height texture height
    void ensure_texture(int width, int height);

    /// Sets the SDL presentation for the current screen.
    ///
    /// A match lays the battlefield and chrome out for the window's pixel size;
    /// other screens present at the canvas size, the load and save dialogs at the
    /// size of the frame they are drawn over. Throws std::runtime_error when SDL
    /// refuses.
    void apply_output_mode();

    /// Returns the battlefield zoom.
    ///
    /// @return canvas pixels per map pixel; 1 draws the map 1:1
    [[nodiscard]] float match_zoom() const;

    /// Returns how many map pixels across the battlefield shows at the current zoom.
    ///
    /// @return the battlefield width over the zoom, at least 1
    [[nodiscard]] int visible_map_width() const;

    /// Returns how many map pixels down the battlefield shows at the current zoom.
    ///
    /// @return the battlefield height over the zoom, at least 1
    [[nodiscard]] int visible_map_height() const;

    /// Returns the battlefield viewport for a camera position with the live layout and zoom.
    ///
    /// @param camera_x camera column in map pixels
    /// @param camera_y camera row in map pixels
    /// @return the viewport
    [[nodiscard]] oa::present::world_renderer::BattlefieldViewport
    live_viewport(uint32_t camera_x, uint32_t camera_y) const;

    /// Starts SDL video and audio with a resizable window and renderer unless both were borrowed,
    /// then sets the presentation and loads the software cursor.
    ///
    /// Throws std::runtime_error when SDL refuses.
    void initialize_sdl();

    /// Switches the window between full screen and a window (Alt+Enter).
    ///
    /// A switch SDL refuses leaves the window as it is and is reported on stderr.
    void toggle_full_screen();

    /// Returns an XRGB8888 streaming texture of a size, recreating it when the size changed.
    ///
    /// Throws std::runtime_error when SDL cannot create it.
    ///
    /// @param existing current texture, or null
    /// @param width texture width
    /// @param height texture height
    /// @param[in,out] stored_w width of `existing`; updated on recreation
    /// @param[in,out] stored_h height of `existing`; updated on recreation
    /// @return the texture of that size
    SDL_Texture*
    ensure_xrgb_texture(SDL_Texture* existing, int width, int height, int& stored_w, int& stored_h);

    /// Uploads an RGB24 surface into an XRGB texture through the display gamma.
    ///
    /// Throws std::runtime_error when the texture cannot be locked.
    ///
    /// @param texture XRGB texture of the surface's size; null uploads nothing
    /// @param source RGB surface
    void upload_rgb24_xrgb(SDL_Texture* texture, const renderer::Surface& source);

    /// Destroys the match layer textures (HUD, world, cursor and dialog) and forgets their sizes.
    void destroy_match_layer_textures();

    /// A HUD-layer rectangle in 640x480 source space and where it lands on
    /// the canvas.
    struct HudStrip {
        int source_x = 0, source_y = 0, source_w = 0, source_h = 0;
        int x = 0, y = 0, w = 0, h = 0;
    };

    /// Returns the side column, top bar and bottom bar of the 640x480 HUD layer, scaled to the live
    /// layout.
    ///
    /// The top and bottom bars hang from the column's right edge.
    ///
    /// @return each strip's canvas rectangle and source rectangle
    std::array<HudStrip, 3> match_hud_strips() const;

    /// Composes the HUD strips and the world layer on the match canvas, black where the chrome
    /// stops short of the window, before the display gamma.
    ///
    /// @param[out] frame canvas-sized RGB frame
    void compose_match_layers(renderer::Surface& frame);

    /// Builds on the CPU the frame present_match_layers shows.
    ///
    /// The match layers, then the dialog layer the last presented frame carried,
    /// all at the display gamma.
    ///
    /// @param[out] frame canvas-sized RGB frame
    void compose_match_frame(renderer::Surface& frame);

    /// Presents the match as layers through SDL: the HUD strips and the world layer, the dialog
    /// layer and the software cursor.
    void present_match_layers();

    /// Presents the software cursor over the match layers at the pointer.
    void present_software_cursor();

    /// Composes the current screen and presents it.
    ///
    /// The loading screen went out through the display sink as it was drawn; a
    /// match presents in layers when it can; any other frame is uploaded at the
    /// display gamma (a match's frame is composed at it already).
    void render();

    /// Returns the current frontend screen's gadgets and selection as the input handlers' menu.
    ///
    /// @return the menu
    [[nodiscard]] oa::ui::gui_input::MenuObject input_menu() const;

    /// Moves the pointer and finds what it hovers.
    ///
    /// In a match: the HUD or end overlay gadget, else the unit under the
    /// pointer. Elsewhere: the gadget of the screen, the centred map modal or the
    /// load dialog; the hit test's first candidate is kept, even while the
    /// button is down.
    ///
    /// @param x canvas column
    /// @param y canvas row
    void update_pointer(float x, float y);

    /// Tells whether a press on a gadget of the current screen selects it: a grayed-out button
    /// takes no press, as in the game's panels.
    ///
    /// @param index gadget index in the current screen's layout
    /// @return false for a grayed-out button or an index past the layout, else true
    [[nodiscard]] bool frontend_gadget_pressable(std::size_t index) const;

    /// Activates the selected gadget of the current screen through its menu handler, then runs the
    /// frontend dispatcher when the menu asked for a new state.
    ///
    /// MULTI asks the extension what to do (Extension::select_multiplayer);
    /// without an answer it only says multiplayer is unavailable and the main
    /// menu stays up. With no map holding a multiplayer schema, MULTI and
    /// SINGLE.GUI's Skirmish show the missing-content notice instead. The
    /// frontend mode tick runs its unit header step before each dispatcher
    /// pass; dispatching every frame
    /// instead would rebuild MAINMENU while its signal stays initialize, dropping
    /// a press before its release.
    void activate();

    /// Steps the stage of the selected button as releasing a click on it does.
    ///
    /// A plain push button with stages shows its next stage's frame and
    /// caption; other gadgets keep theirs.
    void step_released_button_stage();

    /// Maps a canvas point to the game's screen, where the pointer word (Game.pointer_state) and the
    /// on-screen list live.
    ///
    /// Over the battlefield it is the game view (Game.battlefield_rect): map
    /// pixels from the view's corner, which sits at (128, 32) as on the
    /// unzoomed 640x480 screen, however large the window or the zoom; elsewhere
    /// it is the 640x480 HUD space (display_layout::canvas_to_source).
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return the point on the game's screen
    [[nodiscard]] oa::ui::display_layout::Point game_screen_point(float x, float y) const;

    /// Maps a point of the game's screen back to the canvas, the inverse of game_screen_point().
    ///
    /// @param x column on the game's screen
    /// @param y row on the game's screen
    /// @return the canvas point
    [[nodiscard]] oa::ui::display_layout::Point game_screen_canvas(int32_t x, int32_t y) const;

    /// Returns the on-screen unit list (on_screen_units_) and the radar's hot units as the
    /// selection module reads them.
    ///
    /// @return the buffers Game.hot_unit_count and hot_radar_unit_count count into
    [[nodiscard]] oa::sim::selection::VisibleLists on_screen_lists();

    /// Returns the selection module's services over the running match.
    ///
    /// Sight is Match::unit_visible; the pointer test is the unit type's root
    /// box, turned by the unit's angles (gameplay_input::hits_root_bounds); the
    /// armed command and the order panel are the runtime's; the camera centres
    /// at once. A unit's select speech is not queued.
    ///
    /// @return the hooks, bound to this runtime
    [[nodiscard]] oa::sim::selection::Hooks selection_hooks();

    /// Rebuilds the on-screen unit list for the viewpoint player (Game.hot_unit_count).
    ///
    /// Every unit whose type box overlaps the game view and that the
    /// viewpoint player owns or sees is listed. Each drawn frame rebuilds it
    /// after the match's ticks, so the under-attack notice and the pointer
    /// test the list of the frame last drawn. The buffer takes the match's
    /// unit slot count, and the offline services' on-screen test reads it.
    void rebuild_on_screen_units();

    /// Brings the view up to date as a frame would before the pointer picks: the camera held on
    /// the map and bound to the Game block (bind_match_view), and the on-screen list rebuilt.
    ///
    /// Between frames nothing else moves the view, so the list is the frame's
    /// own; a camera moved without a frame drawn is taken as the next frame's.
    void refresh_on_screen_view();

    /// Picks the unit under the pointer into Game.cursor_unit_id, which hovered_match_unit_ mirrors.
    ///
    /// The pointer goes into Game.pointer_state on the game's screen. Over the
    /// game view the smallest listed unit whose turned root box holds the
    /// pointer wins (selection::unit_under_pointer), whoever owns it, the
    /// earlier listed on a tie; over the radar the nearest blip within reach.
    /// Off both, or over the game view while a building is being placed, the
    /// unit picked last stays. Every frame picks again, so a unit that moves
    /// under a still pointer becomes the cursor unit; that pick tests the
    /// frame last drawn. An input event first brings the view up to date
    /// (refresh_on_screen_view), since a check can move the camera without
    /// drawing.
    ///
    /// @param refresh_view true to bring the view up to date first
    void pick_cursor_unit(bool refresh_view = true);

    /// Returns the order cursor's queries over the match: point visibility, the feature at a point
    /// and weapon reach.
    ///
    /// @return the hooks, bound to this runtime
    [[nodiscard]] oa::sim::gameplay_input::OrderCursorHooks order_cursor_hooks();

    /// Strips a side prefix (ARM or COR) from a HUD gadget name.
    ///
    /// @param name gadget name
    /// @return the action name
    std::string_view match_hud_action(std::string_view name) const;

    /// Tests whether a unit definition names a weapon other than NoWeapon.
    ///
    /// @param definition unit definition
    /// @return true when weapon1, weapon2 or weapon3 names one
    bool definition_has_weapon(const oa::data::unit_definitions::UnitDefinition& definition) const;

    /// Tests whether a unit can D-gun: its definition says so or one of its weapons is
    /// command-fired.
    ///
    /// @param definition unit definition
    /// @return true when it can
    bool definition_has_dgun(const oa::data::unit_definitions::UnitDefinition& definition) const;

    /// Tests whether a HUD gadget's command is open to the selection.
    ///
    /// Greyed buttons and greyed status gadgets are not; unit buttons are; order
    /// buttons follow the selected unit's definition (MOVE, ATTACK, BLAST/DGUN,
    /// PATROL and the rest).
    ///
    /// @param gadget HUD gadget
    /// @return true when the command is available
    bool gadget_command_available(const oa::ui::gui_layout::Gadget& gadget) const;

    // Order page buttons through the order panel (runtime_order_panel.cpp).
    struct MatchGadgetState {
        // The button's status: on (nonzero) or off for a toggle, and the frame
        // of its art for FIREORD, MOVEORD, ONOFF and CLOAK.
        int16_t status{};
        bool grayed{}; // greyed: its command is not open to the selection
    };

    /// Returns the match's unit and unit type tables for the order panel.
    ///
    /// @return the tables
    [[nodiscard]] oa::ui::hud::UnitTable order_panel_table();

    /// Returns the order panel's controls over the match HUD's gadgets and states.
    ///
    /// @return the controls, bound to this runtime
    [[nodiscard]] oa::ui::hud::PanelControls order_panel_controls();

    /// Returns the order panel's events: interface sounds and group orders.
    ///
    /// @return the events, bound to this runtime
    [[nodiscard]] oa::ui::hud::HudEvents order_panel_events();

    /// Returns the gadget engine's side of the build and order page loaders.
    ///
    /// Panels are GUI files under guis\ (the name's extension swapped for GUI),
    /// loaded as the match HUD one at a time, so closing to the root always
    /// succeeds. A linked download button becomes an ungreyed unit button named
    /// after the unit, with its art from the unit's _gadget GAF.
    ///
    /// @return the loader, bound to this runtime
    [[nodiscard]] oa::ui::hud::PanelLoader order_panel_loader();

    /// Returns the build panel click's collaborators.
    ///
    /// Order buttons go through the HUD's own order handling, and a queue change
    /// is classified and then applied to the queue.
    ///
    /// @return the host, bound to this runtime
    [[nodiscard]] oa::ui::hud::BuildPanelHost build_panel_host();

    /// Returns the viewpoint side's SIDEDATA nameprefix ("ARM", "COR").
    ///
    /// @return the prefix, else the side prefix in capitals
    [[nodiscard]] std::string match_side_name_prefix() const;

    /// Loads the art of each gadget with gaffile set: the sequence of its own name in
    /// anims\<name>_gadget.gaf, whatever the page GAF holds.
    void bind_gadget_gaf_art();

    /// Runs a HUD order button: the standing order toggles, then the command buttons, then
    /// self-destruct, as the build panel click tries them.
    ///
    /// @param index gadget index in the match HUD
    /// @param gadget_name gadget name
    /// @return false for any other button
    bool run_match_order_button(std::size_t index, std::string_view gadget_name);

    /// Gives a named order to each selected local unit it reaches, at the head of its orders; no
    /// position is added for the order panel's tags.
    ///
    /// @param tag order name
    /// @param value order value
    void apply_group_order(const char* tag, int32_t value);

    /// Gives a mission with no position to the selected local units, as "Assign" gives it.
    ///
    /// A mission that takes a target unit is aimed at the unit under the pointer,
    /// which then stays out of the group; the standing orders reach only the
    /// types that take them; each selected local unit gets the mission through
    /// the order queue, queued while shift is held (the last pointer event's
    /// shift bit, bit 2 of Game.pointer_state word 2). A failure is shown on
    /// the status line.
    ///
    /// @param kind mission kind
    /// @param parameter_1 the order's first parameter (type, slot or duration by kind)
    /// @param parameter_2 the order's second parameter (count or radius by kind)
    void issue_group_mission(uint8_t kind, int32_t parameter_1, int32_t parameter_2);

    /// Summarizes the local player's selected units, as the order panel does before it loads a
    /// build or general order page.
    ///
    /// @param[in,out] state order panel state; its frame and order flags take the
    ///     summary's
    /// @return the summary
    oa::ui::hud::SelectionSummary summarize_order_panel(oa::ui::hud::OrderPanelState& state);

    /// Toggles a standing order button of the order panel over the Game block's panel state.
    ///
    /// @param index gadget index in the match HUD
    void toggle_order_button(std::size_t index);

    /// Returns the kept state of a match HUD gadget that shows a status frame.
    ///
    /// @param gadget match HUD gadget
    /// @return the state, or null for a gadget without one
    [[nodiscard]] const MatchGadgetState*
    match_gadget_state(const oa::ui::gui_layout::Gadget& gadget) const;

    /// Returns the art frame a match HUD status gadget shows.
    ///
    /// A grayed one shows the last frame of its art and any other the frame its
    /// status names.
    ///
    /// @param index gadget index in the match HUD
    /// @return the frame, or nullopt for a gadget without status or art
    [[nodiscard]] std::optional<std::size_t> match_status_frame(std::size_t index) const;
    // The kills board F4 pins out (runtime_kill_board.cpp).

    /// Draws the kills board over the battlefield's top-right corner.
    ///
    /// The HUD overlay draws it in skirmish and multiplayer games (session kinds
    /// 2 and 3), after the selection outlines and before the message log; a
    /// campaign mission has none.
    void draw_match_kill_board();

    /// Loads hattfont12.gaf for the kills board once; a failure is reported on stderr.
    void ensure_gui_font();

    /// Converts a point of the kills board's 640x480 screen to the canvas.
    ///
    /// The board is laid out on a 640x480 screen whose right edge is the
    /// canvas's and whose y 32 is the battlefield's top; a screen pixel is a
    /// hud_text_scale() block.
    ///
    /// @param x board screen column
    /// @param y board screen row
    /// @return canvas point
    [[nodiscard]] oa::ui::display_layout::Point board_canvas(int x, int y) const;

    /// Returns the paint target's RGB pixel under a canvas point.
    ///
    /// @param canvas_x canvas column
    /// @param canvas_y canvas row
    /// @return the pixel's three bytes, or null off the target
    uint8_t* board_pixel(int canvas_x, int canvas_y);

    /// Shades the board's screen rectangle x0,y0-x1,y1 (inclusive): every canvas pixel under it
    /// goes through the level's shade table row.
    ///
    /// @param x0 left board column
    /// @param y0 top board row
    /// @param x1 right board column
    /// @param y1 bottom board row
    /// @param level shade table row
    void shade_board_rect(int x0, int y0, int x1, int y1, int level);

    /// Paints an 8-bit drawing over the paint target.
    ///
    /// `draw` paints a width x height patch into an 8-bit surface whose origin
    /// is the patch's top-left pixel, once over each pass fill. Each pixel both
    /// passes agree on (what `draw` painted) goes to the paint target as a
    /// hud_text_scale() block: the patch's pixel (column, row) covers the block
    /// at `corner` + (column, row) x scale, clipped to the target.
    ///
    /// @param corner paint point of the block of the patch's top-left pixel
    /// @param width patch width
    /// @param height patch height
    /// @param columns patch columns painted, from the left; the rest are left out
    /// @param draw paints the patch
    /// @param user passed to `draw`
    void overlay_patch(
        oa::ui::display_layout::Point corner,
        int width,
        int height,
        int columns,
        void (*draw)(void* user, oa::Surface& surface),
        void* user
    );

    /// Renders the frame of the logo sequence for a player's colour.
    ///
    /// @param player player whose PlayerSetupInfo holds the colour
    /// @return the frame, or nothing without the player's PlayerSetupInfo, the
    ///         logo sequence, a frame for the colour or a frame that renders
    std::optional<oa::formats::gaf::RenderedFrame> player_logo_frame(const oa::Player& player);

    /// Paints part of the kills board through an 8-bit drawing.
    ///
    /// `draw` paints the board's screen pixels x..x+width-1, y..y+height-1 into an
    /// 8-bit surface whose origin is that corner, once over each pass fill; what
    /// it paints (the pixels both passes agree on) goes to the canvas as blocks,
    /// cut at the screen's right edge.
    ///
    /// @param x board screen column of the patch
    /// @param y board screen row of the patch
    /// @param width patch width
    /// @param height patch height
    /// @param draw paints the patch
    /// @param user passed to `draw`
    void overlay_board_patch(
        int x,
        int y,
        int width,
        int height,
        void (*draw)(void* user, oa::Surface& surface),
        void* user
    );

    /// Refreshes the loaded build page, as the build page loader does.
    ///
    /// Each button's queued count and, with `check_validity`, the unit buttons
    /// greyed whose types the mission's use-only list removed (every page past
    /// page 0 is checked, and the app's build pages start at 1). The counts are
    /// refreshed every frame so they follow the queue.
    ///
    /// @param check_validity true to grey the unavailable unit buttons too
    void refresh_build_page(bool check_validity);

    /// Returns the frame a greyed picture button of the match HUD shows.
    ///
    /// The gadget drawer draws a greyed picture button at its last frame under
    /// attribute 0x100, at its first under 0x1800, else at frame status + 2 (at
    /// most the last). A greyed button without a picture of its own stays hidden.
    ///
    /// @param gadget match HUD gadget
    /// @return the frame, or nullopt when the gadget is not a greyed picture
    ///     button with art
    [[nodiscard]] std::optional<std::size_t>
    greyed_picture_frame(const oa::ui::gui_layout::Gadget& gadget) const;

    /// Draws the unit and weapon buttons' captions (their queued counts) at each button's corner.
    void draw_build_captions();

    /// Clicks the loaded build or order page through the build panel click.
    ///
    /// A paused match sends the click to the pause menu instead. Its page and
    /// menu requests are serviced here at once instead of waiting in
    /// frame_flags for the next frame's page-flag pass.
    ///
    /// @param index gadget index in the match HUD
    /// @param left_button false for a right click
    void activate_match_hud(std::size_t index, bool left_button = true);

    /// Presses a command button as the gadget click and the panel take it.
    ///
    /// A toggle button's status flips and a plain one's (STOP) ends at 0, the lit
    /// buttons of its group go out, then the command arms the order and plays the
    /// button's sound. STOP stops the selected units at once.
    ///
    /// @param index gadget index in the match HUD
    /// @return false when the gadget is not a command button
    bool press_match_command_button(std::size_t index);

    /// Tests whether a command button draws lit: a toggle button whose status is set.
    ///
    /// @param index gadget index in the match HUD
    /// @return true when lit
    [[nodiscard]] bool match_command_lit(std::size_t index) const;

    /// Returns to the default order; the buttons sharing the STOP button's group, the command
    /// buttons, go out as the game clears them.
    void reset_match_command();

    /// Tests whether the pointer's orders queue: the shift bit of the last pointer event (bit 2
    /// of Game.pointer_state word 2).
    ///
    /// While it is held the orders the pointer gives are queued and the armed
    /// command then stays.
    ///
    /// @return true while shift was held
    bool queueing() const;

    /// Drops the armed command after an order unless shift keeps it for queueing.
    void finish_issued_command();

    /// Plays a named interface sound unless muted, and records it for a check that listens.
    ///
    /// @param name sound name
    void play_match_interface_sound(std::string_view name);

    /// Places the pending building at a site, as a build click does.
    ///
    /// A refused site only plays notoktobuild; otherwise each selected mobile
    /// builder takes its MobileBuild (VTOL_MobileBuild for one that flies) there,
    /// or has the one queued there taken off, oktobuild plays, and build mode
    /// stays while shift is held.
    ///
    /// @param target 16.16 world point of the click
    void place_pending_build_at(const oa::sim::ground_orders::Point& target);

    /// Places the pending building under a canvas point: the radar's map point, else the
    /// battlefield's.
    ///
    /// @param x canvas column
    /// @param y canvas row
    void place_pending_build(float x, float y);

    /// Gives the selected units Reclaim on the reclaimable feature under the pointer.
    ///
    /// The feature is the one on the cell of the ground point under the pointer
    /// (Game.cursor_position), aimed at its footprint's centre; a matching queued order is
    /// taken off instead. A failure is shown on the status line.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return false without a selection, ground or reclaimable feature there
    bool try_reclaim_feature_at(float x, float y);

    /// Returns the centre of the reclaimable feature's footprint on the cell of a ground point,
    /// where the engine aims a Reclaim order.
    ///
    /// @param ground 16.16 ground point
    /// @return 16.16 point at height 0, or nullopt without a reclaimable feature
    [[nodiscard]] std::optional<oa::sim::ground_orders::Point>
    feature_reclaim_point(const oa::sim::ground_orders::Point& ground) const;

    /// Returns the feature definition loader over feature_assets_ for the match's FeatureDef table.
    ///
    /// It loads animation GAFs, sequences and 3DO models by name into
    /// feature_assets_ and hands out their references.
    ///
    /// @return the host, bound to this runtime
    oa::sim::map_runtime::FeatureDefHost feature_def_host();

    /// Loads and parses the feature TDF set, every features/**/*.tdf document in listing order.
    ///
    /// @return the documents, or the first listing, read or parse error
    oa::data::unit_definitions::Result<std::vector<oa::data::unit_definitions::TdfDocument>>
    load_feature_tdf_set() const;

    /// Reads one frame of a feature sequence reference (feature_assets_), as the match's feature
    /// runtime reads it.
    ///
    /// @param sequence sequence reference, from 1
    /// @param frame frame index
    /// @param[out] out the frame's size, origin, duration, and the sequence's
    ///     frame count and repeat byte
    /// @return false for no sequence or past the last frame
    bool feature_sequence_frame(
        oa_ref32 sequence, uint16_t frame, oa::sim::feature_runtime::FeatureSequenceFrame& out
    ) const;

    /// Finds a feature of the loaded feature catalog by name, ignoring ASCII case.
    ///
    /// @param name feature name
    /// @return the feature, or null
    const oa::sim::map_runtime::NamedFeature* find_catalog_feature(std::string_view name) const;

    /// Adds a GAF feature animation to the match's animation list once per file and sequence.
    ///
    /// @param filename GAF file the sequence came from
    /// @param seqname sequence name
    /// @param sequence sequence to render
    /// @param animating true to step its frames each tick
    /// @return the animation's index, or SIZE_MAX when no frame renders
    std::size_t intern_gaf_feature_anim(
        const std::string& filename,
        const std::string& seqname,
        const oa::formats::gaf::Sequence& sequence,
        bool animating
    );

    /// Adds the shadow sequence of a sprite feature (FeatureDef.seq_name_shadow) as an animation.
    ///
    /// It runs on a cursor of its own, the def's shadow_cursor, that the
    /// feature tick steps with the body's.
    ///
    /// @param feature_index index into the feature table
    /// @param filename GAF file of the feature
    /// @param animating true to step its frames each tick
    /// @return the animation's index, or SIZE_MAX without a shadow sequence
    std::size_t
    intern_feature_shadow_anim(uint16_t feature_index, const std::string& filename, bool animating);

    /// Blits a GAF frame through the alpha table onto the battlefield frame at a hotspot.
    ///
    /// Each covered pixel takes table[source * 256 + destination], the
    /// destination's palette index read back from the RGB world through the model
    /// bridge. Nothing is drawn without an alpha table.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param frame rendered frame
    /// @param screen frame point the GAF origin lands on
    /// @param scale size factor; 0 or less draws at 1
    /// @param models renderer state holding the display, palette and bridge
    void blit_gaf_blended_hotspot(
        oa::present::world_renderer::Surface& destination,
        const oa::formats::gaf::RenderedFrame& frame,
        const oa::present::world_renderer::ScreenPoint& screen,
        float scale,
        MatchModels& models
    );

    /// Steps the animating GAF feature animations once per tick up to a tick.
    ///
    /// @param tick match tick to catch up to
    void advance_gaf_feature_anims(uint32_t tick);

    /// Replaces the draws of features the feature runtime replaced.
    ///
    /// A drawn feature follows its origin plot's word: once the feature runtime
    /// replaces it (a die, burn or reclamate remnant, or nothing) the draw goes and
    /// the remnant's is placed.
    void sync_dead_feature_draws();

    /// Places the draw of the FeatureDef on an origin plot.
    ///
    /// A 3DO feature where its pool record stands and faces, a sprite on the
    /// ground at its footprint.
    ///
    /// @param cell_x origin cell column
    /// @param cell_z origin cell row
    /// @param feature_index index into the feature table
    void place_catalog_feature_draw(int32_t cell_x, int32_t cell_z, uint16_t feature_index);

    /// Places the draw of a unit's wreck.
    ///
    /// @param wreck wreck the match left
    void place_match_wreck(const oa::sim::match_runtime::Match::Wreck& wreck);

    /// Places the draws of the wrecks the match left since the last call.
    void sync_match_wrecks();

    /// Rebuilds every feature draw from the canonical plots, as a loaded savegame leaves them.
    ///
    /// Each origin plot holding a FeatureDef gets its draw through
    /// place_catalog_feature_draw, and the wrecks the match left so far count
    /// as drawn.
    void rebuild_feature_draws();
    // The pointer event word, the right button, radar scrolls, mouse look and
    // the queued-order cancel of pointer orders (runtime_pointer_press.cpp).

    /// Records the pointer event words in Game.pointer_state.
    ///
    /// The pointer in screen (source) pixels and the key word of each pointer
    /// event: the buttons held and the shift and control bits. The buttons
    /// follow the events; the modifiers are read with each.
    ///
    /// @param event mouse event
    void record_pointer_event(const SDL_Event& event);

    /// Records whether the pointer is over the radar or the battlefield in the Game block.
    void refresh_pointer_area();

    /// Tests whether a canvas point is on the battlefield.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return true inside the battlefield rectangle
    [[nodiscard]] bool battlefield_contains(float x, float y) const;

    /// Handles the right button pressed on the game screen, off the panel's gadgets.
    ///
    /// The frame's pointer pass runs first; the press then cancels the armed
    /// command, starts mouse look, drops the selection, starts a radar scroll or
    /// gives the default order to the unit (a radar blip over the radar) and
    /// ground under the pointer.
    ///
    /// @param x canvas column
    /// @param y canvas row
    void handle_match_right_press(float x, float y);

    /// Sends the pointer events to a running radar scroll or mouse look alone.
    ///
    /// The scroll ends on its button's release and otherwise centres the view on
    /// the radar point; mouse look moves the view.
    ///
    /// @param event pointer event
    /// @return true when a mode took the event
    bool follow_pointer_modes(const SDL_Event& event);

    /// Runs mouse look through the platform cursor.
    ///
    /// The pointer is read in screen pixels, warped back to the anchor, and the
    /// view follows the Game camera.
    ///
    /// @param begin true when the look starts
    void drive_mouse_look(bool begin);

    /// Centres the view on the map point under a radar position and stops following a unit.
    ///
    /// The camera goes half the visible battlefield (visible_map_width() and
    /// visible_map_height(), at the current zoom) up and left of the point; the
    /// pointer may lie off the radar, and the render clamps the camera to the map.
    ///
    /// @param x canvas column
    /// @param y canvas row
    void center_camera_on_radar_point(float x, float y);

    /// Handles a left click over open ground with no command armed, dispatched through the cursor
    /// as over a unit.
    ///
    /// An order cursor gives each selected unit its default order at the ground
    /// under the pointer, a highlight cursor (the right-click interface) drops the
    /// selection, any other does nothing.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param queue true to queue the orders
    void issue_pointer_ground_orders(float x, float y, bool queue);

    /// Gives the selection the orders a command resolves to.
    ///
    /// The group order resolves them against the unit under the pointer
    /// (Game.cursor_unit_id, put back to the pointer's own pick afterwards) and
    /// the ground under it (Game.cursor_position), each given through the
    /// match's issuers; an aircraft sent onto an allied air pad lands on it
    /// (VTOL_Landing). A queued one that matches an order already queued
    /// removes that order instead.
    ///
    /// @param command pointer command
    /// @param target unit under the pointer, or 0
    /// @param ground 16.16 ground point under the pointer, if any
    /// @param queue true to queue the orders
    /// @return the name of what was given; empty for nothing
    std::string_view issue_selection_orders(
        oa::sim::gameplay_input::OrderCommand command,
        uint16_t target,
        const std::optional<oa::sim::ground_orders::Point>& ground,
        bool queue
    );

    /// Cancels a queued pointer order instead of queueing it again.
    ///
    /// While shift is held, the queued order of the kind `order` names for
    /// `source` whose target is `target` (any when 0) and whose point lies within
    /// 16 pixels of the ground under the pointer is removed instead of a new one
    /// being queued. The engine's unit-target orders keep the target's position
    /// as their point, which the test then measures.
    ///
    /// @param source ordered unit
    /// @param order unit order the pointer gives
    /// @param target order's target unit, 0 for any
    /// @param ground 16.16 ground point under the pointer, if any
    /// @param queue true while shift is held; false cancels nothing
    /// @return true when a queued order was removed
    bool cancels_queued_order(
        uint16_t source,
        oa::sim::gameplay_input::UnitOrder order,
        uint16_t target,
        const std::optional<oa::sim::ground_orders::Point>& ground,
        bool queue
    );

    /// Cancels a queued order for an armed command: the order it resolves to for `source` over the
    /// target and the ground.
    ///
    /// @param source ordered unit
    /// @param command armed command
    /// @param target unit under the pointer, 0 for none
    /// @param ground 16.16 ground point under the pointer, if any
    /// @param queue true while shift is held; false cancels nothing
    /// @return true when a queued order was removed
    bool cancels_queued_command(
        uint16_t source,
        oa::sim::gameplay_input::OrderCommand command,
        uint16_t target,
        const std::optional<oa::sim::ground_orders::Point>& ground,
        bool queue
    );

    /// Checks both interface types (Game.interface_type) through SDL input on a skirmish.
    ///
    /// Left-click interface: a left click on open ground moves the selection, a
    /// shift click queues and a shift click at a queued point takes it back; a
    /// right press deselects, cancels an armed command, scrolls with the radar
    /// until its release, and with control looks round with the mouse; a shift
    /// click on a queued build site takes the MobileBuild back. Right-click
    /// interface: a left click on open ground deselects, the right press gives
    /// the default order (move, guard on an own unit) with the same shift cancel,
    /// the left button scrolls with the radar. A right click on a factory build
    /// button takes that unit type off the queue even when another type was
    /// queued after it. The on-screen list and the pick follow
    /// (check_pointer_picks). Throws std::runtime_error on a failure.
    void check_pointer_interfaces();

    /// Checks the on-screen unit list, the pointer's pick and what they drive, over the skirmish
    /// check_pointer_interfaces() leaves.
    ///
    /// The list holds the units whose box overlaps the view, never an enemy
    /// out of sight or cloaked; the pick turns the root box by the heading,
    /// prefers the smaller unit whoever owns it and follows a unit that moves
    /// under a still pointer; an enemy out of sight takes no click. A unit on
    /// screen says no under-attack notice and one off screen says it once. The
    /// unit panel shows the cursor unit's status and target, a build button's
    /// cost line and a radar blip's unidentified line; F1 opens the unit info
    /// panel with the unit's picture; 'n', Ctrl+S, clicks and Escape follow
    /// the list and the armed command; LOAD, UNLOAD and a pad go through the
    /// order table. Throws std::runtime_error on a failure.
    void check_pointer_picks();

    /// Handles a left click on the game screen.
    ///
    /// The click first picks the unit under the pointer (the cursor unit), and
    /// an open unit info panel takes it. Over the radar it gives the
    /// selection's orders there or pans the camera; off the battlefield it
    /// does nothing; otherwise the armed command decides: build places the
    /// pending building, the D-gun fires at a unit or the ground, ATTACK,
    /// RECLAIM, CAPTURE, LOAD and UNLOAD give the orders the order table
    /// resolves for each selected unit over the cursor unit and the ground
    /// (a click that gives none leaves the command armed), and the others
    /// select, order or box-select through the pointer's cursor.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param clicks click count (2 for a double click)
    void handle_match_left_click(float x, float y, int32_t clicks = 1);

    /// Handles a left click over a unit, dispatched through the cursor as the game does.
    ///
    /// The select cursor picks the unit, a highlight cursor is left to the
    /// caller, and an order cursor issues each selected unit's resolved order
    /// with the pointer unit as its target.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param clicks click count (2 for a double click)
    /// @param queue true to queue the orders
    /// @return true when the click was consumed
    bool issue_pointer_unit_orders(float x, float y, int32_t clicks, bool queue);

    /// Handles a left click over open ground with BLAST armed.
    ///
    /// The selected units the order table gives the D-gun order (commanders) take
    /// AttackSpecial at the map position under the pointer. The click leaves the
    /// command armed when the cursor issues nothing, as over a unit.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param queue true to queue the orders
    void issue_pointer_ground_blast(float x, float y, bool queue);

    /// Checks the builder pointer orders headless.
    ///
    /// The repair cursor over a unit under construction, the assist order a click
    /// issues, a shift click taking it back and queueing it again, then the select
    /// cursor over a damaged finished unit and the repair order once REPAIR is
    /// armed. Snapshots go to local/reports; throws std::runtime_error on a
    /// failure.
    void check_builder_orders();

    /// Checks the D-gun order in a new skirmish from the skirmish menu.
    ///
    /// While the local commander's laser fires at a near enemy, a click selects
    /// the commander, the BLAST quickkey 'd' arms the D-gun and a click on an
    /// enemy beyond the D-gun's reach issues AttackSpecial; the commander closes
    /// in and fires its commandfire weapon, paying its energy. After the reload
    /// the BLAST button arms it again for the near enemy. Each ball flies on
    /// through what it strikes until its range runs out, bursting on every
    /// tick it spends below the ground, and the near shot leaves such a trail.
    /// With --snapshot, frames of each ball's flight go beside the snapshot as
    /// <stem>-dgun-key-<tick>.ppm and <stem>-dgun-button-<tick>.ppm. Returns to
    /// the skirmish menu; throws std::runtime_error on a failure.
    void check_dgun_order();

    /// Checks that the ghost and placement click refuse and accept ARMMEX sites as the match does.
    ///
    /// Throws std::runtime_error on a failure.
    void check_build_placement();

    /// Checks the build ghost and the placing click over one site of each kind the game refuses and
    /// one it accepts.
    ///
    /// With the pointer on the site as a player puts it there, the ghost must be
    /// outlined in the refused colour and the click must leave build mode armed
    /// and the builder without an order; over the clear site the outline must be
    /// the clear colour and the click must give the builder MobileBuild there.
    /// Throws std::runtime_error on a failure.
    ///
    /// @param builder local mobile builder
    /// @param type building type to place
    void check_build_site_pointer(uint16_t builder, uint16_t type);

    /// Returns the world point under a canvas point: the radar's map point, else the terrain the
    /// battlefield shows there.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return 16.16 world point, or nullopt off the radar and battlefield
    std::optional<oa::sim::ground_orders::Point> match_world_point(float x, float y);

    /// Force-attacks what is under a canvas point with the selection: a unit other than the
    /// selected one, else the ground.
    ///
    /// A failure is shown on the status line.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return false without a selection or anything to attack there
    bool issue_force_attack(float x, float y);

    /// Calls a function with each selected unit of the local player.
    ///
    /// @param fn called with each unit id
    template <typename Fn>
    void for_each_selected(Fn&& fn) {
        if (!match_)
            return;
        for (auto& slot : match_->world().slots) {
            if (slot.unit_index == 0 || slot.unit == nullptr ||
                slot.owner_index != match_local_player_ ||
                (slot.unit->flags & OA_UNIT_FLAG_SELECTED) == 0)
                continue;
            fn(slot.unit_index);
        }
    }

    /// Tests whether the local player has a unit selected.
    ///
    /// @return true when one is
    bool has_local_selection() const;

    /// Tests whether a match unit exists.
    ///
    /// @param id unit id
    /// @return true when its slot holds a unit of a type
    [[nodiscard]] bool match_unit_present(uint16_t id) const;

    /// Stops following a unit with the camera.
    void stop_match_tracking();

    /// Lists the local player's selected units.
    ///
    /// @return unit ids in slot order
    std::vector<uint16_t> selected_local_ids() const;

    /// Follows a unit with the camera, centred on it at once, and names it on the status line.
    ///
    /// @param id unit id
    void begin_match_tracking(uint16_t id);

    /// Follows Game.follow_unit with the camera, which the match's observer pulse and the observer
    /// camera's console toggle move too.
    void follow_match_camera_unit();

    /// Follows the selected unit after the one followed (T; shift+T backwards); with nothing
    /// selected the follow ends.
    ///
    /// @param reverse true to go backwards
    void cycle_match_tracking(bool reverse);

    /// Drops every unit's selection and select-next marks; callers rebuild the order panel.
    void clear_local_selection();

    /// Adds a unit to the local selection and makes it the primary unit when there is none.
    ///
    /// @param id unit id; 0 does nothing
    void adopt_selection(uint16_t id);

    /// Makes the selection squad `squad` (Ctrl+digit) through the match's squad member lists, then
    /// plays CreateSquad.
    ///
    /// @param squad squad number, 1 through 9
    void assign_squad(int squad);

    /// Selects a squad (digit; shift joins it to the selection), then plays SelectSquad.
    ///
    /// Its CTRL_F members are left out while it also holds an armed unit.
    ///
    /// @param squad squad number, 1 through 9
    /// @param add true to add to the selection
    void select_squad(int squad, bool add);

    /// Selects the local units a predicate accepts, replacing the selection.
    ///
    /// @param pred tests each local unit slot
    void select_units_matching(const std::function<bool(const oa::sim::unit_spawn::Slot&)>& pred);

    /// Returns the squad a digit key names.
    ///
    /// @param key keyboard event
    /// @return 1 through 9, or 0 for another key
    int squad_from_key(const SDL_KeyboardEvent& key) const;

    /// Handles a key in a match: the function keys, speed, squads, quick keys, pages and the other
    /// match shortcuts.
    ///
    /// Pause toggles the pause of a match that is not finished and opens no
    /// menu; F2 opens and closes the in-game options menu. Escape takes back
    /// an armed command, else drops the selection, and never opens the menu;
    /// with the menu open it is left to the event handler, which closes it.
    /// F1 opens the unit info panel, whose Enter and Escape press its DONE;
    /// Shift+F1 pins the cursor unit. Ctrl+S selects the local units on
    /// screen and 'n' centres on the next unvisited local unit. Outside a
    /// multiplayer game 'h' does nothing.
    ///
    /// @param key keyboard event; repeats are ignored
    /// @return true when the key was taken
    bool handle_match_hotkey(const SDL_KeyboardEvent& key);

    /// Selects the unit under the cursor at a canvas point (selection::select_cursor_unit).
    ///
    /// A selectable local cursor unit replaces the selection, or is toggled
    /// with shift, and the local units on screen are visited for 'n'; any
    /// other cursor unit, or none, changes nothing. A double click adds every
    /// other selectable local unit of the same type.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param clicks click count (2 for a double click)
    void select_match_unit(float x, float y, int32_t clicks = 1);

    /// Takes the selection the selection module left in the unit flags: each selected local unit
    /// refreshes its order panel, the primary unit stays when it is still selected (else the first
    /// selected unit takes its place), and the order panel shows the selection.
    void adopt_selected_units();

    /// Selects the local units whose position lies in a drag box: the canvas corners go back to
    /// Game.drag_start and drag_end, and selection::select_units_in_box selects or, with shift,
    /// toggles the units there and visits the local units on screen for 'n'.
    ///
    /// @param x0 one corner's column
    /// @param y0 one corner's row
    /// @param x1 the other corner's column
    /// @param y1 the other corner's row
    /// @param add true (shift) to toggle the units in the box instead of replacing the selection
    void box_select_units(int x0, int y0, int x1, int y1, bool add);

    /// Gives the selection an order on each unit whose projected position lies in a canvas box.
    ///
    /// @param x0 one corner's column
    /// @param y0 one corner's row
    /// @param x1 the other corner's column
    /// @param y1 the other corner's row
    /// @param kind "attack" (enemy units), "reclaim" (any unit) or "repair" (local units)
    void area_order_units(int x0, int y0, int x1, int y1, std::string_view kind);

    /// Steps the requested game speed (1..20, 10 normal) with '+' and '-'; the change is posted to
    /// the message log, the next frame steps at the new rate and a key that set the speed reports
    /// it through Extension::speed_changed.
    ///
    /// @param delta positive to raise, negative to lower
    void adjust_game_speed(int delta);

    /// Saves a screenshot, as Ctrl+F9 does in the idle tick.
    ///
    /// The output directory and its screenshots folder are made, the frame goes
    /// to the next SHOTnnnn.pcx there and the frame clock restarts so the save's
    /// time is not played as ticks.
    void capture_screenshot();
    // Numbered PCX captures (runtime_poster.cpp).

    /// Saves the frame on screen as the next numbered PCX of a folder.
    ///
    /// The frame is the active surface of a capture display: the match through
    /// the palette the poster writes with, a frontend screen through its
    /// background's palette.
    ///
    /// @param directory folder under the save root
    /// @param prefix file name prefix (SHOT, FRAM)
    /// @return false when there is no frame or it cannot be saved
    bool save_numbered_frame(const char* directory, const char* prefix);

    /// Runs the film step at the end of each game frame.
    ///
    /// While a capture runs, a frame whose tick has come goes to the next
    /// FRAMnnnn.pcx of the capture folder and the next is due FilmSpeed frames a
    /// second later.
    void capture_film_frame();

    /// Saves Ctrl+F10's first frame: the HUD is drawn and the frame saved before the capture tick
    /// is set.
    ///
    /// @param path capture folder
    void begin_film_capture(const char* path);

    /// Opens the unit info panel (UNITINFOx.GUI) for F1, as open_unit_info_panel() fills it.
    ///
    /// Its subject is the unit type of the build button under the pointer,
    /// else that of the cursor unit (Game.cursor_unit_id) when the viewpoint
    /// player sees it. Nothing opens while the panel is open
    /// (kFrameUnitInfoOpen) or without a subject.
    ///
    /// @return true when the panel opened
    bool open_unit_info();

    /// Closes the unit info panel through its click handler with no control, releasing its
    /// picture; nothing when it is not open.
    void close_unit_info();

    /// Presses the unit info panel's DONE, which the panel's Enter and Escape defaults also press:
    /// the button sound, then the panel closes.
    void press_unit_info_done();

    /// Handles a left click while the unit info panel is open: DONE closes it.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return true when the click was on the panel
    bool click_unit_info(float x, float y);

    /// Draws the unit info panel over the battlefield: its controls, the statistic labels and the
    /// unit's picture at HOTR at the picture's own size.
    void draw_unit_info_panel();

    /// Draws the bottom bar's unit panel: the unit under the cursor, the build button under the
    /// pointer or the feature under the cursor (ui::hud::unit_panel_snapshot).
    void draw_unit_panel();

    /// Returns the name of the match panel's gadget under the pointer: over a build button, the
    /// unit it builds.
    ///
    /// @return the gadget's name, or null when the pointer is over no gadget
    [[nodiscard]] const char* hovered_gadget_name() const;

    /// Draws the game clock and the message log over the battlefield.
    void draw_chat_overlay();

    /// Has the extension draw its readouts over the battlefield (Extension::draw_match_overlay).
    ///
    /// Nothing without the hook or a match. The battlefield layer is the paint
    /// target: the hook's painter draws in its pixels, text at hud_text_scale().
    void draw_extension_overlay();

    /// Returns the font an extension's overlay text is drawn in.
    ///
    /// @param font which font
    /// @return the match label font for the side panel's, the message log's
    ///         font for the log's; null when the side panel's is not loaded
    const oa::formats::fnt::Font* overlay_font(OverlayFont font);

    /// Loads TALK.GUI and talk.gaf for the chat line once; a failure is reported on stderr.
    void ensure_talk_panel();

    /// Draws the chat line while one is typed.
    ///
    /// The chat panel opens TALK.GUI while a line is typed. Its root hangs above
    /// the bottom edge (a negative root y counts from the screen height), so the
    /// CONSOLE picture and the TALK field cover the bottom bar.
    void draw_chat_entry();

    /// Checks the overlays the match draws over the battlefield and in the bars.
    ///
    /// Each must change the frame `frame_of` produces where the game puts it: the
    /// radar picture in the side column, a posted message, the game clock, the
    /// paused title, the speed line '+' posts in the message log, the build
    /// outline under the pointer in build mode, and the chat line Enter opens in
    /// the bottom bar. An extension's overlay, drawn through
    /// Extension::draw_match_overlay once per frame with the loaded fonts'
    /// heights, must show over the battlefield: a probe's 65 by 9 bar, scaled,
    /// holds its colour in every pixel, and its lines of text in both fonts
    /// show. The message log starts empty and gets its lines back afterwards.
    /// Throws std::runtime_error on a failure.
    ///
    /// @param frame_of composes the frame to test
    /// @param snapshot file the chat line's frame is written to, with the speed
    ///     line in the log and the outline on the battlefield; the probe's frame
    ///     is written beside it, with "-overlay" added to its name
    void check_match_overlays(
        const std::function<void(renderer::Surface&)>& frame_of, const fs::path& snapshot
    );

    /// Checks the CPU composition a headless match makes, laid out for a 2000x1109 window so that
    /// the chrome scales and canvas coordinates leave the 640x480 HUD layer.
    void check_composed_frame();

    /// Checks the drag box and the selection boxes.
    ///
    /// A drag over the battlefield, sent as SDL mouse input, shows the game's drag
    /// box from the press to the pointer on the frame `frame_of` produces, over
    /// the fog: UI colour 15 with colour 0 a pixel inside. The release selects the
    /// local unit and two kbots beside it, and each then shows its selection box,
    /// the root object's bounds turned with the unit, in UI colour 10; with
    /// SelBoxes off none does. The selection before the check returns. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param frame_of composes the frame to test
    void check_selection_visuals(const std::function<void(renderer::Surface&)>& frame_of);

    /// Checks the loading screen through the SDL display sink.
    ///
    /// Each pixel of the 8-bit frame, sampled at the centre of its block in the
    /// letterboxed area, shows its palette colour. Pixels near the software cursor
    /// are not compared. Throws std::runtime_error on a failure.
    void check_loading_sink();

    /// Checks the match presented through SDL layers: every presented frame must equal
    /// compose_match_frame outside the software cursor.
    ///
    /// The loading sink check runs first, and the won match's end
    /// (check_presented_match_end()) last. Throws std::runtime_error on a
    /// failure.
    void check_match_layers();

    /// Checks the standing order buttons against the selection and the units.
    ///
    /// ARMPW and ARMSOLAR beside the local commander and, in the peewee's range,
    /// an enemy CORRAD: a ShootMe type with no weapon, so nothing shoots back.
    /// Each button must draw the frame of its art the game draws for the
    /// selection (the last, blank one where no selected unit takes it), each click
    /// must step the selection's order as the order toggle does and give it to the
    /// selected units that take it, and the units must carry it out: under hold
    /// and return fire the peewee leaves the radar alone, under fire at will it
    /// shoots it. Throws std::runtime_error on a failure.
    void check_match_orders();

    /// Checks the command buttons through ARMGEN.GUI clicks.
    ///
    /// A lit command button arms its order, a click on it again turns it off, a
    /// button of the group puts the others out, STOP puts them all out; REPAIR,
    /// RECLAIM, CAPTURE and UNLOAD play specialorders, the rest immediateorders.
    /// Throws std::runtime_error on a failure.
    ///
    /// @param peewee local ARMPW
    /// @param commander local commander
    void check_command_buttons(uint16_t peewee, uint16_t commander);

    /// Checks the unit readout's damage bar.
    ///
    /// It fills health / max_damage of SIDEDATA's DAMAGEBAR in UI colour 10
    /// (PALETTE.PAL 233 from guipal's light green) and the rest in UI colour 4
    /// (213, from its dark red); another player's commander, whose type hides
    /// damage, shows none. Throws std::runtime_error on a failure.
    ///
    /// @param peewee local unit whose bar is read
    void check_unit_damage_bar(uint16_t peewee);

    /// Checks factory MOVE and PATROL orders and what the built unit does with them.
    ///
    /// Places a finished Kbot Lab, selects it with a left click and sends it to a
    /// point twice: with its MOVE button and with its PATROL button, each followed
    /// by a left click on the ground. Each time the lab must hold one QMove or
    /// QPatrol at the clicked point, and the Peewee its build button then queues
    /// must leave the pad with a Move_Ground (its only order) or Patrol there, as
    /// its GetBuilt order gives it, and walk towards it. (A lab has no movement
    /// object, so the default order a click or a right press gives it is none.)
    /// Throws std::runtime_error on a failure.
    void check_factory_orders();

    /// Places a finished structure of the local player's on the first free site in the rings of
    /// cells around a unit.
    ///
    /// @param type building type
    /// @param near unit id the rings centre on
    /// @return the structure's slot, or null when no site is free
    oa::sim::unit_spawn::Slot* place_finished_structure(uint16_t type, uint16_t near);

    /// Gives the units orders for a save/load run.
    ///
    /// The local commander starts a solar plant beside itself, a finished Kbot
    /// Lab near it queues three Peewees, and three new Peewees patrol, guard the
    /// commander and walk to the far side of the map. An Atlas is ordered to
    /// pick up a Peewee and then to set it down on the far side, and a
    /// transport ship in the water nearest the commander starts with three
    /// Peewees in its hold. Throws std::runtime_error when a type or site is
    /// missing.
    void give_saveload_orders();

    /// Gives the local player's units orders towards the mission's victory,
    /// for a headless campaign run with --give-orders.
    ///
    /// The armed mobile units a MoveUnitToRadius names (every one for any
    /// type) move to its point. The other armed mobile units that are not
    /// builders attack the nearest enemy unit, a type KillUnitType,
    /// KillAllOfType or CaptureUnitType names first: all at once when eight
    /// have gathered or some already chase a unit of that type, or at once
    /// when the player has no builder or factory.
    /// An idle mobile builder puts up the first structure the player lacks of
    /// an energy maker, a metal extractor and a factory, then up to three
    /// energy makers and metal extractors, of the types its build pages offer;
    /// an extractor goes where its footprint holds the most metal. An idle
    /// factory queues two of each armed unit its build pages offer. Prints one
    /// "mission orders:" line.
    void give_mission_orders();

    /// Returns where the running mission's first MoveUnitToRadius victory
    /// condition sends units, its point placed on the terrain as the game
    /// places it.
    ///
    /// @return the goal; nothing outside a mission or when no such condition
    ///     was registered
    [[nodiscard]] std::optional<MissionMoveGoal> mission_move_goal();

    /// Returns the unit types a builder's build pages offer the local player:
    /// the unit buttons of each page that are not greyed out, in page order.
    ///
    /// Opens each page as the build menu does, then restores the selection's
    /// panel.
    ///
    /// @param builder unit slot of the builder
    /// @return the type indices, each once
    std::vector<uint16_t> offered_build_types(uint16_t builder);

    /// Orders a builder to put up a structure at a site the local player may
    /// build on in the rings of cells around it: the nearest, or for a metal
    /// extractor the one whose footprint holds the most metal.
    ///
    /// @param builder unit slot of the builder
    /// @param type structure type index
    /// @return true when a site was found and the order given
    bool order_structure_near(uint16_t builder, uint16_t type);

    /// Prints "saveload: orders N" and the count of each mission among the saved orders of the live
    /// units, in mission order.
    void print_saved_orders() const;

    /// Starts the feature changes a save/load run saves while they play.
    ///
    /// In row order, the first flammable sprite with a burn sequence catches
    /// fire, the next sprites with a die and a reclamate sequence start them,
    /// and the next destructible feature is cleared away. Throws
    /// std::runtime_error when the map has no feature for one of them.
    void give_saveload_feature_events();

    /// Prints the Features section the match would save now.
    ///
    /// The line "saveload: features" gives the normal, 3D and animating record
    /// counts, the animating records playing a burn, die and reclamate
    /// sequence, and a digest of the section, which a load of that save
    /// reproduces. The digest names each feature type by its name rather than
    /// by its place in the type table, whose order a load changes: a fresh
    /// game loads the mission schema's feature types before its units'
    /// remnants, and a resumed game after them, from the save.
    void print_saved_features();

    /// Checks download build pages and a missile silo's build page.
    ///
    /// ARMLAB has ARMLAB1.GUI only, and download/armwar.tdf and armflea.tdf give
    /// it MENU=3 BUTTON=0 and 1, so its second page is ARMDL.GUI with the Warrior
    /// and the Flea linked into its first two patches. Clicked through SDL, NEXT
    /// must open that page with both buttons live and drawn from their _gadget
    /// art; Flea, Warrior, Flea queue three orders, a right click on the Warrior
    /// takes the middle order out and one on the Flea the last, and the Flea left
    /// must come out of the lab. A Nuclear Silo's BUILD page then queues a
    /// missile with MAKENUKE and a right click takes it off again. Throws
    /// std::runtime_error on a failure.
    void check_download_builds();

    /// Checks that a patrolling construction kbot reclaims a feature on its way.
    ///
    /// Places an ARMCK a few cells from the auto-reclaimable map feature nearest
    /// the commander, empties the player's stores, selects the kbot with a click
    /// and gives it PATROL to the far side of the feature. The order lookup gives
    /// a type that can repair RepairPatrol (kind 34), not Patrol; on its way the
    /// patrol pushes a Reclaim (kind 32) of a feature sampled at random around it,
    /// the feature leaves its plots, the player is credited with its metal and
    /// energy, and the RepairPatrol orders are left to go on with. Throws
    /// std::runtime_error on a failure.
    void check_patrol_reclaim();

    /// Checks the Reclaim and Move cursors over features and a wreck.
    ///
    /// Selects the local commander with a click and moves the pointer in small
    /// steps over several kinds of vegetation, a reclaimable feature that does not
    /// burn (a rock or a burnt tree) and a placed wreck. Each pointer whose ground
    /// point lies on a cell of the feature's footprint must show Reclaim, each on
    /// an empty cell Move, as the order cursor resolves them through the plot
    /// under Game.cursor_position; every footprint cell must be reached. A click with
    /// RECLAIM armed on the first tree must then give the commander Reclaim
    /// there. Throws std::runtime_error on a failure.
    void check_reclaim_cursor();
    // Chat line and "+command" console (runtime_console.cpp).

    /// Returns the match's console, binding it to the running match's world on first use.
    ///
    /// Its host reaches the runtime's options, message log, units, features,
    /// camera, files and sound. Each match start binds it at once, so the host
    /// is filled, the extension's part too, before the match's first tick. The
    /// host keeps its address for as long as the runtime lives.
    ///
    /// @return the console, or null without a match
    oa::ui::console::Console* match_console();

    /// Asks the extension, as each match starts, for the label the match's
    /// return names (Extension::return_label).
    ///
    /// Keeps up to kReturnLabelBytes - 1 characters of it in return_label_
    /// for the match's in-game menus and end-of-game screen, empty for none.
    void take_return_label();

    /// Returns what kind of session the running match is (the map context's object state); a replay
    /// plays back the multiplayer game it recorded.
    ///
    /// @return campaign, multiplayer (shared or replayed) or skirmish
    [[nodiscard]] oa::data::campaign::SessionKind match_session_kind() const;

    /// Tests whether the local player's player-info record marks it as a watcher.
    ///
    /// @return true for a watcher
    [[nodiscard]] bool local_player_watches() const;

    /// Opens the chat line and starts text input; no chat line opens for a watcher, a replay's
    /// viewer included.
    void open_chat_line();

    /// Closes the chat line, drops the typed text and stops text input.
    void close_chat_line();

    /// Submits the chat line, as TALK's Enter does.
    ///
    /// A '+' line runs as a console command first, whose echo reaches everyone
    /// after a cheat and this player alone after an option command; the line then
    /// goes out through the chat formatter as "<name> text".
    void submit_chat_line();

    /// Posts console output, chat or a notice to the match message log and the status line.
    ///
    /// @param text line to post
    /// @param kind message kind
    /// @param sender Player.index of the player who sent the line, whose logo
    ///        starts it; oa::sim::messages::sender_none for none
    void console_post_message(
        std::string_view text,
        uint8_t kind = oa::sim::messages::kind_status,
        uint8_t sender = oa::sim::messages::sender_none
    );

    /// Moves pending Film and FilmSpeed changes from the Game block into the preferences.
    ///
    /// Film and FilmSpeed write the Game block and flag the change; the flags move
    /// with the values into the preferences, whose save writes and clears them.
    ///
    /// @param[in,out] game match Game block; its change flags are cleared
    void take_console_capture_options(oa::Game& game);
    // The option fields kept only in the Game block
    // (runtime_console_options.cpp).

    /// Seeds a match's Game block with the option fields from the preferences.
    ///
    /// The console option word (Game.console_flags) is set at match start with the bits
    /// the loader clears taken out, so it is only handed back by
    /// take_match_options(). The capture directory and rate go in with their
    /// change flags clear.
    ///
    /// @param[out] game match Game block
    void seed_match_options(oa::Game& game) const;

    /// Takes the option fields a match changed back into the preferences, pending capture changes
    /// first.
    ///
    /// @param game match Game block; its capture change flags are cleared
    void take_match_options(oa::Game& game);

    /// Checks the console's option commands.
    ///
    /// ScrollSpeed keeps the argument's low byte, IFace the whole value, and both
    /// save the options at once. SwitchAlt flips the switch_alt bit of
    /// Game.graphics_flags and saves; with an argument it takes the argument's
    /// low bit and saves nothing.
    /// The digit keys pick a squad when Alt differs from that bit and a build page
    /// otherwise. Throws std::runtime_error on a failure.
    ///
    /// @param enter_line runs one console line
    void check_console_option_commands(const std::function<void(const char*)>& enter_line);

    /// Reads the value the preferences file holds for a General key.
    ///
    /// @param key key name
    /// @return the value, or -1 without the key
    [[nodiscard]] int64_t saved_general_number(const char* key) const;
    // The display options the console sets (runtime_console_display.cpp and
    // runtime_match_render.cpp).

    /// Drops every cached model image and building silhouette, which are built again from their
    /// pieces at the next draw.
    ///
    /// The console's arena release (and the option toggles, through their claim
    /// on the render arena the runtime keeps in place of Game.render_arena_block)
    /// claims the whole render arena, which drops every block it lent.
    void release_model_images();

    /// Counts the cached model images of the match's units and features.
    ///
    /// @return the count
    [[nodiscard]] std::size_t cached_model_images() const;

    /// Sets the model light, as "Light" does; the light outlives the match.
    ///
    /// @param x light x
    /// @param y light y
    /// @param z light z
    void set_match_model_light(int32_t x, int32_t y, int32_t z);

    /// Resets the palette of the session display at a gamma.
    ///
    /// The device palette the indexed frames go out through is rebuilt at the new
    /// gamma, and the RGB layers the match and the frontend screens present take
    /// the same mapping of each palette channel (the step before the colours
    /// reach the hardware palette).
    ///
    /// @param gamma display gamma, 1.0 unchanged
    void set_display_gamma(float gamma);

    /// Applies the saved Gamma to the display: the saved 12 is 1.0 here; the console's "Gamma"
    /// takes tenths instead.
    void apply_saved_gamma();

    /// Reapplies the saved settings, as the options screens do.
    ///
    /// The display gamma (value / 24 + 0.5), the effects level (fxvol << 10) and
    /// the music line (musicvol << 10).
    void apply_saved_volumes();

    /// Maps RGB pixels through the display gamma in place; nothing at gamma 1.
    ///
    /// @param[in,out] rgb first pixel's red byte
    /// @param pixels pixel count
    /// @param stride bytes from one pixel to the next
    void apply_gamma_rgb(uint8_t* rgb, std::size_t pixels, std::size_t stride) const;

    /// Checks the effect of each display console command.
    ///
    /// In a skirmish with a solar collector of the local player beside its
    /// commander and a feature with a shadow sequence above it: Shading,
    /// AntiAlias and Shadow flip their bit of the graphics word
    /// (Game.graphics_flags), release the render arena and save; Dither flips
    /// DitheredFog and saves; TShadow and FShadow flip theirs without saving, and
    /// FShadow changes only pixels the features' shadow frames cover; Light sets
    /// the model light and releases the arena; ScreenChat flips Game.screen_chat
    /// and saves; Gamma n shows the frame at n tenths, keeps n in Game.gamma and
    /// saves; Clock flips OA_CONSOLE_FLAG_CLOCK of Game.console_flags, saves and
    /// draws the game time above the bottom bar; BigBrother moves
    /// the selection and the camera through the viewpoint player's units every 90
    /// ticks and leaves the follow when it stops. Throws std::runtime_error on a
    /// failure.
    ///
    /// @param enter_line runs one console line
    void check_console_display_commands(const std::function<void(const char*)>& enter_line);

    /// Checks the Options screen's GAMMA slider from SINGLE.GUI, its CANCEL and its Previous Menu.
    ///
    /// OPTIONS, then VISUALS, then a press at the top of the GAMMA track: the
    /// slider stores 20 and the options reapply shows every presented pixel
    /// through the gamma 20 / 24 + 0.5. CANCEL restores the entry gamma and shows
    /// it again (the UNDOs the options apply runs end in the reapply), keeping 20
    /// out of the saved preferences; the top step and Previous Menu keep it and
    /// save it under Gamma. The entry gamma is put back and saved at the end, back
    /// on SINGLE.GUI. Throws std::runtime_error on a failure.
    void check_options_gamma();
    // The in-game message log, Game.chat_lines (runtime_messages.cpp).

    /// Returns the message log's hooks: interface sounds, camera centring and the match's rand()
    /// random numbers, then the extension's.
    ///
    /// Camera centring moves the camera at once; 3.1c glides it when asked.
    ///
    /// @return the hooks, bound to this runtime
    [[nodiscard]] oa::sim::messages::Hooks message_hooks();

    /// Binds the message log, the kills board flashes and the last-unit notices to the match.
    ///
    /// The log settings kept in Game: TextLines and TextScroll from the
    /// preferences, the line filter session start sets for every session, and
    /// ScreenChat; the UI colours the log draws in. Elimination lights the
    /// killer's kills and the victim's losses on the kills board while F4 holds it
    /// out. Once a player's last unit is gone a multiplayer game announces the
    /// player leaving, a skirmish its forces' end, a campaign nothing.
    void bind_message_log();

    /// Posts a line to the match message log.
    ///
    /// @param text line to post
    /// @param kind message kind
    /// @param value message value (a unit id for unit reports)
    /// @param sender Player.index of the player who sent the line, whose logo
    ///        starts it and who plays the arrival sound;
    ///        oa::sim::messages::sender_none for none
    void post_match_message(
        std::string_view text,
        uint8_t kind,
        uint16_t value = 0,
        uint8_t sender = oa::sim::messages::sender_none
    );

    /// Captions a unit's speech in the message log as chatter does: "<name>: <text>", carrying the
    /// unit id.
    ///
    /// @param unit speaking unit
    /// @param text what it says
    void post_unit_report(uint16_t unit, std::string_view text);

    /// Returns the lines the match message log holds, oldest first.
    ///
    /// @return the lines; empty without a match
    [[nodiscard]] std::vector<std::string> match_message_lines();

    /// Returns the message log's font: the COMIX loaded at start-up, or the small HUD font when
    /// COMIX.FNT is missing.
    ///
    /// @return the font
    const oa::formats::fnt::Font& message_font();

    /// Draws the message log on the battlefield layer.
    ///
    /// The log draws down from screen (0x8a, 0x34), just inside the
    /// battlefield's top-left corner. The corner scales with the chrome and the
    /// lines grow with the HUD text scale. A line with a sender starts with the
    /// logo of the sender's colour: the whole frame of the logo sequence
    /// stretched over the square the log sets aside for it.
    void draw_match_message_log();

    /// Returns the canvas rectangle some log lines cover in the composed frame.
    ///
    /// From the log's corner across to the battlefield's right edge, cut at its
    /// bottom.
    ///
    /// @param lines number of log lines
    /// @return canvas rectangle
    [[nodiscard]] CanvasRect message_log_rect(std::size_t lines);

    /// Checks the game speed keys and the message log.
    ///
    /// '+' and '-' arrive as key events, change how many ticks one second of
    /// frames runs and post the game's speed lines; a commander's under-attack
    /// report reaches the log; the log draws over the battlefield. A chat line
    /// posted through the console's host with a sender is stored as another
    /// player's chat from that sender, starts with the logo of the sender's
    /// colour and has its text past the logo; the same line from no player
    /// starts its text at the log's left edge. Throws std::runtime_error on a
    /// failure.
    void check_game_speed_messages();

    /// Draws "Game Time : hh:mm:ss" while the console's Clock is on.
    ///
    /// In UI colour 15, two pixels right of the side column and two above the
    /// bottom bar (screen x 0x82, y height-0x22-font), scaled with the chrome.
    void draw_console_clock();

    /// Checks the chat line and console commands with synthetic key events.
    ///
    /// Enter, "+clock", Enter must set the clock option and echo the line; "+atm"
    /// must add 1000 metal; "+give" must move metal to the computer player and
    /// "+reload" must remove the units of a type at the next update. The cheat,
    /// option, cursor, debug, sound and display command checks follow, then the
    /// extension's, the unit, poster and range overlay commands and the Game
    /// Settings sheet. Throws std::runtime_error on a failure.
    void check_console_commands();

    /// Checks the developer commands that act at the pointer.
    ///
    /// On the free slot and a clear patch of map beside the local commander: a
    /// unit spawned by its name for the free slot, which stands in as a computer
    /// player so that "+kill" takes the sweep's branch for players this machine
    /// simulates, then "+feature" and "+burnone" at the cursor cell. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param enter_line runs one console line
    void check_console_cursor_commands(const std::function<void(const char*)>& enter_line);
    // Debug grid, "Profile" bars and DebugBreak (runtime_console_debug.cpp).

    /// Draws the debug grid over the terrain and, in the terrain view, the pointer's cross, before
    /// the units.
    ///
    /// The grid is drawn in the 8-bit view of a bridge over the battlefield, as
    /// the models are; nothing is drawn while the debug view is off and no
    /// Contour is set.
    ///
    /// @param[in,out] bridge 8-bit view of the battlefield
    /// @param display display the bridge draws through
    /// @param frame battlefield frame
    /// @param area bridge rectangle
    /// @param scale battlefield zoom
    void draw_match_debug_grid(
        oa::present::model::RgbBridge& bridge,
        oa::present::model::ModelDisplay& display,
        const oa::present::model::RgbFrame& frame,
        const oa::Rect32& area,
        float scale
    );

    /// Starts a profile sample window as each game frame begins.
    void begin_profile_window();

    /// Charges the time since the last mark to a profile category, on the host millisecond clock.
    ///
    /// @param category profile category (OA_PROFILE_*)
    void mark_profile(int32_t category);

    /// Draws the nine "Profile" bars while profiling is on.
    ///
    /// Drawn after the HUD's gadgets, against the 640-wide source screen, in the
    /// label font and text colour the HUD last set.
    void draw_profile_bars();

    /// Runs a DebugBreak crash test.
    ///
    /// 1 and 2 take 32 MB blocks until the heap gives out and the out-of-memory
    /// report ends the game, 3 divides by zero; a break first takes a fullscreen
    /// display back to a window and waits half a second so that the debugger
    /// can show.
    ///
    /// @param test crash test the console asked for
    void run_console_crash_test(oa::ui::console::CrashTest test);

    /// Records Game.profiling after a console line, for the next game.
    void keep_console_carry();

    /// Restores Game.profiling for a new game: "Profile" stays as the last game left it.
    void restore_console_carry();

    /// Checks the debug console commands.
    ///
    /// "Contour 3" lays height lines in the ramp's colours over the battlefield
    /// and "Contour 0" takes them away; "Profile" is refused before the
    /// passphrase, then shows the nine bars of the last window and hides them
    /// again; a game tick charges its clock time to the units category; and
    /// "DebugBreak" reaches the crash hook only with the debug keys on. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param enter_line runs one console line
    void check_console_debug_commands(const std::function<void(const char*)>& enter_line);
    // ShootAll, Assign and Search (runtime_console_units.cpp).

    /// Checks the ShootAll, Assign and Search console commands.
    ///
    /// "ShootAll" flips OA_CONSOLE_FLAG_SHOOT_ALL of Game.console_flags, under
    /// which the automatic target search takes candidates whose type lacks
    /// ShootMe: the local commander at fire at will then picks a computer
    /// player's solar collector beside it, and without the bit picks nothing.
    /// "Assign <mission> <n>" gives the selected local units the mission with its
    /// first parameter the name read as a number (0) and its second n, as the
    /// group's standing orders and the order queue give it: Standing_FireOrder
    /// reaches the commander, whose fire order becomes hold fire when the order
    /// runs, and not a selected solar collector. "Search <nodes> [weight]" sets
    /// the path search's per-tick node credit (tick_credit) and its base
    /// heuristic weight times 65536 (base_heuristic); both developer commands do
    /// nothing without the passphrase. Throws std::runtime_error on a failure.
    ///
    /// @param enter_line runs one console line
    void check_console_unit_commands(const std::function<void(const char*)>& enter_line);
    // Sound3D and Sing (runtime_console_sound.cpp).

    /// Plays a sound the match places at a point.
    ///
    /// The sound object plays what play_sound_at lets through while the effects
    /// volume and sound mode are on and "-s" did not silence it, placed at the
    /// clip's position when 3D sound is on. A failure is reported on stderr.
    ///
    /// @param name sound name
    /// @param sound volume, position and distances of the clip
    void play_point_sound(const char* name, const oa::sim::match_runtime::Match::PointSound& sound);

    /// Plays a wave file by its path (unit chatter, the sound panel's TEST) on the route
    /// audio::game_audio::wave_file_route picks from the sound options.
    ///
    /// A failure is reported on stderr.
    ///
    /// @param path wave file path; backslashes are allowed
    void play_wave_file(std::string_view path);

    /// Flips the novelty voice ("Sing"): unit speech then plays honk and sing.
    void toggle_novelty_voice();

    /// Checks the Sound3D and Sing console commands.
    ///
    /// "Sound3D" flips the 3D switch and saves the options, whose "Sound Mode"
    /// stays the sound screen's mode (the options save writes Game.sound_flags & 7).
    /// With the switch on, a clip at the local commander plays at -585 from the
    /// middle of the view; off, it plays unplaced at -585 on screen and -1585 off
    /// it. "Sing" flips the novelty voice: unit speech then plays honk on one
    /// 30-tick window in eight and sing on the others. Throws std::runtime_error
    /// on a failure.
    ///
    /// @param enter_line runs one console line
    void check_console_sound_commands(const std::function<void(const char*)>& enter_line);

    /// Lists the user directory's files that match a pattern, for movie, screenshot and poster
    /// numbering.
    ///
    /// @param pattern path under the save root whose file name holds one '*';
    ///     matching ignores case
    /// @param visit called with each matching file name
    /// @param user passed to `visit`
    void list_save_files(
        const char* pattern, void (*visit)(void* user, const char* name), void* user
    ) const;
    // MakePoster's capture (runtime_poster.cpp): the battlefield at 1:1 into
    // the next <directory>\<prefix>nnnn.bmp.

    /// Draws a map rectangle into the next <directory>\<prefix>nnnn.bmp, as MakePoster captures it.
    ///
    /// The battlefield is drawn at 1:1 in strips one view high less a row, each
    /// filled one view wide at a time with the camera there.
    ///
    /// @param directory output folder under the save root
    /// @param prefix file name prefix
    /// @param x map column of the rectangle
    /// @param y map row of the rectangle
    /// @param width rectangle width in map pixels
    /// @param height rectangle height in map pixels
    /// @quirk Every strip after the first starts on the previous strip's last row
    ///     and skips it, yet its rows go one image row higher, so the image drops
    ///     one map row per later strip and its bottom row stays unwritten.
    void render_poster(
        const char* directory,
        const char* prefix,
        int32_t x,
        int32_t y,
        int32_t width,
        int32_t height
    );

    // The state the poster writer turns off around its drawing, as it was.
    struct PosterScene {
        int32_t camera_x{};
        int32_t camera_z{};
        uint8_t sight_flags{};
        uint16_t paused_flag{};
        bool paused{};
        uint16_t clock_flag{};
        int32_t message_lines{};
    };

    /// Sets the match up for a poster capture and returns what it changed.
    ///
    /// Mapping and line of sight off with the sight grids rebuilt, the pause
    /// picture, the clock and the message log hidden. The engine draws the pause
    /// picture while match_paused_ is set.
    ///
    /// @return the camera, sight rules, pause, clock and log lines to restore
    [[nodiscard]] PosterScene enter_poster_scene();

    /// Restores what enter_poster_scene() changed.
    ///
    /// @param scene state enter_poster_scene() returned
    void leave_poster_scene(const PosterScene& scene);

    /// Checks the MakePoster console command.
    ///
    /// "MakePoster all" writes BIGSHOT0001.bmp into the output directory's
    /// screenshots folder: the whole map as a bottom-up 8-bit BMP in the match
    /// palette whose first rows are the view at the map's corner, whose rows from
    /// the second strip on sit one row higher than the map's, and whose bottom row
    /// is black; the camera, sight rules, clock and log are as they were.
    /// "MakePoster 10 10" then writes BIGSHOT0002.bmp one view in size. Throws
    /// std::runtime_error on a failure.
    ///
    /// @param enter_line runs one console line
    void check_console_poster_command(const std::function<void(const char*)>& enter_line);
    // The 8-bit view of the match frame the poster writes: the palette index
    // of an RGB pixel and the display whose palette the image carries.

    /// Returns the match palette index of an RGB pixel, as the poster writes it.
    ///
    /// @param r red
    /// @param g green
    /// @param b blue
    /// @return palette index
    [[nodiscard]] uint8_t match_palette_index(uint8_t r, uint8_t g, uint8_t b);

    /// Returns the display whose palette the poster image carries.
    ///
    /// @return the match renderer's display
    [[nodiscard]] const oa::present::DisplayContext& match_display_context();

    /// Enters a console line through the chat line's own input path: Enter, the line, Enter.
    ///
    /// Throws std::runtime_error when Enter does not open or submit the line.
    ///
    /// @param text line to type
    void enter_console_check_line(const char* text);
    // The cheat gate per session kind (runtime_console_cheats.cpp): the
    // skirmish and campaign mission starts.

    /// Checks the cheat gate of a skirmish.
    ///
    /// The mission start opened the cheat class to the chat line, so "+atm" runs
    /// and its echo goes to everyone; a word no list registers goes out as plain
    /// chat, since the spawn fallback needs the developer class. The Game
    /// Settings sheet MISSION opens shows the difficulty and no Cheat Codes row,
    /// and its OK returns to the options panel. Throws std::runtime_error on a
    /// failure.
    ///
    /// @param enter_line runs one console line
    void check_console_skirmish_cheats(const std::function<void(const char*)>& enter_line);

    /// Checks the cheat gate of a campaign.
    ///
    /// The mission start closed the cheat class, so "+atm" changes nothing and
    /// goes out as plain chat while option commands still run. The developer
    /// passphrase opens cheats in a campaign too; "+Now" alone closes them again.
    /// Throws std::runtime_error on a failure.
    void check_console_campaign_cheats();

    /// Sends a match key to the console's hotkeys.
    ///
    /// F4 pins the kills board and '`' turns the damage bars on and off. Pause
    /// flips the pause bit of Game.sim_run_flags and reports it through
    /// Extension::pause_changed. In a multiplayer game Tab opens and closes the
    /// team menu and 'h' opens SHARE.GUI. Developer keys: backslash repeats
    /// the last console line and F11 toggles the debug keys once the passphrase
    /// is accepted; while the debug keys are on, '=', ']', 'i' and 'm' go to the
    /// debug dispatcher instead of their normal use. Ctrl+F10 starts a film
    /// capture.
    ///
    /// @param key keyboard event
    /// @return false when the key is not a console hotkey or there is no match
    bool handle_console_hotkey(const SDL_KeyboardEvent& key);

    /// Gives resources from one player to another, as "Give" does.
    ///
    /// The extension sends a shared match's gift itself (a replay only watches);
    /// otherwise the match's share transfers give it.
    ///
    /// @param from giving player
    /// @param to receiving player
    /// @param amount amount given
    /// @param metal true for metal, false for energy
    void console_give(uint8_t from, uint8_t to, float amount, bool metal);

    /// Kills every unit, as "Kill" with no player does.
    void console_kill_all_units();

    /// Kills every unit of a type, the kill half of "Reload <unit>".
    ///
    /// @param type unit type index
    void console_kill_units_of_type(uint16_t type);

    /// Reloads a unit type, the reload half of "Reload <unit>".
    ///
    /// The type's FBI over its record (through the unit definition update,
    /// against the match's Game.weapon_defs and the corpse features already
    /// loaded) and then its COB, which new units of the type run.
    ///
    /// @param type unit type index
    void console_reload_unit_type(uint16_t type);

    /// Clears every feature on the map, indestructible ones included, as "BurnAll" does.
    void console_burn_all_features();

    /// Clears the feature covering a cell through the feature runtime; an indestructible one stays
    /// unless forced.
    ///
    /// The runtime's draws follow the plot words.
    ///
    /// @param cell_x cell column
    /// @param cell_z cell row
    /// @param force true to clear an indestructible feature too
    /// @return false off the map or when nothing was cleared
    bool console_burn_feature(int32_t cell_x, int32_t cell_z, bool force);

    /// Places a feature definition by name at a cell with no placing player, as "Feature <name>"
    /// does, with its draw.
    ///
    /// @param name feature name
    /// @param cell_x cell column
    /// @param cell_z cell row
    /// @return false for an unknown name, a cell off the map or a refused placement
    bool console_place_feature(const char* name, int32_t cell_x, int32_t cell_z);

    /// Reads a text file for "Include" and the debugdat scripts: loose files in the game directory,
    /// then the mounted archives.
    ///
    /// @param path file path
    /// @param[out] length the file's size in bytes; may be null
    /// @return a NUL-terminated malloc copy the caller frees, or null when the
    ///     file is missing or memory runs out
    char* console_read_text_file(const char* path, int32_t* length);
    // The pointer's map cell and the feature word there, for the console.

    /// Keeps the map cell under the pointer and the feature word there in the Game block for the
    /// console's cursor commands.
    ///
    /// A footprint continuation word stands for its origin's feature, and a
    /// reserved word for none.
    ///
    /// @param ground 16.16 ground point under the pointer
    void store_cursor_cell(const oa::sim::ground_orders::Point& ground);
    // Savegames (runtime_saveload.cpp): the running match to and from HAPIBANK.
    struct SaveLoadState;

    /// Frees a save/load state.
    ///
    /// @param state state to free; null is allowed
    static void destroy_saveload_state(SaveLoadState* state) noexcept;

    /// Returns the save/load state, creating it on first use.
    ///
    /// @return the state
    SaveLoadState& saveload_state();

    /// Saves the running match: the Summary and every match section through the summary writer.
    ///
    /// @param path savegame file
    /// @param description save name
    /// @param game_id game id the Summary carries
    /// @return false without a match or when the save fails, which the status
    ///     line shows
    bool save_match_game(const fs::path& path, const char* description, int32_t game_id);

    /// Saves between missions, as ENDMSN's save does.
    ///
    /// The summary writer outside a match writes the finished mission's kept
    /// Game block, whose Summary names the next mission and sets
    /// BetweenMissions; no match sections follow.
    ///
    /// @param path savegame file
    /// @param description save name
    /// @param game_id game id the Summary carries
    /// @return false without a finished campaign mission, which the status line
    ///     shows
    bool save_between_missions(const fs::path& path, const char* description, int32_t game_id);

    /// Writes a savegame through the summary writer.
    ///
    /// A campaign's Summary names the campaign object and its mission-list name
    /// (CampaignFile::mission_name), which the load binds by. A Game block outside a match (the
    /// end-of-mission screen's save) names the next mission and then binds the
    /// campaign object back to the mission at Game.mission_index. The Summary
    /// carries the 3.1c build stamp.
    ///
    /// @param save save context over the world to write
    /// @param campaign true for a campaign save
    /// @param path savegame file
    /// @param description save name
    /// @param game_id game id the Summary carries
    /// @return false when the file cannot be written
    bool write_saved_game(
        oa::data::persist::SaveContext& save,
        bool campaign,
        const fs::path& path,
        const char* description,
        int32_t game_id
    );

    /// Starts a savegame as the load dialog and the mission start do.
    ///
    /// A skirmish takes the Summary's map, difficulty, player count and rules,
    /// the saved controllers, a world without commanders, then the saved
    /// session; a campaign save goes to load_saved_campaign(). A file that is not
    /// a save is shown on the status line.
    ///
    /// @param path savegame file
    /// @return false when the save cannot be started
    bool load_saved_game(const fs::path& path);

    /// Reads the Summary fields a load needs from a save file.
    ///
    /// @param path savegame file
    /// @param[out] summary the Summary's fields
    /// @return false when the file is not a save or names no mission
    bool read_save_summary(const fs::path& path, oa::ui::frontend::LoadSummary& summary);

    /// Starts a campaign savegame.
    ///
    /// The saved campaign, side and difficulty, the mission bound by its
    /// mission-list name and the saved mission results. A save made between
    /// missions opens that mission's briefing, whose Back returns to Single
    /// Player (frontend state 0xF); any other starts the mission, which resumes
    /// the saved session in place of its units.
    ///
    /// @param path savegame file
    /// @param bank the save's open bank
    /// @param summary the save's Summary
    /// @return false when the campaign or mission cannot be found, which the
    ///     status line shows
    bool load_saved_campaign(
        const fs::path& path,
        oa::data::persist::Bank* bank,
        const oa::ui::frontend::LoadSummary& summary
    );

    /// Finishes starting a saved game once the saved session is in the match.
    ///
    /// The saved clock, the restored alliance rows the match keeps its own copy
    /// of, and a fresh host clock (the saved one belongs to the session that
    /// wrote it).
    ///
    /// @param restored_players true when the Players section was restored
    void finish_saved_game_start(bool restored_players);

    /// Creates the unit saved under an id.
    ///
    /// Finds its Units record, creates it in the saved slot and restores the
    /// record, economy, movement, script and weapon state. Carriers and linked
    /// units are restored first, recursively. The walk over the ids that hold
    /// records stops at the first id below the count without one, as the game's
    /// does.
    ///
    /// @param id saved unit id
    /// @param bank the save's open bank
    /// @return the unit, the already restored one, or null when the save has no
    ///     record for the id
    oa::Unit* restore_saved_unit(uint16_t id, oa::data::persist::Bank* bank);

    /// Restores a unit's saved orders.
    ///
    /// They replace the queues its creation and carrier link filled, then its
    /// head order's goal is handed to its movement object. A target or goal unit
    /// not restored yet is restored first, as the order and goal loaders do.
    /// An order blob the account lacks, or one that does not read, adds no
    /// order and counts as a restore failure; 3.1c queues an empty order for
    /// each absent blob, so a unit restored from such a save has fewer orders
    /// here.
    ///
    /// @param unit restored unit
    /// @param order_count orders the unit's record counts
    /// @param bank the save's open bank, at the unit's orders account
    /// @quirk The walk stops at the account's blob count.
    void
    restore_saved_orders(const oa::Unit& unit, uint32_t order_count, oa::data::persist::Bank* bank);

    /// Returns the map list the scenario conditions are saved and restored from.
    ///
    /// The conditions are saved and restored only from the single-player map
    /// list (Game.game_options), which a campaign mission runs from.
    ///
    /// @return the selection set-up list for a campaign mission, else the
    ///     skirmish list
    [[nodiscard]] int32_t scenario_map_kind() const noexcept;

    /// Restores a saved session into the bootstrapped match, section by section.
    ///
    /// @param bank the save's open bank
    /// @return true when the Players section was restored
    bool restore_saved_session(oa::data::persist::Bank* bank);

    /// Restores the saved session in place of creating the mission's units, for a campaign mission
    /// started from a savegame (Game.saved_game).
    ///
    /// @return false when no save is being resumed
    bool resume_saved_mission();

    /// Tells whether the match being built resumes a savegame (Game.saved_game).
    ///
    /// Such a match places only the map's blocking markers: the save's
    /// Features section places its features.
    ///
    /// @return true while a skirmish or campaign mission starts from a savegame
    [[nodiscard]] bool resuming_saved_game() const noexcept;

    /// Digests the match state a savegame carries.
    ///
    /// The clock, each active player's economy, counters and colours, every live
    /// unit's record, economy, weapons, script and movement state and saved
    /// orders, the map's metal, placing-player and sight words, the camera and
    /// the meteor state.
    ///
    /// @return the 64-bit digest
    [[nodiscard]] uint64_t match_world_digest() const;

    /// Runs the headless savegame run.
    ///
    /// A fresh skirmish with any --combat armies, the --campaign mission
    /// --mission names, or --load; --match-ticks simulated ticks, a save once the
    /// tick reaches --save-after, and the world digest and the Features section
    /// printed for comparison across runs, with the script, movement and
    /// economy state a save or load dropped. With --give-orders, feature
    /// changes start two ticks before the save. A victory or defeat decided
    /// during the ticks is printed with its tick ("saveload: outcome"). A save
    /// made between missions loads into its mission's briefing, which the run
    /// reports instead. Throws std::runtime_error when the save does not load.
    void run_headless_saveload();

    /// Switches meteor storms on or off, as "Meteor <n>" does (MeteorState.enabled).
    ///
    /// @param enabled true to enable storms
    void set_meteor_enabled(bool enabled);

    /// Tests whether meteor storms are on.
    ///
    /// @return MeteorState.enabled
    bool meteor_enabled();

    /// Places a meteor strike, as "Meteor" does, drawing on the match's rand() stream.
    ///
    /// The match's meteor step lets it fall when storms are on.
    void start_meteor_strike();

    /// Loads the storm settings a map's mission info carries.
    ///
    /// The keys of the session's schema, or gamedata/meteor.tdf [Default] when
    /// the schema names no weapon or leaves a value at zero. Only a schema that
    /// names a weapon enables storms. The match start then stops any strike and
    /// resolves the weapon. Throws std::runtime_error when meteor.tdf does not
    /// parse or holds bogus defaults.
    void reset_meteors();

    /// Runs one match tick of the storm: a strike starts on its schedule and, while storms are on,
    /// drops a meteor every hit interval.
    void step_meteors();

    /// Draws the drag box while the button is held, over the fog.
    ///
    /// An outline in UI colour 15 with one in colour 0 a pixel inside it, each
    /// drawn edge by edge even when the box is too small to hold the inner one.
    ///
    /// @param[in,out] destination battlefield frame
    /// @param viewport battlefield viewport
    void draw_selection_band(
        oa::present::world_renderer::Surface& destination,
        const oa::present::world_renderer::BattlefieldViewport& viewport
    );

    /// Returns the terrain point under the pointer, the pointer held inside the battlefield.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return whole map pixels x, height and z, or nullopt without a match
    [[nodiscard]] std::optional<std::array<int32_t, 3>> match_pointer_ground(float x, float y);

    /// Moves the drag box's far corner to the terrain under the pointer while the button is held,
    /// each frame and as it moves.
    void track_match_drag();

    /// Returns the drag box's corners on a viewport.
    ///
    /// x less the camera, z less half the height and the camera, from the
    /// viewport's origin and at its scale.
    ///
    /// @param viewport battlefield viewport
    /// @return the press corner and the far corner; zeros without a drag
    [[nodiscard]] std::array<oa::present::world_renderer::ScreenPoint, 2>
    match_drag_corners(const oa::present::world_renderer::BattlefieldViewport& viewport) const;

    /// Tests whether the release ends a click rather than a box selection: soon after the press and
    /// with the box small in x and z.
    ///
    /// @return true for a click, or without a drag
    [[nodiscard]] bool match_drag_is_click() const;

    /// Makes the next (or previous) selected unit the primary one.
    ///
    /// @param reverse true to go backwards
    void cycle_selected_primary(bool reverse);

    /// Centres the camera on a unit through the camera setter.
    ///
    /// @param id unit id; 0 or an empty slot does nothing
    void center_camera_on_unit(uint16_t id);

    /// Shows the order panel for the selection: a builder's first build page, else the order page.
    ///
    /// Nothing while an in-game menu or the outcome is up (match_paused_): the
    /// menu keeps its panel, and resume_match_pause() shows the selection's.
    void apply_match_hud_for_selection();

    /// Tests whether a canvas point is on the radar picture.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return true inside the picture
    bool radar_contains(float x, float y) const;

    /// Returns the terrain point under a radar position.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return 16.16 world point, or nullopt off the radar or without a map
    std::optional<oa::sim::ground_orders::Point> radar_world_point(float x, float y);

    /// Picks the unit whose blip is under the pointer, from the hot list the radar last composed,
    /// through the radar branch of the pointer hit test.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return unit id, or 0
    uint16_t pick_radar_unit(float x, float y);

    /// Gives the selection the armed command at a radar position.
    ///
    /// Patrol and build take the map point; attack and D-gun take the enemy blip
    /// or the ground; move (and no command) takes the map point. A failure is
    /// shown on the status line.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return true when the click was used
    bool issue_radar_orders(float x, float y);

    /// Centres the view on the map point under a radar click.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @return false off the radar
    bool pan_camera_from_radar(float x, float y);

    /// Keeps the map point under the zoom anchor at its screen position while zooming.
    void apply_zoom_anchor();

    /// Eases the battlefield zoom toward its target by frame time, keeping the anchor in place.
    void step_match_zoom();

    /// Zooms the battlefield with the mouse wheel about the pointer.
    ///
    /// @param wheel_y wheel steps; positive zooms in
    /// @param pointer_x canvas column of the pointer
    /// @param pointer_y canvas row of the pointer
    void handle_match_zoom(float wheel_y, float pointer_x, float pointer_y);

    /// Scrolls the camera with the arrow and WASD keys and at the battlefield's edges.
    ///
    /// Paced as the game paces it: the map pixels moved in a frame are the scroll
    /// speed times the whole 30 Hz clock units since the previous frame, so a
    /// second of scrolling covers the same ground at any frame rate, at the match
    /// record's speed (the preference, or the console's ScrollSpeed). Zoom keeps
    /// the on-screen rate constant; the carried fraction keeps the world rate
    /// exact.
    void pan_match_camera();

    /// Sends the primary selected unit to resume building or repair a unit.
    ///
    /// @param id target unit id
    void issue_resume_or_repair(uint16_t id);

    /// Gives a unit HelpBuild on an unfinished target or Repair on a finished one, queued while
    /// shift is held.
    ///
    /// @param source ordered unit
    /// @param id target unit id; 0 or `source` does nothing
    void issue_resume_or_repair_from(uint16_t source, uint16_t id);

    /// Moves the selection to the ground under a canvas point, or gives the radar's orders over the
    /// radar.
    ///
    /// A matching queued move is taken off instead; units that take no move
    /// order are skipped. A failure is shown on the status line.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param queue true to queue the move
    void issue_match_move(float x, float y, bool queue);

    /// Sends the selection to patrol to the ground under a canvas point.
    ///
    /// A matching queued patrol is taken off instead. A failure is shown on the
    /// status line.
    ///
    /// @param x canvas column
    /// @param y canvas row
    /// @param queue true to queue the patrol
    void issue_match_patrol(float x, float y, bool queue = false);

    /// Handles an SDL event the screen packages did not take.
    ///
    /// Quit ends the loop, a resize lays the frame out again, typed text feeds
    /// the chat line, keys go to the match hotkeys, the typed-key hooks and the
    /// end panel, Escape backs out of the current screen, the wheel zooms the
    /// match or scrolls a list, and pointer events drive the frontend screens
    /// or the match.
    ///
    /// @param event event to handle
    /// @param[in,out] running loop flag; cleared on quit
    void handle_sdl_event(SDL_Event& event, bool& running);

    /// Shows an unsupported operation: the status line, stderr and, with a window, a message box.
    ///
    /// @param message what is not supported
    void show_unsupported(std::string_view message);

    /// Returns the playback state named sounds are selected with.
    ///
    /// "-s" suppresses every named sound; "-w" also sets the system-sound flag,
    /// which the selection accepts before it looks at the suppression.
    ///
    /// @return the state
    [[nodiscard]] oa::audio::game_audio::PlaybackState sound_playback_state() const noexcept {
        oa::audio::game_audio::PlaybackState state{
            true,
            1,
            false,
            options_.launch.system_sound != 0,
            oa::audio::game_audio::PlaybackRoute::primary
        };
        if (options_.launch.playback_suppressed != 0)
            oa::audio::game_audio::suppress_playback(state);
        return state;
    }

    /// Plays a frontend entry sound by its ALLSOUND name unless muted; a failure is reported on
    /// stderr.
    ///
    /// @param sound sound to play
    template <typename Sound>
    void play_named_sound(Sound sound) {
        if (options_.mute)
            return;
        const auto selection = oa::audio::game_audio::select(
            audio_registry_, entry::resource_name(sound), false, sound_playback_state()
        );
        std::string error;
        if (selection.status == oa::audio::game_audio::SelectionStatus::selected &&
            !audio_player_.play(selection, error))
            std::cerr << "sound unavailable: " << error << '\n';
    }

    /// Starts the main menu's music: the CD music's menu mode and the menu voice loop.
    void start_menu_music();

    /// Loops an ALLSOUND alternate voice as the menu music (the menu's BGM entry is drone2).
    ///
    /// Nothing plays when muted, headless or already playing; a failure is
    /// reported on stderr.
    ///
    /// @param sound ALLSOUND name
    void play_menu_voice(std::string_view sound);

    /// Plays an ALLSOUND sound on the alternate route, looping it as the menu
    /// music plays, in place of whatever that route plays.
    ///
    /// Nothing plays when muted, headless or for a name the registry does not
    /// hold; a failure is reported on stderr. Once it plays, the menu music
    /// counts as playing, so play_menu_voice starts no second loop.
    ///
    /// @param sound ALLSOUND name, compared case-insensitively
    /// @return true when the sound started
    bool play_alternate_sound(std::string_view sound);

    /// Stops the menu music loop and puts the CD music into its match mode.
    void stop_menu_music();

    // CD music (runtime_music.cpp): music files play as the disc.
    struct MusicHost;

    /// Frees a music host.
    ///
    /// @param host host to free; null is allowed
    static void destroy_music_host(MusicHost* host) noexcept;

    /// Starts the CD music once: the numbered music files of the game directory play as the
    /// disc.
    ///
    /// Nothing starts when muted or headless; a missing disc or decoder is
    /// reported on stderr and the music stays silent.
    void music_start();

    /// Puts the CD music into its main-menu mode, ending the match's music first.
    void music_main_menu();

    /// Tests whether another program holds the CD player.
    ///
    /// @return true when the mixer reports a foreign player
    [[nodiscard]] bool music_foreign_player() const;

    /// Closes the CD player another program holds.
    void music_close_foreign_player();

    /// Tests whether the mixer found no music driver.
    ///
    /// @return true without a driver
    [[nodiscard]] bool music_no_driver() const;

    /// Puts the CD music into its match mode for the viewed player.
    void music_begin_match();

    /// Steps the CD music once per idle pass.
    ///
    /// Ticks its timers and track end, takes the options screens' volume, music
    /// and CD mode changes, leaves the music panel when it closes, feeds a
    /// match's hits and kills to the mood and pauses the music while the in-game
    /// menu holds the game.
    void step_music();
    // Console commands: CDPlay <track>, CDStop, MusicMode <kind>.

    /// Plays a CD track, as the console's CDPlay does.
    ///
    /// @param track track number
    /// @return false when the music cannot start or the track is refused
    bool music_cd_play(int32_t track);

    /// Stops the CD music, as the console's CDStop does.
    ///
    /// @return false when the music cannot start or the stop is refused
    bool music_cd_stop();

    /// Sets the music mood kind, as the console's MusicMode does.
    ///
    /// @param kind music kind
    void music_mode(int32_t kind);
    // MUSIC.GUI hooks; clicked returns true when it handled the control.

    /// Enters the MUSIC.GUI panel: the CD controls and the track display.
    ///
    /// @param[in,out] panel music panel
    /// @param context options screen context
    void music_panel_entered(
        oa::ui::frontend::Panel& panel, const oa::ui::frontend::OptionsContext& context
    );

    /// Handles a MUSIC.GUI control click.
    ///
    /// The music panel's arms: NOTRAK, TRACKMODE, TRACKTYPE, CDPLAY, CDSTOP,
    /// CDNEXT/CDPREV and RESTORE, each followed by the track display refresh
    /// (refresh_music_panel). UNDO and panel buttons go through the options tab
    /// handler, and leaving the panel (no selection) through step_music's
    /// music_panel_leave, once neither the Music screen nor the MUSIC tab of
    /// the preferences a match opens shows.
    ///
    /// @param[in,out] panel music panel
    /// @param context options screen context
    /// @return true when it handled the control
    bool music_panel_clicked(
        oa::ui::frontend::Panel& panel, const oa::ui::frontend::OptionsContext& context
    );
    // The match's mapping and line-of-sight rules (Game.visibility_flags): the
    // session rules in a skirmish or campaign, the host's in a multiplayer
    // game, both off for a watcher.

    /// Tests whether the match's mapping rule is on (Game.visibility_flags).
    ///
    /// The session rules in a skirmish or campaign, the host's in a multiplayer
    /// game, both off for a watcher.
    ///
    /// @return true with mapping on
    [[nodiscard]] bool match_mapping_on() const;

    /// Tests whether the match's line-of-sight rule is on (Game.visibility_flags).
    ///
    /// @return true with line of sight on
    [[nodiscard]] bool match_line_of_sight_on() const;

    /// Plays a main-menu sound by its ALLSOUND name unless muted; a failure is reported on stderr.
    ///
    /// @param sound sound to play
    void play_menu_sound(menu::Sound sound);

    /// Plays a movie of the game's Data directory in the window, then lays the frame out again.
    ///
    /// A missing or failed movie is shown on the status line.
    ///
    /// @param filename movie file (1.zrb .. 5.zrb)
    void play_movie_resource(std::string_view filename);

    /// Runs a dispatcher step through its registered handler.
    ///
    /// A registered step runs its handler; a step with no handler, engine or
    /// multiplayer, shows its number on the status line and does nothing else.
    ///
    /// @param step_id step to run
    /// @param state dispatcher state
    void step(frontend::Step step_id, frontend::State& state) override;

    /// Answers a dispatcher query; every query answers 0 here.
    ///
    /// @param query query to answer
    /// @param state dispatcher state
    /// @return 0
    uint32_t query(frontend::Query query, frontend::State& state) override;

    /// Plays a frontend movie.
    ///
    /// @param state dispatcher state
    /// @param filename movie resource (1.zrb .. 5.zrb)
    void play_movie(frontend::State& state, std::string_view filename) override;

    /// Shows or hides the cursor overlay.
    ///
    /// @param state dispatcher state
    /// @param value 1 shows, 0 hides
    void set_cursor_visible(frontend::State& state, int32_t value) override;

    /// Selects the map list a menu offers.
    ///
    /// @param state dispatcher state
    /// @param selector_value 0 main menu, 1 campaign, 2 skirmish, 3 multiplayer
    void select_map_list(frontend::State& state, int32_t selector_value) override;

    /// Opens NEWGAME.GUI as New Campaign, or as Any Mission when the dispatcher signals it.
    ///
    /// @param state dispatcher state
    /// @param value 0 for a new campaign, 1 for any mission; kept for the screen
    void open_new_game_panel(frontend::State& state, int32_t value) override;

    /// Sets the application mode, kept here and in the frontend Game block,
    /// and reports it through Extension::app_mode_set.
    ///
    /// @param state dispatcher state
    /// @param mode a mode_id value
    void set_app_mode(frontend::State& state, int32_t mode) override;

    /// Starts a cursor animation.
    ///
    /// @param state dispatcher state
    /// @param index cursor table index
    void set_cursor(frontend::State& state, int32_t index) override;

    /// Sets the end screen's state on the kept Game block, when the dispatcher returns to the
    /// panel.
    ///
    /// @param state dispatcher state
    /// @param step end-game step to enter
    void set_endgame_state(frontend::State& state, int32_t step) override;

    /// Shuts the application down.
    ///
    /// @param state dispatcher state
    void shut_down(frontend::State& state) override;

    /// Builds the preferences file key of a section's setting.
    ///
    /// @param section preference section
    /// @param key value name
    /// @return "<section>|<key>"
    static std::string preference_key(std::string_view section, std::string_view key);

    /// Loads the preferences file, or imports the earlier settings file once when there is none.
    ///
    /// An explicit --preferences-file starts from defaults. Without a file, the
    /// game directory's open-annihilation.ini (1 MiB at most) is read once;
    /// later reads and writes use the platform location only. Throws
    /// std::runtime_error when the legacy file is too large or unreadable.
    void load_preference_file();

    /// Writes the preferences file when a setting changed since the last write.
    void flush_preferences();

    /// Reads a numeric setting.
    ///
    /// @param section preference section
    /// @param key value name
    /// @return the value, or nullopt when absent or not a whole number
    std::optional<uint32_t> read_number(std::string_view section, std::string_view key) override;

    /// Writes a numeric setting; the file is written on the next flush.
    ///
    /// @param section preference section
    /// @param key value name
    /// @param value value to store
    void write_number(std::string_view section, std::string_view key, uint32_t value) override;

    /// Reads a string setting.
    ///
    /// @param section preference section
    /// @param key value name
    /// @param capacity field capacity including the terminating zero
    /// @return the value, or nullopt when absent or too long for the field
    std::optional<std::string>
    read_string(std::string_view section, std::string_view key, std::size_t capacity) override;

    /// Writes a string setting; the file is written on the next flush.
    ///
    /// @param section preference section
    /// @param key value name
    /// @param value value to store
    void
    write_string(std::string_view section, std::string_view key, std::string_view value) override;

    /// Applies a sound mode to the 3D sound switch.
    ///
    /// @param mode what the load or restore does to the switch
    void audio_mode(init::AudioMode mode) override;

    /// Keeps the MixingBuffers setting.
    ///
    /// @param value mixing buffer count
    void mixing_buffers(uint32_t value) override;

    /// Restores the saved wave output volume onto the effects player.
    ///
    /// @param value WaveOutVolume setting
    void wave_volume(uint32_t value) override;

    /// Keeps the saved CD-audio volume.
    ///
    /// @param value CDAudioVolume setting
    void cd_volume(uint32_t value) override;

    /// Asks the extension for the frontend's launch values and reports whether they name a
    /// nickname.
    ///
    /// The preferences load calls it first of the three overrides, once per
    /// load: the extension's frontend_entry is asked here, and its nickname
    /// and game name are kept for nickname_override and game_name_override.
    ///
    /// @return 1 when the extension gave a nickname that is not empty, else 0
    uint32_t nickname_override_enabled() override;

    /// Returns the nickname the extension gave at the last preferences load; the preferences
    /// load prefers it to the stored one.
    ///
    /// @return the nickname, or empty
    std::string nickname_override() override;

    /// Returns the game name the extension gave at the last preferences load (a launch
    /// switch's -h, say); the preferences load prefers it to the stored one.
    ///
    /// @return the game name, or empty
    std::string game_name_override() override;

    /// Returns the operating-system user name from USER or USERNAME.
    ///
    /// @return the name, or nullopt when neither is set
    std::optional<std::string> user_name() override;

    /// Returns the application directory: the game directory.
    ///
    /// @return the directory
    std::string application_directory() override;

    /// Selects the map list.
    ///
    /// @param selector_value map_list_kind value
    void select_map_list(int32_t selector_value) override;

    /// Selects the first map of the cached map list, rebuilding the list when the mode returns to 0
    /// from another mode or no list is cached. With no eligible map nothing is selected.
    ///
    /// Throws std::runtime_error when the first eligible map cannot be selected.
    ///
    /// @param index map list mode the selection is made for
    void select_map_index(int32_t index) override;

    /// Returns the name of the selected map: the first eligible one.
    ///
    /// @return the map name, empty when the game data offers no eligible map
    std::string selected_map_name() override;

    /// Returns the mixing buffer count the preferences set.
    ///
    /// @return the count
    uint32_t mixing_buffer_count() override;

    /// Returns the wave output volume the preferences set.
    ///
    /// @return the packed volume
    uint32_t wave_out_volume() override;

    /// Returns the CD-audio volume the preferences set.
    ///
    /// @return the packed volume
    uint32_t cd_audio_volume() override;

    /// Reports whether the preferences write keeps the stored password
    /// (Extension::keep_stored_password).
    ///
    /// @return 1 when the extension says so, else 0
    uint8_t keep_stored_password() override;

    /// Returns the selector a map-list object was built for.
    ///
    /// @param handle map-list object
    /// @return its map_list_kind value, or -1 for an unknown object
    int32_t selector(init::MapListHandle handle) override;

    /// Destroys a map-list object's contents; there are none here.
    ///
    /// @param object map-list object
    void destroy(init::MapListHandle object) override;

    /// Frees a map-list object.
    ///
    /// @param handle map-list object
    void release(init::MapListHandle handle) override;

    /// Allocates a map-list object.
    ///
    /// @return a new handle
    init::MapListHandle allocate() override;

    /// Builds a map-list object for a selector.
    ///
    /// @param handle allocation from allocate()
    /// @param selector_value map_list_kind value
    /// @return the object
    init::MapListHandle construct(init::MapListHandle handle, int32_t selector_value) override;

    /// Lists the maps the skirmish and multiplayer map pickers offer: every map with a multiplayer
    /// schema, in find order; the first one is the default selection.
    ///
    /// Game data with no such map, such as the Total Annihilation demo (1997), leaves both the
    /// list and the default selection empty.
    void discover_first_map();

    /// Finds a gadget of the current screen by name.
    ///
    /// @param name gadget name
    /// @return the gadget, or null
    oa::ui::gui_layout::Gadget* widget(std::string_view name);

    /// Returns the frontend panel's handle.
    ///
    /// @return the handle
    skirmish::MenuHandle frontend_menu() override;

    /// Clears the back buffer; the next frame redraws everything here.
    void clear_backbuffer() override;

    /// Loads SKIRMISH.GUI as the skirmish screen.
    ///
    /// Throws std::runtime_error for any other layout.
    ///
    /// @param name GUI file name
    /// @return the frontend panel
    skirmish::MenuHandle load_menu(std::string_view name) override;

    /// Installs the skirmish event callback; the runtime dispatches the events itself.
    ///
    /// @param menu the panel
    void install_event_callback(skirmish::MenuHandle menu) override;

    /// Loads the panel's named background and redraws the screen.
    ///
    /// @param name bitmap name
    void load_background(std::string_view name) override;

    /// Installs SKIRMISH.GUI's key hook, which reads the typed history for the player-count code.
    void install_input_callback() override;

    /// Enables or disables panel input.
    ///
    /// @param enabled nonzero enables
    void set_input_enabled(int32_t enabled) override;

    /// Adds flags to the menu's panel flags.
    ///
    /// @param flags panel flags
    void add_menu_flags(uint32_t flags) override;

    /// Returns how many records the current screen's GUI holds.
    ///
    /// @return the count, at most 32767
    int16_t widget_count() override;

    /// Drops the current screen's records from an index on.
    ///
    /// @param count records to keep; out of range keeps them all
    void set_widget_count(int16_t count) override;

    /// Appends one per-slot widget to the current screen's GUI.
    ///
    /// A button with a sprite resource is bound to it; an image becomes a
    /// hot surface, which takes clicks. The widget's tooltip becomes its help,
    /// which HELPTEXT shows while the pointer is over it; a widget without one
    /// clears HELPTEXT. Throws std::runtime_error when the GUI is full.
    ///
    /// @param source widget description
    void create_slot_widget(const skirmish::SlotWidget& source) override;

    /// Binds a slot button to the GUI archive's named sequence and sizes it to the frame its stage
    /// shows, when the sequence and frame exist.
    ///
    /// @param[in,out] gadget slot button
    /// @param sequence sequence name
    void link_slot_sprite(oa::ui::gui_layout::Gadget& gadget, std::string_view sequence);
    // The frontend panel's typed-key history and the hook that reads it.
    enum class TypedKeyHook : uint8_t { none, single_player_code, skirmish_players };

    /// Shifts a key into the panel's typed history, newest last, runs the panel's key hook over it
    /// and redraws.
    ///
    /// @param key typed key, upper case
    void record_typed_key(uint8_t key);

    /// Runs the SINGLE.GUI setup and installs its DRDEATH key hook.
    void enter_single_player_panel();

    /// Tells whether the game data holds the any-mission screen: NEWGAME.GUI over playanygame4.
    ///
    /// The Total Annihilation demo (1997) has no playanygame4 bitmap; SINGLE.GUI then keeps
    /// AnyMsn hidden, and DRDEATH does not show it.
    ///
    /// @return true when the screen's layout and background are both present
    [[nodiscard]] bool offers_any_mission() const;

    /// Tells whether the game data holds LOADGAME.GUI, the save and load dialog.
    ///
    /// The Total Annihilation demo (1997) has none; the entries that open the dialog are then
    /// grayed out.
    ///
    /// @return true when the layout is present
    [[nodiscard]] bool offers_saved_games() const;

    /// Tells whether the game data holds a campaign for a side.
    ///
    /// @param side 0 for Arm, 1 for Core
    /// @return true when a campaign file names the side
    [[nodiscard]] bool side_has_campaign(uint32_t side);

    /// Sets NEWGAME.GUI up through its setup.
    ///
    /// The Campaign and Missions lists it fills are the screen's lists; with the
    /// side buttons choosing the campaign it fills none, and Start takes the
    /// side's campaign. A side with no campaign in the game data is grayed out,
    /// and the panel opens on the other side when the preferred one has none.
    ///
    /// @param any_mission true for Any Mission, false for New Campaign
    void enter_new_game_panel(bool any_mission);

    /// Checks the typed history for SINGLE.GUI's cheat code.
    void check_single_player_code();

    /// Stores whether every mission is unlocked in the preferences.
    ///
    /// @param unlocked true to unlock every mission
    void store_all_missions(bool unlocked);

    /// Returns SINGLE.GUI's services over the loaded widgets: control values, cursor animations and
    /// the all-missions setting.
    ///
    /// @return the services, bound to this runtime
    [[nodiscard]] oa::ui::campaign::FrontendHost single_player_host();

    /// Moves a gadget of the current screen to a new y, as FrontendHost::set_control_y asks.
    ///
    /// Drawing and pointer tests read the new y. It lasts until the screen is loaded again, which
    /// places every gadget where its GUI file does.
    ///
    /// @param context the runtime
    /// @param name gadget name; a missing gadget, or one whose type is not `type`, is left as is
    /// @param type GUI record type (oa::ui::gui_layout::GadgetType) the gadget must have
    /// @param y new top edge, in the panel's coordinates
    static void set_widget_y(void* context, const char* name, uint8_t type, int16_t y);

    /// Returns the TextRegion rectangle and row pitch the briefing pages are laid out in.
    ///
    /// @return the region; zeros without a TextRegion
    [[nodiscard]] oa::ui::campaign::BriefingRegion briefing_region();

    /// Sets a button's or label's text.
    ///
    /// @param menu panel holding the gadget
    /// @param name gadget name; a missing gadget is ignored
    /// @param text new text
    /// @param length text-box length; unused here
    void set_text(
        skirmish::MenuHandle menu, std::string_view name, std::string_view text, int32_t length
    ) override;

    /// Sets a gadget's active byte.
    ///
    /// @param name gadget name; a missing gadget is ignored
    /// @param enabled nonzero activates
    void set_enabled(std::string_view name, int32_t enabled) override;

    /// Sets a button's stage, the frame and caption it shows.
    ///
    /// @param name gadget name
    /// @param stage new stage
    void set_button_stage(std::string_view name, uint8_t stage) override;

    /// Sets the status of the button named after the difficulty.
    ///
    /// SKIRMISH.GUI has no such button, so this changes nothing there: its
    /// Difficulty button shows the caption its stage picks from
    /// "Easy|Medium|Hard".
    ///
    /// @param label "Easy", "Medium" or "Hard"
    /// @param value status to set, always 1
    void select_difficulty_label(std::string_view label, int32_t value) override;

    /// Sets the status of a named button; a nonzero status clears the other buttons of its group.
    ///
    /// A frontend button with a nonzero status shows pressed, as the chosen
    /// member of a group does. Buttons share a group by their association
    /// byte; association 0 is no group.
    ///
    /// @param name gadget name; a missing gadget or one that is not a button changes nothing
    /// @param value new status
    void set_button_status(std::string_view name, int16_t value);

    /// Sets a side button's stage, the frame and caption it shows.
    ///
    /// @param name gadget name
    /// @param stage side index
    void set_side_stage(std::string_view name, uint8_t stage) override;

    /// Sets a gadget's help text.
    ///
    /// @param name gadget name; a missing gadget is ignored
    /// @param text translated help
    void set_tooltip(std::string_view name, std::string_view text) override;

    /// Shows a sequence frame on a gadget: the player colour logos or the ally icons.
    ///
    /// @param name gadget name; a missing gadget is ignored
    /// @param sprite sequence to show
    /// @param frame frame index
    void set_image(std::string_view name, skirmish::Sprite sprite, uint16_t frame) override;

    /// Sets a gadget's image frame.
    ///
    /// @param name gadget name; a missing gadget is ignored
    /// @param frame frame index
    void set_image_frame(std::string_view name, uint16_t frame) override;

    /// Returns the frame count of the screen's "ally icons" sequence.
    ///
    /// @return the count, or nullopt when the sequence is missing
    std::optional<uint16_t> team_icon_frame_count() override;

    /// Zeroes the origin of a TEAMICONS frame; the icons draw from their frames here, so nothing
    /// changes.
    ///
    /// @param frame frame index
    void zero_team_icon_frame_origin(uint32_t frame) override;

    /// Returns the frame count of the 32x32 logo sequence: one player colour per frame.
    ///
    /// @return the count; 0 without the logos
    uint16_t color_frame_count() override;

    /// Loads the side-logo texture archive (textures/logos.gaf) and finds its 32x32 logo sequence.
    void load_logo_textures();

    /// Redraws the screen.
    void invalidate_menu() override;

    /// Copies the hovered gadget's help into the screen's HELPTEXT label.
    ///
    /// Nothing hovered clears it; a screen without HELPTEXT is left alone.
    void refresh_help_text() override;

    /// Returns the name of the selected gadget of the current screen.
    ///
    /// @param event menu event
    /// @return the name; empty with nothing selected
    std::string selected_widget_name(const entry::Event& event) override;

    /// Reports whether a skirmish button was activated: the selected gadget is the button's.
    ///
    /// @param menu menu the event came from
    /// @param button button to test
    /// @return nonzero when it was
    uint32_t button_result(skirmish::MenuHandle menu, skirmish::Button button) override;

    /// Returns the mouse button of the last pointer event.
    ///
    /// @param menu menu the event came from
    /// @return 1 for left, 2 for right
    int32_t event_button(skirmish::MenuHandle menu) override;

    /// Reads the current pointer state; the runtime keeps it from the SDL events already.
    void capture_input() override;

    /// Plays an interface sound by its ALLSOUND name unless muted.
    ///
    /// @param name ALLSOUND name
    /// @param argument second argument, always 0
    void play_ui_sound(std::string_view name, uint32_t argument) override;
    // The WAV a screen package's sound name plays: the file registered under
    // the name, else sounds/<name>.wav.

    /// Returns the WAV a screen package's sound name plays: the file registered under the name,
    /// else sounds/<name>.wav.
    ///
    /// @param name sound name
    /// @return the resource path
    [[nodiscard]] std::string screen_sound_resource(std::string_view name) const;

    /// Opens the map selection modal over the skirmish screen.
    void open_map_selection() override;

    /// Counts the maps eligible for skirmish.
    ///
    /// @return the count
    int32_t map_count() override;

    /// Copies the names of the eligible maps.
    ///
    /// @return the names, in find order
    std::vector<std::string> copy_map_names() override;

    /// Loads SELMAP.GUI as the map selection screen over the skirmish screen's frame.
    ///
    /// Throws std::runtime_error for any other layout or flags.
    ///
    /// @param resource GUI file name
    /// @param flags panel load flags
    /// @return the frontend panel
    map_modal::MenuHandle load_modal(std::string_view resource, uint32_t flags) override;

    /// Installs the map modal's event callback; the runtime dispatches the events itself.
    ///
    /// @param menu the modal panel
    void install_map_event_callback(map_modal::MenuHandle menu) override;

    /// Binds the map names to the MAPNAMES list.
    ///
    /// @param names sorted map names
    void bind_map_names(std::span<const std::string> names) override;

    /// Installs the MAPNAMES selection-change callback; the runtime previews the selection itself.
    void install_map_selection_callback() override;

    /// Selects a row of the MAPNAMES list.
    ///
    /// @param index row, from 0
    void set_selected_map_index(int16_t index) override;

    /// Returns the selected row of the MAPNAMES list.
    ///
    /// @return the row, from 0
    int16_t selected_map_index() override;

    /// Reports whether a map modal button was activated: the selected gadget is the button's.
    ///
    /// @param menu menu the event came from
    /// @param button button to test
    /// @return nonzero when it was
    uint32_t button_result(map_modal::MenuHandle menu, map_modal::Button button) override;

    /// Keeps the chosen map name for the MapName gadget of the skirmish screen below.
    ///
    /// @param menu menu of the event
    /// @param text map name
    void set_parent_map_name(map_modal::MenuHandle menu, std::string_view text) override;

    /// Tests whether the modal has a MAPNAME gadget.
    ///
    /// @return true when it does
    bool has_map_name_widget() override;

    /// Returns the selected map's display name.
    ///
    /// @return the name; empty without a selected map
    std::string map_display_name() override;

    /// Returns the selected map's memory requirement (OTA GlobalHeader memory).
    ///
    /// @return the requirement text; empty without a selected map
    std::string map_memory_requirement_text() override;

    /// Returns the selected map's permitted player counts.
    ///
    /// @return the counts text; empty without a selected map
    std::string permitted_player_counts_text() override;

    /// Returns the selected map's description, the map object's summary text.
    ///
    /// @return the description; empty without a selected map
    std::string map_description() override;

    /// Returns the selected map's terrain resource path.
    ///
    /// @return maps/<name>.tnt
    std::string terrain_resource_path() override;

    /// Sets the text of a modal gadget.
    ///
    /// @param name gadget name
    /// @param text new text
    /// @param argument text-box length; unused here
    void set_modal_text(std::string_view name, std::string_view text, int32_t argument) override;

    /// Returns the MAPPIC gadget's picture.
    ///
    /// @return the picture handle
    map_modal::PictureHandle picture() override;

    /// Sets the MAPPIC gadget's picture.
    ///
    /// @param picture_value picture handle; null for none
    void set_picture(map_modal::PictureHandle picture_value) override;

    /// Frees the map preview picture and its fit.
    ///
    /// @param picture picture to free
    void release_picture(map_modal::PictureHandle picture) override;

    /// Loads a map's terrain and its minimap as the preview picture, in PALETTE.PAL colours.
    ///
    /// Throws std::runtime_error when the terrain does not parse or the palette
    /// is malformed.
    ///
    /// @param terrain_path TNT path
    /// @return the picture and the TNT header width and height
    map_modal::LoadedPicture load_picture(std::string_view terrain_path) override;

    /// Returns the MAPPIC gadget's size.
    ///
    /// @return the size in pixels; zeros without the gadget
    map_modal::PictureSize picture_size() override;

    /// Fits the map picture into MAPPIC.
    ///
    /// Keeps the map's aspect less its 32 by 128 border: the longer side fills the
    /// gadget, the other is centred, and the matching part of the picture is the
    /// source. Throws std::runtime_error for empty sizes.
    ///
    /// @param picture picture to fit
    /// @param widget_width gadget width in pixels
    /// @param widget_height gadget height in pixels
    /// @param world_width map width in world units (TNT width << 4)
    /// @param world_height map height in world units (TNT height << 4)
    void fit_picture(
        map_modal::PictureHandle picture,
        int32_t widget_width,
        int32_t widget_height,
        int32_t world_width,
        int32_t world_height
    ) override;

    /// Selects a map by name for a skirmish: its OTA metadata, terrain and start markers.
    ///
    /// Throws std::runtime_error when the metadata or terrain does not parse.
    ///
    /// @param name map name
    /// @return 1 when the map loads; 0 when a file is missing or it has no
    ///     two-player schema
    int32_t select_map(std::string_view name) override;

    /// Returns how many players the selected map holds for the roster's player count, keeping its
    /// start markers.
    ///
    /// @return the start position count; 0 without a map or schema
    int32_t map_player_capacity() override;

    /// Reads a game file.
    ///
    /// @param path path inside the game data, such as objects3d/armcom.3DO
    /// @return the bytes, or nullopt when the file is absent; other read failures
    ///     throw
    std::optional<std::vector<uint8_t>> read(std::string_view path) override;

    /// Reads an integer key of the selected map's OTA GlobalHeader.
    ///
    /// @param key key name
    /// @param fallback value without the header or key
    /// @return the key's value, or `fallback`
    int32_t integer(std::string_view key, int32_t fallback) override;

    /// Reads a text key of the selected map's OTA GlobalHeader; a missing key differs from an empty
    /// one.
    ///
    /// @param key key name
    /// @return the key's text, or nullopt without it
    std::optional<std::string> text(std::string_view key) override;

    /// Returns the commander unit type of a side.
    ///
    /// Throws std::out_of_range for a side the table lacks and
    /// std::runtime_error when the commander is not in the unit catalog.
    ///
    /// @param side side index from the player setup
    /// @return unit type index
    uint16_t commander_type_for_side(uint8_t side) override;

    /// Reports a start position the map does not have: throws std::runtime_error naming it.
    ///
    /// @param index missing start position index
    void report_missing_start_position(int32_t index) override;

    /// Moves the local camera.
    ///
    /// @param x camera left edge, whole world units
    /// @param z camera top edge, whole world units
    /// @param flags always 0 from commander placement
    void set_camera_position(int32_t x, int32_t z, uint32_t flags) override;

    /// Builds the skirmish match from the roster, as the Start click does.
    void apply_skirmish_players() override;

    /// Saves the preferences and writes the preferences file.
    void save_preferences() override;

    /// Returns the main menu's environment object.
    ///
    /// @return the environment
    menu::Environment& environment() override;

    /// Releases the main-menu spark animation.
    void release_sparks() override;

    /// Reports whether a main menu button was activated: the selected gadget is the button's.
    ///
    /// @param menu menu the event came from
    /// @param button button to test
    /// @return nonzero when it was
    uint32_t button_result(menu::MenuHandle menu, menu::Button button) override;

    /// Plays a main-menu sound.
    ///
    /// @param sound sound to play
    /// @param argument second argument, always 0
    void play_sound(menu::Sound sound, uint32_t argument) override;

    /// Starts a cursor animation.
    ///
    /// @param index cursor table index
    void select_cursor_animation(uint32_t index) override;

    /// Prepares the multiplayer menus; this runtime only says on the status line that it cannot.
    void prepare_multiplayer() override;

    /// Resolves a resource path.
    ///
    /// @param request directory, name and extension
    /// @return "<directory>/<name>.<extension>"
    std::string resolve_resource(menu::ResourceRequest request) override;

    /// Constructs a document object.
    ///
    /// @return a new handle
    menu::DocumentHandle construct_document() override;

    /// Loads a document: tests that the file exists and is not empty.
    ///
    /// @param handle the last constructed document
    /// @param path file path
    /// @return 1 on success, 0 for another handle or a missing or empty file
    uint32_t load_document(menu::DocumentHandle handle, std::string_view path) override;

    /// Destroys a document object; there is nothing to free.
    ///
    /// @param document document to destroy
    void destroy_document(menu::DocumentHandle document) noexcept override;

    /// Resets the frontend after the multiplayer selection: the selection is dropped.
    ///
    /// The game redraws the frame before the multiplayer screens open.
    void reset_after_multiplayer_selection() override;

    /// Returns the application's flags byte.
    ///
    /// @return the frontend state's video context flags; fullscreen_mode is tested
    uint8_t application_flags() override;

    /// Tells whether the game folder holds a game disc's archive, totala1.hpi or totala2.hpi.
    ///
    /// @return true when either file is there
    [[nodiscard]] bool holds_disc_archive() const;

    /// Looks for a game disc: its archive in the game directory.
    ///
    /// A game directory that holds neither disc archive (holds_disc_archive()), such as the
    /// Total Annihilation demo (1997), keeps all its data in the archives it mounts; every disc
    /// counts as found there.
    ///
    /// @param disc disc wanted
    /// @return 1 when totala2.hpi (the campaign disc) or totala1.hpi is there, or neither is;
    ///         else 0
    uint32_t find_disc(menu::Disc disc) override;

    /// Returns the Shift key's state.
    ///
    /// @return -1 while either Shift is held, else 0
    int16_t shift_key_state() override;

    /// Drops every pending SDL event.
    void drain_input() override;

    /// Checks the frontend state checksum; the engine keeps no image checksum, so nothing is done.
    void check_frontend_integrity() override;

    /// Shows a main menu message as a 200-wide box that grows to its longest line.
    ///
    /// @param target where the message is shown
    /// @param message message to show
    void show_message(menu::MessageTarget target, menu::Message message) override;

    /// Runs the default handling of an event no button took: the selection is dropped.
    ///
    /// @param menu menu the event came from
    void default_event(menu::MenuHandle menu) override;

    /// Plays a single-player menu sound.
    ///
    /// @param sound sound to play
    /// @param argument second argument, always 0
    void play_sound(entry::Sound sound, uint32_t argument) override;

    /// Refreshes the archives read from the game discs; the game directory holds them, so nothing
    /// is done.
    void refresh_disc_archives() override;

    /// Translates a message into the game's language (gamedata/translate.tdf).
    ///
    /// @param message message to translate
    /// @return its text's translation, or its text when the language has none
    std::string translate(entry::Message message) override;

    /// Shows a frontend message box over the current screen, or an unsupported notice when it
    /// cannot open.
    ///
    /// @param text translated message
    /// @param width message box width in pixels
    /// @param show_ok nonzero shows the OK button
    /// @param fit_width nonzero fits the box to its longest line
    void show_frontend_message(
        std::string_view text, int32_t width, int32_t show_ok, int32_t fit_width
    ) override;

    /// Clears the selected record of the menu an event came from.
    ///
    /// @param menu menu of the event
    void clear_event_selection(entry::MenuHandle menu) override;

    /// Clears the selected record of the frontend panel.
    void clear_frontend_selection() override;

    /// Reports whether a single-player button was activated: the selected gadget is the button's.
    ///
    /// @param menu menu the event came from
    /// @param button button to test
    /// @return nonzero when it was
    uint32_t button_result(entry::MenuHandle menu, entry::Button button) override;

    /// Opens the load-game screen over a match or Single Player.
    void open_load_game() override;

    /// Opens the options screen, remembering the screen it returns to.
    void open_options() override;

    /// Opens HELP.GUI; a failure is shown on the status line.
    void open_help();

    /// Opens CDCHECK.GUI; a failure is shown on the status line.
    void show_cd_check();

    /// Game data a frontend entry needs, when it is absent.
    enum class MissingContent : uint8_t {
        skirmish_maps,    ///< maps with a multiplayer schema, for SINGLE.GUI's Skirmish
        multiplayer_maps, ///< maps with a multiplayer schema, for the main menu's MULTI
        further_missions, ///< the missions after a final campaign victory, with no ending movie
    };

    /// Tells the player that the game data lacks what an entry needs: the data's DEMOMSG.GUI
    /// notice when it can be drawn, else a message box.
    ///
    /// The notice's OK returns to the main menu, as in the Total Annihilation demo (1997); its
    /// website button, captioned with the address (web_link_caption()), opens
    /// project_website_address and closes it.
    ///
    /// @param missing what the game data lacks
    void show_missing_content(MissingContent missing);

    /// Returns to the main menu when the OK of a notice asked for it.
    void run_pending_notice_return();

    /// Opens a web address through the web link hooks.
    ///
    /// @param address address to open
    void open_web_link(std::string_view address);

    /// Chooses the web link hooks: the browser, or in a run nobody watches (an unattended run,
    /// CI, or the dummy or offscreen video driver) a record of the requests.
    void choose_web_links();

    Options options_;
    oa::AssetStore& assets_;
    Extension extension_;
    frontend::State state_{};
    frontend::StateHandler frontend_states_{}; // the extension's states; empty without one
    menu::Environment environment_{};
    oa::audio::game_audio::Registry audio_registry_;
    oa::audio::game_audio::UnitSoundCatalog unit_sound_catalog_;
    // SDL outlives audio_player_: reverse member destruction closes streams
    // before the final SDL_Quit.
    SdlObjects sdl_;
    oa::ui::display_layout::MatchLayout match_layout_{};
    int output_texture_w_ = 0;
    int output_texture_h_ = 0;
    oa::audio::game_audio::SdlWavPlayer audio_player_;
    NativeOfflineServices offline_services_;
    NativeEffectBoundary effect_boundary_;
    oa::sim::unit_effects::OfflineEffects offline_effects_;
    std::unique_ptr<oa::sim::match_runtime::Match> match_;
    init::PlayerStorage player_storage_{};
    init::Preferences preferences_{};
    entry::SkirmishSettings skirmish_settings_{};
    skirmish::UiState skirmish_ui_{};
    TypedKeyHook typed_key_hook_ = TypedKeyHook::none;
    skirmish::TypedKeys typed_keys_{};
    init::MapListState map_list_state_{};
    std::map<uintptr_t, int32_t> map_list_objects_;
    std::map<std::string, std::string> preference_values_;
    fs::path preference_path_;
    std::string first_map_name_;
    std::vector<std::string> eligible_map_names_;
    std::optional<oa::formats::ota::MapMetadata> selected_map_metadata_;
    std::optional<oa::data::unit_definitions::TdfDocument> selected_ota_document_;
    // [Schema N] the session object selected for the match being built.
    std::string session_schema_;
    std::optional<oa::formats::tnt::Map> selected_tnt_;
    std::string selected_map_name_runtime_;
    std::vector<oa::sim::unit_spawn::StartMarker> selected_start_markers_;
    std::vector<oa::sim::unit_spawn::LoadedType> loaded_commander_types_;
    std::vector<oa::data::unit_definitions::UnitDefinition> unit_definitions_;
    std::vector<oa::data::unit_definitions::RuntimeDefinitionMetadata> runtime_definition_metadata_;
    std::vector<oa::sim::match_runtime::RuntimeTypeFields> offline_type_fields_;
    std::optional<oa::sim::map_runtime::PreparedMap> prepared_map_;
    std::vector<oa::sim::visibility_state::AltitudeCell> altitude_cells_;
    std::vector<oa::sim::visibility_state::AltitudeSightPattern> altitude_patterns_;
    std::vector<oa::sim::spatial_state::Plot> collision_plots_;
    oa::PaletteBytes match_palette_{};
    oa::Image match_chrome_{};
    std::optional<renderer::ScreenResources> match_hud_;
    std::string match_hud_panel_; // GUI file of match_hud_, as its loader named it
    std::vector<MatchGadgetState> match_hud_states_; // one per match_hud_ gadget
    std::optional<oa::formats::fnt::Font> match_small_font_;
    // The fonts start-up loads for the whole run, COMIX and smlfont, kept here
    // in place of Game.common_fonts.
    std::optional<oa::formats::fnt::Font> message_font_;
    std::optional<oa::formats::fnt::Font> small_font_;

    // The texts of gamedata/translate.tdf in the game's language, freed with
    // the runtime.
    struct Translations {
        oa::data::defs::LocaleTable table{};

        /// Starts with no language loaded.
        Translations() noexcept { oa::data::defs::locale_table_init(&table); }

        /// Frees the loaded texts.
        ~Translations() { oa::data::defs::locale_table_free(&table); }

        Translations(const Translations&) = delete;
        Translations& operator=(const Translations&) = delete;
    };

    Translations translations_;
    SideHud side_hud_{};
    HudRect radar_picture_{};
    int radar_map_w_ = 0;
    std::vector<uint8_t> radar_explored_{};

    // Radar surfaces over the match's Game block, built by the radar
    // picture set-up when a match is first drawn, and the 0x7e-square well
    // the radar picture fills (runtime_radar.cpp).
    struct RadarState {
        const oa::World* built_for = nullptr;
        oa::present::world_renderer::RadarSurfaces surfaces{};
        oa::Surface* well = nullptr;
        std::array<uint8_t, 256> gray_table{};
        std::vector<uint16_t> sight_bits{};
        std::vector<uint8_t> coverage{};
        std::vector<oa::RadarHotUnit> hot_units{};           // Game.hot_radar_units
        oa::present::GafSprites fx{};                        // anims/FX.GAF, kept across matches
        oa::present::world_renderer::RadarSprites sprites{}; // sequences of fx
        uint32_t tick = 0;                                   // simulation tick last composed
        uint32_t viewer_deadline = 0; // viewpoint Player.next_economy_tick then
        bool reset_sight = false;     // sight reset pending

        /// Frees the radar surfaces and well.
        void release();

        /// Frees the radar surfaces and well, as release() does.
        ~RadarState();
    };

    RadarState radar_state_{};

    /// Binds the runtime's view into the Game block, where the match passes read it.
    ///
    /// The camera, the view size in cells and the battlefield rectangle the load
    /// screen sets on the 640x480 screen. As the camera setter does, a moved view
    /// asks the radar for a redraw once its rectangle is refreshed.
    void bind_match_view();

    /// Builds the radar surfaces over the match's Game block the first time the match is drawn (at
    /// load).
    ///
    /// Then fills the mapped image and composes the final one as the game does
    /// when it starts.
    void ensure_radar_surfaces();

    /// Loads the radlogo, radlogohigh and nuclogo sequences of FX.GAF, which the HUD resources look
    /// up by name when a game loads; the file is read once.
    void load_radar_sprites();

    /// Refreshes the mapped radar image from the viewer's explored cells and current coverage
    /// through the radar fill.
    ///
    /// The radar fill walks a 32-pixel cell grid; the match sight grid is
    /// resampled onto it. Disabled mapping or line of sight reads as fully
    /// explored or seen.
    void refresh_radar_mapped();

    /// Composes the final radar image over the match, with the viewer's sight answering for shots.
    void compose_radar_final();

    /// Runs the viewpoint slot's radar passes for the simulation ticks run since the last frame, in
    /// their per-tick order.
    ///
    /// The slot tick composes the final image every tick and refreshes the mapped
    /// image after composing when the slot's deadline comes due, and the frame
    /// loop steps the blink clock last. The ticks between two frames are not
    /// kept, so one compose over the newest state stands for them, and a deadline
    /// passed before the newest tick is refreshed ahead of it.
    void run_radar_ticks();
    uint32_t status_panel_next_step_ms_ = 0; // the strip's step timer
    std::optional<oa::formats::gaf::RenderedFrame> status_lightbar_{};
    bool status_lightbar_loaded_ = false;
    std::string status_label_;           // last label translated for the status strip
    std::string unit_panel_word_{};      // last word translated for the unit panel
    uint32_t options_lightbar_sounds_{}; // "Options" sounds the lightbar sweeps have played

    /// Draws the status strip that slides up from the bottom of the battlefield while Space is
    /// held: the lightbar and the time, unit and speed readouts.
    void draw_status_panel();
    int radar_map_h_ = 0;
    std::optional<oa::present::world_renderer::TextureCatalog> texture_catalog_;
    std::shared_ptr<MatchModels> match_models_; // 3DO renderer state, runtime_match_render.cpp
    oa::formats::gaf::Archive match_fx_{};
    oa::formats::gaf::Archive match_fog_{};
    std::map<std::string, oa::formats::gaf::Archive> match_explosion_gafs_{};

    struct MatchGafFeatureAnim {
        std::vector<oa::formats::gaf::RenderedFrame> frames;
        std::vector<uint16_t> durations;
        uint16_t frame{};
        uint16_t remaining{};
        bool loop = true;
        bool animating{};
    };

    struct MatchGafFeatureDraw {
        std::size_t anim{};
        oa::formats::objects3d::FixedVector3 position{};
        int32_t cell_x{};
        int32_t cell_z{};
        uint16_t feature_index = 0xffff; // MapPlot.feature at load; 0xffff is not a map feature
        std::size_t shadow_anim = static_cast<std::size_t>(-1); // seqnameshad, none: -1
    };

    // GAF archives and 3DO models the match's FeatureDef table references;
    // a FeatureDefHost ref is the table index + 1. The rendered frames of a
    // sequence are made when a feature first draws it.
    struct FeatureAssets {
        std::vector<std::unique_ptr<oa::formats::gaf::Archive>> archives;
        std::vector<oa::formats::gaf::Sequence*> sequences;
        std::vector<std::vector<oa::formats::gaf::RenderedFrame>> rendered;
        std::vector<std::shared_ptr<const oa::formats::objects3d::Model>> models;
    };

    FeatureAssets feature_assets_;

    /// Returns a rendered frame of a feature sequence reference (feature_assets_), rendering the
    /// sequence on first use.
    ///
    /// @param sequence sequence reference, from 1
    /// @param frame frame index
    /// @return the frame, or null for no sequence, a frame past the end or one that
    ///     did not render
    const oa::formats::gaf::RenderedFrame*
    feature_sequence_image(oa_ref32 sequence, uint16_t frame);

    std::vector<MatchFeatureDraw> match_features_; // each with its draw state
    std::vector<MatchGafFeatureDraw> match_gaf_features_;
    std::vector<MatchGafFeatureAnim> match_gaf_anims_;
    std::map<std::string, std::size_t> match_gaf_anim_index_;
    uint32_t gaf_feature_anim_tick_{};
    std::vector<oa::sim::map_runtime::NamedFeature> feature_catalog_;
    oa::sim::map_runtime::FeatureDefTable feature_table_; // Game.feature_defs source for the match
    // The campaign schema's [features] entries the match places.
    std::vector<oa::sim::feature_runtime::FeaturePlacement> mission_features_;

    struct ReclaimCheck {
        uint16_t builder{};
        int32_t cell_x{}, cell_z{};
        std::size_t plot{};
        uint16_t feature_word{};
        float feature_metal{};
        double produced_before{};
        double produced_last{};
        float store_before{};
        bool credited{};
    };

    std::optional<ReclaimCheck> reclaim_check_;
    std::size_t wrecks_drawn_{};
    int panel_top_width_ = kBattlefieldWidth;
    int32_t configured_map_metal_ = 0;
    std::vector<oa::sim::unit_spawn::Type> spawn_types_;
    std::vector<std::string> spawn_type_names_;
    // Build page button captions by gadget index (the queued counts).
    std::vector<std::string> build_captions_;
    oa::data::defs::SideTable side_table_{};

    // Game.unit_defs: the unit table the FBI loader fills, with the yard maps,
    // build lists, category masks and download menus its records point at,
    // and the movement classes and sound categories its names resolve against.
    struct UnitTable {
        oa::data::defs::UnitDefTables tables{};
        oa::data::defs::MoveClassTable move_classes{};
        oa::data::defs::SoundCategoryTable sound_categories{};

        /// Initializes the unit definition and movement class tables.
        UnitTable() noexcept {
            oa::data::defs::unit_def_tables_init(&tables);
            oa::data::defs::move_class_table_init(&move_classes);
        }

        /// Frees the unit definition tables and the sound category table.
        ~UnitTable() {
            oa::data::defs::unit_def_tables_free(&tables);
            oa::data::defs::sound_category_table_free(&sound_categories);
        }

        UnitTable(const UnitTable&) = delete;
        UnitTable& operator=(const UnitTable&) = delete;
    };

    UnitTable unit_table_;
    oa::sim::combat_state::WeaponRegistry weapon_registry_;
    oa::data::unit_definitions::ResolvedCategoryRegistry category_registry_;
    map_modal::ModalState map_modal_{};
    std::vector<std::string> bound_map_names_;
    std::string pending_parent_map_name_;
    std::vector<uint8_t> preview_rgb_;
    std::array<uint8_t, 3> preview_clear_rgb_{}; // palette index 0 behind the fitted map
    std::size_t preview_width_ = 0;
    std::size_t preview_height_ = 0;
    std::size_t preview_source_width_ = 0;
    std::size_t preview_source_height_ = 0;
    int32_t preview_destination_x_ = 0;
    int32_t preview_destination_y_ = 0;
    int32_t preview_destination_width_ = 0;
    int32_t preview_destination_height_ = 0;
    map_modal::PictureHandle map_picture_{};
    uintptr_t next_picture_handle_ = 0;
    int16_t modal_map_index_ = 0;
    uintptr_t next_map_list_handle_ = 0;
    int32_t map_list_mode_ = 0; // map list mode of the last map selection
    int32_t new_game_selection_ = 0;
    int32_t frontend_mode_ = frontend::mode_id::frontend;
    // The 3D sound switch: "Sound Mode" 2, the sound screen's MODE and
    // "Sound3D" set it; play_sound_at places clips by it.
    int32_t sound_spatial_ = 0;
    // The novelty voice "Sing" toggles: unit speech plays honk
    // and sing while it is on.
    int32_t novelty_voice_ = 0;
    uint32_t mixing_buffers_ = 0;
    uint32_t wave_volume_ = 65535;
    uint32_t cd_volume_ = 0;
    std::map<std::string, std::size_t> widget_gaf_frames_;
    std::map<std::string, std::size_t> widget_text_stages_;
    std::map<std::string, renderer::SpriteOverride> widget_sprites_;
    uint32_t menu_flags_ = 0;
    int32_t event_button_ = 1;
    bool input_enabled_ = true;
    bool preferences_dirty_ = false;
    renderer::ScreenResources resources_;
    // The frontend screen's scroll bars and lists, bound to resources_'s
    // layout, and the gadgets and records they were bound over.
    renderer::LayoutScrolls frontend_scrolls_;
    const oa::ui::gui_layout::Gadget* frontend_scrolls_layout_ = nullptr;
    std::size_t frontend_scrolls_count_ = 0;
    bool frontend_scrolls_own_art_ = false; // resources_.sprites is the panel's own GAF
    std::vector<uint8_t> frontend_gray_table_;
    // The match HUD panel's scroll bars and lists, bound to match_hud_'s
    // layout, and the panel's own GAF.
    renderer::LayoutScrolls hud_scrolls_;
    const oa::ui::gui_layout::Gadget* hud_scrolls_layout_ = nullptr;
    std::size_t hud_scrolls_count_ = 0;
    oa::formats::gaf::Archive hud_own_art_;
    std::vector<uint8_t> hud_gray_table_;
    // The HUD layer as the preferences' sub-panel was drawn on it, before the
    // bottom bar in a window taller than the chrome took back its own rows.
    renderer::Surface preferences_hud_;
    NamedBackgrounds named_backgrounds_;
    // The options lightbar's FLIPSURFACE (the frame below) and BKUPSURFACE.
    static constexpr oa_ref32 kOptionsFlipSurface = 1;
    static constexpr oa_ref32 kOptionsBackupSurface = 2;
    renderer::Surface options_flip_;
    std::vector<uint8_t> options_backup_;
    renderer::Surface surface_;
    renderer::MenuSparks menu_sparks_{};
    // A package overlay stands over the main menu, so it takes the
    // MAINMENU.GUI laid out under that overlay.
    bool main_menu_overlay_ = false;
    // Replaces the frontend clock while a headless check steps it.
    std::optional<uint32_t> fake_frontend_tick_;
    renderer::Surface modal_parent_surface_;
    // The frame the load and save dialogs are drawn over, the panel below
    // already darkened, and the palette its pixels come from.
    renderer::Surface load_game_parent_;
    oa::PaletteBytes load_game_palette_{};
    // The paused match frame the in-game briefing is drawn over.
    renderer::Surface in_game_briefing_parent_;
    // Set while a parent frame is rebuilt for a dialog: no software cursor.
    bool frame_without_cursor_ = false;
    Screen screen_ = Screen::main_menu;
    std::optional<std::size_t> hovered_;
    int32_t selected_ = -1;
    bool exit_requested_ = false;
    // The status run() returns after the application loop (ScreenServices::quit).
    int exit_status_{};
    bool application_active_ = true;
    uint32_t last_stream_sweep_ms_ = 0;
    oa::platform::MemoryStatusReport memory_report_{};
    bool match_paused_ = false;
    bool match_finished_ = false;
    // A paused menu was on the HUD as the match finished (pause_menu_shown).
    bool outcome_over_menu_ = false;
    // The last match frame drawn was of the finished match, its outcome title included.
    bool outcome_frame_drawn_ = false;
    bool campaign_mission_ = false;
    // Whether the chat line may run cheats, set by each mission start and
    // kept until the next.
    bool session_cheats_allowed_ = false;
    std::vector<std::string> campaign_files_{};
    std::vector<std::string> campaign_labels_{};
    std::vector<std::string> campaign_mission_files_{};
    std::vector<std::string> campaign_mission_labels_{};
    std::vector<std::string> end_mission_rows_{}; // ENDMSN Missions list, with result markers
    std::size_t selected_campaign_index_ = 0;
    std::size_t selected_mission_index_ = 0;
    std::size_t campaign_first_visible_ = 0;
    std::size_t campaign_mission_first_visible_ = 0;
    Screen briefing_parent_ = Screen::new_campaign;
    bool briefing_from_pause_ = false;
    std::string briefing_text_{};
    std::vector<std::string> briefing_lines_{};
    std::size_t briefing_first_visible_ = 0;
    // The FNT fonts of a briefing's TextRegion and of its MOREBAR caption
    // (load_briefing_fonts()); empty where the GUI font stands in.
    std::optional<oa::formats::fnt::Font> briefing_text_font_{};
    std::optional<oa::formats::fnt::Font> briefing_more_font_{};
    // clock_milliseconds() when the briefing's page was last laid out; its
    // highlighted words flash from then.
    uint32_t briefing_page_ms_ = 0;
    sim::scenario::Outcome match_outcome_{sim::scenario::Outcome::ongoing};
    oa::formats::gaf::Archive match_titles_{};
    // textures/logos.gaf and its 32x32 logos, kept here in place of their
    // entries in Game.sprite_and_effect_tables.
    oa::formats::gaf::Archive logo_textures_{};
    const oa::formats::gaf::Sequence* logo_sequence_ = nullptr;
    Screen options_parent_ = Screen::main_menu;
    int squad_double_tap_ = 0;
    oa::ui::hud::KillBoard kill_board_{};
    std::optional<oa::ui::gui_layout::Layout> talk_layout_{};
    oa::formats::gaf::Archive match_talk_{};
    int32_t match_camera_x_ = 0, match_camera_z_ = 0;
    uint32_t match_camera_flags_ = 0;
    float match_zoom_ = kDefaultBattlefieldZoom;
    float match_zoom_target_ = kDefaultBattlefieldZoom;
    bool zoom_anchored_ = false;
    uint32_t zoom_anchor_map_x_{};
    uint32_t zoom_anchor_map_y_{};
    int zoom_anchor_sx_{};
    int zoom_anchor_sy_{};
    std::chrono::steady_clock::time_point zoom_clock_{};
    bool zoom_clock_valid_ = false;
    oa::ui::hud::ScrollClock scroll_clock_{};
    double scroll_zoom_carry_ = 0.0;

    // The drag box kept while the left button is held on the
    // battlefield (Game.drag_start and drag_end): whole map pixels x,
    // height and z of the terrain under the pointer, and the game tick
    // of the press.
    struct MatchDragBox {
        std::array<int32_t, 3> start{};
        std::array<int32_t, 3> end{};
        uint32_t pressed_tick{};
    };

    std::optional<MatchDragBox> match_drag_{};
    bool match_tracking_ = false;
    uint16_t tracked_match_unit_ = 0;
    // The on-screen unit list (Game.hot_units), one id per unit slot at most;
    // Game.hot_unit_count says how many the last drawn frame listed.
    std::vector<uint16_t> on_screen_units_{};

    // The unit info panel F1 opens: UNITINFOx.GUI, its controls moved to the
    // screen with the added statistic labels, drawn once as it opens.
    struct UnitInfoPanel {
        std::optional<renderer::ScreenResources> screen{};
        renderer::Surface frame{};               // the panel drawn over black, 640x480
        oa::ui::gui_layout::CommonFields root{}; // the panel's rectangle on screen
        std::string picture_path{};              // unitpics\<name>.PCX, empty once released
    };

    std::optional<UnitInfoPanel> unit_info_panel_{};
    bool chat_composing_ = false;
    std::string chat_buffer_{};
    std::shared_ptr<MatchConsole> console_;
    // What the team panels tell the other players' machines, filled by the
    // extension (Extension::team_panel_host) as each match starts.
    oa::ui::hud::TeamPanelHost team_panel_host_{};
    // The label the running match's return names, zero-terminated and empty
    // for none; asked as each match starts (take_return_label) for its
    // in-game menus and end-of-game screen.
    std::array<char, oa::ui::frontend::kReturnLabelBytes> return_label_{};
    // The nickname and game name the extension gave at the last preferences
    // load (Extension::frontend_entry); empty for none.
    std::string entry_nickname_{};
    std::string entry_game_name_{};
    // Model light direction once "Light" set it; later matches keep it.
    std::optional<std::array<float, 3>> model_light_;
    // Per-channel table of the display gamma for the RGB layers.
    std::array<uint8_t, 256> gamma_table_{};
    bool gamma_identity_ = true;
    std::vector<uint8_t> debug_font_; // smlfont.FNT as loaded, for the debug grid
    oa::present::world_renderer::FogTileSet fog_tiles_{};
    oa::present::world_renderer::FogShading fog_shading_{};
    bool fog_frames_ready_ = false;

    // Per TNT feature index of the current map: drawn only in line of sight,
    // and the footprint whose far corner may be the one in sight.
    struct FeatureFogRules {
        const oa::formats::tnt::Map* map = nullptr;
        std::vector<uint8_t> hidden_under_gray{};
        std::vector<std::array<int16_t, 2>> footprints{};
    };

    FeatureFogRules feature_fog_rules_{};
    oa::present::world_renderer::Surface match_terrain_cache_{};
    uint32_t terrain_cache_cam_x_ = ~0u;
    uint32_t terrain_cache_cam_y_ = ~0u;
    float terrain_cache_zoom_ = -1.0F;
    // View of the last box-filtered fill of match_terrain_cache_ (runtime_terrain_filter.cpp).
    uint32_t terrain_filtered_cam_x_ = ~0u;
    uint32_t terrain_filtered_cam_y_ = ~0u;
    float terrain_filtered_zoom_ = -1.0F;
    std::vector<uint8_t> match_fog_grid_{};
    std::vector<int> scale_src_x_{};
    renderer::Surface* overlay_target_ = nullptr;
    bool hud_source_space_ = false;
    oa::ui::display_layout::Point paint_origin_{}; // canvas position of the paint target's (0,0)
    bool match_use_layers_ = false;
    renderer::Surface* capture_frame_ = nullptr;  // receives the next presented frame
    std::unique_ptr<VideoCapture> video_capture_; // --capture-video, while it runs
    std::vector<uint8_t> match_dialog_rgba_;      // frontend dialogs over the match canvas
    SDL_Texture* match_dialog_tex_ = nullptr;
    int match_dialog_tex_w_ = 0, match_dialog_tex_h_ = 0;
    renderer::Surface match_hud_cpu_{};
    renderer::Surface match_world_cpu_{};
    SDL_Texture* match_hud_tex_ = nullptr;
    SDL_Texture* match_world_tex_ = nullptr;
    SDL_Texture* match_cursor_tex_ = nullptr;
    int match_hud_tex_w_ = 0, match_hud_tex_h_ = 0;
    int match_world_tex_w_ = 0, match_world_tex_h_ = 0;
    int match_cursor_tex_w_ = 0, match_cursor_tex_h_ = 0;

    /// Accumulated wall time per frame phase, in nanoseconds, for --benchmark.
    struct PhaseTimes {
        int64_t simulation = 0;
        int64_t compose = 0;
        int64_t fog = 0; // within compose
        int64_t hud = 0;
        int64_t upload = 0;
        int64_t present = 0;
    };

    PhaseTimes phase_times_{};

    struct {
        int x = 0;
        int y = 0;
        int w = 100000;
        int h = 100000;
    } world_pixel_clip_{};

    bool altitude_sight_blocked_ = false;
    bool match_tick_blocked_ = false;
    std::string last_tick_error_{};
    uint32_t tick_error_repeats_{};
    uint8_t match_local_player_ = 0;
    uint16_t selected_match_unit_ = 0;
    uint16_t hovered_match_unit_ = 0;
    float match_pointer_x_ = 0;
    float match_pointer_y_ = 0;
    float pointer_x_ = 0;
    float pointer_y_ = 0;
    oa::formats::gaf::Archive cursor_gaf_{};
    std::set<std::string> missing_gaf_paths_{};
    // The GUI context's cursor and pointer, with the picture the software
    // cursor shows and the cursor table index of the animation shown; the
    // runtime keeps them in place of the Game block's gui_context_block and
    // cursor_animation_block.
    oa::ui::gui_input::GadgetPanel gui_context_;
    const oa::formats::gaf::Frame* cursor_image_ = nullptr;
    uint8_t cursor_index_ = 0xff;
    bool cursors_loaded_ = false;
    bool menu_music_playing_ = false;
    std::unique_ptr<MusicHost, void (*)(MusicHost*) noexcept> music_{nullptr, destroy_music_host};
#ifdef OA_RUNTIME_EXTENSION_MEMBERS
    // The members of the one extension that adds any, from the header its
    // project names in OA_RUNTIME_EXTENSION_MEMBERS; frozen, and only to
    // shrink (src/app/README.md). Declared after match_, they go
    // before the match they bind.
#include OA_RUNTIME_EXTENSION_MEMBERS
#endif
    std::unique_ptr<SaveLoadState, void (*)(SaveLoadState*) noexcept> saveload_{
        nullptr, destroy_saveload_state
    };
    std::unique_ptr<EndgameState, void (*)(EndgameState*) noexcept> endgame_{
        nullptr, destroy_endgame_state
    };
    SessionDisplay display_{};
    CapturedFrame captured_frame_{};
    IndexedOutput indexed_output_{};
    oa::present::SurfaceBuffer loading_background_{}; // Loadgame2bg.pcx
    oa::present::GafSprites gui_font_{};              // hattfont12.gaf, the GUI context's font
    oa::present::GafSprites loading_gui_{};           // commongui.gaf
    oa::Sprite* loading_lightbar_ = nullptr;          // LIGHTBAR in loading_gui_
    oa::PaletteBytes loading_palette_{}; // PALETTE.PAL: the display palette while loading
    // guipal -> PALETTE.PAL nearest-colour remap (Game UI colour table).
    oa::PaletteMap ui_colors_{};
    bool ui_colors_ready_ = false;
    std::array<uint8_t, 6> load_progress_{};
    std::array<uint8_t, 6> loading_flash_{};
    MatchCommand match_command_ = MatchCommand::none;
    // Names play_match_interface_sound was given while a check listens.
    std::vector<std::string>* heard_interface_sounds_ = nullptr;
    int match_build_page_ = 0;
    uint16_t pending_build_type_ = 0;
    // Cursor GAF frames the order overlays draw, rendered once each.
    std::map<const oa::formats::gaf::Frame*, oa::formats::gaf::RenderedFrame>
        overlay_sprite_frames_{};
    oa::base::game_loop::Timing match_timing_{};
    uintptr_t next_document_ = 0;
    std::string status_;
    // Opens web addresses: the browser in a watched run, else a record of the requests.
    WebLinkHooks web_links_{};
    std::vector<std::string> web_link_requests_;
    // Set by a notice's OK; the main menu replaces the screen after the frame's input.
    bool notice_returns_to_main_menu_ = false;
    // A final campaign victory without ending movies shows its notice over the main menu.
    bool ending_notice_pending_ = false;
    // The frontend's Game block unless the extension keeps its own.
    std::unique_ptr<oa::Game> frontend_game_;
};

// Every unit that includes this header refers to the marker of the Runtime
// layout it sees, with or without an extension's members, and only the one
// oa-game is built with is defined (extension_members.cpp): a unit that
// sees the other layout fails to link instead of misreading Runtime. Where
// _MSC_VER is defined the compiler may drop an unused reference, so the
// linker compares a named value instead.
#ifdef OA_RUNTIME_EXTENSION_MEMBERS
#define OA_RUNTIME_LAYOUT_MARKER runtime_layout_with_extension_members
#if defined(_MSC_VER)
#pragma detect_mismatch("oa_runtime_layout", "with extension members")
#endif
#else
#define OA_RUNTIME_LAYOUT_MARKER runtime_layout_without_extension_members
#if defined(_MSC_VER)
#pragma detect_mismatch("oa_runtime_layout", "without extension members")
#endif
#endif
extern const uint8_t OA_RUNTIME_LAYOUT_MARKER;
#if !defined(_MSC_VER)
[[gnu::used]] static const uint8_t* const runtime_layout_seen = &OA_RUNTIME_LAYOUT_MARKER;
#endif

/// Converts a console or dialog path to a host path under the save root.
///
/// '\' separators become '/', and a leading savegame directory takes the load
/// dialog's spelling.
///
/// @param path path relative to the save root
/// @return the relative host path
[[nodiscard]] fs::path save_relative_path(std::string_view path);

} // namespace oa::app
