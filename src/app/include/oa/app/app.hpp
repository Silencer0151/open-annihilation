// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Shared application options, screen identifiers and layout constants for oa-game.
#pragma once

#include "oa/app/command_line.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_state/game_entry.hpp"
#include "oa/ui/frontend_state/initialization.hpp"
#include "oa/ui/frontend_state/main_menu.hpp"
#include "oa/ui/frontend_state/map_selection.hpp"
#include "oa/sim/scenario/outcome.hpp"
#include "oa/ui/frontend_state/skirmish_ui.hpp"
#include "oa/ui/screen_registry.hpp"
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace oa::app {

namespace fs = std::filesystem;
namespace renderer = oa::ui::frontend_renderer;
namespace frontend = oa::ui::frontend_state;
namespace menu = oa::ui::frontend_state::main_menu;
namespace entry = oa::ui::frontend_state::game_entry;
namespace init = oa::ui::frontend_state::initialization;
namespace skirmish = oa::ui::frontend_state::skirmish_ui;
namespace map_modal = oa::ui::frontend_state::map_selection;
namespace scenario = oa::sim::scenario;

constexpr int kCanvasWidth = 640;
constexpr int kCanvasHeight = 480;
constexpr int kBattlefieldLeft = 128;
constexpr int kBattlefieldTop = 32;
constexpr int kBattlefieldBottom = 32;
constexpr int kBattlefieldWidth = kCanvasWidth - kBattlefieldLeft;
constexpr int kBattlefieldHeight = kCanvasHeight - kBattlefieldTop - kBattlefieldBottom;
constexpr std::size_t kDefaultCampaignTicks = 3000; // headless --mission without --match-ticks
constexpr uint16_t kSkirmishUnitsPerPlayer = 250;
constexpr float kMinBattlefieldZoom = 0.5F;
constexpr float kMaxBattlefieldZoom = 4.0F;
constexpr float kDefaultBattlefieldZoom = 1.0F;
constexpr float kZoomWheelFactor = 1.15F;
constexpr float kZoomLerpHz = 12.0F;
constexpr uint8_t kPaletteGreen = 250;   // PALETTE.PAL RGB(0,255,0)
constexpr std::size_t kUiColorText = 15; // Game.ui_colors slot of message and clock text
constexpr int kDefaultWindowWidth = 1920;
constexpr int kDefaultWindowHeight = 1080;
// Headless and composed-frame checks run on a clock that advances only with
// the simulation (at 30 ticks a second) and seed the match with rand()'s
// initial seed, so two runs draw the same frames.
constexpr uint32_t kFixedClockMsPerTick = 1000 / 30;
constexpr uint32_t kFixedRandomSeed = 1;
// The most frames a second the application loop draws unless --max-fps says
// otherwise; the simulation keeps its 30 ticks a second whatever the rate.
constexpr uint32_t kDefaultMaxFramesPerSecond = 120;
// The lowest limit --max-fps takes: the simulation's 30 ticks a second, so
// that each tick is drawn before the next runs. Above it, frames on time are
// less than a clock unit apart, and no frame's clock step at normal speed runs
// two ticks, even as the match clock rounds to whole milliseconds. At it, each
// frame is due at the middle of a clock unit (frame_pacing::end_paced_frame)
// and runs exactly one tick at normal speed, which it shows whole. Below it,
// some frames would run two.
constexpr uint32_t kLowestMaxFramesPerSecond = 30;
// The highest --max-fps and --frame-rate take.
constexpr uint32_t kHighestFrameRate = 1000;
constexpr uintptr_t kFrontendMenuHandle = 1;
constexpr uintptr_t kMessageTargetHandle = 1;

enum class Screen : ScreenId {
    main_menu,
    single_player,
    skirmish,
    map_selection,
    loading,
    match,
    options,
    sound,
    visuals,
    speeds,
    music,
    new_campaign,
    any_mission,
    load_game,
    campaign_end,
    briefing
};

/// Returns the registry id of a built-in screen.
///
/// @param screen built-in screen
/// @return the ScreenId the screen registers under, the enumerator's value
[[nodiscard]] constexpr ScreenId screen_id(Screen screen) {
    return static_cast<ScreenId>(screen);
}

// The scripted showcases --showcase plays (runtime_showcase.cpp).
enum class Showcase {
    none,
    // From the main menu into the first Arm mission, whose units are sent to
    // the Galactic Gate, and through its victory to the score screen.
    arm_first_mission,
    // From the main menu into a two-player skirmish whose armies fight for a
    // minute, reporting the ticks and frames a second it played at.
    skirmish_battle
};

enum class MatchCommand {
    none,
    move,
    attack,
    dgun,
    build,
    patrol,
    repair,
    reclaim,
    capture,
    load,
    unload,
    guard
};

struct Options {
    // Empty until main() resolves it when --game-dir is not passed.
    fs::path game_dir;
    // Opens the folder dialog even when a usable folder is remembered.
    bool choose_game_dir = false;
    // The folder the dialog chose, which the runtime stores in the
    // preferences; empty when it came from elsewhere. It differs from
    // game_dir when it held the installer of the Total Annihilation demo
    // (1997), whose unpacked archive's folder is game_dir.
    fs::path remember_game_dir;
    // Where data the engine unpacks is kept (--data-dir); unset: the
    // platform's per-user data folder.
    std::optional<fs::path> data_dir;
    std::vector<fs::path> archives;
    fs::path snapshot;
    std::optional<fs::path> preferences_file;
    std::optional<std::size_t> frame_limit;
    std::optional<std::size_t> benchmark_frames;
    // Headless in-match run: ticks to simulate, output size, per-side army.
    std::optional<std::size_t> match_ticks;
    // Headless campaign run: the campaign's name and the mission index that
    // --headless-check starts through the briefing and ticks for match_ticks.
    std::string campaign;
    std::optional<std::size_t> campaign_mission;
    bool campaign_past_outcome = false;
    // Restarts the campaign mission from the exit menu at this tick and
    // checks the restart creates the units the first start did.
    std::optional<std::size_t> campaign_restart_tick;
    int match_width = 640;
    int match_height = 480;
    // --resolution was given: a run with a window opens it at match_width by
    // match_height instead of kDefaultWindowWidth by kDefaultWindowHeight.
    bool window_resolution = false;
    float match_zoom = kDefaultBattlefieldZoom;
    std::size_t combat_units = 0;
    // --busy-combat: the --combat armies also bring missile trucks, the
    // local player a kbot lab building peewees, an air transport loading a
    // peewee, and its army starts selected with selection boxes shown, so
    // that the run's frames draw every kind of battlefield draw.
    bool busy_combat = false;
    bool reclaim_check = false;
    // Headless camera placement: map pixel at the view's top-left.
    std::optional<std::pair<int, int>> camera;
    // Headless savegames: save once the tick reaches save_after (to save_file),
    // or start from load_file instead of a fresh skirmish.
    std::optional<std::size_t> save_after;
    fs::path save_file;
    fs::path load_file;
    // A fresh headless savegame run of a skirmish first gives a factory
    // queue, a building, a patrol, a guard and a move. A headless campaign
    // run gives the player's units orders towards the mission's victory, at
    // its start and every 300 of its ticks, and plays on through the end
    // screen and, after a victory, into the next mission; a savegame run of
    // a campaign mission gives those orders once, at its start. Two ticks
    // before the save, a savegame run also sets a feature burning, starts a
    // die and a reclamate sequence and clears a feature away.
    bool give_orders = false;
    bool skip_intro = false;
    bool headless_check = false;
    bool mute = false;
    bool check_navigation = false;
    // Opens HELP.GUI over a live match through the SDL presenter.
    bool check_match_dialogs = false;
    // Opens LOADGAME.GUI from Single Player and, through the SDL presenter,
    // as the save and load dialogs over a paused match, checking where each
    // panel sits, its backdrop against the bitmap in the palette below and
    // the panel it darkens (<report dir>/native-loadsave-*.ppm).
    bool check_load_save = false;
    // Clicks every option of the skirmish, campaign and options setup
    // screens through the SDL presenter and checks each click changes the
    // setting and what the screen shows for it (<stem>-*.ppm beside
    // --snapshot).
    bool check_frontend_controls = false;
    // Drives the scroll bars of the options, the map and mission lists and
    // the match's preferences through the SDL presenter and checks what they draw and
    // set, and where the preferences' sub-panel shows in a window taller
    // than the chrome (<report dir>/native-scroll-bars-*.ppm).
    bool check_scroll_bars = false;
    // Opens the first mission's briefing through NEWGAME.GUI with sound on and
    // checks its narration plays exactly while SHUTUP is on and stops as the
    // briefing is left by its buttons or keys (<stem>-*.ppm beside
    // --snapshot).
    bool check_briefing_narration = false;
    // Checks the presented match frame against the CPU composition.
    bool check_match_layers = false;
    // Clicks the order page's standing order and toggle buttons through the
    // SDL presenter and checks what they show and what the units do.
    bool check_match_orders = false;
    // Gives a Kbot Lab a move through the SDL presenter and checks the unit it
    // builds carries it out.
    bool check_factory_orders = false;
    // Opens a download page and a missile silo's page through the SDL
    // presenter, builds a download unit and queues and removes a missile.
    bool check_download_builds = false;
    // Walks the commander's build pages on windows of several sizes through
    // the SDL presenter and checks each fits the side column.
    bool check_side_column = false;
    // Presses F4 in a skirmish and checks the kills board at the top right.
    bool check_kill_board = false;
    // Sends a construction kbot on PATROL through the SDL presenter with the
    // metal store low and checks it reclaims a feature on its way.
    bool check_patrol_reclaim = false;
    // Moves the pointer of a selected commander over trees, another
    // reclaimable feature and a wreck through the SDL presenter and checks
    // the cursor it shows.
    bool check_reclaim_cursor = false;
    // Drives both interface types' pointer buttons through the SDL presenter:
    // clicks and right presses, shift cancels, radar scrolls, mouse look and a
    // factory build button's right click.
    bool check_pointer_interfaces = false;
    // Clicks MULTI on the main menu through the SDL presenter and checks
    // what it reaches: network play's check of the multiplayer screens (the
    // check_multiplayer_menu hook); over game data with no multiplayer map,
    // the missing-content notice, closed by OK and by Enter.
    bool check_multiplayer_menu = false;
    // Plays the headless skirmish's first ticks in director mode, drawn and
    // again undrawn, and checks the two reach one world, that director frames
    // show the battlefield alone and move by a fraction of a map pixel, and
    // that the match's sounds reach the director's sound hooks. Implies
    // --headless-check and --skip-intro.
    bool check_director_view = false;
    // Renders a small director script over the headless skirmish's first
    // ticks, with encoding off, twice (all of it, then its second chunk
    // alone), and checks the files it writes, that neither the sound of a
    // chunk nor the world depends on the chunks drawn before it, and that
    // the render reaches the world the generator's undrawn replay of the
    // same ticks reaches. Implies --headless-check and --skip-intro.
    bool check_director_render = false;
    // Draws frames between ticks over the headless skirmish's fight and checks
    // that they show units, pieces and projectiles part of the way from one
    // tick to the next, that frames of a whole tick and the world are as
    // without them, and that units move evenly from frame to frame. Implies
    // --headless-check and --skip-intro.
    bool check_interpolation = false;
    // Draws the headless skirmish with the other player taken as another
    // machine's, whose records arrive in bursts, and checks that its units
    // move evenly on their playout, on whole ticks too, that the tracking
    // camera and the pointer's pick follow where they are drawn, and that
    // the local units, the world and, with no such player, every frame are
    // as without the playout. Implies --headless-check and --skip-intro.
    bool check_unit_playout = false;
    // Drives the Open Annihilation settings through the SDL presenter: the OA
    // button on the main menu and in the in-game menu, the dialog over each,
    // its sections, controls, keys, OK, Cancel and Restore defaults, the
    // preferences it writes, and each setting taking effect.
    bool check_engine_settings = false;
    // --generate-script RECORDING: the recording a director script is
    // generated from; empty for none. Implies --headless-check and
    // --skip-intro.
    fs::path generate_script;
    // --render-script FILE: the director script (.oascript) or bundle
    // (.oamovie) to render; empty for none. Implies --headless-check and
    // --skip-intro.
    fs::path render_script;
    // --output PATH: where --generate-script writes (a .oascript or
    // .oamovie) or --render-script renders (a directory); empty for the
    // default beside the input.
    fs::path director_output;
    // --chunks A-B (or A): the chunks --render-script draws and encodes,
    // counted from 0, both included; unset for all.
    std::optional<std::pair<uint32_t, uint32_t>> director_chunks;
    bool trace_input = false;
    bool debug_order_lines = false;
    // The most frames a second the application loop draws (--max-fps); 0
    // for no limit. While nothing moves on its own and no input comes, the
    // loop draws fewer (frame_pacing.hpp).
    uint32_t max_frames_per_second = kDefaultMaxFramesPerSecond;
    // --max-fps was given: its rate holds for the run whatever the settings
    // say, and is never saved.
    bool max_frames_per_second_given = false;
    // --frame-rate FPS: the headless match run (--match-ticks) draws every
    // frame of a loop running at FPS frames a second on a clock that
    // advances a frame at a time, each frame between two ticks as the
    // application loop draws it; unset for a tick at a time.
    std::optional<uint32_t> frame_rate;
    // --frame-log FILE: that run writes one line a frame: the time, the
    // tick, the fraction of a tick shown, the camera and a unit it follows.
    fs::path frame_log;
    // --scroll-camera: that run holds the camera's scroll, as the arrow keys
    // would, sweeping it right and back over the army it starts on.
    bool scroll_camera = false;
    // --march: that run orders the local player's army (--combat) to march
    // south at its start.
    bool march = false;
    // --follow: that run's camera tracks the unit its frame log follows, as
    // the T key does.
    bool follow = false;
    // --frame-clock MS: that run's clock starts MS milliseconds into the
    // steady clock instead of at 0, so that a run can cross the moment the
    // match clock's reading turns over (2^32 milliseconds); unset for 0.
    std::optional<uint64_t> frame_clock_ms;
    // Set by the checks above: no wall-clock input reaches the match or the frame.
    bool fixed_clock = false;
    // Scripted runs (fixed clock, navigation and menu checks, benchmarks,
    // frame limits, snapshots, and the extension's): nobody is there to answer
    // a dialog.
    bool unattended = false;
    // On Windows, a player's run without the -d switch opens its window full
    // screen; unattended runs and video captures keep a window.
    bool start_full_screen = false;
    // Trace stream (oa/sim/match_runtime/match_trace.hpp) of each match the run starts,
    // and its optional per-slot unit dump; a new match rewrites both files.
    fs::path trace_digest;
    fs::path trace_units;
    // Argument for the generator seeder in place of mission start's
    // performance-counter sum, so two runs start from the same state.
    std::optional<uint32_t> seed;
    // Threads the per-row drawing passes run on, the drawing thread
    // included (--draw-threads N, else the OA_DRAW_THREADS environment
    // variable); unset for the job pool's default for the machine. Every
    // count draws the same frames.
    std::optional<uint32_t> draw_threads;
    // The MP4 a video capture of the run makes (video_capture.hpp); empty
    // for none.
    fs::path capture_video;
    // The scripted showcase the run plays in place of a player.
    Showcase showcase = Showcase::none;
    // The game switches: every argument that is not one of the options above.
    oa::app::command_line::Switches launch{};
};

struct Extension;

/// Parses the command line into the run options.
///
/// Long options the engine knows set their fields; any other long option goes
/// to `extension`, and every other argument joins the game switch line, which
/// fills Options::launch. --help and a bare -h print the usage and exit. After
/// the loop the combinations are checked, OA_DEBUG_ORDER_LINES is read,
/// OA_DRAW_THREADS stands in for an absent --draw-threads, and fixed_clock
/// and unattended follow from the checks and runs asked for.
/// Throws std::runtime_error on an unknown option, a missing or malformed
/// value, a refused or over-long switch line, or options that cannot be used
/// together.
///
/// @param argc argument count, the program name included
/// @param argv arguments; argv[0] is skipped
/// @param extension extension that takes the long options, switches and usage
///     text the engine does not know
/// @return the parsed options
[[nodiscard]] Options parse_options(int argc, char** argv, const Extension& extension);

/// Parses a frame or tick count option's value.
///
/// Throws std::runtime_error unless the whole text is a decimal integer from 0
/// through 10'000'000.
///
/// @param text option value
/// @return the count
[[nodiscard]] std::size_t parse_count(std::string_view text);

/// Writes an RGB surface as a binary PPM (P6) file.
///
/// Throws std::runtime_error when the file cannot be created or written.
///
/// @param path file to create or truncate
/// @param surface frame to write, 3 bytes per pixel
void write_ppm(const fs::path& path, const renderer::Surface& surface);

// A rectangle of a composed frame, in canvas pixels, that a headless check
// compares between renders.
struct CanvasRect {
    int x = 0, y = 0, w = 0, h = 0;
};

// Pixels a typed or posted line must change in the frame.
constexpr std::size_t kTextMinPixels = 40;

/// Copies the RGB bytes of a rectangle of a frame, clipped to the frame, row by row.
///
/// @param frame composed RGB frame
/// @param rect canvas rectangle; parts outside the frame are skipped
/// @return 3 bytes per copied pixel, rows top to bottom
[[nodiscard]] std::vector<uint8_t> copy_rect(const renderer::Surface& frame, CanvasRect rect);

/// Counts the pixels whose colour differs between two copies of the same rectangle.
///
/// @param before earlier copy_rect() result
/// @param after later copy_rect() result of the same rectangle
/// @return differing pixels, over the shorter of the two copies
[[nodiscard]] std::size_t
changed_pixels(const std::vector<uint8_t>& before, const std::vector<uint8_t>& after);

} // namespace oa::app
