// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Game files screen's model, with no SDL: what the app tells the screen
// (its step, sheet, banner, problem and rows), its texts through the
// interface catalogue, its layout in points for the tablet and phone forms,
// the hit test, the focus order, and what a press or a key asks the app to
// do. The app maps the import's state onto the model and paints the layout.
#pragma once

#include "oa/ui/display_layout.hpp"
#include "oa/ui/touch_hud.hpp"
#include <array>
#include <cstddef>
#include <optional>
#include <stdint.h>
#include <string>
#include <string_view>
#include <vector>

namespace oa::ui::game_files {

using oa::ui::display_layout::Insets;
using oa::ui::display_layout::Point;
using oa::ui::display_layout::Rect;
using oa::ui::touch_hud::DeviceClass;

/// A colour, 0 to 255 a channel.
struct Colour {
    uint8_t r{};    ///< red
    uint8_t g{};    ///< green
    uint8_t b{};    ///< blue
    uint8_t a{255}; ///< opacity
};

inline constexpr Colour background_colour{0x1b, 0x1e, 0x19}; ///< the screen behind everything
inline constexpr Colour panel_colour{0x23, 0x27, 0x21};      ///< cards, rows and sheets
inline constexpr Colour line_colour{0x3a, 0x40, 0x37};       ///< outlines and dividers
inline constexpr Colour text_colour{0xe8, 0xea, 0xe4};       ///< text
inline constexpr Colour dim_colour{0x9a, 0xa0, 0x94};        ///< secondary text
inline constexpr Colour green_colour{0x9c, 0xcc, 0x3c};      ///< the main button, found, done
inline constexpr Colour amber_colour{0xe8, 0xb4, 0x4c};      ///< warnings
inline constexpr Colour red_colour{0xe0, 0x6c, 0x5c};        ///< problems and removals

inline constexpr float min_button_points = 44.0f; ///< every button is at least this high
inline constexpr float pick_reach_points = oa::ui::touch_hud::gadget_pick_points; ///< 22 pt

/// The canvas the screen is laid out on.
struct Viewport {
    int width{};              ///< canvas pixels
    int height{};             ///< canvas pixels
    float px_per_point{1.0f}; ///< canvas pixels per point
    Insets safe{};            ///< canvas pixels kept clear on each side (the window's safe area)
};

/// The device class: phone when the shorter side is under phone_short_side_points (460 pt).
///
/// @param viewport the canvas
/// @return phone or tablet, from the canvas's size in points
[[nodiscard]] DeviceClass device_class(const Viewport& viewport) noexcept;

/// The screen's steps (S1 to S8).
enum class Step : uint8_t {
    first_run,     ///< S1, with S7's banner when a copy waits
    looking,       ///< S2: listing, the count growing
    nested_offer,  ///< S2: a game folder found below
    already_there, ///< S2: the chosen folder is the game folder
    ready_to_copy, ///< S3
    copying,       ///< S4
    checking,      ///< S5: the engine's check, or unpacking the demo
    ready_to_play, ///< S5
    problem,       ///< S6
    manage,        ///< S8
};

/// Sheets over a step.
enum class Sheet : uint8_t {
    none,                ///< no sheet
    stop,                ///< S4: keep or discard what was copied
    replace_confirm,     ///< S3: replace the game files
    left_out_list,       ///< S3: the files left out (SHOW)
    mod_errors,          ///< S3: why a mod cannot be used (WHY)
    remove_old_confirm,  ///< S5: remove the folder set aside
    remove_part_confirm, ///< S8: remove a part
    remove_all_confirm,  ///< S8: remove every game file
    add_files,           ///< S8: choose a folder or files to add
    scheduled_note,      ///< S8: the changes are used from the next start
};

/// S1's and S8's banners (one at a time).
enum class Banner : uint8_t {
    none,          ///< no banner
    continue_copy, ///< S7: "Copying from … stopped at … of …": CHOOSE FOLDER · DISCARD
    folder_refused, ///< "The Total Annihilation folder on this {device} cannot be played: …": CHECK AGAIN
    nothing_added, ///< "Copying stopped. Nothing was added."
    copy_went_on,  ///< S4: "Copying went on while Open Annihilation was away"
    copy_resumed,  ///< S4: "Copying paused while Open Annihilation was away and has started again"
    added,      ///< S8: files were added; names already there were kept; used from the next start
    next_start, ///< S8: a change waits for the next start
};

/// S6's problems, row by row.
enum class Problem : uint8_t {
    not_a_game,         ///< no archives and no installer, here or two levels down
    cannot_play,        ///< the engine's check failed on the source or the copy
    not_demo_installer, ///< the chosen file is not the demo's installer
    short_space,        ///< not enough space, before copying
    disk_full,          ///< the disk filled during the copy
    source_unreadable,  ///< the source went away or a file cannot be read
    download_failed,    ///< a cloud download failed
    access_withdrawn,   ///< access withdrawn
    source_changed,     ///< the folder changed since the copy began
    too_large,          ///< far too large
    no_game_folder_yet, ///< I have copied it, nothing there
    found_misnamed,     ///< I have copied it, a folder under another name
    found_loose,        ///< I have copied it, archives loose in the game folder's parent
    picker_failed,      ///< the file picker did not open
    staging_unwritable, ///< the staging folder cannot be written
};

/// The parts' rows (mirrors oa::app::game_files::Part, plus the rows only S3 and S8 show).
enum class PartKind : uint8_t {
    game_archives,    ///< totala1-4.hpi, worlds.hpi
    update_31c,       ///< rev31.gp3
    core_contingency, ///< the Core Contingency expansion
    battle_tactics,   ///< the Battle Tactics expansion
    extra,            ///< other archives at the top
    music,            ///< music/
    movies,           ///< the intro and ending movies
    mod,              ///< one mod
    left_out,         ///< the files left out
    demo,             ///< the demo's installer
    demo_data,        ///< the demo's unpacked data
};

/// A row's mark.
enum class Mark : uint8_t {
    none,     ///< no mark
    found,    ///< found in the source
    missing,  ///< not found
    refused,  ///< cannot be used
    left_out, ///< left out of the copy
    done,     ///< copied
    copying,  ///< being copied
    waiting,  ///< waiting to be copied
    warning,  ///< needs the player's attention
};

/// One part's row on S3, S4 and S8.
struct PartRow {
    PartKind kind{};                   ///< which part
    std::string name{};                ///< a mod's name (for PartKind::mod); empty otherwise
    std::string files{};               ///< "totala1.hpi, totala2.hpi, …" or "16 tracks"
    uint64_t bytes{};                  ///< its known size
    bool bytes_known = true;           ///< false when some sizes were not reported
    uint32_t count{};                  ///< files, or music tracks
    Mark mark{Mark::found};            ///< its mark
    bool has_switch{};                 ///< S3: an optional part
    bool on = true;                    ///< its switch
    bool removable{};                  ///< S8: REMOVE
    std::vector<std::string> errors{}; ///< a mod's profile errors (WHY)
};

/// One left-out file (SHOW).
struct LeftOutRow {
    std::string path{}; ///< relative to the source
    uint64_t bytes{};   ///< its size
    uint8_t reason{};   ///< oa::app::game_files::LeftOutReason, as a number
};

/// The copy's progress (S4).
struct Progress {
    uint64_t done_bytes{};  ///< bytes copied
    uint64_t total_bytes{}; ///< bytes the copy copies
    std::string current{};  ///< the current file's name
    bool downloading{};     ///< "Downloading … from {cloud}…"
    std::optional<uint32_t>
        seconds_left{}; ///< none: "working out the time left" (first 5 s, or a download waited on)
};

/// What the app tells the screen; the UI keeps only sheets, focus, scroll and presses.
struct Model {
    Step step{Step::first_run};           ///< the step shown
    Sheet sheet{Sheet::none};             ///< the sheet over it
    Banner banner{Banner::none};          ///< the banner shown
    Problem problem{Problem::not_a_game}; ///< S6's problem
    std::string version{};                ///< the header's version text, "v0.6"
    bool management{};                    ///< opened from Settings: Back goes to S8, no OA · Aa
    bool offers_pick_folder{};            ///< capability pick_folder
    bool offers_pick_files{};             ///< capability pick_files
    bool offers_copy_yourself{};          ///< capability shared_documents (card 2)
    std::array<std::string, 8> words{};   ///< GameFilesText words, by its order; empty: neutral
    std::string location{};               ///< the source's display location
    std::string detail{};                 ///< the engine's or the importer's sentence (S6, banner)
    std::string file{};                   ///< the file a problem names
    uint64_t stopped_bytes{};             ///< S7 banner and S6: what was copied
    uint64_t stopped_total{};             ///< S7 banner and S6: of how much
    std::vector<std::string> nested{};    ///< S2 nested candidates
    std::vector<PartRow> parts{};         ///< S3, S4, S8 rows
    std::vector<LeftOutRow> left_out{};   ///< SHOW
    uint64_t left_out_bytes{};            ///< the left-out files' size
    std::vector<std::string> warnings{};  ///< case clashes and the like
    uint64_t copy_bytes{};                ///< S3 "Copies 1.12 GB"
    uint64_t need_bytes{};                ///< with the margin
    uint64_t free_bytes{};                ///< free on the device
    bool free_known{};                    ///< the free space could be read
    uint64_t remote_bytes{};              ///< "312 MB is in {cloud} only …"
    bool sizes_unknown{};                 ///< "at least"
    bool source_checked{};                ///< "Checked: the game can start from these files."
    bool source_check_skipped{};          ///< "Checked fully once copied."
    bool replace{};                       ///< the main button reads REPLACE GAME FILES
    uint64_t replace_bytes{};             ///< the confirm sheet's "(1.1 GB)"
    bool space_short{};                   ///< not enough space
    std::vector<PartKind> fitting_off{};  ///< parts whose switch would make it fit
    bool demo{};                          ///< the demo route: its single row, and S2 names a file
    Progress progress{};                  ///< S4
    std::string ready_summary{}; ///< S5 "Total Annihilation 3.1c with …" (texts.cpp builds it)
    std::array<bool, 11>
        ready_parts{};     ///< parts present, by PartKind, from which ready_summary is built
    uint64_t uses_bytes{}; ///< S5 "Uses 1.1 GB on this {device}"
    std::vector<std::string> skipped_archives{}; ///< S5 warnings
    bool backed_up{};            ///< false: "These files are not in this {device}'s backups."
    uint64_t old_folder_bytes{}; ///< S5: the folder set aside; 0: none
    uint64_t installed_bytes{};  ///< S8 subtitle
    bool pending_replacement{};  ///< S8: a replacement waits
    std::vector<std::string> pending_removals{}; ///< S8: names waiting to be removed
    uint32_t added_files{};                      ///< S8 banner: files added
    std::string added_from{};                    ///< S8 banner: where from
    std::vector<std::string> added_kept{};       ///< S8 banner: names already there, kept
    uint16_t sheet_part{};                       ///< the row a remove sheet is about
    int32_t scroll_points{};                     ///< the rows' scroll, in points (the UI clamps it)
    Point pressed_at{};                          ///< UI-owned
    uint32_t listed_files{}; ///< S2: files listed so far ("143 files, 1.1 GB so far")
    uint64_t listed_bytes{}; ///< S2: their bytes so far
};

/// What can be pressed.
enum class ControlKind : uint8_t {
    none,          ///< nothing
    language,      ///< header OA · Aa
    choose_folder, ///< S1 card 1, S7 banner, S6 "CHOOSE ANOTHER FOLDER", "CHOOSE THE FOLDER AGAIN"
    i_have_copied, ///< S1 card 2
    choose_installer,      ///< S1 card 3, S6 "CHOOSE ANOTHER FILE"
    banner_action,         ///< the banner's button (CHECK AGAIN)
    banner_discard,        ///< S7 DISCARD
    cancel,                ///< S2 CANCEL
    nested_choice,         ///< S2: index = candidate
    check_it,              ///< S2 already there: CHECK IT
    part_switch,           ///< S3: index = row
    part_why,              ///< S3: index = row
    show_left_out,         ///< S3 SHOW
    copy,                  ///< S3 COPY / REPLACE GAME FILES
    check_space,           ///< S3 short space: CHECK AGAIN
    stop,                  ///< S4 STOP
    play,                  ///< S5 PLAY
    remove_old,            ///< S5 REMOVE OLD FOLDER
    problem_action,        ///< S6: index = button 0, 1 or 2, left to right
    back,                  ///< S6 BACK and every step's Esc
    sheet_option,          ///< a sheet's buttons: index = 0, 1, … top to bottom
    manage_check,          ///< S8 CHECK AGAIN
    manage_add,            ///< S8 ADD FILES…
    manage_remove,         ///< S8: index = row
    manage_replace,        ///< S8 REPLACE THE GAME FILES…
    manage_remove_all,     ///< S8 REMOVE ALL GAME FILES…
    manage_cancel_pending, ///< S8 CANCEL THE REPLACEMENT
    manage_done,           ///< S8 DONE
};

/// A control: its kind and, for repeated ones, its index.
struct Control {
    ControlKind kind{ControlKind::none}; ///< what it is
    uint16_t index{};                    ///< which of a repeated kind
    /// Tells whether two controls are the same.
    friend bool operator==(const Control&, const Control&) = default;
};

/// What the app must do after a press or a key.
enum class Command : uint8_t {
    none,                  ///< nothing
    redraw,                ///< only the look changed (sheet, switch, focus, scroll)
    pick_game_folder,      ///< show the picker for the game folder
    pick_installer,        ///< show the picker for the demo's installer
    pick_additions_folder, ///< show the picker for an additions folder
    pick_archives,         ///< show the picker for archives
    check_copied,          ///< I HAVE COPIED IT, CHECK AGAIN (S1 banner, S6 no game folder yet)
    cancel_scan,           ///< stop the scan
    use_nested,            ///< index = candidate
    check_in_place,        ///< CHECK IT
    recheck_space,         ///< CHECK AGAIN on short space
    start_copy,            ///< COPY (after the replace confirm when model.replace)
    copy_anyway,           ///< far too large: COPY ANYWAY → S3
    stop_keep,             ///< stop, keeping what was copied
    stop_discard,          ///< stop, discarding what was copied
    continue_copy,         ///< S6 CONTINUE: start the run again (resume)
    start_again,           ///< S6 source changed: discard staging, copy again
    discard,               ///< S7 / S6 DISCARD
    adopt,                 ///< S6 USE IT / GATHER THEM
    play,                  ///< PLAY
    remove_old,            ///< after its confirm
    back,                  ///< to S1, or S8 in management
    open_language,         ///< OA · Aa
    manage_check,          ///< S8 CHECK AGAIN
    manage_remove,         ///< S8 remove a part, after its confirm; index = row
    manage_remove_all,     ///< S8 remove every game file, after its confirm
    manage_replace,        ///< S8 replace the game files
    manage_cancel_pending, ///< S8 cancel the waiting replacement
    remove_demo_data,      ///< S8 remove the demo's unused data, after its confirm
    retry,                 ///< S6 TRY AGAIN (picker or staging)
    done,                  ///< S8 DONE: close the screen
};

/// A command and its index.
struct Outcome {
    Command command{Command::none}; ///< what to do
    uint16_t index{};               ///< which candidate or row
};

/// The keys the screen answers to; the app maps the platform's keys to these.
enum class Key : uint8_t {
    tab,       ///< next focus
    back_tab,  ///< previous focus
    enter,     ///< press the focused control
    escape,    ///< back, or close a sheet
    up,        ///< scroll up
    down,      ///< scroll down
    page_up,   ///< scroll up a page
    page_down, ///< scroll down a page
    space,     ///< press the focused control
};

/// A text's look.
enum class TextRole : uint8_t {
    header,     ///< the header bar
    title,      ///< a step's title
    subtitle,   ///< under the title
    lead,       ///< a card's or banner's lead line
    body,       ///< ordinary text
    small,      ///< footers and hints
    row_title,  ///< a row's name
    row_detail, ///< a row's detail
    button,     ///< a button's label
    mark_text,  ///< text beside a mark
};
/// What an item draws.
enum class ItemRole : uint8_t {
    header_bar,     ///< the header's bar
    badge,          ///< the OA badge: the Open Annihilation icon filling its box, no text
    header_text,    ///< the header's title
    version,        ///< the version text
    title,          ///< a title
    text,           ///< a text block
    card,           ///< a card
    icon,           ///< a mark
    banner,         ///< a banner
    row,            ///< a part's row
    divider,        ///< a line between rows
    switch_off_on,  ///< a switch
    button_main,    ///< the main button
    button,         ///< a button
    button_danger,  ///< a button that removes
    progress_track, ///< the progress bar's track
    progress_fill,  ///< the progress bar's fill
    progress_busy,  ///< a busy bar without a fraction
    backdrop,       ///< the dimming behind a sheet
    sheet,          ///< a sheet
};
/// A mark the paint draws in an item.
enum class Glyph : uint8_t {
    none,    ///< no mark
    folder,  ///< a folder
    device,  ///< the device
    disk,    ///< a disk
    clock,   ///< a clock
    warning, ///< a warning triangle
    check,   ///< a check mark
    cross,   ///< a cross
    dash,    ///< a dash
    info,    ///< an information mark
    stop,    ///< a stop square
    play,    ///< a play triangle
    trash,   ///< a bin
    refresh, ///< a circular arrow
    plus,    ///< a plus
    file,    ///< a file
    oa,      ///< the Open Annihilation icon, or the OA mark without it (the badge, OA · Aa)
};

/// One thing to draw, and what pressing it does.
struct Item {
    ItemRole role{};                  ///< what it draws
    Rect box{};                       ///< canvas pixels
    std::vector<std::string> lines{}; ///< wrapped text, top to bottom; empty for none
    TextRole text{TextRole::body};    ///< the text's look
    int pixel_size{};                 ///< text size in canvas pixels
    bool bold{};                      ///< bold text
    Colour colour{text_colour};       ///< text (or fill for marks)
    Glyph glyph{Glyph::none};         ///< the mark
    int glyph_size{};                 ///< a button's mark's side in canvas pixels; 0: pixel_size
    Control control{};                ///< none for display only
    bool enabled = true;              ///< can be pressed
    bool on{};                        ///< a switch's state; a button's "main"
    bool focused{};                   ///< has the keyboard focus
    bool pressed{};                   ///< is held
    float fraction{};                 ///< progress_fill: 0 to 1
    Rect clip{};                      ///< non-empty: clip drawing (the scrolled rows)
};

/// A laid-out step.
struct Layout {
    DeviceClass device{DeviceClass::tablet}; ///< the form laid out
    std::vector<Item> items{};               ///< in painting order
    std::vector<Control> focus_order{};      ///< Tab order, top to bottom, left to right
    Rect rows{};                             ///< the scrolling region, empty when none
    int32_t scroll_max_points{};             ///< how far it scrolls
    float px_per_point{1.0f};                ///< the viewport's canvas pixels per point
};

/// Text measuring, which the app fills from the bundled fonts.
struct TextMeasureHooks {
    void* context{}; ///< passed back to every hook
    /// Width in pixels of a line at a pixel size; null: 0.55 em a character.
    int (*width)(void* context, std::string_view text, int pixel_size, bool bold){};
    /// Height of a line in pixels; null: 1.25 em.
    int (*line_height)(void* context, int pixel_size, bool bold){};
};

/// Returns a platform word, or the engine's neutral word when the platform gives none.
///
/// @param model the model whose words are used
/// @param which an oa::app::GameFilesText value, as a number
/// @return the word, through the interface catalogue when it is the neutral one
[[nodiscard]] std::string platform_word(const Model& model, uint8_t which);
/// "1.1 GB" (two significant digits), "1.12 GB" with `precise` (three); decimal units.
///
/// @param bytes the size
/// @param precise three significant digits instead of two
/// @return the size as players read it
[[nodiscard]] std::string size_text(uint64_t bytes, bool precise = false);
/// "about a minute left", "about 4 minutes left", "a few seconds left", or "working out the time left".
///
/// @param seconds the time left; none while it is being worked out
/// @return the text S4 shows
[[nodiscard]] std::string time_left_text(std::optional<uint32_t> seconds);
/// S5's sentence from the parts present: "Total Annihilation 3.1c with Core Contingency and
/// Battle Tactics, music and movies." or "The Total Annihilation demo (1997)."
///
/// @param parts the parts present, by PartKind
/// @param demo the demo is installed
/// @return the sentence
[[nodiscard]] std::string ready_text(const std::array<bool, 11>& parts, bool demo);
/// Settings' summary: "3.1c · Core Contingency · Battle Tactics · music · 1 mod".
///
/// @param parts the parts present, by PartKind
/// @param mods the mods installed
/// @param demo the demo is installed
/// @return the summary line
[[nodiscard]] std::string summary_text(const std::array<bool, 11>& parts, uint32_t mods, bool demo);

/// Lays a step out for a viewport (tablet or phone form by device_class).
///
/// @param model what to show
/// @param viewport the canvas
/// @param measure the text measure
/// @return the items to draw, in order, with their controls
[[nodiscard]] Layout
lay_out(const Model& model, const Viewport& viewport, const TextMeasureHooks& measure);
/// The control at a point: the nearest enabled control whose box is within `reach_px`
/// (pick_reach_points × px_per_point), the containing one first; none when there is none.
///
/// @param layout the layout last drawn
/// @param point the point, canvas pixels
/// @param reach_px how far from a box a press still reaches it
/// @return the control; ControlKind::none when there is none
[[nodiscard]] Control hit_test(const Layout& layout, Point point, float reach_px) noexcept;

/// UI state the app keeps beside the model between frames.
struct Interaction {
    Control focused{};  ///< keyboard focus; shown once a key moved it
    bool focus_shown{}; ///< the focus ring is shown
    Control pressed{};  ///< the control a held press is on
};

/// A press landed (finger down, mouse down): sets the pressed look.
///
/// @param[in,out] model the model
/// @param[in,out] interaction the UI state
/// @param layout the layout last drawn
/// @param control the control under the press
/// @return what the app must do
[[nodiscard]] Outcome
press_down(Model& model, Interaction& interaction, const Layout& layout, Control control);
/// The press ended on `control` (none: slid off): acts, opening and closing sheets itself, and
/// returns what the app must do.
///
/// @param[in,out] model the model
/// @param[in,out] interaction the UI state
/// @param layout the layout last drawn
/// @param control the control under the release
/// @return what the app must do
[[nodiscard]] Outcome
press_up(Model& model, Interaction& interaction, const Layout& layout, Control control);
/// A key: Tab and Shift+Tab move the focus, Return and Space press, Esc goes back (closes a
/// sheet first), arrows and page keys scroll.
///
/// @param[in,out] model the model
/// @param[in,out] interaction the UI state
/// @param layout the layout last drawn
/// @param key the key
/// @return what the app must do
[[nodiscard]] Outcome key(Model& model, Interaction& interaction, const Layout& layout, Key key);
/// Scrolls the rows by points (a drag or the wheel), clamped.
///
/// @param[in,out] model the model
/// @param layout the layout last drawn
/// @param points how far; positive scrolls down
/// @return what the app must do
[[nodiscard]] Outcome scroll(Model& model, const Layout& layout, float points);

/// Marks the items of the focused control (once a key has shown the focus) as focused and
/// those of the held control as pressed, for painting; lay_out leaves both clear, since it
/// does not see the Interaction.
///
/// @param[in,out] layout the layout about to be painted
/// @param interaction the UI state
void mark_interaction(Layout& layout, const Interaction& interaction) noexcept;

/// The glyph of a button with one: a square as wide as the label's pixel size, this share
/// of the pixel size left of the label, the two centred together in the button.
inline constexpr float button_glyph_gap_em = 0.5f;

} // namespace oa::ui::game_files
