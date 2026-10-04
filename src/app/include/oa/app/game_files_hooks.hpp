// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// What the platform the game runs on provides for bringing the player's game
// files into the game's own storage: its system file picker, listing and
// copying the chosen files, free space, time to finish a copy away from the
// screen, device backups and its own words for the Game files screen. A
// platform's extension init fills the hooks; the desktop leaves them null, so
// the Game files screen is never offered there.
#pragma once

#include <atomic>
#include <cstddef>
#include <stdint.h>
#include <string>
#include <vector>

namespace oa::app {

/// What the platform offers for bringing game files in (bits of GameFilesHooks::capabilities).
enum class GameFilesCapability : uint32_t {
    pick_folder = 1u << 0,      ///< the picker chooses folders
    pick_files = 1u << 1,       ///< the picker chooses several files at once
    shared_documents = 1u << 2, ///< the player's own file manager reaches the game folder
    remote_files = 1u << 3,     ///< sources may hold files that are downloaded when read
    background_time = 1u << 4,  ///< a copy may go on for a while away from the screen
};

/// Tells whether a capability bit is set.
///
/// @param bits GameFilesHooks::capabilities' answer
/// @param capability the capability asked about
/// @return true when its bit is set in `bits`
[[nodiscard]] constexpr bool
has_capability(uint32_t bits, GameFilesCapability capability) noexcept {
    return (bits & static_cast<uint32_t>(capability)) != 0;
}

/// What a picker is asked to choose.
enum class PickKind : uint8_t {
    game_folder,      ///< one folder: the Total Annihilation folder
    demo_installer,   ///< one file, copied for the game: the demo's installer
    additions_folder, ///< one folder: an expansion, extra maps or a mod
    archives,         ///< several .hpi, .ufo, .ccx or .gp3 files
};

/// The platform's own words in the screen's texts.
enum class GameFilesText : uint8_t {
    device,              ///< the device's own name; neutral "this device" is written by the engine
    copy_yourself_steps, ///< the steps of Copy it yourself, one or two sentences
    copy_yourself_short, ///< the same in one line, for the phone rows
    picker_places, ///< where the picker reaches, as a phrase after "Pick the folder that holds totala1.hpi:"
    picker_places_short, ///< the same for the phone rows
    free_space_advice,   ///< how to free space, an imperative phrase
    folder_location,     ///< where the game folder shows in the file manager
    cloud_name,          ///< the cloud service's name
};
/// How many GameFilesText words there are.
inline constexpr std::size_t game_files_text_count = 8;

/// How copying one file ended.
enum class CopyOutcome : uint8_t {
    copied,     ///< the staged <name>.part was written whole, synced, stamped and renamed
    stopped,    ///< the stop flag was seen; the .part file was removed
    no_space,   ///< the disk filled; the .part file was removed
    unreadable, ///< the source could not be read (error says why)
    gone,       ///< the source went away (a drive, a share)
    offline,    ///< a cloud download failed
    denied,     ///< access to the source was withdrawn
    changed,    ///< the source's size or time no longer match the plan
};

/// One entry of a source folder.
struct SourceEntry {
    std::string path{};     ///< relative to the source, '/'-separated, UTF-8
    uint64_t size{};        ///< bytes, when size_known
    bool size_known = true; ///< false when the provider did not report a size
    int64_t modified{};     ///< seconds since 1970, for resuming
    bool folder{};          ///< a folder
    bool link{};            ///< a symbolic link: never followed, never copied
    bool remote{};          ///< held by a cloud service and downloaded when it is read
};

struct FileCopy;

/// The engine's chunked copy: what the platform calls once `readable_source` can be read.
using ChunkedCopy =
    CopyOutcome (*)(const char* readable_source, const FileCopy& file, std::string* error);

/// One file to copy.
struct FileCopy {
    const char* source{};   ///< UTF-8, absolute: the source file as listed
    const char* target{};   ///< UTF-8, absolute: the staged <name>.part
    uint64_t size{};        ///< the planned size, when size_known
    bool size_known = true; ///< false: the size was unknown at listing
    int64_t modified{};     ///< set on the target once complete; checked against the source
    std::atomic<uint64_t>*
        bytes_done{};                ///< bytes of this file written so far, which the screen reads
    const std::atomic<bool>* stop{}; ///< set by Stop and by the background expiry
    ChunkedCopy copy{};              ///< the engine's copy (game_files::chunked_copy)
};

/// The answer show_picker gives, on the main thread.
using PickerDone = void (*)(
    void* userdata, const std::vector<std::string>& paths, bool movable, const char* error
);

/// The platform's side of bringing the player's game files into the game's own storage.
/// Installed by the platform's extension init; every member may be null; no member throws.
struct GameFilesHooks {
    void* context{}; ///< passed back to every hook
    /// Returns the GameFilesCapability bits. Null: the import is not offered and resolution
    /// behaves as without these hooks.
    uint32_t (*capabilities)(void* context){};
    /// Returns the platform's words for `which`, UTF-8 in static storage. Null, or a null
    /// result: the engine's neutral words.
    const char* (*text)(void* context, GameFilesText which){};
    /// Writes where the game folder goes, absolute UTF-8, whether or not it exists, and returns
    /// true. Null or false: the import is not offered.
    bool (*game_folder)(void* context, std::string* folder){};
    /// Shows the system's picker for `kind`; `done` runs once, on the main thread while events
    /// are pumped: the chosen paths (readable until release_source; `movable` when they are
    /// copies made for the game that the engine may move), none for a cancel, or `error`.
    /// Null: the picker routes are not offered.
    void (*show_picker)(void* context, PickKind kind, PickerDone done, void* userdata){};
    /// Ends the access show_picker gave to `path`. Null: there is nothing to end.
    void (*release_source)(void* context, const char* path){};
    /// Lists `path` down to `max_depth` levels below it (0: its own entries only), calling
    /// `entry` for each, on the calling thread; false with `error` when it cannot be listed.
    /// Null: the engine walks it with std::filesystem.
    bool (*list_source)(
        void* context,
        const char* path,
        uint32_t max_depth,
        void (*entry)(void* userdata, const SourceEntry& entry),
        void* userdata,
        std::string* error
    ){};
    /// Copies one file on the worker thread, downloading it first where needed, by calling
    /// file.copy with a readable path. Null: file.copy runs on file.source as it is.
    CopyOutcome (*copy_file)(void* context, const FileCopy& file, std::string* error){};
    /// Writes the bytes the game files may use on the volume of `folder`. Null: std::filesystem::space.
    bool (*free_space)(void* context, const char* folder, uint64_t* bytes){};
    /// Asks for time to finish away from the screen (true) or gives it back (false);
    /// `expiring` runs on any thread when the time is up. Null: no such request is made.
    void (*keep_running)(
        void* context, bool running, void (*expiring)(void* userdata), void* userdata
    ){};
    /// Keeps `path` out of device backups (false) or puts it back (true). Null: not managed.
    bool (*set_backed_up)(void* context, const char* path, bool backed_up){};
};

/// Installs the platform's game files hooks (a copy is kept).
///
/// @param hooks the hooks; members left null are not used
void set_game_files_hooks(const GameFilesHooks& hooks) noexcept;
/// Returns the installed hooks; all null when none were installed.
///
/// @return the hooks set_game_files_hooks kept
[[nodiscard]] const GameFilesHooks& game_files_hooks() noexcept;
/// Tells whether the import is offered: capabilities and game_folder are set.
///
/// @param hooks the hooks to look at, usually game_files_hooks()
/// @return true when both members are set
[[nodiscard]] bool game_files_import_offered(const GameFilesHooks& hooks) noexcept;

} // namespace oa::app
