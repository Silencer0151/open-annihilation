// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// End-of-game screen: score table, outcome background, palette fade and the
// ENDMSN.GUI button set.
#pragma once

#include "oa/data/campaign/campaign_file.hpp"
#include "oa/ui/campaign/frontend_host.hpp"

#include <cstddef>
#include <cstdint>

#include "oa/core/world.h"

namespace oa::formats::fnt {
struct Font;
}

namespace oa::ui::campaign {

inline constexpr std::size_t kScorePlayers = OA_PLAYER_COUNT;
inline constexpr std::size_t kScoreNameBytes = OA_SCORE_NAME_BYTES;
inline constexpr std::size_t kPaletteBytes = 0x400;
inline constexpr int32_t kShadeSteps = 10;
inline constexpr int32_t kShadeBias = 0x1d;

// Columns of Game.scores[].values.
enum ScoreColumn : uint32_t {
    score_kills,
    score_losses,
    score_energy_produced,
    score_metal_produced,
    score_energy_wasted,
    score_metal_wasted,
    score_total,
    score_column_count
};

static_assert(score_column_count == OA_SCORE_COLUMNS);

// Control-name prefixes of the stat-bar columns; a bar is "<prefix><player>".
inline constexpr const char* kScoreColumnNames[score_column_count] = {
    "Kills", "Losses", "EProduced", "MProduced", "EWasted", "MWasted", "Score"
};

/// Fills Game.scores and the column maxima from the Player records and records the campaign mission's W/L.
///
/// Game.victory comes from the outcome flags; a campaign session also stores
/// the mission index and its 'W' or 'L'. The maxima start at 10 for kills and
/// losses and 100 for the rest. Every player that is live and not a watcher,
/// or that ever built a unit, gets an entry: its name (bounded to the 30-byte
/// field and terminated there), kills, losses, resource totals and a score of
/// (tick / 60) * timemul + kills * killmul, at least 0.
///
/// @param[in,out] world World whose Game scores, maxima and results are written.
/// @param campaign Campaign object supplying the OTA killmul and timemul; null
///                 scores with multipliers of 0.
void build_score_summary(World& world, const oa::data::campaign::CampaignFile* campaign);

// One stat bar: the type-13 progress gadget the end screen shows per column.
struct ScoreBar {
    int32_t x{};
    int32_t value{};     // target (the gadget's progress_limit)
    int32_t maximum{};   // column maximum (the gadget's progress_scale)
    float rate{};        // value / 15, at least 1
    int32_t current{};   // shown value, counting up (the gadget's progress_value)
    bool active{};       // shown (the gadget's active byte), set when its column is activated
    bool running{};      // still counting (the gadget's progress_running)
    uint32_t deadline{}; // engine tick of the next count step (the gadget's progress_deadline)
};

struct ScoreRow {
    uint32_t player{};
    int32_t y{};
    int32_t name_x{};
    uint8_t color{}; // PlayerSetupInfo.color: the 32xlogos frame of the "PlayerColor%d" swatch
    ScoreBar bars[score_column_count]{};
};

struct ScoreLayout {
    ScoreRow rows[kScorePlayers]{};
    uint32_t row_count{};
};

inline constexpr int32_t kScoreFirstRowY = 0x5d;
inline constexpr int32_t kScoreRowStep = 0x14;
inline constexpr int32_t kScoreNameX = 0x10;
inline constexpr int32_t kScoreBarWidth = 0x43;
inline constexpr int32_t kScoreBarHeight = 0x12;
inline constexpr int32_t kScoreSwatchWidth = 0x5b;
inline constexpr int32_t kScoreSwatchHeight = 0x15;
// The name label over the swatch: it starts at kScoreNameX and the name is
// centred across this width and cut to it.
inline constexpr int32_t kScoreNameWidth = 0x5a;
// Light-table row the name is drawn through (the label's foreground colour).
inline constexpr uint8_t kScoreNameLight = 0xf;

/// Lays out one row per named score entry: its colour swatch, name and seven stat bars.
///
/// Rows start at y kScoreFirstRowY, kScoreRowStep apart; each bar starts
/// hidden at zero and counts up by a fifteenth of its value (at least 1) per
/// tick once shown. Game.endgame_column restarts at 0.
///
/// @param[in,out] world World holding the score table and the player colours.
/// @param[out] layout Rows laid out.
void layout_score_entries(World& world, ScoreLayout* layout);

/// Activates the stat bar of one column in every row.
///
/// @param[in,out] layout Score rows.
/// @param column ScoreColumn to show; past the last column does nothing.
void activate_score_column(ScoreLayout* layout, uint32_t column);

/// Counts every active bar up by its rate once a tick, as the progress gadget's own step does.
///
/// A bar that passes its value stops at it.
///
/// @param[in,out] layout Score rows.
/// @param tick Current engine tick.
void advance_score_bars(ScoreLayout* layout, uint32_t tick);

/// Tells whether every stat bar is shown and has counted up to its value.
///
/// @param layout Score rows.
/// @return true once every bar is active and full.
bool score_bars_settled(const ScoreLayout* layout);

// Pixels of one drawn stat bar: the raised frame, the inner well, its filled
// part and the value centred on it. Every rectangle is inclusive.
struct ScoreBarDraw {
    int32_t left{}, top{}, right{}, bottom{}; // frame
    int32_t inner_left{}, inner_top{}, inner_right{}, inner_bottom{};
    int32_t fill_right{}; // fill spans inner_left..fill_right
    char label[16]{};
    int32_t label_x{}; // pen column of the value's first glyph
    int32_t label_y{}; // pen row of the value, the top of its line
};

/// Places a stat bar's frame, fill and value text.
///
/// The frame spans kScoreBarWidth + 1 by kScoreBarHeight + 1 pixels from the
/// bar's corner and the well lies two pixels inside it. The fill ends
/// current / maximum * (kScoreBarWidth - 4) pixels past the well's left edge,
/// in that order at double precision and truncated. The value is the
/// counted-up current value as a signed decimal, measured in `font` and
/// centred on the bar: half the bar's width less half the text's width
/// across, half its height less half the font's line height down, each half
/// truncated toward zero.
///
/// @param bar Stat bar drawn.
/// @param y Top of the bar in screen pixels.
/// @param font Font the value is drawn in (hattfont11, GUI font slot 1).
/// @param[out] out Frame, inner well, fill, label and label position.
void draw_score_bar(
    const ScoreBar& bar, int32_t y, const oa::formats::fnt::Font& font, ScoreBarDraw* out
);

// Where a row's name is drawn: the label the end screen adds over its swatch.
struct ScoreNameDraw {
    int32_t x{};                  // pen column of the first glyph
    int32_t y{};                  // pen row, the top of the line
    char text[kScoreNameBytes]{}; // the part of the name that fits the label
};

/// Places a row's name in its label, which starts at the row's name_x.
///
/// The name is measured in `font` and centred across kScoreNameWidth: half
/// that width less half the name's width, each half truncated toward zero.
/// Its line starts (kScoreRowStep - line height) / 2 rows below the row's
/// top, truncated toward zero, so it sits in the middle of the row. Glyphs
/// are kept up to the first one wider than what is left of the label's
/// width, spaces included; bytes below 0x20 and bytes without a glyph take
/// no room. The centring still measures the whole name.
///
/// @param row Score row the name belongs to.
/// @param name Player name; null is empty.
/// @param font Font the name is drawn in (hattfont11, GUI font slot 1).
/// @param[out] out Pen position and the drawn part of the name.
void place_score_name(
    const ScoreRow& row, const char* name, const oa::formats::fnt::Font& font, ScoreNameDraw* out
);

/// Tells whether a campaign session can continue: lost, or more missions remain.
///
/// @param campaign Campaign object; null or another session kind cannot continue.
/// @param game Game block holding the victory flag and the mission index.
/// @return Whether the campaign continues.
[[nodiscard]] bool
campaign_can_continue(oa::data::campaign::CampaignFile* campaign, const Game& game);

/// Shows the ENDMSN.GUI buttons: MainMenu only, or the full campaign set.
///
/// A continuing campaign shows Start, LoadGame, SaveGame, KNOB, Missions,
/// Difficulty, AdjustDiff and MainMenu.
///
/// @param campaign Campaign object.
/// @param game Game block of the finished game.
/// @param host Frontend services.
void update_end_mission_buttons(
    oa::data::campaign::CampaignFile* campaign, const Game& game, const FrontendHost* host
);

// Background of the outcome screen: the mission's glamour picture after a
// campaign victory, otherwise Outcome1 (campaign) or Outcome0.
struct OutcomeBackground {
    char glamour[0x100]{}; // bitmap path, empty when a palette screen is used
    const char* palette_screen{};
};

/// Picks the background of the outcome screen.
///
/// A campaign victory with a glamour picture uses glamour/<name>, or
/// glamour/Arm01.PCX when the named file is missing; otherwise the palette
/// screen is Outcome1 for a campaign and Outcome0 for anything else.
///
/// @param campaign Campaign object; may be null.
/// @param victory Whether the local side won.
/// @param files Campaign file services, used to check the picture exists; may be null.
/// @param[out] out Glamour path or palette screen.
void choose_outcome_background(
    const oa::data::campaign::CampaignFile* campaign,
    bool victory,
    const oa::data::campaign::CampaignFiles* files,
    OutcomeBackground* out
);

// The palette fade's buffers, which the engine keeps here rather than in
// Game.endgame_pictures; its timer lives in Game.endgame_fade_tick and
// endgame_fade_done.
struct EndgameFade {
    uint8_t desired[kPaletteBytes]{};
    uint8_t current[kPaletteBytes]{};
    int8_t steps[kPaletteBytes]{};
};

/// Restarts the ten-step shade: countdown kShadeSteps, next step on the following tick.
///
/// @param[out] game Game block whose endgame shade countdown and fade timer are reset.
/// @param tick Current engine tick.
void endgame_reset_step_timer(Game& game, uint32_t tick);

/// Darkens one more step per tick for kShadeSteps ticks.
///
/// @param[in,out] game Game block holding the shade countdown and fade timer;
///                     endgame_fade_done is set after the last step.
/// @param tick Current engine tick.
/// @return The shade level to apply this tick (countdown - kShadeBias), or
///         INT32_MIN when the step is not due.
int32_t endgame_shade_step(Game& game, uint32_t tick);

/// Prepares a palette fade from `current` to `desired`.
///
/// Each byte's step is the signed difference divided by `steps`, at least one
/// unit towards the target when the bytes differ.
///
/// @param[out] game Game block whose endgame_fade_done is cleared.
/// @param[out] fade Fade buffers filled.
/// @param desired Target palette, kPaletteBytes bytes.
/// @param current Starting palette, kPaletteBytes bytes.
/// @param steps Number of fade steps; 0 moves one unit per step.
void endgame_build_fade(
    Game& game, EndgameFade* fade, const uint8_t* desired, const uint8_t* current, int32_t steps
);

/// Moves every palette byte one step towards the desired palette without passing it.
///
/// @param[in,out] game Game block holding the fade timer; endgame_fade_done is
///                     set once the palettes match.
/// @param[in,out] fade Fade buffers; current moves.
/// @param tick Current engine tick.
/// @return Whether `current` changed and should be applied (false before the step is due).
bool endgame_fade_step(Game& game, EndgameFade* fade, uint32_t tick);

/// Stores the end-of-game screen step.
///
/// @param[out] game Game block whose endgame_state is set.
/// @param state OA_ENDGAME_* step.
void set_endgame_state(Game& game, uint8_t state);

// Services of the end-of-game screen. Every member may be null.
struct EndgameHost {
    void* context{};
    uint32_t (*now)(void* context){};              // engine tick
    uint32_t (*ticks_per_second)(void* context){}; // engine clock rate
    // Keeps a copy of the last game frame; false when there is none.
    bool (*capture_frame)(void* context){};
    void (*report_end)(void* context){}; // the reporter's end-of-game event
    // Shows why the local player was dropped (Player.reject_reason).
    void (*disconnect_message)(void* context, uint8_t reason){};
    bool (*message_open)(void* context){};
    void (*draw_wait_frame)(void* context){}; // captured frame, chat, gadgets, cursor
    void (*apply_shade)(void* context, int32_t level){};
    void (*finish_shade)(void* context){};
    void (*open_cd_check)(void* context){};
    void (*show_outcome)(void* context){}; // outcome background (choose_outcome_background)
    // Shows a loaded glamour picture and returns its palette, or null.
    const uint8_t* (*glamour_palette)(void* context){};
    // The dispatcher's movie gate: the intro_enabled display flag.
    bool (*movies_enabled)(void* context){};
    // Final campaign victory: leaves for the frontend in this dispatcher state
    // (ui::frontend_state::state_id main_menu, movies_3_then_5 or movies_4_then_5).
    void (*play_ending)(void* context, uint8_t frontend_state){};
    void (*apply_palette)(void* context, const uint8_t* palette){};
    // Streams a sound file after `delay` mixer timer ticks; stop_stream ends it.
    void (*play_stream)(void* context, const char* path, int32_t volume, uint32_t delay){};
    void (*stop_stream)(void* context){};
    void (*open_panel)(void* context){}; // ENDMSN.GUI setup
    bool (*input)(void* context){};      // pops a waiting key or click
    bool (*button_held)(void* context){};
    void (*draw_continue)(void* context, const char* text){};
    void (*play_sound)(void* context, const char* name){};
    void (*send_status)(void* context){}; // the local player's state to the other machines
    void (*draw_panel)(void* context){};
    void (*leave_to_panel)(void* context){}; // the stat bars are done; the buttons take over
    const FrontendHost* frontend{};
};

// End-of-game screen state kept beside the Game block's endgame_* fields.
struct EndgameScreen {
    ScoreLayout layout{};
    EndgameFade fade{};
    bool frame_captured{}; // a copy of the last frame is held
    bool glamour_sound_started{};
};

inline constexpr int32_t kDisconnectMessageWidth = 0x140;
inline constexpr uint32_t kGlamourSoundDelay = 0x3c;
inline constexpr int32_t kContinueTextRise =
    0x14; // "Click to continue." row above the screen's bottom edge
inline constexpr uint32_t kStatColumnDelay = 10; // ticks between two stat-bar columns
inline constexpr uint32_t kContinueDelaySeconds = 5;
inline constexpr int32_t kGlamourFadeSteps = 5;
inline constexpr const char* kClickToContinue = "Click to continue.";

/// Streams the mission's glamour sound once its picture has faded in, unless a match is running.
///
/// The sound is the mission's glamoursound WAV, streamed at full volume
/// kGlamourSoundDelay mixer timer ticks later.
///
/// @param game Game block holding the application mode.
/// @param campaign Campaign object with the bound mission; null plays nothing.
/// @param host Streams the sound.
void start_glamour_sound(
    const Game& game, const oa::data::campaign::CampaignFile* campaign, const EndgameHost& host
);

/// Redraws the captured frame under the message box while the screen waits on it.
///
/// @param game Game block holding the end-of-game step.
/// @param host Draws the wait frame.
/// @return Whether the screen is waiting on a message (OA_ENDGAME_WAIT_MESSAGE).
bool draw_endgame_wait_frame(const Game& game, const EndgameHost& host);

/// Runs one frame of the end-of-game screen: shade, disc check, outcome, stat bars, panel.
///
/// The end-game application mode runs it once per frame; the step lives in
/// Game.endgame_state. Multiplayer games keep the last frame and wait out any
/// disconnect message; campaigns check the disc; a final campaign victory
/// plays the ending, other campaign victories may fade to the glamour
/// picture; then the stat bars count up one column every kStatColumnDelay
/// ticks (all at once on input outside multiplayer) before the buttons take
/// over. Multiplayer games send the local player's state twice per column.
///
/// @param[in,out] world World whose Game endgame fields change.
/// @param campaign Campaign object of the finished game; may be null.
/// @param[in,out] screen Score layout, fade buffers and screen flags.
/// @param host End-of-game services.
void endgame_tick(
    World& world,
    oa::data::campaign::CampaignFile* campaign,
    EndgameScreen& screen,
    const EndgameHost& host
);

/// Handles a click on CDCHECK.GUI.
///
/// OK plays "Options" and continues when the disc is in, otherwise shows the
/// Campaign CD message; any click but a closing one clears the selection.
///
/// @param host Frontend services.
/// @param control Clicked control name; null when the panel closes.
/// @param state Current end-game state.
/// @return The next end-game state (5) when the disc is in, otherwise `state`.
int32_t cd_check_click(const FrontendHost* host, const char* control, int32_t state);

/// Copies text into the 0xF9-byte frontend status text, bounded and terminated.
///
/// @param[out] status Status text buffer, kStatusTextBytes bytes.
/// @param text Text copied; null is empty.
void copy_status_text(char* status, const char* text);
inline constexpr std::size_t kStatusTextBytes = 0xf9;

} // namespace oa::ui::campaign
