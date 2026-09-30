// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Load-game and save-game dialogs (LOADGAME.GUI in both roles) and the unit
// restriction list dialogs (SAVELIST.GUI / LOADLIST.GUI).
//
// Save files are read through SaveSummaryReader, a narrow boundary over the
// hapibank summary record; savegame_persist_reader backs it with src/data/persist.
#pragma once

#include "oa/data/campaign/directory_list.hpp"
#include "oa/present/surface.hpp"
#include "oa/ui/frontend/ingame_menu.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace oa::ui::frontend {

inline constexpr std::string_view kSaveDirectory = "SAVEGAME";
inline constexpr std::string_view kSaveExtension = "SAV";
inline constexpr std::string_view kRestrictListExtension = "LST";
inline constexpr std::size_t kSaveFileBytes = 0x100;       // one name slot
inline constexpr std::size_t kSaveDescriptionBytes = 0x40; // one description slot
inline constexpr int32_t kSaveTicksPerSecond = 30;
inline constexpr int32_t kNoSavesMessageWidth = 0x140;
inline constexpr int32_t kRestrictNoLimit = -1;
inline constexpr int32_t kRestrictLimitCeiling = 100;
/// Smallest width and height, in pixels, of a saved radar image the preview
/// shows: the image is drawn from one pixel inside each of its edges.
inline constexpr int32_t kSaveRadarMinimumSize = 2;

struct SaveEntry {
    std::array<char, kSaveFileBytes> file{};               // name in the save directory
    std::array<char, kSaveDescriptionBytes> description{}; // GAMES list text
};

struct SaveList {
    std::vector<SaveEntry> entries;
};

// Directory boundary: the find walk, remove, make-directory and whole-file calls.
struct SaveFiles {
    void* context = nullptr;
    // The find walk over a "<directory>\*.<extension>" pattern, with each
    // file's write time.
    void (*find)(
        void* context,
        const char* pattern,
        void (*visit)(void* user, const data::campaign::FindRecord& record),
        void* user
    ) = nullptr;
    bool (*remove)(void* context, const char* path) = nullptr;
    void (*make_directory)(void* context, const char* path) = nullptr;
    // Whole-file access for the restriction lists.
    bool (*read_file)(void* context, const char* path, std::vector<uint8_t>& bytes) = nullptr;
    bool (*write_file)(void* context, const char* path, std::span<const uint8_t> bytes) = nullptr;
};

/// Summary-record boundary over a save file (hapibank "Summary" record).
struct SaveSummaryReader {
    void* context = nullptr;
    /// Opens the save at a path under the root; returns null when it cannot
    /// be read. A null hook reads no save.
    void* (*open)(void* context, const char* path) = nullptr;
    /// Returns the integer field, or `fallback` when it is absent. A null
    /// hook reads no integer field.
    int32_t (*get_int)(void* context, void* bank, const char* field, int32_t fallback) = nullptr;
    /// Copies the text field (NUL-terminated, truncated to capacity); false
    /// when absent. A null hook reads no text field.
    bool (*get_string)(
        void* context, void* bank, const char* field, char* out, std::size_t capacity
    ) = nullptr;
    /// Reports whether the field is present. A null hook reports none.
    bool (*has_field)(void* context, void* bank, const char* field) = nullptr;
    /// Reads the "Radar Image" blob (its width and height, then its rows) into
    /// `picture`; false, leaving it empty, when the blob holds no whole image
    /// of at least kSaveRadarMinimumSize by kSaveRadarMinimumSize pixels. A
    /// null hook reads no picture, so RADAR stays hidden.
    bool (*load_radar)(void* context, void* bank, present::SurfaceBuffer& picture) = nullptr;
    /// Releases a bank `open` returned. A null hook releases nothing.
    void (*close)(void* context, void* bank) = nullptr;
};

