// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Open Annihilation settings dialog: its sections and rows, what a press,
// a drag or a key does to them, and how it and the OA button that opens it
// are drawn. The dialog is laid out in the game's 640x480 source pixels and
// drawn without the game's art (oa/ui/frontend_renderer/artless.hpp) in the
// game's own fonts. A host places it, darkens what lies under it, turns its
// events into dialog pixels and puts the settings it reports in effect.
#pragma once

#include "oa/formats/fnt.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/data/languages.hpp"
#include "oa/present/game_text.hpp"
#include "oa/ui/engine_settings.hpp"
#include "oa/ui/frontend_renderer.hpp"
#include "oa/ui/frontend_renderer/artless.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::engine_settings {

/// The dialog's width, in source pixels.
inline constexpr int32_t dialog_width = 480;
/// The dialog's height, in source pixels.
inline constexpr int32_t dialog_height = 324;
/// The OA button's side on the main menu, in source pixels.
inline constexpr int32_t menu_button_side = 32;
/// The OA button's side in the in-game menu's column, in source pixels.
inline constexpr int32_t ingame_button_side = 24;

/// The colour the screen under the dialog is darkened with.
inline constexpr oa::ui::frontend_renderer::Rgb backdrop_color{5, 6, 4};
/// How far the main menu is darkened under the dialog, in 256ths.
inline constexpr uint32_t menu_backdrop_opacity = 159;
/// How far the in-game menu's column is darkened beside the dialog, in 256ths.
inline constexpr uint32_t ingame_backdrop_opacity = 128;

/// The dialog's sections, in the order its list shows them: the engine's
/// settings, then the mod options' (ui.options-dialog), which a dialog of
/// each kind lists alone.
enum class Page : uint8_t {
    mods,          ///< Mods: the mods the game can play, the one played first
    controls,      ///< Controls
    common_tweaks, ///< Common Tweaks: the player's own folder, the unit limit and pathfinding
    language,      ///< Language: the language and how game text is drawn
    graphics,      ///< Graphics
    /// Touch: how the touch controls answer a finger; listed only while the
    /// game has touch controls (Dialog::touch)
    touch,
    /// Controller: how a gamepad points, moves the map and answers; listed,
    /// after Touch, only once a gamepad has sent input in this run
    /// (Dialog::controller)
    controller,
    /// Developer, at the foot of the list under a divider: its rows over
    /// Developer Mode's list of the standard hacks
    developer,
    /// Game files: what is installed, the backups switch and where the
    /// files are; listed, between Controller and Developer, only where the
    /// platform brings game files in and the dialog is the main menu's
    /// (Dialog::game_files)
    game_files,
    mod_keys,   ///< the mod's keys
    mod_patrol, ///< what patrolling builders do
    mod_guard,  ///< what guarding builders do
    mod_tools,  ///< the build tools and the mex snap
    mod_chat,   ///< the wreck snap, the chat and the resource bar
};

/// The number of sections, of every kind of dialog.
inline constexpr std::size_t page_count = 14;
/// The most sections a dialog lists: the engine's settings' nine with
/// Touch, Controller and Game files, six without them; a mod's options have
/// five.
inline constexpr std::size_t most_listed_pages = 9;

/// Which settings a dialog shows.
enum class DialogKind : uint8_t {
    /// the engine's settings: the sections up to Developer, Touch among them
    /// only while the game has touch controls and Controller only once a
    /// gamepad has sent input
    engine,
    mod_options, ///< a mod's options: the last five sections
    /// Language alone, as the Game files screen opens it before the game's
    /// files are installed
    language_text,
};

/// Returns the sections a kind of dialog lists, in order.
///
/// @param kind the dialog's kind
/// @param touch the game has touch controls (Dialog::touch), so that the
///     engine's settings list Touch between Graphics and Developer
/// @param game_files the dialog lists Game files (Dialog::game_files),
///     after Touch and Controller and before Developer
/// @param controller a gamepad has sent input in this run
///     (Dialog::controller), so that the engine's settings list Controller
///     after Touch and before Game files and Developer
/// @return six sections for the engine's settings, one more for each of
///     Touch, Controller and Game files listed, nine with all three; five
///     for a mod's options and one for Language alone, whatever the others
///     say
[[nodiscard]] std::span<const Page> dialog_pages(
    DialogKind kind, bool touch = false, bool game_files = false, bool controller = false
) noexcept;

