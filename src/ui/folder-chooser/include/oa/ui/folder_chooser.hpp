// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The in-engine game folder chooser's model: the list of found folders with
// Browse…, the 1997 demo, the desktop's dialog where offered and Quit; the
// folder browser with its places, the parent folder, the folders and "Play
// this folder"; their layouts in the Game files screen's look, the hit test,
// the focus order and what presses and keys ask of the app. Pure: no SDL and
// no app header.
#pragma once

#include "oa/ui/game_files.hpp"
#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>

namespace oa::ui::folder_chooser {

using oa::ui::display_layout::Point;
using oa::ui::display_layout::Rect;
using oa::ui::game_files::TextMeasureHooks;
using oa::ui::game_files::Viewport;

/// What the chooser shows.
enum class View : uint8_t {
    list,   ///< the found folders and the other ways to a folder
    browse, ///< the folder browser
};

/// A found folder's row.
struct InstallRow {
    std::string folder{}; ///< UTF-8
    std::string source{}; ///< "your Steam library on the SD card"
    /// What the check found: "Total Annihilation 3.1c with Core Contingency", or why not.
    std::string verdict{};
    bool usable{}; ///< the folder can be played
};

/// One folder in the browser.
struct FolderEntry {
    std::string name{}; ///< UTF-8
    bool game_folder{}; ///< holds totala1.hpi
};

/// What the app tells the chooser.
struct Model {
    View view{View::list}; ///< what shows
    std::string version{}; ///< "v0.7"
    std::string notice{};  ///< a remembered folder that has gone, or the dialog's failure
    std::vector<InstallRow> installs{}; ///< the found folders
    bool offers_dialog{};               ///< "Use the desktop's folder dialog" (not in Game Mode)
    std::string folder{};               ///< browse: the folder shown, UTF-8
    std::vector<FolderEntry> entries{}; ///< browse: its folders, sorted
    std::vector<std::string> place_labels{}; ///< browse: Home, Downloads, each removable drive
    bool folder_usable{};                    ///< browse: the folder shown can be played
    std::string folder_verdict{};            ///< browse: what the check found there
    bool has_parent{};                       ///< browse: the folder shown has a parent folder
    std::string hint{}; ///< browse: a line under the folder (where the demo's installer lies)
};
/// What a press reaches.
enum class TargetKind : uint8_t {
    none,    ///< nothing
    install, ///< a found folder's row; index: the row
    browse,  ///< Browse…
    dialog,  ///< the desktop's folder dialog
    demo,    ///< the 1997 demo
    quit,    ///< Quit
    place,   ///< a place of the browser; index: the place
    parent,  ///< ↑ the parent folder
    entry,   ///< a folder of the browser; index: the entry
    choose,  ///< "Play this folder"
    back,    ///< back to the list
};

/// A target: its kind and which one.
struct Target {
    TargetKind kind{TargetKind::none}; ///< what it is
    uint16_t index{};                  ///< which row, place or entry
    /// Tells whether two targets are the same.
    friend bool operator==(const Target&, const Target&) = default;
};

/// A target's box.
struct HitBox {
    Rect box{};         ///< canvas pixels
    Target target{};    ///< what a press there reaches
    bool enabled{true}; ///< a press reaches it
    /// The scrolling region the box lies in: only the part inside it can be pressed; empty for a
    /// box that does not scroll.
    Rect clip{};
};

/// A laid-out chooser: what to paint (with paint_game_files) and where presses go.
struct Layout {
    oa::ui::game_files::Layout paint{}; ///< what to paint
    std::vector<HitBox> targets{};      ///< where presses go
    std::vector<Target> focus_order{};  ///< the order the focus moves in
    int32_t scroll_max_points{};        ///< how far the rows scroll, points
};

/// What the chooser itself keeps between frames.
struct UiState {
    int32_t focus{-1};       ///< the focused entry of focus_order, -1 for none
    bool focus_shown{};      ///< the focus is drawn (keys or a pad were used)
    int32_t pressed{-1};     ///< the pressed entry of targets, -1 for none
    int32_t scroll_points{}; ///< how far the rows are scrolled, points
};
/// What the chooser asks of the app.
enum class Command : uint8_t {
    none,          ///< nothing
    play_install,  ///< plays a found folder; index: the row
    open_browser,  ///< opens the browser
    use_dialog,    ///< opens the desktop's folder dialog
    find_demo,     ///< opens the browser at the demo's usual folder
    quit,          ///< quits
    open_place,    ///< shows a place; index: the place
    parent_folder, ///< shows the parent folder
    enter_folder,  ///< shows a folder; index: the entry
    choose_folder, ///< plays the folder shown
    back_to_list,  ///< back to the list
};

/// A command and which one.
struct Outcome {
    Command command{Command::none}; ///< what is asked
    uint16_t index{};               ///< which row, place or entry
};
/// The keys the chooser takes.
enum class Key : uint8_t {
    up,        ///< the focus up
    down,      ///< the focus down
    left,      ///< the focus left
    right,     ///< the focus right
    enter,     ///< presses the focused target
    escape,    ///< back: the browser to the list; the list: nothing
    tab,       ///< the next target
    back_tab,  ///< the previous target
    page_up,   ///< a page of rows up
    page_down, ///< a page of rows down
};

/// Lays the chooser out: the list (found folders, Browse…, the 1997 demo, the desktop's dialog
/// where offered, Quit) or the browser (places, ↑ parent, folders, "Play this folder"), in the
/// Game files screen's look, every control at least 44 points high.
///
/// @param model what the app tells the chooser
/// @param state what the chooser keeps between frames
/// @param viewport the canvas
/// @param measure how text is measured
/// @return the layout
[[nodiscard]] Layout lay_out(
    const Model& model,
    const UiState& state,
    const Viewport& viewport,
    const TextMeasureHooks& measure
);
/// Returns the target under a point: the containing one, else the nearest enabled within
/// reach.
///
/// @param layout the laid-out chooser
/// @param point canvas pixels
/// @param reach_px how far from a box a point still reaches it, canvas pixels
/// @return the target; kind none for none
[[nodiscard]] Target hit_test(const Layout& layout, Point point, int reach_px) noexcept;
/// Takes a press going down at a point: marks its target pressed.
///
/// @param layout the laid-out chooser
/// @param[in,out] state what the chooser keeps between frames
/// @param point canvas pixels
void press_down(const Layout& layout, UiState& state, Point point) noexcept;
/// Takes the press coming up at a point.
///
/// @param layout the laid-out chooser
/// @param[in,out] state what the chooser keeps between frames
/// @param point canvas pixels
/// @return the target's command when the press is still on its target; none otherwise
[[nodiscard]] Outcome press_up(const Layout& layout, UiState& state, Point point) noexcept;
/// Takes a key: arrows and Tab move the focus (and scroll it into view), Enter presses, Escape
/// goes back (browser to list; list: none).
///
/// @param layout the laid-out chooser
/// @param[in,out] state what the chooser keeps between frames
/// @param key the key
/// @return what the key asks of the app
[[nodiscard]] Outcome key(const Layout& layout, UiState& state, Key key) noexcept;
/// Scrolls the rows, held within the layout's range.
///
/// @param layout the laid-out chooser
/// @param[in,out] state what the chooser keeps between frames
/// @param points how far, points; positive scrolls down
void scroll(const Layout& layout, UiState& state, int32_t points) noexcept;

} // namespace oa::ui::folder_chooser