struct SaveDialogContext {
    SaveFiles files{};
    SaveSummaryReader reader{};
    IngameHost host{};
    std::string_view directory = kSaveDirectory;
    // Side names shown for the saved "Side" field; empty prints "???".
    std::span<const std::string_view> side_names;
    // The RADAR picture: the radar image saved with the previewed game, as
    // palette indices; empty when none is loaded.
    present::SurfaceBuffer radar_picture;
    bool in_game = false;
    bool hold_game = false; // Game.sim_run_flags bit 0
    SaveList list;
};

/// Lists the *.SAV files, newest first, keeping those whose summary has a Description.
///
/// @param[in,out] context Directory, file and summary services; its list is
///                        replaced by the kept files and their descriptions.
/// @return Number of saves listed.
std::size_t savegame_build_list(SaveDialogContext& context);

/// Formats "<directory>\<file>" for a list entry.
///
/// @param context Directory and list.
/// @param index List entry; past the list gives an empty path.
/// @param[out] out Destination buffer.
/// @param capacity Size of `out` in bytes; 0 writes nothing.
void savegame_entry_path(
    const SaveDialogContext& context, std::size_t index, char* out, std::size_t capacity
) noexcept;

/// Formats saved ticks as hh:mm:ss (30 ticks per second).
///
/// @param ticks Game time in ticks.
/// @param[out] out Destination buffer.
/// @param capacity Size of `out` in bytes.
void savegame_format_time(int32_t ticks, char* out, std::size_t capacity) noexcept;

/// Fills the save preview from the summary of the selected GAMES entry, or clears it.
///
/// GAMENAME takes the description; the radar image saved with the game
/// replaces context.radar_picture and RADAR shows while it has one;
/// GAMETYPE reads "Single", "Skirmish (<n> players)" or "???"; a campaign save
/// shows CAMPAIGN and its mission, a skirmish save hides CAMPAIGN and shows
/// its map; TIME, SIDE (the side name at the saved "Side" index, "???"
/// without side names) and DIFF follow. An entry without a description or an
/// unreadable file clears the text fields.
///
/// @param[in,out] panel The loaded dialog; nothing happens without a GAMES list.
/// @param[in,out] context List and summary reader.
/// @quirk Selecting an entry without a description, or a save that cannot be
///        read, leaves RADAR showing the picture of the save previewed before
///        it, as 3.1c does.
void savegame_fill_preview(Panel& panel, SaveDialogContext& context);

/// Refreshes the preview when the GAMES list selection of the load or save dialog changes.
///
/// @param[in,out] panel The loaded dialog.
/// @param[in,out] context List and summary reader.
void savegame_on_games_selected(Panel& panel, SaveDialogContext& context);

/// Shows "Invalid savegame file" and deselects.
///
/// @param[in,out] panel The loaded dialog; its selection is cleared.
/// @param context Host for the message.
void savegame_report_invalid(Panel& panel, SaveDialogContext& context);

/// Sets up LOADGAME.GUI as the load dialog.
///
/// The first save is selected and previewed; DELETE, GAMENAME and the
/// SaveGame title are hidden; the game is held.
///
/// @param[in,out] panel The loaded LOADGAME.GUI.
/// @param[in,out] context Directory and summary services; list and hold_game change.
/// @return false, after "There are no saved games to choose from", when there is nothing to load.
bool savegame_enter_load(Panel& panel, SaveDialogContext& context);

/// Sets up LOADGAME.GUI as the save dialog.
///
/// The game is held and the save directory created; the title reads "Save
/// Game", GAMENAME becomes editable, DELETE is hidden while no save exists and
/// the LoadGame title is hidden; the first save, if any, is selected and
/// previewed.
///
/// @param[in,out] panel The loaded LOADGAME.GUI.
/// @param[in,out] context Directory and summary services; list and hold_game change.
void savegame_enter_save(Panel& panel, SaveDialogContext& context);

enum class SaveDialogAction : uint8_t { none, cancelled, refreshed, load, save, invalid };

struct SaveDialogResult {
    SaveDialogAction action = SaveDialogAction::none;
    int32_t game_type = 0;          // load: summary Gametype
    std::array<char, 0x100> path{}; // file to load or write
};