/// The settings, as the dialog's rows show them.
enum class Setting : uint8_t {
    path_search, ///< Pathfinding cycles: a slider
    wheel_zoom,  ///< Mouse wheel zoom: a switch
    /// Maximum zoom out: a drop-down of Automatic, Whole map, 1/32, 1/16,
    /// 1/8, 1/4 and 1/2
    max_zoom_out,
    max_zoom_in, ///< Maximum zoom in: a drop-down of None, 2x, 3x and 4x
    /// View past the map's edge: a strip of Off, 25% and 50%
    view_past_map_edge,
    escape_opens_menu, ///< Escape opens the game menu: a switch
    switch_alt,        ///< Select groups without Alt: a switch
    unit_limit,        ///< Unit limit: a slider
    max_frame_rate,    ///< Maximum frame rate: a slider
    anti_aliasing,     ///< Enhanced anti-aliasing: a strip of levels
    screen_size,       ///< Screen size: a slider
    developer_mode,    ///< Enable Developer Mode: a switch
    frame_stats,       ///< Show performance statistics: a switch
    /// Hardware acceleration: a strip of Off, Basic and Full whose two hint
    /// lines are its status
    hardware_acceleration,
    vertical_sync, ///< Vertical sync: a switch
    /// Language: a drop-down of System default and the languages the game
    /// draws, each named in itself
    language,
    modern_fonts,    ///< Use modern fonts for game text: a switch
    text_outline,    ///< Font outline: a switch
    text_shadow,     ///< Font shadow: a switch
    text_background, ///< Game text background: a switch
    unicode_chat,    ///< Enable Unicode Multiplayer Chat: a switch
    text_size,       ///< Text size: a slider, locked while modern fonts are off
    /// One-finger drag: a strip of Automatic, Box and Scroll
    touch_drag,
    touch_hold_delay,  ///< Hold delay: a slider of milliseconds
    touch_latches,     ///< QUEUE and ADD: a strip of Stay on and One action
    touch_haptics,     ///< Haptics: a switch
    touch_left_handed, ///< Left-handed layout: a switch
    /// Control size: a strip of Standard, Large and Larger, in Touch and in
    /// Controller
    touch_control_size,
    pad_scheme, ///< Scheme: a strip of Trackpads and Sticks
    /// Right trackpad: a strip of the pointer's two ways, Relative and
    /// Absolute
    pad_right_trackpad,
    pad_pointer_speed, ///< Pointer speed: a slider of percent
    pad_acceleration,  ///< Pointer acceleration: a strip of Off, Low and High
    pad_glide,         ///< Trackpad glide: a switch
    /// Right stick: a strip of Zoom (zoom and build pages), Pointer and
    /// Nothing
    pad_right_stick,
    pad_magnetism, ///< Magnetism (stick pointer): a switch
    /// Gyro pointer: a drop-down of Off, While the right pad is touched,
    /// While the right stick is touched and Always
    pad_gyro,
    pad_gyro_speed, ///< Gyro speed: a slider of percent
    pad_haptics,    ///< Haptics: a strip of Off, Light and Strong
    /// Button prompts: a drop-down of Automatic, Steam Deck, Xbox,
    /// PlayStation, Nintendo and Off
    pad_prompts,
    pad_left_handed, ///< Left-handed: a switch
    /// Steam Input: a text row, Controller's first while the gamepad reaches
    /// the game through Steam Input (Dialog::steam_input), saying so; it
    /// changes no setting
    pad_steam_input_notice,
    /// Mods: the list of the mods the game can play, one row each, which
    /// switches the game to the one chosen once the player confirms it
    mod,
    snap_override_key, ///< the mod's snap override key: a slider of option_keys
    autoclick_key,     ///< the mod's autoclick key: a slider of option_keys
    rotate_build_key,  ///< the mod's rotate key: a slider of option_keys
    patrol_hold,       ///< patrolling builders under Hold position: a slider of three
    patrol_maneuver,   ///< patrolling builders under Maneuver
    patrol_roam,       ///< patrolling builders under Roam
    guard_hold,        ///< guarding builders under Hold position: a slider of three
    guard_maneuver,    ///< guarding builders under Maneuver
    guard_roam,        ///< guarding builders under Roam
    mex_snap_radius,   ///< the mex snap radius: a slider up to the mod's most
    wreck_snap_radius, ///< the wreck snap radius: a slider up to the mod's most
    optimize_dt_rows,  ///< Optimize DT rows: a switch
    full_rings,        ///< Full rings: a switch
    chat_backdrop,     ///< Accessible chat: a switch
    panel_background,  ///< the resource bar's background: a slider of three
    /// Game files: what is installed, its sizes line and a MANAGE… button
    game_files_summary,
    game_files_backed_up, ///< Include in device backups: a switch, with its hint
    game_files_location,  ///< Where the files are: a text row
    /// Your files: where the player's own folder is, and buttons that open
    /// its Saves, Screenshots and Mods folders in the system's file manager;
    /// it changes no setting
    user_folder,
    /// Menu scaling: a strip of Sharp, Whole steps and Unfiltered
    menu_scaling,
    native_density, ///< Native pixel density: a switch
    /// Explosion flash: a strip of Off, Reduced and Full
    explosion_flash,
    /// Zoomed out units: a strip of Rendered, Dots and Icons, of which
    /// Icons is shown faded and cannot be chosen yet
    zoomed_out_units,
    /// After zoom: a drop-down of 1/2, 1/3, 1/4, 1/6, 1/8, 1/12 and 1/16,
    /// locked while Zoomed out units shows Rendered
    zoomed_out_after,
    /// Window frame: a strip of Hidden in play and Always shown
    window_frame,
    hud_scaling, ///< HUD scaling: a switch
};

/// Returns the settings a section shows, top to bottom.
///
/// A section holds any number of rows: when they are taller than the space
/// under its heading, its rows scroll there. Developer's rows stay at its
/// top, over Developer Mode's list of the standard hacks, which scrolls.
///
/// @param page the section
/// @return one or more settings
[[nodiscard]] std::span<const Setting> page_settings(Page page) noexcept;

// Every control has a number: the sections' entries, then the footer's
// buttons, then the scroll bar, then the open section's rows, which have no
// upper end, so a row never takes a fixed control's number. The entries
// keep room for the most sections a dialog lists, so every other number is
// the same whether or not the dialog lists Touch, Controller or Game files.

/// No control: what Dialog::hovered, pressed and focused hold when they name none.
inline constexpr int32_t no_control = -1;
/// The Switch Mod question's SWITCH button, or the Roll Back Mod question's
/// ROLL BACK, while the question shows (Dialog::switch_question). The question's buttons count down from
/// no_control, where no section, row or button of the dialog takes a number.
inline constexpr int32_t question_yes_control = no_control - 1;
/// The question's CANCEL button, while the question shows.
inline constexpr int32_t question_no_control = no_control - 2;
/// The first section's entry in the list; each section's entry is its
/// place among its kind of dialog's sections with Touch and Controller
/// (page_control), whether or not the dialog lists them.
inline constexpr int32_t first_page_control = 0;
/// Restore defaults: the first number after the sections' entries.
inline constexpr int32_t restore_control =
    first_page_control + static_cast<int32_t>(most_listed_pages);
/// Cancel.
inline constexpr int32_t cancel_control = restore_control + 1;
/// OK.
inline constexpr int32_t ok_control = cancel_control + 1;
/// The open section's scroll bar, shown while its rows are taller than the
/// space they scroll in. It takes no keyboard focus.
inline constexpr int32_t scroll_bar_control = ok_control + 1;
/// The open section's first row's control; the next rows' follow it, one
/// for each row the section has.
inline constexpr int32_t first_row_control = scroll_bar_control + 1;
/// Developer's rows, over its list: Enable Developer Mode, then Show
/// performance statistics.
inline constexpr int32_t developer_row_count = 2;
/// Developer's Enable Developer Mode switch, its first row's control.
inline constexpr int32_t developer_mode_control = first_row_control;
/// Developer's Show Active Only switch, under its list.
inline constexpr int32_t active_only_control = first_row_control + developer_row_count;
/// Developer's Restore profile values button, under its list.
inline constexpr int32_t restore_profile_control = active_only_control + 1;
/// The control of the first row of Developer's list that takes input: an
/// area's or a hack's header, or a parameter's control. The next such
/// rows' follow it in the list's order, which has no upper end.
inline constexpr int32_t first_hack_list_control = restore_profile_control + 1;
static_assert(
    first_page_control + static_cast<int32_t>(most_listed_pages) <= restore_control &&
        restore_control < cancel_control && cancel_control < ok_control &&
        ok_control < scroll_bar_control && scroll_bar_control < first_row_control,
    "the sections' entries come first, then the footer's buttons, the scroll bar and the rows"
);
static_assert(
    static_cast<std::size_t>(Page::developer) < most_listed_pages &&
        static_cast<std::size_t>(Page::mod_chat) + 1 == page_count,
    "every section of the engine's settings has an entry's number, and every section a scroll"
);

