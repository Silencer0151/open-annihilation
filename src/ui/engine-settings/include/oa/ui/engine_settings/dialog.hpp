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
    path_search,   ///< AI & Pathfinding
    controls,      ///< Controls & Input
    gameplay,      ///< Gameplay: the unit limit and the mod
    graphics,      ///< Graphics
    language_text, ///< Language & Text: how game text is drawn
    /// Developer, after a divider: its rows over Developer Mode's list of
    /// the standard hacks
    developer,
    mod_keys,   ///< the mod's keys
    mod_patrol, ///< what patrolling builders do
    mod_guard,  ///< what guarding builders do
    mod_tools,  ///< the build tools and the mex snap
    mod_chat,   ///< the wreck snap, the chat and the resource bar
};

/// The number of sections, of both kinds of dialog.
inline constexpr std::size_t page_count = 11;
/// The most sections a dialog lists: the engine's settings' six; a mod's
/// options have five.
inline constexpr std::size_t most_listed_pages = 6;

/// Which settings a dialog shows.
enum class DialogKind : uint8_t {
    engine,      ///< the engine's settings: the first six sections
    mod_options, ///< a mod's options: the last five sections
};

/// Returns the sections a kind of dialog lists, in order.
///
/// @param kind the dialog's kind
/// @return six sections for the engine's settings, five for a mod's options
[[nodiscard]] std::span<const Page> dialog_pages(DialogKind kind) noexcept;

/// The settings, as the dialog's rows show them.
enum class Setting : uint8_t {
    path_search,       ///< Pathfinding cycles: a slider
    wheel_zoom,        ///< Mouse wheel zoom: a switch
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
    modern_fonts,      ///< Use modern fonts for game text: a switch
    text_outline,      ///< Font outline: a switch
    text_shadow,       ///< Font shadow: a switch
    text_background,   ///< Game text background: a switch
    text_size,         ///< Text size: a slider, locked while modern fonts are off
    mod,               ///< Mod: a slider of none and the offered mod folders
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
// upper end, so a row never takes a fixed control's number.

/// No control: what Dialog::hovered, pressed and focused hold when they name none.
inline constexpr int32_t no_control = -1;
/// The first section's entry in the list; the others follow in the order
/// the dialog lists them (dialog_pages).
inline constexpr int32_t first_page_control = 0;
/// Restore defaults.
inline constexpr int32_t restore_control = 6;
/// Cancel.
inline constexpr int32_t cancel_control = 7;
/// OK.
inline constexpr int32_t ok_control = 8;
/// The open section's scroll bar, shown while its rows are taller than the
/// space they scroll in. It takes no keyboard focus.
inline constexpr int32_t scroll_bar_control = 9;
/// The open section's first row's control; the next rows' follow it, one
/// for each row the section has.
inline constexpr int32_t first_row_control = 10;
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

/// Returns the control of a section's entry in the list: its place among
/// its kind of dialog's sections.
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
};

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
    Page page{Page::path_search};      ///< the section shown
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
    /// A check's own section in place of the dialog's; null for the dialog's.
    const SectionHooks* section_hooks{};
    /// The unit limit slider's highest stop (highest_offered_unit_limit).
    uint16_t highest_offered_unit{highest_unit_limit};
    /// The names of the offered mod folders, in the order of Inputs::mod_folders.
    std::vector<std::string> mod_names;
    /// Which settings it shows.
    DialogKind kind{DialogKind::engine};
    /// Developer Mode's list of the standard hacks.
    DeveloperList developer{};
    /// The language the operating system's preferred locales choose, which
    /// the Language drop-down's System default names; null names English.
    const oa::data::languages::Language* system_language{};
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

/// Returns the parts the dialog draws now: the header's texts, the list's
/// entries, the open section's heading, labels, hints, locks, controls and
/// values, the scroll bar while the section scrolls, and the footer's
/// buttons. No two overlap, and each lies inside the dialog's edge. Of the
/// open section's rows only the parts that lie wholly in its view are
/// listed; a part the view cuts is drawn but not listed.
///
/// @param dialog the dialog
/// @return the parts
[[nodiscard]] std::vector<LayoutPart> dialog_layout(const Dialog& dialog);

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
/// @param mod_names the names of the offered mod folders, in the order of
///     Inputs::mod_folders
/// @param profile_hacks every standard hack as the profile the game plays
///     resolves it, in the registry's order (DeveloperList::profile); empty
///     gives every one off, as 3.1c plays it
/// @param system_language the language the operating system's preferred
///     locales choose, which the Language drop-down's System default names;
///     null names English
void open_dialog(
    Dialog& dialog,
    const EngineSettings& current,
    const EngineSettings& defaults,
    const Locks& locks,
    std::string_view version,
    Page page,
    const AccelerationStatus& acceleration = {},
    uint16_t highest_offered_unit = highest_unit_limit,
    std::span<const std::string> mod_names = {},
    std::span<const oa::data::mod_profile::HackState> profile_hacks = {},
    const oa::data::languages::Language* system_language = nullptr
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

/// Gives the dialog Hardware acceleration's status as it is now; a host
/// calls it each frame while the dialog is open.
///
/// @param[in,out] dialog the dialog
/// @param acceleration the status
/// @return DialogAction::redraw when the status changed, else DialogAction::none
[[nodiscard]] DialogAction
set_acceleration_status(Dialog& dialog, const AccelerationStatus& acceleration) noexcept;

/// Moves the pointer: hovers a control, or drags what a held press holds. A
/// slider's knob follows the pointer's column only; the scroll bar's thumb
/// follows its row only, wherever the pointer goes, and the open section
/// scrolls with the thumb. Over an open drop-down list it marks the item
/// under it.
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
/// closes the list and does nothing more.
///
/// @param[in,out] dialog the dialog
/// @param x the pointer's column, in source pixels from the dialog's left edge
/// @param y the pointer's row, in source pixels from the dialog's top edge
/// @return what the press asks of the host
[[nodiscard]] DialogAction dialog_pointer_down(Dialog& dialog, int32_t x, int32_t y);

/// Releases the pointer's button: a release over the control the press held
/// acts on it; over a drop-down's field, it opens the field's list, marking
/// the item chosen. A release over the list item the press held chooses the
/// item and closes the list.
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
/// area or turn a hack off and on.
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
/// and the section stays.
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

} // namespace oa::ui::engine_settings