/// Handles a click on the load dialog.
///
/// CANCEL releases the lists and plays "Previous". LOAD or a double-clicked
/// GAMES entry opens the selected save: an unreadable file or a game type
/// other than campaign or skirmish reports an invalid save; a missing disc
/// shows the Campaign CD (Disc 2) or Multiplayer CD (Disc 1) message and
/// deselects; otherwise the archives are refreshed and "SMLBUTTON" plays.
///
/// @param[in,out] panel The loaded dialog.
/// @param[in,out] context List, summary reader and host.
/// @return cancelled, invalid, or load with the save's game type and path; none otherwise.
SaveDialogResult savegame_on_load_click(Panel& panel, SaveDialogContext& context);

/// Handles a click on the save dialog.
///
/// Closing releases the lists. CANCEL plays "Previous". DELETE removes the
/// selected save, relists, keeps the selection in range and refreshes the
/// preview. LOAD, GAMES or GAMENAME plays "smlbutton" and returns the path
/// "<directory>\<GAMENAME>.SAV" (any extension replaced); an empty name does
/// nothing.
///
/// @param[in,out] panel The loaded dialog.
/// @param[in,out] context Directory services, list, summary reader and host.
/// @return cancelled, refreshed, or save with the path to write; none otherwise.
SaveDialogResult savegame_on_save_click(Panel& panel, SaveDialogContext& context);

// Summary fields the loader copies into the game before starting it.
struct LoadSummary {
    int32_t game_type = 0;
    int32_t side = 0;
    int32_t difficulty = 0;
    std::array<char, 0x40> campaign{};
    std::array<char, 0x40> mission{};
    std::array<char, 0x1a> start_pattern{}; // "Thumbs": 25 mission results for Game.mission_results
    bool has_campaign = false;
    bool between_missions = false;
    // Skirmish only.
    int32_t players = 0;
    int32_t commander_death = 1;
    int32_t location = 1;
    int32_t mapping = 1;
    int32_t line_of_sight = 1;
    int32_t line_of_sight_type = 1;
};

/// Reads the summary fields the loader copies into the game before starting it.
///
/// The skirmish rules are read only for a skirmish save; between_missions
/// only for a campaign save. A save without a "Thumbs" field gets the 25 'U'
/// bytes a skirmish start writes; 3.1c does not handle such a save.
///
/// @param reader Summary reader.
/// @param bank Open summary bank of the save.
/// @param[out] summary Fields read; reset first.
/// @return false when the bank or reader is missing or the mission is missing or empty.
/// @quirk "Thumbs" is copied with a 25-byte bound; a record that is not exactly
///        25 results long becomes the 25 'U' bytes a skirmish start writes.
bool savegame_read_load_summary(const SaveSummaryReader& reader, void* bank, LoadSummary& summary);

// ---------------------------------------------------------------------------
// Unit restriction lists (*.LST)

// One row of the restriction table.
struct RestrictRow {
    int32_t unit_index = 0; // unit-definition index
    int32_t limit = 0;      // the definition's unit limit
};

/// Serialises the unit limits as a *.LST file.
///
/// Little-endian 32-bit words: unit count - 1, then (unit id, limit) for each
/// definition 1..count-1 that has a row.
///
/// @param unit_ids Unit id of each definition index.
/// @param rows Restriction table rows.
/// @return The file bytes.
std::vector<uint8_t>
restrict_list_encode(std::span<const int32_t> unit_ids, std::span<const RestrictRow> rows);

/// Applies a saved *.LST file to the restriction table.
///
/// Each (unit id, limit) pair sets the limit of the row for the first
/// definition 1..count-1 with that id. Short files stop early.
///
/// @param bytes File bytes.
/// @param unit_ids Unit id of each definition index.
/// @param[in,out] rows Restriction table rows whose limits change.
void restrict_list_apply(
    std::span<const uint8_t> bytes, std::span<const int32_t> unit_ids, std::span<RestrictRow> rows
);