/// Returns the control of a section's entry in the list: its place among
/// its kind of dialog's sections, each section counted whether or not it is
/// listed, so that Touch is 5, Controller 6, Developer 7 and Game files 8
/// in every dialog of the engine's settings, and a dialog without Touch has
/// no control 5, one without Controller no control 6.
///
/// @param page the section
/// @return its control's number
[[nodiscard]] constexpr int32_t page_control(Page page) noexcept {
    const auto index = static_cast<int32_t>(page);
    const auto first_mod = static_cast<int32_t>(Page::mod_keys);
    return first_page_control + (index >= first_mod ? index - first_mod : index);
}

/// The keys the dialog answers to; a host gives the platform's keys these meanings.
enum class DialogKey : uint8_t {
    enter,     ///< OK
    escape,    ///< Cancel
    up,        ///< the focus to the control above
    down,      ///< the focus to the control below
    left,      ///< the focused control one step down: Off, a lower value
    right,     ///< the focused control one step up: On, a higher value
    space,     ///< presses the focused button or flips the focused switch
    tab,       ///< the focus to the next control
    back_tab,  ///< the focus to the previous control
    page_up,   ///< scrolls the open section up by most of its view
    page_down, ///< scrolls the open section down by most of its view
    home,      ///< scrolls the open section to its top
    end,       ///< scrolls the open section to its end
    yes,       ///< Y: answers the Switch Mod question SWITCH; nothing while it does not show
    no,        ///< N: answers the Switch Mod question CANCEL; nothing while it does not show
};

/// The buttons of the Your files row, left to right: each opens a folder of
/// the player's own folder.
enum class FolderButton : uint8_t {
    saves,       ///< the Saves folder
    screenshots, ///< the Screenshots folder
    mods,        ///< the Mods folder
};
/// The number of buttons the Your files row has.
inline constexpr std::size_t folder_button_count = 3;

/// What Hardware acceleration's status says: whether the graphics card
/// scales the frames, and why not when it does not. The states keep the
/// order the host tests them in; the first that applies is shown. Basic or
/// Full, by the setting or a flag, asks for the graphics card; Off does not.
enum class AccelerationState : uint8_t {
    /// Off, by the setting or --no-hardware-acceleration, and a failed
    /// graphics driver was passed over at this start.
    off_driver_skipped,
    /// Basic or Full, but the machine has under 2 GiB of memory, or does
    /// not say, and a failed graphics driver was passed over at this start.
    needs_memory_driver_skipped,
    /// Not in use: the machine has under 2 GiB of memory, or does not say,
    /// whatever the setting or the flags.
    needs_memory,
    off_by_setting,      ///< Off, by the setting
    off_by_command_line, ///< Off, by --no-hardware-acceleration or --hardware-acceleration=off
    /// Basic or Full, but the environment names a render driver, or the
    /// video driver draws no window, so the processor scales the frames.
    environment_driver,
    /// Basic or Full, but dropped in this run when the machine ran short of
    /// memory.
    too_little_memory,
    /// Basic or Full, waiting for a shared game or a replay to end: in one,
    /// either takes effect from the next game (AccelerationStatus::replay
    /// says which, and AccelerationStatus::asked which level).
    waiting_for_game_end,
    engine_error,   ///< Basic or Full, but an error stopped it for this run
    driver_failed,  ///< Basic or Full, but the graphics driver failed, in this run or before
    game_stopped,   ///< Basic or Full, but the game stopped while using it before
    no_usable_card, ///< Basic or Full, but no usable graphics card was found
    lacks_feature,  ///< Basic or Full, but the graphics card lacks something it needs
    /// Basic or Full, but the game cannot save the files that guard trying it.
    cannot_save,
    next_start, ///< Basic or Full, from the next start
    /// Full, but the game cannot save the files that guard trying it: Basic
    /// is in use in its place.
    full_cannot_save,
    /// Full, but dropped to Basic in this run when the machine ran short of
    /// memory.
    full_too_little_memory,
    /// Full, but the graphics card failed while drawing the battlefield, so
    /// Basic is in use for the rest of the run.
    full_stopped,
    /// Full, but Full failed before on this graphics driver, so Basic is in
    /// use.
    full_failed_before,
    /// Full, but the graphics card lacks something Full needs: Basic is in
    /// use.
    full_lacks_feature,
    /// Full, waiting for a shared game or a replay to end: Basic is in use
    /// for this game, and Full takes effect from the next
    /// (AccelerationStatus::replay says which match).
    full_waiting_for_game_end,
    in_use_on_another_driver, ///< In use, on another graphics driver: one failed
    in_use_no_smoothing,      ///< In use, with no smoothing when zoomed out on this machine
    full_in_use,              ///< Full in use: the graphics card draws the view
    in_use,                   ///< In use
};

/// What the graphics card does on this machine while it is in use, which
/// the status's second line says.
enum class AccelerationReach : uint8_t {
    menus,      ///< it scales the menus and the interface
    zoomed_in,  ///< it scales the interface and the zoomed-in battlefield
    zoomed_out, ///< it scales everything and smooths the zoomed-out battlefield
    /// it scales nothing, and smooths the zoomed-out battlefield
    nearest_zoomed_out,
    nearest_none, ///< it scales nothing: the frames look as with it off
};

/// Hardware acceleration's status, as the host reports it. It names no
/// graphics interface and no driver.
struct AccelerationStatus {
    AccelerationState state{AccelerationState::off_by_setting}; ///< what runs, or why not
    AccelerationReach reach{AccelerationReach::menus};          ///< what it does while in use
    /// The match AccelerationState::waiting_for_game_end and
    /// full_waiting_for_game_end wait for replays a recording rather than
    /// being played with other machines.
    bool replay{};
    /// The level asked for, by the setting or a flag, which
    /// AccelerationState::waiting_for_game_end names.
    HardwareAcceleration asked{HardwareAcceleration::off};
    /// Full's anti-aliasing while it is in use: the samples a pixel across
    /// the graphics card draws the view with, 1 for none, which the second
    /// line of AccelerationState::full_in_use names.
    uint8_t supersample{1};
    /// How many times finer than the window, along each axis, the graphics
    /// card drew the battlefield in the last Full frame: the Enhanced
    /// anti-aliasing row's level, as the texture limit and the memory allow;
    /// 0 while frames are not drawn in Full. The row's hint says what its
    /// level does in Full from it.
    uint8_t full_supersample{};

