// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the Game files screen's parts share inside the module: the interface
// words with their places filled, the parts' names and notes, and what each
// problem, sheet and banner shows and what its buttons do, so that the
// layout and the presses read one description.
#pragma once

#include "oa/ui/game_files.hpp"

#include <initializer_list>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace oa::ui::game_files::detail {

/// Model::words index of the device's name ("this {device}").
inline constexpr uint8_t word_device = 0;
/// Model::words index of the steps of Copy it yourself.
inline constexpr uint8_t word_copy_yourself_steps = 1;
/// Model::words index of the same steps in one line.
inline constexpr uint8_t word_copy_yourself_short = 2;
/// Model::words index of where the picker reaches.
inline constexpr uint8_t word_picker_places = 3;
/// Model::words index of the same for the phone rows.
inline constexpr uint8_t word_picker_places_short = 4;
/// Model::words index of how to free space.
inline constexpr uint8_t word_free_space_advice = 5;
/// Model::words index of where the game folder shows in the file manager.
inline constexpr uint8_t word_folder_location = 6;
/// Model::words index of the cloud service's name.
inline constexpr uint8_t word_cloud_name = 7;
/// How many platform words there are.
inline constexpr uint8_t word_count = 8;

/// The demo installer's unpacked game data, as S1 and S5 name it.
inline constexpr std::string_view demo_data_size_text = "20 MB";

/// Returns one of the interface's words in the installed language.
///
/// @param english the word as the source writes it
/// @return its translation, or the word itself
[[nodiscard]] std::string tr(std::string_view english);

/// A place in a text and what fills it.
using Place = std::pair<std::string_view, std::string>;

/// Returns an interface text in the installed language with its {name} places filled. The
/// whole text, places and all, is what the catalogue translates.
///
/// @param english the text as the source writes it, with {name} places
/// @param places each place's name and the text that fills it
/// @return the text; a place with no value given is left as written
[[nodiscard]] std::string fill(std::string_view english, std::initializer_list<Place> places);

/// Lists words as a sentence does: "a", "a and b", "a, b and c".
///
/// @param words the words
/// @return the list
[[nodiscard]] std::string join_and(const std::vector<std::string>& words);

/// Lists words joined by "or": "a", "a or b", "a, b or c".
///
/// @param words the words
/// @return the list
[[nodiscard]] std::string join_or(const std::vector<std::string>& words);

/// Returns a part's name as its row shows it.
///
/// @param kind the part
/// @return "Core Contingency", "Music", ...; "Mod" for a mod
[[nodiscard]] std::string part_kind_name(PartKind kind);

/// Returns a row's name: the part's, or the mod's own.
///
/// @param row the row
/// @param management the management state names mods "Mod: <name>"
/// @return the name
[[nodiscard]] std::string part_name(const PartRow& row, bool management);

/// Tells whether a row is a part that is not in the folder.
///
/// @param row the row
/// @return true for the missing mark
[[nodiscard]] bool part_missing(const PartRow& row) noexcept;

/// Tells whether a mod's profile cannot be used.
///
/// @param row the row
/// @return true for a refused mod
[[nodiscard]] bool part_refused(const PartRow& row) noexcept;

/// Returns the note after a row's name: its files, tracks, "optional", "Not found".
///
/// @param model the model (its step)
/// @param row the row
/// @return the note; empty for none
[[nodiscard]] std::string part_detail(const Model& model, const PartRow& row);

/// Returns the amber hint after a row's note (S3 only), long and short forms.
///
/// @param model the model (its step)
/// @param row the row
/// @param short_form the form for a narrow row
/// @return the hint; empty for none
[[nodiscard]] std::string part_hint(const Model& model, const PartRow& row, bool short_form);

/// Returns the size a row shows on the right.
///
/// @param row the row
/// @return "131 MB", "at least 12 MB"; empty for a missing part
[[nodiscard]] std::string part_size(const PartRow& row);

/// Returns the reason a left-out file is left out.
///
/// @param reason oa::app::game_files::LeftOutReason, as a number
/// @return the reason in a few words
[[nodiscard]] std::string left_out_reason_text(uint8_t reason);

/// Returns the left-out row's description: what kinds of file are left out.
///
/// @return the description
[[nodiscard]] std::string left_out_kinds_text();

/// One button of a problem, a sheet or a banner: its label and what pressing it does.
struct Action {
    std::string label{};             ///< upper-case label
    Control control{};               ///< the control it is
    Command command{Command::none};  ///< what it asks of the app; none: the UI's own
    ItemRole role{ItemRole::button}; ///< main, plain or danger
    Glyph glyph{Glyph::none};        ///< the mark beside the label
    float glyph_points{};            ///< the mark's side in points; 0: the label's size
    bool enabled = true;             ///< can be pressed
    bool closes_sheet = true;        ///< a sheet's button closes the sheet when pressed
    Sheet opens{Sheet::none};        ///< a sheet it opens instead of a command
};

/// What S6 shows for the model's problem.
struct ProblemContent {
    std::string title{};                   ///< the title
    std::vector<std::string> paragraphs{}; ///< what happened (first) and what to do
    Glyph glyph{Glyph::warning};           ///< the mark beside the title
    Colour colour{amber_colour};           ///< its colour
    std::vector<Action> actions{};         ///< the buttons, left to right as listed
};

/// Returns what S6 shows for the model's problem.
///
/// @param model the model
/// @return the title, texts and buttons
[[nodiscard]] ProblemContent problem_content(const Model& model);

/// One line of a sheet's list: a left-out file and why.
struct SheetRow {
    std::string text{};   ///< the file
    std::string detail{}; ///< why it is left out
    std::string right{};  ///< its size
};

/// What a sheet shows.
struct SheetContent {
    std::string title{};                   ///< the title
    std::vector<std::string> paragraphs{}; ///< the text, paragraph by paragraph
    std::vector<SheetRow> rows{};          ///< a list (the left-out files)
    std::vector<std::string> lines{};      ///< single lines (a mod's errors)
    std::vector<std::string> notes{};      ///< amber notes after the list (warnings)
    std::vector<std::string> after{};      ///< dim text after the lines
    std::vector<Action> actions{};         ///< the buttons, top to bottom (sheet_option index)
};

/// Returns what the model's sheet shows; empty for Sheet::none.
///
/// @param model the model
/// @return the sheet's content
[[nodiscard]] SheetContent sheet_content(const Model& model);

/// What a banner shows.
struct BannerContent {
    bool shown{};                  ///< a banner is shown
    std::string text{};            ///< its text
    Glyph glyph{Glyph::info};      ///< the mark at its left
    Colour colour{amber_colour};   ///< its rule and mark
    std::vector<Action> actions{}; ///< its buttons, left to right
};

/// Returns the banner the model's step shows (S8's pending replacement first).
///
/// @param model the model
/// @return the banner
[[nodiscard]] BannerContent banner_content(const Model& model);

/// Tells whether the management state has a change that waits for the next start, so DONE
/// shows the scheduled note first.
///
/// @param model the model
/// @return true when a change waits
[[nodiscard]] bool change_waits(const Model& model) noexcept;

} // namespace oa::ui::game_files::detail
