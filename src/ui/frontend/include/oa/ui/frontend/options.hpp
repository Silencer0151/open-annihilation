// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Options screens: the tab panel (STARTOPT/PREFS), the SOUNDS, VISUALS and
// SPEEDS sub-panels, their slider callbacks and the display-mode list.
//
// Handlers operate on a Panel, a model of the gadget-record fields they touch.
// The runtime copies a loaded layout into a Panel, runs a handler, and copies
// stages, grayed state, text, quick keys and knob positions back to its
// widgets.
#pragma once

#include "oa/core/types.h"
#include "oa/data/campaign/campaign_file.hpp"
#include "oa/ui/gui_layout.hpp"
#include "oa/ui/frontend_state/initialization.hpp"
#include "oa/present/world_renderer/world_display_modes.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

namespace oa::ui::frontend {

namespace prefs = oa::ui::frontend_state::initialization;

// ---------------------------------------------------------------------------
// Panel model

inline constexpr std::size_t kPanelControls = 200;   // gadget-record capacity
inline constexpr std::size_t kControlNameBytes = 16; // names compare over 16 bytes
inline constexpr std::size_t kControlTextBytes = 0x80;
inline constexpr int32_t kNoSelection = -1;
inline constexpr uint32_t kNoStage = 0xffffffffU;

enum class ControlType : uint8_t {
    panel = 0,
    button = 1,
    list_box = 2,
    text_box = 3,
    slider = 4,
    label = 5,
    hot_surface = 6,
    filler = 11, // PANEL: the area an in-game sub-panel is merged into
    frame = 12,
};

// The fields of a scroll-bar record the options screens use.
struct SliderState {
    int16_t range = 0;     // step count
    int32_t maximum = 0;   // value maximum, stored over the loaded thickness
    int16_t knob = 0;      // knob step
    int16_t knob_size = 0; // knob length in pixels
};

struct Panel;
struct OptionsContext;
// A scroll bar's change callback: invoked with the owning panel when the knob
// moves.
using SliderHandler = void (*)(Panel& panel, OptionsContext& context);

struct Control {
    std::array<char, kControlNameBytes> name{};
    ControlType type = ControlType::panel;
    uint8_t group = 0; // association
    int16_t x = 0, y = 0, width = 0, height = 0;
    uint8_t active = 0;
    uint8_t stage = 0;       // button stage
    int8_t stages = 0;       // a button's stage count, 0 for a plain button
    int16_t group_value = 0; // a button's status
    // Per-type grayed state: bit 0 of a button's flags word, a slider's lock
    // byte, bit 0 of a label's flags, bit 0 of a list's second attribute byte.
    uint8_t grayed = 0;
    uint32_t attributes = 0;     // attribs word
    int16_t list_selection = -1; // list cursor
    SliderState slider{};
    SliderHandler on_change = nullptr;
    std::array<char, kControlTextBytes> text{};
    // A button's quick key: the caption letter its underline marks and a key
    // press matches, or 0 for none.
    int8_t quick_key = 0;
};

// Record 0 is the root; loaded records are 1..count (the root's record count).
struct Panel {
    std::array<Control, kPanelControls> controls{};
    int16_t count = 0;
    int32_t selected = kNoSelection; // index of the selected record, or kNoSelection
    bool dirty = false;
};

/// Reads a control's name.
///
/// @param control Control read.
/// @return The name up to its first NUL (at most kControlNameBytes characters).
[[nodiscard]] std::string_view control_name(const Control& control) noexcept;

/// Replaces a control's name, cut to kControlNameBytes bytes.
///
/// @param[out] control Control renamed.
/// @param name New name.
void set_control_name(Control& control, std::string_view name) noexcept;

/// Reads a control's text.
///
/// @param control Control read.
/// @return The text up to its first NUL.
[[nodiscard]] std::string_view control_text(const Control& control) noexcept;

/// Replaces a control's text, cut to kControlTextBytes - 1 characters.
///
/// @param[out] control Control whose text changes.
/// @param text New text.
void set_control_text(Control& control, std::string_view text) noexcept;

/// Copies a parsed GUI layout into the record model.
///
/// Records past kPanelControls are dropped; count becomes the number of
/// loaded records after the root. A button's status becomes its group value
/// and its stage starts at 0.
///
/// @param[out] panel Panel replaced by the layout.
/// @param layout Parsed GUI layout.
void panel_load_layout(Panel& panel, const ui::gui_layout::Layout& layout) noexcept;

/// Appends a record after the last loaded one.
///
/// @param[in,out] panel Panel extended.
/// @param type Type of the new record.
/// @param name Name of the new record.
/// @return The new record, or null when the panel is full.
Control* panel_append(Panel& panel, ControlType type, std::string_view name) noexcept;

/// Finds the first record 1..count whose 16-byte name matches.
///
/// @param panel Panel searched.
/// @param name Name compared over its first kControlNameBytes bytes.
/// @return The record index, or -1.
[[nodiscard]] int32_t panel_find(const Panel& panel, std::string_view name) noexcept;

/// Finds a record by name (see panel_find).
///
/// @param panel Panel searched.
/// @param name Record name.
/// @return The record, or null when there is none.
[[nodiscard]] Control* panel_control(Panel& panel, std::string_view name) noexcept;

/// Finds a record by name (see panel_find).
///
/// @param panel Panel searched.
/// @param name Record name.
/// @return The record, or null when there is none.
[[nodiscard]] const Control* panel_control(const Panel& panel, std::string_view name) noexcept;

/// Tells whether the selected record has exactly the given name.
///
/// @param panel Panel read.
/// @param name Record name.
/// @return false when nothing is selected.
[[nodiscard]] bool panel_selected_is(const Panel& panel, std::string_view name) noexcept;

/// Clears the panel's selection.
///
/// @param[out] panel Panel whose selection becomes kNoSelection.
void panel_clear_selection(Panel& panel) noexcept;

/// Reads a button's stage.
///
/// @param panel Panel searched.
/// @param name Button name.
/// @return The stage, or kNoStage when the record is missing or not a button.
[[nodiscard]] uint32_t panel_stage(const Panel& panel, std::string_view name) noexcept;

/// Sets a record's button stage.
///
/// @param[in,out] panel Panel searched.
/// @param name Record name.
/// @param stage New stage.
/// @return false when the record is missing.
bool panel_set_stage(Panel& panel, std::string_view name, uint8_t stage) noexcept;

/// Sets a record's group value and marks the panel dirty.
///
/// @param[in,out] panel Panel searched.
/// @param name Record name.
/// @param value New group value.
/// @return false when the record is missing.
bool panel_set_group_value(Panel& panel, std::string_view name, int16_t value) noexcept;

/// Sets a record's active byte and marks the panel dirty.
///
/// A slider's value also reaches every button in its group.
///
/// @param[in,out] panel Panel searched.
/// @param name Record name; a missing record changes nothing.
/// @param active New active byte.
void panel_set_active(Panel& panel, std::string_view name, uint8_t active) noexcept;

/// Replaces bit 0 of a button's state word (its grayed state).
///
/// @param[in,out] panel Panel searched.
/// @param name Record name; a missing record changes nothing.
/// @param grayed New grayed state.
void panel_set_grayed(Panel& panel, std::string_view name, bool grayed) noexcept;

/// Sets a record's per-type grayed state.
///
/// A slider also greys the stepping buttons (attribute bits 0x18) in its group.
///
/// @param[in,out] panel Panel searched.
/// @param name Record name; a missing record changes nothing.
/// @param disabled New grayed state.
void panel_set_disabled(Panel& panel, std::string_view name, bool disabled) noexcept;

/// Replaces a record's text, in the language shown, and marks the panel dirty.
///
/// The text is the game's own: the record shows the game data's translation
/// of it (oa/data/languages/translation.hpp), or the text itself where the
/// data has none, as a text a GUI file holds shows. A button then takes the
/// quick key its new caption gives it, as the gadget engine assigns one
/// (ui::gui_input::caption_quick_key): none for a
/// button with stages, the key it had with the no_quick_key attribute or an
/// empty caption, else the first caption letter no other button's key
/// takes (ui::gui_input::free_quick_key). The panel's labels hold no quick
/// key: the gadget engine gives a label one only when a caption is set on a
/// label with a link.
///
/// @param[in,out] panel Panel searched.
/// @param name Record name; a missing record changes nothing.
/// @param text New text, before translation.
void panel_set_text(Panel& panel, std::string_view name, std::string_view text) noexcept;

// ---------------------------------------------------------------------------
// Sliders

/// Moves the knob to the step for a value, rounding any fractional step up.
///
/// The step is value / maximum * (range - 1) at double precision.
///
/// @param[in,out] slider Slider whose knob moves.
/// @param value Value to show; clamped to the maximum.
/// @quirk The fraction test compares with the low 32 bits of the truncated
///        step, as 3.1c does, and a NaN step counts as whole.
void slider_set_value(SliderState& slider, int32_t value) noexcept;

/// Reads the value the knob step selects.
///
/// @param slider Slider read.
/// @return knob / (range - 1) * maximum, truncated; 0 when the range has
///         fewer than two steps.
[[nodiscard]] int32_t slider_value(const SliderState& slider) noexcept;

// ---------------------------------------------------------------------------
// Display modes

inline constexpr std::size_t kDisplayModeCapacity = 100; // 0x4B0-byte table

using DisplayMode = oa::present::world_renderer::DisplayMode;

struct DisplayModeList {
    int32_t count = 0;
    std::array<DisplayMode, kDisplayModeCapacity> modes{};
};

// ---------------------------------------------------------------------------
// Options context

// Side effects the handlers request from the application. Null entries are
// skipped.
struct OptionsHost {
    void* context = nullptr;
    void (*play_sound)(void* context, const char* name) = nullptr; // named interface sound
    void (*apply_volumes)(void* context) = nullptr;                // mixer/gamma reapply
    void (*set_game_speed)(void* context, uint16_t speed) = nullptr;
    void (*stop_sound)(void* context) = nullptr;
    // Sets the sound object's 3D switch: on for sound mode 2, off otherwise.
    void (*set_spatial_sound)(void* context, bool on) = nullptr;
    void (*play_voice_test)(void* context) = nullptr;
    void (*play_wave)(void* context, const char* path) = nullptr;
    bool (*scan_display_modes)(void* context, DisplayModeList& list) = nullptr;
    void (*save_options)(void* context) = nullptr;  // writes preferences
    void (*restore_all)(void* context) = nullptr;   // entry snapshot, every group
    void (*restore_sound)(void* context) = nullptr; // sound UNDO arm
    void (*reset_sound)(void* context) = nullptr;   // sound RESTORE arm
    // Visual RESTORE arm, after the display stores were set to 640x480:
    // puts the host's own default Screen Size in them instead.
    void (*reset_screen_size)(void* context) = nullptr;
    void (*release_lightbar)(void* context) = nullptr;   // frees the lightbar buffers
    void (*draw_current_frame)(void* context) = nullptr; // clears and presents the frame
    // FLIPSURFACE: a copy of the top panel's picture, with the panel's size
    // and y; 0 when there is none.
    oa_ref32 (*copy_top_panel)(void* context, int32_t* width, int32_t* height, int32_t* y) =
        nullptr;
    // A cleared surface of the size.
    oa_ref32 (*create_surface)(void* context, const char* tag, int32_t width, int32_t height) =
        nullptr;
    // Loads STARTOPT.GUI (PREFS.GUI in a match) as the top panel into `panel`.
    void (*load_panel)(void* context, Panel& panel) = nullptr;
    // Named background, nothing deferred.
    void (*load_background)(void* context, const char* name) = nullptr;
};

// The lightbar the options panel opens with.
struct OptionsLightbar {
    oa_ref32 flip = 0;   // FLIPSURFACE, the panel below's picture
    oa_ref32 backup = 0; // BKUPSURFACE
    int32_t scroll = 0;
    int32_t last_column = 0; // flip width - 1
    int32_t last_row = 0;    // flip height - 1
    int32_t panel_y = 0;     // the panel below's y
    int32_t active = 0;      // 1 while the lightbar runs
    int32_t velocity = 0;
};

inline constexpr int32_t kLightbarBackupWidth = 300;
inline constexpr int32_t kLightbarBackupHeight = 480;
// The sweep: the scroll grows by kLightbarScrollStep a frame up to
// kLightbarScrollEnd, one column a frame once past the flip picture's last
// column, and the fold's lift by kLightbarVelocityStep a frame.
inline constexpr int32_t kLightbarScrollStep = 21;
inline constexpr int32_t kLightbarScrollEnd = 277;
inline constexpr int32_t kLightbarVelocityStep = 6;
// The fixed edge the picture folds on before the scroll passes the last
// column, and the frame's last row, which every quad reaches.
inline constexpr int32_t kLightbarFoldColumn = 127;
inline constexpr int32_t kLightbarBottomRow = 479;
// Frame of the COMMONGUI LIGHTBAR entry stamped onto the flip picture.
inline constexpr int32_t kLightbarStampFrame = 2;

/// A corner of the lightbar's blit, in pixels.
struct LightbarPoint {
    int32_t x = 0;
    int32_t y = 0;
    bool operator==(const LightbarPoint&) const = default;
};

/// What one HUD frame of the OPTIONS lightbar sweep does.
struct OptionsLightbarStep {
    bool drawn = false;              // the lightbar is running: the blit below is drawn
    bool stamp_lightbar = false;     // LIGHTBAR frame 2 goes onto the flip picture first
    bool play_options_sound = false; // the sweep reached its end: "Options" plays
    // Corners of the flip picture, inset by one pixel, clockwise from the
    // top-left.
    std::array<LightbarPoint, 4> source{};
    // The frame corners the source corners map to, in the same order.
    std::array<LightbarPoint, 4> destination{};
};

// Application state the options handlers read and write.
struct OptionsContext {
    prefs::Preferences* preferences = nullptr;
    const oa::ui::frontend_state::State* state = nullptr;
    prefs::OptionsEntrySnapshot snapshot{};
    OptionsHost host{};
    DisplayModeList display_modes{};
    bool display_modes_ready = false; // the display-mode list VIDSLDR steps through is allocated
    // The shortest display mode VIDSLDR offers, in rows: 480 in 3.1c; a
    // mod's display rules may raise it.
    int32_t minimum_mode_height = oa::present::world_renderer::minimum_mode_height;
    // The visual RESTORE set the display stores to the default Screen Size
    // and neither VIDSLDR nor UNDO has changed them since; cleared as the
    // entry snapshot is taken.
    bool screen_size_restored = false;
    bool in_game = false;         // Game.session_flags bit 2
    bool realtime_panels = false; // Game.frame_flags bit 0: in-game *RT.GUI variants
    bool audio_device_missing = false;
    bool game_speed_locked = false;                 // current player may not change the game speed
    bool hold_game = false;                         // Game.sim_run_flags bit 0
    oa::data::campaign::SessionKind session_kind{}; // campaign object state
    uint8_t options_dirty = 0;                      // flag written by the options tab handler
    OptionsLightbar lightbar{};
};

// Which GUI file an options panel loads.
enum class OptionsPanel : uint8_t {
    tabs,
    sound,
    visuals,
    select_video_mode,
    speeds,
    music,
};

// What the application must do after a handler returns.
enum class OptionsAction : uint8_t {
    none,
    open_speeds,
    open_visuals,
    open_music,
    open_sound,
    reload,         // rebuild the current sub-panel (after UNDO/RESTORE)
    close_saved,    // PREV: preferences saved, leave
    close_restored, // CANCEL: entry snapshot restored, leave
};

/// Names the GUI file an options panel loads.
///
/// @param panel Which options panel.
/// @param realtime Whether the in-game *RT variant is wanted (PREFS.GUI for the tabs).
/// @return The GUI file name; SELVMODE.GUI has no in-game variant.
[[nodiscard]] std::string_view options_panel_file(OptionsPanel panel, bool realtime) noexcept;

/// Names the background bitmap of an options panel ("options4x", "optsound4x", ...).
///
/// @param panel Which options panel.
/// @return The background name.
[[nodiscard]] std::string_view options_panel_background(OptionsPanel panel) noexcept;

/// Opens OPTIONS over the panel below.
///
/// Outside a match the current frame is cleared and presented first. The
/// lightbar copies the panel below (FLIPSURFACE), takes its last column,
/// last row and y, and gets a 300x480
/// BKUPSURFACE; the tab panel then loads and is set up (options_enter_tabs),
/// options4x becomes the background outside a match, and the entry snapshot
/// is captured. In a match the "Panel" sound plays.
///
/// @param[out] panel Receives the loaded tab panel.
/// @param[in,out] context Preferences, flags, host and lightbar state.
void options_open(Panel& panel, OptionsContext& context) noexcept;

/// Advances the OPTIONS lightbar by one HUD frame.
///
/// While the scroll is under kLightbarScrollEnd it grows by
/// kLightbarScrollStep, stopping at the end, where "Options" plays. The step
/// that first takes the scroll past the flip picture's last column stamps
/// LIGHTBAR frame 2 onto the picture; past that column the scroll then gains
/// one more column a frame up to the end. The lift grows by
/// kLightbarVelocityStep while the scroll is under the last column and
/// falls by as much, down to 0, once it is not. Before the last column the
/// picture folds from the scroll onto kLightbarFoldColumn; past it the
/// picture turns over, from the last column out to the scroll. The moving
/// edge's top is lifted by the lift, and both edges reach
/// kLightbarBottomRow. A lightbar that is not active does nothing.
///
/// @param[in,out] lightbar The running lightbar; its scroll and lift change.
/// @return What to stamp, play and draw this frame.
[[nodiscard]] OptionsLightbarStep options_lightbar_step(OptionsLightbar& lightbar) noexcept;

/// Sets up the options tab panel.
///
/// In game the realtime-panel flag is raised (PREFS.GUI and the *RT
/// sub-panels) and the game is held outside multiplayer; MUSIC is greyed
/// without an audio device.
///
/// @param[in,out] panel The loaded tab panel or sub-panel.
/// @param[in,out] context Its realtime_panels and hold_game flags change.
void options_enter_tabs(Panel& panel, OptionsContext& context) noexcept;

/// Captures the entry snapshot the CANCEL/UNDO arms restore from.
///
/// @param[in,out] context Its snapshot is filled from the preferences; nothing
///                        happens without preferences. Its
///                        screen_size_restored is cleared.
void options_capture_entry(OptionsContext& context) noexcept;

/// Widens an in-game options panel and adds its PANEL filler record.
///
/// With the realtime-panel flag the root grows by 150 pixels and, when the
/// layout has no PANEL record, a filler from x 128 to the new right edge, as
/// high as the root, is appended.
///
/// @param[in,out] panel The loaded panel.
/// @param context Supplies the realtime-panel flag.
void options_extend_panel_for_game(Panel& panel, const OptionsContext& context) noexcept;

/// Sets up a freshly loaded PREFS.GUI for the in-game sub-panel a tab opens.
///
/// Runs options_enter_tabs, then widens the panel for the sub-panel
/// (options_extend_panel_for_game). The sub-panel's own set-up
/// (options_enter_sound and the others) runs after the merge and widens
/// nothing more.
///
/// @param[in,out] panel The loaded PREFS.GUI, positions relative to its root.
/// @param[in,out] context Its realtime_panels and hold_game flags change.
void options_prepare_realtime_panel(Panel& panel, OptionsContext& context) noexcept;

/// Merges an in-game sub-panel (SOUNDSRT, MUSICRT, SPEEDSRT or VISUALRT.GUI)
/// into the tab panel.
///
/// The sub-panel's records after its root are appended after the panel's
/// last record, centred in the PANEL filler: each moves by PANEL's position
/// plus half the difference between PANEL's size and the sub-panel root's,
/// rounded toward zero, and PANEL's active byte clears. Without a PANEL record
/// they move by the sub-panel root's own position. Records past
/// kPanelControls are dropped.
///
/// @param[in,out] panel The prepared tab panel, positions relative to its root.
/// @param sub The loaded sub-panel, positions relative to its root.
void options_merge_realtime_panel(Panel& panel, const Panel& sub) noexcept;

/// Handles a click on the options tab panel.
///
/// SPEEDS, VISUALS, MUSIC and SOUND highlight their tab, play "Options" and
/// open their sub-panel. PREV plays "Options" and saves the preferences;
/// CANCEL plays "Previous", restores the entry snapshot of every group and
/// reapplies the volumes and gamma. Any other control is deselected. The
/// lightbar is released after a tab click and when the panel closes.
///
/// @param[in,out] panel The loaded tab panel.
/// @param[in,out] context Preferences, host and options_dirty (1 after
///                        PREV/CANCEL, 0 after a tab).
/// @return The sub-panel to open, close_saved, close_restored or none.
OptionsAction options_on_tab_click(Panel& panel, OptionsContext& context) noexcept;

/// Refreshes MODE, VOLTEXT and the controls greyed while sound is off.
///
/// @param[in,out] panel The loaded SOUNDS panel.
/// @param context Supplies the sound mode from the preferences.
void options_update_sound_state(Panel& panel, const OptionsContext& context) noexcept;

/// Sets up SOUNDS.
///
/// FXVOL ranges 0..64 at the saved volume, every slider callback runs once,
/// and SPEECH shows the unit speech volume step while the speech flag is set.
///
/// @param[in,out] panel The loaded SOUNDS panel.
/// @param[in,out] context Preferences, flags and host.
void options_enter_sound(Panel& panel, OptionsContext& context) noexcept;

/// Handles a click on SOUNDS: SPEECH, MODE, TEST, UNDO and RESTORE.
///
/// SPEECH stores the speech flag and the unit speech volume (stage * 5). MODE
/// stores the sound mode, stops sound when off, switches 3D sound for mode 2
/// and plays the voice test for mode 1 outside a game. TEST plays
/// sounds/explode.wav. UNDO restores and RESTORE resets the sound settings,
/// then the volumes and gamma are reapplied. Other buttons fall through to
/// the tab handler. Closing clears the realtime-panel flag.
///
/// @param[in,out] panel The loaded SOUNDS panel.
/// @param[in,out] context Preferences and host.
/// @return reload after UNDO/RESTORE, the tab handler's action, or none.
OptionsAction options_on_sound_click(Panel& panel, OptionsContext& context) noexcept;

/// Sets up VISUALS, or SELVMODE when `select_mode`.
///
/// Outside a game the display modes are scanned and sorted and VIDSLDR runs
/// over them at the saved size; in game the MAP* and VID* controls are
/// hidden. VISUALS also shows ANTI, BSHADOWS and SHADING from the graphics
/// flags and GAMMA over 0..20.
///
/// @param[in,out] panel The loaded panel.
/// @param[in,out] context Preferences, flags, host and the display-mode list.
/// @param select_mode Whether the panel is SELVMODE (display modes only).
void options_enter_visuals(Panel& panel, OptionsContext& context, bool select_mode) noexcept;

/// Handles a click on VISUALS: ANTI, BSHADOWS, SHADING, UNDO and RESTORE.
///
/// The flag buttons store their bit (BSHADOWS also sets the unit and vehicle
/// shadow bits from it). UNDO restores and RESTORE resets the visual options,
/// then the volumes and gamma are reapplied so the gamma shows. Where RESTORE
/// resets the display stores, the host's reset_screen_size then puts its own
/// default Screen Size in them, and screen_size_restored is set until
/// VIDSLDR moves or UNDO restores them. OK on
/// SELVMODE.GUI only plays the sound. Other buttons fall through to the tab
/// handler. Closing drops the display-mode list and the realtime-panel flag.
///
/// @param[in,out] panel The loaded panel.
/// @param[in,out] context Preferences, frontend state and host.
/// @return reload after UNDO/RESTORE, the tab handler's action, or none.
OptionsAction options_on_visuals_click(Panel& panel, OptionsContext& context) noexcept;

/// Sets up SPEEDS.
///
/// GAME (0..21, when present), SCREEN (0..65), MAXLINES (0..30) and TXTSCROL
/// (0..40) show the saved values, UNITCHAT and LEFTCLICK their stages, and
/// every slider callback runs once.
///
/// @param[in,out] panel The loaded SPEEDS panel.
/// @param[in,out] context Preferences, flags and host.
void options_enter_speeds(Panel& panel, OptionsContext& context) noexcept;

/// Handles a click on SPEEDS: LEFTCLICK, UNITCHAT, UNDO and RESTORE.
///
/// LEFTCLICK stores the interface type and UNITCHAT the unit chat text volume
/// (stage * 5); UNDO restores and RESTORE resets the speed options. Other
/// buttons fall through to the tab handler. Closing clears the
/// realtime-panel flag.
///
/// @param[in,out] panel The loaded SPEEDS panel.
/// @param[in,out] context Preferences and host.
/// @return reload after UNDO/RESTORE, the tab handler's action, or none.
OptionsAction options_on_speeds_click(Panel& panel, OptionsContext& context) noexcept;

/// Clears the realtime-panel flag when a sub-panel closes.
///
/// @param[in,out] context Its realtime_panels flag is cleared.
void options_leave_subpanel(OptionsContext& context) noexcept;

/// Sets up MUSIC.
///
/// MUSICVOL ranges 0..64 at the saved volume and the CD transport state is
/// shown. The CD track display needs the audio device and is the caller's.
///
/// @param[in,out] panel The loaded MUSIC panel.
/// @param[in,out] context Preferences, flags and host.
void options_enter_music(Panel& panel, OptionsContext& context) noexcept;

/// Enables the CD transport buttons from the music mode and CD mode.
///
/// NOTRAK shows the music mode and TRACKMODE the CD mode; with music off
/// MUSICVOL, CDPREV, CDSTOP, CDPLAY, CDNEXT and TRACKMODE are greyed, and
/// TRACKTYPE is live only in CD mode 4.
///
/// @param[in,out] panel The loaded MUSIC panel.
/// @param context Supplies the preferences.
void options_update_cd_controls(Panel& panel, const OptionsContext& context) noexcept;

/// Hides the controls whose names start with `prefix` by clearing their active byte.
///
/// @param[in,out] panel Panel whose records are visited, root included.
/// @param prefix Name prefix, compared case-sensitively.
void options_hide_controls_with_prefix(Panel& panel, std::string_view prefix) noexcept;

// Slider change callbacks.

/// Moves VIDSLDR to the display mode matching the saved size and shows it in VIDVAL.
///
/// A saved size no mode matches, such as a window's own size the player
/// dragged it to, shows "Custom" in VIDVAL, in the language shown, with
/// the knob on the last mode listed before it, or on the first; 3.1c left
/// the panel's own defaults showing.
///
/// @param[in,out] panel The loaded VISUALS or SELVMODE panel.
/// @param context Supplies the preferences and the display-mode list.
void options_sync_video_mode(Panel& panel, OptionsContext& context) noexcept;

/// Stores the display mode VIDSLDR selects as the saved size and shows it in VIDVAL.
///
/// @param[in,out] panel The loaded panel; marked dirty.
/// @param[in,out] context Its preferences' display size changes.
void options_on_video_mode_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the GAMMA slider's value as the gamma and reapplies it.
///
/// @param panel The loaded VISUALS panel.
/// @param[in,out] context Its preferences' gamma changes.
void options_on_gamma_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the FXVOL slider's value as the effects volume and reapplies the volumes.
///
/// @param panel The loaded SOUNDS panel.
/// @param[in,out] context Its preferences' fx_volume changes.
void options_on_fx_volume_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the MUSICVOL slider's value as the music volume and reapplies the volumes.
///
/// @param panel The loaded MUSIC panel.
/// @param[in,out] context Its preferences' music_volume changes.
void options_on_music_volume_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the GAME slider's value (at least 1) as the game speed and applies it.
///
/// Nothing changes while the game speed is locked for the current player.
///
/// @param[in,out] panel The loaded SPEEDS panel; marked dirty.
/// @param[in,out] context Its preferences' game_speed changes.
void options_on_game_speed_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the SCREEN slider's value (at least 1) as the scroll speed.
///
/// @param[in,out] panel The loaded SPEEDS panel; marked dirty.
/// @param[in,out] context Its preferences' scroll_speed changes.
void options_on_scroll_speed_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the MAXLINES slider's value as the message-log line count and shows it ("None" for 0).
///
/// @param[in,out] panel The loaded SPEEDS panel; MAXLINESTEXT is set.
/// @param[in,out] context Its preferences' text_lines changes.
void options_on_max_lines_slider(Panel& panel, OptionsContext& context) noexcept;

/// Stores the TXTSCROL slider's value as the text scroll time and shows it ("<n> secs").
///
/// @param[in,out] panel The loaded SPEEDS panel; TEXTSCROLLTEXT is set.
/// @param[in,out] context Its preferences' text_scroll changes.
void options_on_text_scroll_slider(Panel& panel, OptionsContext& context) noexcept;

/// Runs every slider's callback once, in record order, as panel entry does.
///
/// @param[in,out] panel Panel whose sliders are visited.
/// @param[in,out] context Passed to each callback.
void options_run_slider_callbacks(Panel& panel, OptionsContext& context) noexcept;

} // namespace oa::ui::frontend