    friend bool operator==(const AccelerationStatus&, const AccelerationStatus&) = default;
};

/// What an event asks of the host.
enum class DialogAction : uint8_t {
    none,   ///< nothing
    redraw, ///< only its look changed: a hover, the focus, a press, a scroll or the section
    /// Dialog::chosen changed, or Restore defaults was pressed, which asks
    /// for this even when no setting moved: put it in effect and redraw
    changed,
    accepted,  ///< OK: keep Dialog::chosen in effect, save it and close the dialog
    cancelled, ///< Cancel: put Dialog::opened back in effect and close the dialog
    /// MANAGE… was pressed: the host opens the Game files screen and keeps
    /// the dialog open
    manage_game_files,
    /// SWITCH on the Switch Mod question: keep Dialog::chosen in effect,
    /// its Mod setting now the mod chosen, save it, close the dialog, and
    /// reload the game for that mod, back on the main menu
    switch_mod,
    /// A button of Your files, or Mods' OPEN MODS FOLDER: open the folder
    /// Dialog::folder_to_open names in the system's file manager, making it
    /// first when it is missing; the dialog stays open. A folder that cannot
    /// be opened is told to the dialog with set_folder_notice.
    open_folder,
    /// ROLL BACK on the Roll Back Mod question: swap the folder
    /// Dialog::roll_back_folder names with the version its .backup keeps;
    /// the dialog stays open, and a failure is told with set_folder_notice
    roll_back_mod,
};

/// How the OA button looks.
enum class ButtonLook : uint8_t {
    idle,    ///< at rest
    hovered, ///< the pointer is over it
    pressed, ///< a press on it is held
};

/// A section of a check's own, shown in place of a section's rows: the rows
/// it holds, and how each is locked. It lets the dialog's tests and the
/// game's own checks give a section more rows than the view under its
/// heading holds, and lock any row. A host never sets one.
struct SectionHooks {
    void* context{}; ///< passed back to each function
    /// Returns the settings a section shows, top to bottom, in place of
    /// page_settings(page), and on Developer of its list and the list's
    /// footer too; the span stays valid while the hooks are set. Null shows
    /// page_settings(page).
    std::span<const Setting> (*settings)(void* context, Page page){};
    /// Returns a setting's lock, given the one Dialog::locks puts on it;
    /// null keeps that one.
    Lock (*lock)(void* context, Setting setting, Lock lock){};
    /// Tells whether a setting's hint lines are its status: a locked switch
    /// or strip whose hint lines are its status shows its lock where its
    /// control was, and only its label line fades. Null leaves it as the
    /// dialog has it.
    bool (*hint_is_status)(void* context, Setting setting){};
};

/// Developer Mode's list of the standard hacks, in the Developer section:
/// how the profile the game plays resolves each, which of the list's parts
/// are open, and its filter.
struct DeveloperList {
    /// Every standard hack as the profile resolves it, without overrides,
    /// in the registry's order (oa::data::mod_profile::standard_hacks).
    std::vector<oa::data::mod_profile::HackState> profile;
    /// Which areas are open (1) or closed (0), in developer_areas' order;
    /// every one starts closed.
    std::vector<uint8_t> areas_open;
    /// Which hacks are open (1) or closed (0), in the registry's order;
    /// every one starts closed.
    std::vector<uint8_t> hacks_open;
    /// Show Active Only: the list shows only the hacks that are on, and the
    /// areas that hold one.
    bool active_only{};
};

/// One area of the standard hacks, as Developer Mode groups them.
struct HackArea {
    std::string_view name;  ///< the registry's area, such as "ui"
    std::string_view title; ///< its name as players see it, in English, such as "Interface"
    /// Its hacks' places among oa::data::mod_profile::standard_hacks,
    /// alphabetically by their titles in English.
    std::vector<std::size_t> hacks;
};

/// Returns the areas of the standard hacks. The list shows the areas, and
/// the hacks within each, alphabetically by their titles in the language
/// shown, which in English is this order.
///
/// @return each area, alphabetically by its title in English
[[nodiscard]] std::span<const HackArea> developer_areas();

struct Dialog;

/// What Mods shows of a mod folder besides its title: read from its
/// oamod.yaml and the oamod.png beside it.
struct ModDetails {
    std::string version;     ///< shown at the row's top right; "N/A" without an oamod.yaml
    std::string description; ///< the row's second line
    /// The folder holds an oamod.yaml; without one its files layer over the
    /// game folder's and the game plays by 3.1c's own rules.
    bool has_profile{true};
    uint32_t badge_width{};            ///< the badge's columns; 0 shows the blank placeholder
    uint32_t badge_height{};           ///< the badge's rows
    std::vector<uint8_t> badge_pixels; ///< RGBA, top row first
    /// The version the folder holds now, as a roll back names it ("1.0", or
    /// "1.0 revision 3" beside a kept version of the same version); empty
    /// when the folder keeps no earlier version, and its row shows no ROLL
    /// BACK.
    std::string roll_back_from{};
    /// The version its .backup keeps, named as roll_back_from is.
    std::string roll_back_to{};
};

/// What the question over Mods asks about its row.
enum class ModQuestion : uint8_t {
    switch_mod, ///< Switch Mod: SWITCH plays the row's mod
    roll_back,  ///< Roll Back Mod: ROLL BACK swaps the row's folder and its .backup
};

/// One row of Mods, in the order Mods lists them: the mod played, then No
/// Mod when it is not the one played, then every other mod by title.
struct ModRow {
    /// The mod folder's place among Dialog::mod_folders; no_mod_row for No Mod.
    int32_t offered{};
    bool playing{}; ///< the game plays it now
};

/// ModRow::offered for No Mod.
inline constexpr int32_t no_mod_row = -1;
/// Dialog::switch_question while no question shows.
inline constexpr int32_t no_question = -2;