/// Formats the COUNT label for a limit slider value.
///
/// @param value Slider value.
/// @param[out] out Destination buffer: the number up to 100, else "No Limit".
/// @param capacity Size of `out` in bytes.
/// @return The limit to store: `value`, or kRestrictNoLimit past 100.
int32_t restrict_limit_label(int32_t value, char* out, std::size_t capacity) noexcept;

/// Lists the *.LST files, newest first, with their names less the extension as descriptions.
///
/// @param[in,out] context Directory services; its list is replaced.
/// @return Number of lists found.
std::size_t restrict_build_list(SaveDialogContext& context);

/// Sets GAMENAME from the selected GAMES entry, empty when none.
///
/// @param[in,out] panel The loaded list dialog.
/// @param context The listed files.
void restrict_sync_name(Panel& panel, const SaveDialogContext& context);

/// Makes GAMENAME follow the newly selected GAMES entry of a list dialog.
///
/// @param[in,out] panel The loaded list dialog.
/// @param context The listed files.
void restrict_on_games_selected(Panel& panel, const SaveDialogContext& context);

/// Sets up SAVELIST.GUI.
///
/// The directory is created and listed; the title reads "Save Game", GAMENAME
/// becomes editable, DELETE is hidden while no list exists and the LoadGame
/// title is hidden; the first list, if any, is selected.
///
/// @param[in,out] panel The loaded SAVELIST.GUI.
/// @param[in,out] context Directory services; its list is replaced.
void restrict_enter_save(Panel& panel, SaveDialogContext& context);

/// Sets up LOADLIST.GUI.
///
/// The first list is selected; DELETE, GAMENAME and the SaveGame title are
/// hidden; the game is held.
///
/// @param[in,out] panel The loaded LOADLIST.GUI.
/// @param[in,out] context Directory services; list and hold_game change.
/// @return false, after "There are no saved lists to choose from", when no list exists.
bool restrict_enter_load(Panel& panel, SaveDialogContext& context);

/// Handles a click on SAVELIST.GUI.
///
/// Closing releases the lists and lets the game run. CANCEL plays
/// "Previous". DELETE plays "SMLBUTTON", removes the selected list and
/// relists. GAMES, LOAD or GAMENAME plays "Options" and returns the path
/// "<directory>\<GAMENAME>.LST"; an empty name does nothing.
///
/// @param[in,out] panel The loaded dialog.
/// @param[in,out] context Directory services, list and host.
/// @return cancelled, refreshed, or save with the path to write; none otherwise.
SaveDialogResult restrict_on_save_click(Panel& panel, SaveDialogContext& context);

/// Handles a click on LOADLIST.GUI.
///
/// CANCEL plays "Previous". LOAD or GAMES plays "Options", returns the
/// selected list's path (empty without a valid selection) and releases the lists.
///
/// @param[in,out] panel The loaded dialog.
/// @param[in,out] context List and host.
/// @return cancelled, or load with the path to read; none otherwise.
SaveDialogResult restrict_on_load_click(Panel& panel, SaveDialogContext& context);

/// Builds the directory services over the host file system.
///
/// Paths use '\' as the game does and are mapped onto the host file system
/// below `root`; the find walk matches extensions case-insensitively and
/// reports write times in seconds.
///
/// @param root Directory that holds SAVEGAME; must outlive the services.
/// @return The services, with `root` as their context.
SaveFiles savegame_host_files(const std::filesystem::path* root);

/// Builds the summary reader over src/data/persist.
///
/// The reader opens only the "Summary" account of a HAPIBANK save and reads
/// the radar image from its "Radar Image" blob.
///
/// @param root Directory that holds SAVEGAME; must outlive the reader.
/// @return The reader, with `root` as its context.
SaveSummaryReader savegame_persist_reader(const std::filesystem::path* root);

/// Releases the name and description lists and the radar picture.
///
/// @param[in,out] context Its list is emptied, its radar picture cleared and their storage freed.
void savegame_release_lists(SaveDialogContext& context) noexcept;

} // namespace oa::ui::frontend