/// Returns Mods' rows in the order it lists them.
///
/// @param dialog the dialog
/// @return one row for No Mod and one for each offered mod folder
[[nodiscard]] std::vector<ModRow> mod_rows(const Dialog& dialog);

/// One open dialog. A host reads opened, chosen, defaults, restored, page
/// and forget_renderer_failures, sets acceleration, and reads and may keep
/// developer between openings; section_hooks is set only by tests and
/// checks; the other members after them are the dialog's own.
struct Dialog {
    EngineSettings opened{};           ///< in effect as it opened; Cancel puts them back
    EngineSettings chosen{};           ///< what it shows; in effect as they change
    EngineSettings defaults{};         ///< what Restore defaults sets
    Locks locks{};                     ///< what cannot be changed now
    AccelerationStatus acceleration{}; ///< Hardware acceleration's status
    std::string version;               ///< the header's version text
    Page page{Page::mods};             ///< the section shown
    bool restored{};                   ///< Restore defaults was pressed
    /// The times the player asked, since the dialog opened, for the graphics
    /// card to be tried afresh: each press of Restore defaults, and each
    /// time Hardware acceleration passed to a higher level, from Off to
    /// Basic or Full or from Basic to Full. The count stays if the row goes
    /// back down.
    uint32_t forget_renderer_failures{};
    int32_t hovered{no_control}; ///< the control under the pointer
    int32_t pressed{no_control}; ///< the control a held press is on
    int32_t focused{no_control}; ///< the control with the keyboard focus; shown once a key moves it
    bool dragging{};             ///< the held press drags a slider's knob or the scroll bar's thumb
    /// Each section's scroll offset, in source pixels from its top; clamped
    /// to the section's limit wherever it is used. Every section starts at
    /// its top when the dialog opens.
    std::array<int32_t, page_count> scroll{};
    /// The part of a source pixel the wheel has turned and not yet
    /// scrolled; negative towards the section's top.
    float wheel_rows{};
    /// The pixel row of the scroll bar's thumb, from the thumb's top, that a
    /// held press on the bar holds.
    int32_t scroll_grab{};
    bool pointer_known{}; ///< the dialog has had a pointer event
    int32_t pointer_x{};  ///< the last pointer event's column, in source pixels
    int32_t pointer_y{};  ///< the last pointer event's row, in source pixels
    /// The columns a finger's held press was moved by to reach the control
    /// it took (dialog_finger_down); the press's moves and its release are
    /// moved as far. Zero for a press on a control and for every mouse
    /// press.
    int32_t finger_shift_x{};
    int32_t finger_shift_y{}; ///< the rows a finger's held press was moved by, as finger_shift_x
    /// A check's own section in place of the dialog's; null for the dialog's.
    const SectionHooks* section_hooks{};
    /// The unit limit slider's highest stop (highest_offered_unit_limit).
    uint16_t highest_offered_unit{highest_unit_limit};
    /// The Screen size slider's stops, in order: Desktop, then the sizes
    /// the display offers, narrower first. open_dialog sets screen_sizes;
    /// the game sets the display's own once the dialog has opened. Never
    /// empty.
    std::vector<ScreenSize> offered_screen_sizes{screen_sizes.begin(), screen_sizes.end()};
    /// The window's own size, which Screen size shows in a window until its
    /// knob moves, whatever the setting chosen: Desktop leaves a window as
    /// it is. Empty in full screen, and once the knob has moved. The game
    /// sets it with the stops, which list it.
    std::optional<ScreenSize> window_screen_size;
    /// The stop of offered_screen_sizes that stands for the window's own
    /// size where the display offers no such size, which the slider shows
    /// as "Custom"; empty when there is none. The game sets it with the
    /// stops.
    std::optional<ScreenSize> custom_screen_size;
    /// The names of the offered mod folders, in the order of Inputs::mod_folders.
    std::vector<std::string> mod_names;
    /// The offered mod folders' paths, in the same order (Inputs::mod_folders).
    std::vector<std::string> mod_folders;
    /// What Mods shows of each offered mod folder, in the same order; a
    /// folder without one shows its name alone.
    std::vector<ModDetails> mod_details;
    /// The mod folder the game plays now, as an absolute UTF-8 path; empty
    /// for none. Mods lists it first, marked PLAYING.
    std::string playing_mod_folder;
    /// The mod the question over Mods asks about (mod_question), as
    /// ModRow::offered names it (no_mod_row for No Mod): the one the Switch
    /// Mod question offers to switch to, or the one the Roll Back Mod
    /// question offers to roll back; no_question while no question shows.
    /// The question lies over the dialog and takes every pointer event and
    /// key. A row of Mods whose ModDetails::roll_back_from is set shows a
    /// ROLL BACK button, whose control is OPEN MODS FOLDER's and one more
    /// for each row before it and itself.
    int32_t switch_question{no_question};
    /// The question's button the keys mark, which Enter and Space press:
    /// CANCEL when set, else SWITCH or ROLL BACK.
    bool question_marks_no{};
    /// What the question asks about the row switch_question names.
    ModQuestion mod_question{ModQuestion::switch_mod};
    /// The folder the last DialogAction::roll_back_mod asks to roll back, as
    /// an absolute UTF-8 path, the row's in mod_folders.
    std::string roll_back_folder;
    /// The player's own folder, as an absolute UTF-8 path, which the Your
    /// files row shows; the host sets it once the dialog has opened. Empty
    /// shows no path.
    std::string user_folder;
    /// The Your files button the keys mark, which Space presses: Saves at
    /// first.
    FolderButton folder_marked{FolderButton::saves};
    /// The Your files button under the pointer, or a held press is on;
    /// folder_button_count for none.
    std::size_t folder_hovered{folder_button_count};
    /// The folder the last DialogAction::open_folder asks for.
    FolderButton folder_to_open{FolderButton::saves};
    /// Why the last folder asked for could not be opened, which the Your
    /// files row's second hint line shows in amber; empty for none.
    std::string folder_notice;
    /// Which settings it shows.
    DialogKind kind{DialogKind::engine};
    /// The game has touch controls, so that the engine's settings list
    /// Touch (dialog_pages); a host gives it to open_dialog and keeps it
    /// with set_touch_controls.
    bool touch{};
    /// A gamepad has sent input in this run, so that the engine's settings
    /// list Controller (dialog_pages); a host gives it to open_dialog and
    /// keeps it with set_controller_section.
    bool controller{};
    /// The gamepad in use reaches the game through Steam Input, so that
    /// Controller's first row says so and how to turn it off; a host keeps
    /// it with set_controller_section.
    bool steam_input{};
    /// The game runs on a Steam Deck whose screen refreshes this many times
    /// a second (Inputs::steam_deck_panel_hz), so that Maximum frame rate's
    /// hint has a second line naming the rate it starts at; 0 elsewhere. A
    /// host sets it as the dialog opens.
    uint32_t steam_deck_panel_hz{};
    /// The Game files section is listed (dialog_pages): a host gives it to
    /// open_dialog where the platform brings game files in and the dialog is
    /// the main menu's.
    bool game_files{};
    /// The Game files section's summary: "3.1c · Core Contingency · Battle Tactics · music · 1 mod".
    std::string game_files_summary{};
    /// Its sizes line: what the game files use and what is free on the device.
    std::string game_files_sizes{};
    /// Where the files are: the platform's folder_location word.
    std::string game_files_location{};
    /// The device's name in the backups switch's hint.
    std::string game_files_device{};
    /// Developer Mode's list of the standard hacks.
    DeveloperList developer{};
    /// The language the operating system's preferred locales choose, which
    /// the Language drop-down's System default names; null names English.
    const oa::data::languages::Language* system_language{};
    /// The tags of the languages whose packs ask for multiplayer chat in
    /// UTF-8 (unicode: true): while the language chosen is one, Enable
    /// Unicode Multiplayer Chat shows On, locked. A host sets it as the
    /// dialog opens.
    std::vector<std::string> unicode_chat_languages{};
    /// The row whose drop-down list is open: its control; no_control while
    /// no list is open. An open list takes every pointer event and key.
    int32_t open_list{no_control};
    /// The open list's item the pointer or the keys mark, from 0.
    int32_t list_marked{};
    /// The open list's item a held press is on; -1 for none.
    int32_t list_pressed{-1};
    /// The open list's first item shown, while it holds more items than it
    /// shows.
    int32_t list_first{};
};

/// Returns every standard hack as Developer Mode shows it: as the profile
/// resolves it, with the chosen overrides laid over it while Developer Mode
/// is on (EngineSettings::developer_mode).
///
/// @param dialog the dialog
/// @return one state for each standard hack, in the registry's order
[[nodiscard]] std::vector<oa::data::mod_profile::HackState> shown_hacks(const Dialog& dialog);

/// Counts the standard hacks that are on as Developer Mode shows them: the
/// X of Show Active Only (X/Y), whose Y is every standard hack.
///
/// @param dialog the dialog
/// @return the hacks that are on
[[nodiscard]] std::size_t active_hack_count(const Dialog& dialog);

/// The font a text of the dialog is drawn in.
enum class DialogFont : uint8_t {
    regular, ///< DialogFonts::regular
    small,   ///< DialogFonts::small
};

/// One part of the dialog as it is drawn now: a text or a control, and the
/// rectangle it keeps to.
struct LayoutPart {
    oa::ui::frontend_renderer::SourceRect rect{}; ///< in source pixels from the dialog's top left
    std::string text;                             ///< the text drawn in it; empty for a control
    DialogFont font{};                            ///< the font of text
    int32_t tracking{};          ///< extra columns after each of text's glyphs but the last
    int32_t control{no_control}; ///< the control it is; no_control for a text
};

/// The fonts the dialog and the OA button draw their texts in.
struct DialogFonts;

/// Returns the parts the dialog draws now: the header's texts, the list's
/// entries, the open section's heading, labels, hints, locks, controls and
/// values, the scroll bar while the section scrolls, and the footer's
/// buttons. No two overlap, and each lies inside the dialog's edge. Of the
/// open section's rows only the parts that lie wholly in its view are
/// listed; a part the view cuts is drawn but not listed. The player's own
/// folder's path shows as its tail that fits its place (path_tail): measured in the
/// fonts when they are given, else at estimated_character_width a
/// character.
///
/// @param dialog the dialog
/// @param fonts the fonts the dialog is drawn in; null to estimate widths
/// @return the parts
[[nodiscard]] std::vector<LayoutPart>
dialog_layout(const Dialog& dialog, const DialogFonts* fonts = nullptr);

/// The fonts the dialog and the OA button draw their texts in.
struct DialogFonts {
    oa::ui::frontend_renderer::TextFont regular; ///< labels, values and buttons
    oa::ui::frontend_renderer::TextFont small;   ///< the section heading, the hints and the version
    /// The characters each font draws, for UTF-8 texts such as a language's
    /// name in itself; the modern fonts draw the others.
    oa::present::FontCharacters regular_characters{};
    oa::present::FontCharacters small_characters{}; ///< the small font's
};

/// Returns a UTF-8 text's width as the dialog draws it: the characters a
/// font draws at its glyphs' widths, and the others in the modern fonts.
///
/// @param fonts the dialog's fonts
/// @param font which of them
/// @param text the text, in UTF-8
/// @return the width, in source pixels
[[nodiscard]] int32_t
dialog_text_width(const DialogFonts& fonts, DialogFont font, std::string_view text);

/// Loads the dialog's fonts from the game's files: the game's button font as
/// the regular one and its label font as the small one, each readied for
/// text in one colour (oa::ui::frontend_renderer::text_font): its letters
/// keep their shading and lose the dark outline round them.
///
/// Throws std::runtime_error when a font or the game's palette is missing, or a
/// font is malformed.
///
/// @param assets the game's files
/// @return the fonts
[[nodiscard]] DialogFonts load_dialog_fonts(oa::AssetStore& assets);

/// What Mods lists besides No Mod.
struct ModOffer {
    /// The offered mod folders' titles, as Mods shows them.
    std::span<const std::string> names{};
    /// Their paths, in the same order, as Inputs::mod_folders holds them.
    std::span<const std::string> folders{};
    /// What Mods shows of each, in the same order; missing ones show the
    /// title alone.
    std::span<const ModDetails> details{};
    /// The mod folder the game plays now; empty for none.
    std::string_view playing{};
};

/// Opens the dialog over settings in effect.
///
/// @param[out] dialog the dialog; whatever it held is replaced
/// @param current the settings in effect
/// @param defaults what Restore defaults sets
/// @param locks what cannot be changed now
/// @param version the header's version text
/// @param page the section to show
/// @param acceleration Hardware acceleration's status
/// @param highest_offered_unit the unit limit slider's highest stop, in units
///     per player (highest_offered_unit_limit)
/// @param mods the mods Mods lists besides No Mod
/// @param profile_hacks every standard hack as the profile the game plays
///     resolves it, in the registry's order (DeveloperList::profile); empty
///     gives every one off, as 3.1c plays it
/// @param system_language the language the operating system's preferred
///     locales choose, which the Language drop-down's System default names;
///     null names English
/// @param touch the game has touch controls, so that the dialog lists Touch
///     (Dialog::touch); without them, a dialog asked to show Touch shows its
///     first section instead
/// @param game_files the dialog lists Game files (Dialog::game_files);
///     without it, a dialog asked to show Game files shows its first section
/// @param controller a gamepad has sent input in this run, so that the
///     dialog lists Controller (Dialog::controller); without it, a dialog
///     asked to show Controller shows its first section
void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page,
    const AccelerationStatus& acceleration = {},
    uint16_t highest_offered_unit = highest_unit_limit,
    const ModOffer& mods = {},
    std::span<const oa::data::mod_profile::HackState> profile_hacks = {},
    const oa::data::languages::Language* system_language = nullptr,
    bool touch = false,
    bool game_files = false,
    bool controller = false
);

/// Opens the dialog over a mod's options (ui.options-dialog): its sections
/// list the mod options alone, and only EngineSettings::mod_options change.
///
/// @param[out] dialog the dialog; whatever it held is replaced
/// @param current the settings in effect, the mod's options among them
/// @param defaults what Restore defaults sets
/// @param locks what cannot be changed now: Locks::mex_snap and wreck_snap
/// @param version the header's version text
/// @param page the section to show; one of the mod options' or the first
void open_mod_options_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page = Page::mod_keys
);

/// Opens the dialog over the Language section alone
/// (DialogKind::language_text), as the Game files screen opens it before
/// the game's files are installed: it lists that one section and keeps
/// Restore defaults, which restores only that section's settings, Cancel
/// and OK. Drawn with DialogFonts that hold no glyphs, every text is in the
/// modern fonts.
///
/// @param[out] dialog the dialog; whatever it held is replaced
/// @param current the settings in effect
/// @param defaults what Restore defaults sets
/// @param locks what cannot be changed now
/// @param version the header's version text
/// @param system_language the language the operating system's preferred
///     locales choose, which the Language drop-down's System default names;
///     null names English
void open_language_text_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    const oa::data::languages::Language* system_language = nullptr
);

/// Tells the dialog why the folder its last DialogAction::open_folder asked
/// for could not be opened, or that it was; the Your files row's second hint
/// line, or the line under Mods' list, shows the reason in amber until
/// another folder opens.
///
/// @param[in,out] dialog the dialog
/// @param reason why it could not be opened, in a few words; empty when it
///     opened
/// @return DialogAction::redraw when the line changed, else DialogAction::none
[[nodiscard]] DialogAction set_folder_notice(Dialog& dialog, std::string_view reason);

/// Shortens a folder's path to a tail that fits a width: the whole path when
/// it fits; else "..." and the most of its last components, each whole, that
/// fit after it, with the separator before them; else "..." and as much of
/// the end of its last component as fits. A separator at the path's end is
/// dropped first.
///
/// @param path the path, in UTF-8; '/' and '\\' separate its components
/// @param width the room, in source pixels
/// @param text_width a text's width in the font it is drawn in
/// @return the text to show, in UTF-8; "..." alone when nothing more fits
[[nodiscard]] std::string path_tail(
    std::string_view path, int32_t width, const std::function<int32_t(std::string_view)>& text_width
);

/// The width dialog_layout counts each character of the player's own
/// folder's path at without the fonts, in source pixels.
inline constexpr int32_t estimated_character_width = 7;

/// Gives the dialog Hardware acceleration's status as it is now; a host
/// calls it each frame while the dialog is open.
///
/// @param[in,out] dialog the dialog
/// @param acceleration the status
/// @return DialogAction::redraw when the status changed, else DialogAction::none
[[nodiscard]] DialogAction
set_acceleration_status(Dialog& dialog, const AccelerationStatus& acceleration) noexcept;

/// Tells the dialog whether the game has touch controls now; a host calls
/// it each frame while the dialog is open, so that Touch is listed from the
/// moment a finger turns the touch controls on. A dialog that stops listing
/// Touch while it shows it shows its first section, and the focus leaves
/// Touch's controls.
///
/// @param[in,out] dialog the dialog
/// @param touch the game has touch controls (Dialog::touch)
/// @return DialogAction::redraw when the list changed, else DialogAction::none
[[nodiscard]] DialogAction set_touch_controls(Dialog& dialog, bool touch) noexcept;

/// Tells the dialog whether a gamepad has sent input in this run and
/// whether it reaches the game through Steam Input; a host calls it each
/// frame while the dialog is open (as set_touch_controls), so that
/// Controller is listed from the moment a gamepad is used and its Steam
/// Input notice shows while it applies. A dialog that stops listing
/// Controller while it shows it shows its first section, and the focus
/// leaves Controller's controls; the notice coming or going while
/// Controller shows keeps the focus on the row it was on.
///
/// @param[in,out] dialog the dialog
/// @param controller a gamepad has sent input in this run (Dialog::controller)
/// @param steam_input the gamepad reaches the game through Steam Input
///     (Dialog::steam_input)
/// @return DialogAction::redraw when the list or the section changed, else
///     DialogAction::none
[[nodiscard]] DialogAction
set_controller_section(Dialog& dialog, bool controller, bool steam_input) noexcept;

/// Moves the pointer: hovers a control, or drags what a held press holds. A
/// slider's knob follows the pointer's column only; the scroll bar's thumb
/// follows its row only, wherever the pointer goes, and the open section
/// scrolls with the thumb. Over an open drop-down list it marks the item
/// under it. While a finger's press is held, the point is moved as the
/// press was (dialog_finger_down). While a question shows, it hovers the
/// question's buttons only.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the move asks of the host
[[nodiscard]] DialogAction dialog_pointer_move(Dialog& dialog, int32_t x, int32_t y);

/// Presses the pointer's button: a press on a control holds it, and moves
/// the keyboard focus to it once a key has shown the focus. A press on a
/// slider moves its knob to the nearest stop. A press on the scroll bar
/// holds the bar and leaves the focus where it is: on the thumb it grabs
/// the thumb where it is pressed; on the well above or below the thumb, the
/// thumb's middle jumps to the pointer, the section scrolls with it and the
/// drag starts there. While a drop-down list is open, a press on one of its
/// items holds the item, and a press anywhere else, its field included,
/// closes the list and does nothing more. While a question shows, a press
/// on one of its buttons holds the button, and a press anywhere else does
/// nothing.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the press asks of the host
[[nodiscard]] DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y);

/// Presses with a finger: as dialog_pointer_down, but a press with no
/// control under it takes the nearest control whose pressable part lies
/// within `reach` of it, pressed at that part's point nearest the finger;
/// while a drop-down list is open, the nearest of its items, and while the
/// Switch Mod question shows, the nearer of its buttons. The press's
/// moves and its release are then moved as far as the press was
/// (Dialog::finger_shift_x and finger_shift_y), so that a release where the
/// finger landed acts on the control it took. With nothing within reach it
/// does what dialog_pointer_down does.
///
/// @param[in,out] dialog the dialog
/// @param x the finger's column, in source pixels from the dialog's left edge
/// @param y the finger's row, in source pixels from the dialog's top edge
/// @param reach how far a control may lie from the finger, in source pixels:
///     the touch controls' pick distance in the dialog's own pixels
/// @return what the press asks of the host
[[nodiscard]] DialogAction dialog_finger_down(Dialog& dialog, int32_t x, int32_t y, int32_t reach);

/// Releases the pointer's button: a release over the control the press held
/// acts on it; over a drop-down's field, it opens the field's list, marking
/// the item chosen. A release over the list item the press held chooses the
/// item and closes the list. A finger's release is moved as its press was
/// (dialog_finger_down). A release over the question's button the press
/// held answers the question.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the release asks of the host
[[nodiscard]] DialogAction dialog_pointer_up(Dialog& dialog, int32_t x, int32_t y);

/// Takes a key.
///
/// Page Up, Page Down, Home and End scroll the open section whatever has
/// the focus, and never move or show it. A key that moves the focus onto a
/// row, or acts on a focused row, first scrolls the least that shows the
/// row whole. Space opens a focused drop-down's list, and Left and Right
/// step its choice. While a list is open the keys work it: Up and Down mark
/// the item above or below, Page Up and Page Down a list's height of items
/// away, Home and End the first and the last; Enter and Space choose the
/// marked item and close the list; Escape closes it unchanged; Tab and
/// Shift+Tab close it and move the focus. In Developer Mode's list, Space
/// opens or closes an area or a hack, and Left and Right close and open an
/// area or turn a hack off and on. While a question shows, the keys answer
/// it: Y, or Enter or Space while SWITCH is marked, answers SWITCH; N,
/// Escape, or Enter or Space while CANCEL is marked, answers CANCEL; Left,
/// Right, Tab and Shift+Tab move the mark. Y and N do nothing else. On
/// Mods, Space on a row other than the one played asks the question.
///
/// @param[in,out] dialog the dialog
/// @param key the key's meaning
/// @return what the key asks of the host
[[nodiscard]] DialogAction dialog_key(Dialog& dialog, DialogKey key);

/// Turns the mouse wheel over the dialog: scrolls the open section 24 source
/// pixels a notch, when its rows are taller than the space they scroll in.
/// A fraction of a pixel carries over to the next turn; what is carried
/// towards an end the section has reached is dropped, and all of it when
/// another section shows. A turn outside the dialog, or while a press is
/// held, does nothing. While a drop-down list is open, a turn over it
/// scrolls a list that holds more items than it shows, an item a notch,
/// and the section stays. While a question shows, a turn does nothing.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @param notches the wheel's turn, positive away from the player, which
///     scrolls towards the section's top
/// @return what the turn asks of the host: DialogAction::redraw when the
///     section scrolled, else DialogAction::none
[[nodiscard]] DialogAction dialog_wheel(Dialog& dialog, int32_t x, int32_t y, float notches);

/// Tells whether a point lies on the dialog.
///
/// @param x the point's column, in source pixels from the dialog's left edge
/// @param y the point's row, in source pixels from the dialog's top edge
/// @return true inside its dialog_width by dialog_height
[[nodiscard]] bool dialog_contains(int32_t x, int32_t y) noexcept;

/// Draws the dialog. Its header shows the Open Annihilation icon, scaled
/// to 20 by 20 source pixels at the surface's own resolution; without the
/// icon it shows the OA mark, green letters in a green outlined square.
///
/// @param[in,out] target the surface
/// @param placement where the dialog's top left corner lands, and its scale
/// @param dialog the dialog
/// @param fonts its fonts
/// @param icon the Open Annihilation icon; an empty picture draws the OA mark
void draw_dialog(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    const Dialog& dialog,
    const DialogFonts& fonts,
    const oa::ui::frontend_renderer::RgbaPicture& icon
);

/// Draws the OA button: a small bevelled square showing the Open
/// Annihilation icon, scaled to the button's side less 3 source pixels all
/// round at the surface's own resolution. Under the pointer the button
/// lights and a green outline rings the icon; held, its bevel sinks and
/// the icon moves one source pixel right and down. Without the icon it
/// shows the OA mark, green letters in a green outlined square, lighter
/// under the pointer and held.
///
/// @param[in,out] target the surface
/// @param placement where the button's top left corner lands, and its scale
/// @param side the button's side, in source pixels (menu_button_side or ingame_button_side)
/// @param look how it looks
/// @param fonts the dialog's fonts
/// @param icon the Open Annihilation icon; an empty picture draws the OA mark
void draw_oa_button(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    int32_t side,
    ButtonLook look,
    const DialogFonts& fonts,
    const oa::ui::frontend_renderer::RgbaPicture& icon
);

/// Draws the Open Annihilation mark alone, for other screens that show it
/// as the dialog does: the icon scaled to fill a square at the surface's
/// own resolution, or without the icon the OA mark the OA button shows at
/// rest, green letters in a green outlined square 20/32 of the square's
/// side in its middle. Nothing else of the square is drawn.
///
/// @param[in,out] target the surface
/// @param placement where the square's top left corner lands, and its scale
/// @param side the square's side, in source pixels; 0 or less draws nothing
/// @param icon the Open Annihilation icon; an empty picture draws the OA mark
void draw_oa_mark(
    oa::ui::frontend_renderer::Surface& target,
    const oa::ui::frontend_renderer::Placement& placement,
    int32_t side,
    const oa::ui::frontend_renderer::RgbaPicture& icon
);

} // namespace oa::ui::engine_settings
