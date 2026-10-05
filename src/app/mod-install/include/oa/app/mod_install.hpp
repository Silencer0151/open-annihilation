// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Mod packages (.oamod): a zip archive of a mod's folder, installed into the
// player's own Mods folder under the id its oamod.yaml gives. A package is
// read and checked before anything is written (open_package); what the
// Mods folder holds decides where it goes and what the player is asked
// (plan_install); its files are unpacked into a staging folder inside Mods,
// a piece at a time (Unpacking), then put in place by renames on the same
// volume (commit_change), which keep the version an update replaced in the
// mod's .backup folder, one version back. A crash at any point leaves
// folders that the next start settles to the state before the change or
// after it (recover_changes), and the folders a change drops are deleted a
// little at a time (Discarder). No function here throws: errors are values.
#pragma once

#include "oa/data/mod_profile.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/formats/zip/stream.hpp"
#include "oa/platform/preferences.hpp"

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oa::app::mod_install {

/// A mod package's file extension, matched without case.
inline constexpr std::string_view package_extension = ".oamod";
/// The folder inside a mod's folder that keeps the version an update
/// replaced, which the game never reads (oa::backup_folder_name).
inline constexpr std::string_view backup_folder_name = oa::backup_folder_name;
/// What every folder an install keeps in Mods while it works starts with;
/// the Mods page lists none of them (list_mods_in).
inline constexpr std::string_view reserved_prefix = ".oamod-";
/// A package's files, unpacked, before they are put in place.
inline constexpr std::string_view staging_prefix = ".oamod-staging-";
/// The version leaving the target, which becomes its .backup.
inline constexpr std::string_view old_prefix = ".oamod-old-";
/// The version a reinstall replaces, which is dropped.
inline constexpr std::string_view replaced_prefix = ".oamod-replaced-";
/// The kept version a roll back brings back.
inline constexpr std::string_view restore_prefix = ".oamod-restore-";
/// A folder to be deleted, a little at a time (Discarder).
inline constexpr std::string_view discard_prefix = ".oamod-discard-";
/// The file whose lock a copy of the game holds while it changes Mods.
inline constexpr std::string_view lock_file_name = ".oamod-lock";
/// What a folder recovery cannot settle is renamed to, after its target's
/// name, so that it shows on the Mods page and never blocks a later change.
inline constexpr std::string_view left_over_suffix = "-left-over";
/// The most a package may unpack to, in bytes.
inline constexpr uint64_t max_install_bytes = uint64_t{4} << 30;
/// The bytes kept free on the disk holding Mods besides the package's files.
inline constexpr uint64_t free_space_margin = uint64_t{64} << 20;
/// The most bytes a package may unpack to for each byte it holds, once it
/// unpacks to more than bomb_floor.
inline constexpr uint64_t bomb_ratio = 200;
/// The unpacked size below which bomb_ratio is not checked, in bytes.
inline constexpr uint64_t bomb_floor = uint64_t{64} << 20;
/// The most folders a package may unpack, those its paths pass through
/// included.
inline constexpr std::size_t max_package_folders = 16'384;
/// What each folder or file an unpacking makes, and each folder it syncs,
/// counts for in a step's budget, in bytes, for the system calls it takes.
inline constexpr uint64_t unpack_entry_cost = uint64_t{16} << 10;
/// The characters each unpacked file's path keeps spare below the longest
/// path the system opens.
inline constexpr uint64_t path_room = 16;
/// The folders Install alongside tries: <id>-<version>, then -2 to -99.
inline constexpr uint32_t alongside_tries = 99;

/// Why a package was not installed.
enum class Refusal : uint8_t {
    none,
    unreadable,        ///< the file cannot be opened or read
    not_zip,           ///< it is not a zip archive
    damaged,           ///< a record or an entry's data is not as recorded
    no_profile,        ///< no oamod.yaml at its top, or in one folder at its top
    profile_too_large, ///< its oamod.yaml is larger than the profile reader takes
    profile_errors,    ///< its oamod.yaml does not resolve
    unsafe_name,       ///< a name that cannot be unpacked on every system
    case_clash,        ///< two names that differ only in case, or one that exists already
    link,              ///< a link or a special file
    encrypted,         ///< an encrypted entry
    method,            ///< an entry packed other than stored or deflated
    too_large,         ///< it unpacks to more than max_install_bytes
    bomb,              ///< it unpacks to far more than its own size
    too_many_folders,  ///< it unpacks more than max_package_folders folders
    no_space,          ///< the disk holding Mods has too little room
    path_too_long,     ///< an unpacked path is longer than the system opens
    reserved_id,       ///< its id names a device on Windows
    no_free_folder,    ///< no folder name is free for it alongside the others
    not_placed,        ///< its files could not be put in place
    changed,           ///< the Mods folder changed while it was being installed
    busy,              ///< another copy of the game is changing the Mods folder
};

/// What went wrong, for the player's text and the log.
struct Problem {
    Refusal refusal{Refusal::none};
    std::string subject{};            ///< the entry name or id the reason names
    std::vector<std::string> lines{}; ///< profile diagnostics, one each (profile_errors)
    std::string detail{};             ///< English, for the log: a system error, a zip status
    /// The size the reason names: the unpacked size (too_large, bomb) or
    /// the room needed (no_space).
    uint64_t size_bytes{};
    /// The size it is measured against: the limit (too_large), the
    /// package's own size (bomb) or the room free (no_space).
    uint64_t limit_bytes{};
};

/// One file or folder a package unpacks.
struct PackagedFile {
    std::size_t entry{}; ///< its entry in the package's directory
    /// Its path below the mod's folder, '/' between folders, with the
    /// package's top folder stripped; a folder's ends in '/'.
    std::string name{};
};

/// A package, read and checked: what it installs and where its files come from.
struct Package {
    std::filesystem::path file{}; ///< the package
    std::string file_name{};      ///< its name, UTF-8, which the texts name
    uint64_t archive_bytes{};     ///< its size
    oa::formats::zip::StreamDirectory directory{};
    std::string root{};                ///< the stripped top folder with its '/', or empty
    std::vector<PackagedFile> files{}; ///< what it unpacks, in archive order
    /// Every folder it unpacks, those its paths pass through included, '/'
    /// between parts, each after the folder that holds it.
    std::vector<std::string> folders{};
    std::size_t profile_entry{}; ///< oamod.yaml's entry
    std::shared_ptr<const oa::data::mod_profile::ModProfile> profile{};
    std::vector<std::string> warnings{}; ///< the profile's warnings, formatted
    uint64_t unpacked_bytes{};           ///< its files' sizes added up
    bool backup_left_out{}; ///< it held a .backup folder of its own, which is not unpacked
};

/// How a package's profile is resolved, as a load of the mod resolves it.
struct PackageOptions {
    /// Accept hacks this build does not implement yet, with a warning each.
    bool accept_unimplemented_hacks{};
    /// The player's preferences, whose registry section the profile's
    /// registry bindings read; null for none.
    const oa::platform::preferences::Values* preferences{};
    /// The game folder the mod layers over, whose INI file the profile's
    /// settings come from when the package holds none; empty for none.
    std::filesystem::path game_folder{};
};

/// A package read, or why not.
struct PackageResult {
    std::optional<Package> package{}; ///< set when it can be installed
    Problem problem{};                ///< why not; refusal none when it can
};

/// Reads and checks a package, writing nothing: its directory, the names
/// and sizes of what it unpacks, and its profile, resolved twice as a load
/// of the mod resolves it (the second time with the settings its INI file
/// and the registry give). The first problem found refuses it.
///
/// It ignores __MACOSX folders, .DS_Store files and AppleDouble files (a
/// last part starting "._"), and a .backup folder of its own. Its oamod.yaml
/// lies at its top, matched without case, or in the one folder its top
/// holds, which is stripped, as the Finder's Compress makes it.
///
/// @param file the package
/// @param options how its profile is resolved
/// @return the package, or the problem
[[nodiscard]] PackageResult
open_package(const std::filesystem::path& file, const PackageOptions& options = {});

/// Says why a relative path in a package cannot be unpacked on every system
/// the game runs on. Each part must be 1 to 255 bytes of valid UTF-8 with no
/// control character, none of < > : " | ? * and backslash, not end in '.' or
/// ' ', and its part before the first '.' must not be a name Windows keeps
/// for a device (CON, PRN, AUX, NUL, COM0 to COM9, LPT0 to LPT9, COM and LPT
/// with a superscript 1, 2 or 3, CONIN$ and CONOUT$, in any case).
///
/// @param relative_name the path, '/' between its parts; a folder's may end in '/'
/// @return the reason, in English; empty when it can be unpacked
[[nodiscard]] std::string portable_name_problem(std::string_view relative_name);

/// Tells whether a name is one Windows keeps for a device, in any case.
///
/// @param name the name, without an extension
/// @return true for CON, PRN, AUX, NUL, COM0 to COM9, LPT0 to LPT9, and the others
[[nodiscard]] bool windows_device_name(std::string_view name) noexcept;

/// Returns the folder-safe form of a version, for Mods/<id>-<version>: ASCII
/// letters lowered, digits, '.', '_' and '-' kept, every other byte '-',
/// runs of '-' made one, '.' and '-' cut from both ends, at most 32 bytes;
/// "version" when nothing is left. "10.2" gives "10.2", "2.1 Beta" "2.1-beta".
///
/// @param version the profile's version
/// @return the folder-safe form
[[nodiscard]] std::string version_folder_part(std::string_view version);

/// Tells whether a path names a .oamod file by its extension, matched without case.
///
/// @param file the path
/// @return true for a .oamod file
[[nodiscard]] bool names_mod_package(const std::filesystem::path& file);

/// What a folder in Mods holds.
enum class FolderKind : uint8_t {
    missing, ///< nothing of that name
    /// a file, a link or junction, a folder without an oamod.yaml, or one
    /// whose oamod.yaml or id cannot be read
    other,
    mod, ///< a folder whose oamod.yaml gives an id
};

/// A mod as a folder's oamod.yaml gives it, read without resolving it.
struct InstalledMod {
    FolderKind kind{FolderKind::missing};
    std::string id{};      ///< the profile's id
    std::string name{};    ///< its name
    std::string version{}; ///< its version, as written
    int64_t revision{};    ///< packaging.revision; 0 when it cannot be read
};

/// Tells whether two reads of a folder found the same: kind, id, version
/// and revision.
///
/// @param left one read
/// @param right the other
/// @return true when they agree
[[nodiscard]] bool same_mod(const InstalledMod& left, const InstalledMod& right) noexcept;

/// The bit of a Windows reparse tag that marks a name surrogate
/// (IsReparseTagNameSurrogate): a reparse point that names another place.
inline constexpr uint32_t reparse_tag_name_surrogate = 0x20000000U;

/// Tells whether a Windows reparse point with this tag is a link: a name
/// surrogate, such as a junction or mount point (IO_REPARSE_TAG_MOUNT_POINT)
/// or a symbolic link (IO_REPARSE_TAG_SYMLINK), which names another place.
/// Any other tag, such as the cloud tags a file sync puts on the folders and
/// files it keeps, marks a folder or file that is itself.
///
/// @param tag the reparse tag
/// @return true for a name surrogate
[[nodiscard]] constexpr bool reparse_tag_is_link(uint32_t tag) noexcept {
    return (tag & reparse_tag_name_surrogate) != 0;
}

/// Tells whether a path is a link: a symbolic link, or on Windows a
/// reparse point whose tag is a name surrogate (reparse_tag_is_link), such
/// as a junction; something that is removed, never entered. A reparse point
/// whose tag cannot be read counts as a link.
///
/// @param path the path
/// @return true for a link or junction
[[nodiscard]] bool is_link_or_junction(const std::filesystem::path& path);

/// Reads what a folder in Mods holds: its oamod.yaml's id, name, version
/// and packaging.revision, read without resolving the profile.
///
/// @param folder the folder
/// @return what it holds
[[nodiscard]] InstalledMod read_installed_mod(const std::filesystem::path& folder);

/// Reads what a mod folder's .backup holds.
///
/// @param folder the mod folder
/// @return nothing when it has no .backup; else what it holds, of kind mod
///         only when it is a folder, not a link, whose oamod.yaml gives the
///         folder's own id
[[nodiscard]] std::optional<InstalledMod> read_backup(const std::filesystem::path& folder);

/// The mod a package installs.
struct Incoming {
    std::string id{};
    std::string name{};
    std::string version{};
    int64_t revision{};
};

/// Returns the mod a profile installs.
///
/// @param profile the package's profile
/// @return its id, name, version and revision
[[nodiscard]] Incoming incoming_of(const oa::data::mod_profile::ModProfile& profile);

/// What an install does, or asks first.
enum class PlanKind : uint8_t {
    install,       ///< no question: unpack into `target`
    ask_update,    ///< the same version, another revision: Replace | Cancel
    ask_reinstall, ///< the same version and revision: Reinstall | Cancel
    ask_version,   ///< another version installed: Replace | Install alongside | Cancel
    ask_alongside, ///< the folder holds something else: Install alongside | Cancel
    refuse,        ///< no folder is free alongside (Refusal::no_free_folder)
};

/// Where an install goes and what it asks.
struct InstallPlan {
    PlanKind kind{PlanKind::install};
    std::string target{};     ///< the folder in Mods that install, replace and reinstall act on
    std::string alongside{};  ///< the folder Install alongside makes; empty when not offered
    InstalledMod installed{}; ///< what `target` holds now
    std::optional<InstalledMod> backup{}; ///< what target/.backup holds now
    bool older{}; ///< ask_update: the incoming revision is below the installed one
};

/// What the folders of Mods hold, for the planner.
struct ModsFolderHooks {
    void* context{}; ///< passed back to each function
    /// Returns what a folder of Mods, by its name, holds; null finds every
    /// folder missing.
    InstalledMod (*look)(void* context, std::string_view folder){};
    /// Returns what a folder's .backup holds (read_backup); null finds none.
    std::optional<InstalledMod> (*backup_of)(void* context, std::string_view folder){};
};

/// Returns the hooks that read a real Mods folder.
///
/// @param mods the Mods folder; it must outlive the hooks
/// @return the hooks
[[nodiscard]] ModsFolderHooks mods_folder_hooks(const std::filesystem::path& mods) noexcept;

/// Returns the folder names Install alongside tries for a version, in order:
/// <id>-<version>, then <id>-<version>-2 to -alongside_tries.
///
/// @param incoming the mod
/// @return the names
[[nodiscard]] std::vector<std::string> alongside_names(const Incoming& incoming);

/// Plans an install from what the folders of Mods hold. The package's file
/// name never matters: only its id and version do. Its own id's folder
/// that holds it at the same version is updated or reinstalled; else the
/// first of the alongside folders that holds it at that version; else a
/// missing folder of its id is installed into; a folder of its id at
/// another version asks whether to replace it or install alongside; a
/// folder of that name that holds something else is never replaced, and
/// asks to install alongside.
///
/// @param incoming the mod the package installs
/// @param hooks what each folder holds
/// @return the plan
[[nodiscard]] InstallPlan plan_install(const Incoming& incoming, const ModsFolderHooks& hooks);

/// A lock on a file, held for the life of the object, which another process,
/// or another FileLock in this one, cannot take while it is held. The file
/// stays open: on Windows others may read it but not write it; elsewhere it
/// carries an exclusive advisory lock. No child process inherits it.
class FileLock {
  public:

    /// Takes the lock, making the file when it is missing.
    ///
    /// @param file the lock file
    /// @return the lock; null when another holder has it or the file cannot be made
    [[nodiscard]] static std::unique_ptr<FileLock> take(const std::filesystem::path& file);

    FileLock(const FileLock&) = delete;
    FileLock& operator=(const FileLock&) = delete;

    /// Releases the lock and closes the file.
    ~FileLock();

  private:

    FileLock() = default;
#ifdef _WIN32
    void* handle_{};
#else
    int descriptor_{-1};
#endif
};

/// A hold on a Mods folder's lock (lock_file_name), shared by every hold
/// this process takes on the same folder: changes and recovery in one
/// process never wait for each other, and another copy of the game cannot
/// change the folder while any is held.
using ModsHold = std::shared_ptr<const FileLock>;

/// Takes a hold on a Mods folder's lock, making the folder's lock file when
/// it is missing.
///
/// @param mods the Mods folder, which must exist
/// @return the hold; null while another copy of the game holds the lock
[[nodiscard]] ModsHold hold_mods_folder(const std::filesystem::path& mods);

/// Stand-ins for the unpacking's reads of the file system.
struct UnpackHooks {
    void* context{}; ///< passed back to each function
    /// Returns the bytes free on the disk that holds a folder; null asks the system.
    uint64_t (*available)(void* context, const std::filesystem::path& folder){};
    /// Runs just before a file of the package is made in staging; null does
    /// nothing. A test makes the file there first, as a name the file system
    /// folds to another would be.
    void (*before_file)(void* context, const std::filesystem::path& file){};
};

/// A package's files unpacked into a staging folder inside Mods, a piece at
/// a time: its folders made, then each file made anew (never over another),
/// written, flushed and synced, its size and CRC-32 checked, then every
/// folder synced and, on macOS, the drive's cache written to its storage. A
/// failure turns the staging folder into a discard folder; nothing else in
/// Mods changes.
class Unpacking {
  public:

    /// Makes an unpacking that has not started.
    Unpacking();

    /// Ends the unpacking; a staging folder it left stays for recovery or
    /// for its change.
    ~Unpacking();
    Unpacking(const Unpacking&) = delete;
    Unpacking& operator=(const Unpacking&) = delete;

    /// Starts: makes Mods, takes its lock, checks that the target still
    /// holds what the plan found, that the disk has room for the files and
    /// free_space_margin and that no unpacked path is too long, then makes
    /// the staging folder afresh (an old one becomes a discard folder) and
    /// opens the package.
    ///
    /// @param package the package
    /// @param mods the Mods folder
    /// @param target the folder in Mods the files are for
    /// @param expected what the target held when the plan was made
    /// @param[out] problem why it cannot start
    /// @param hooks stand-ins for the file system's reads
    /// @return true when the unpacking started
    [[nodiscard]] bool start(
        const Package& package,
        const std::filesystem::path& mods,
        std::string_view target,
        const InstalledMod& expected,
        Problem& problem,
        const UnpackHooks& hooks = {}
    );

    /// Unpacks on until `budget` bytes of the package are read, each folder
    /// or file made and each folder synced counting as unpack_entry_cost
    /// bytes, or until about 4 ms have passed, or the end.
    ///
    /// @param budget the package bytes this step may read
    /// @param[out] problem why it failed
    /// @return more, done once every file is in staging and synced, or failed
    [[nodiscard]] oa::formats::zip::StreamStep step(uint64_t budget, Problem& problem);

    /// Stops: the staging folder becomes a discard folder.
    void cancel() noexcept;

    /// Returns the uncompressed bytes written so far.
    ///
    /// @return the bytes
    [[nodiscard]] uint64_t done_bytes() const noexcept;

    /// Returns the uncompressed bytes the package unpacks to.
    ///
    /// @return the bytes
    [[nodiscard]] uint64_t total_bytes() const noexcept;

    /// Returns the staging folder.
    ///
    /// @return its path; empty before start
    [[nodiscard]] std::filesystem::path staging() const;

    /// Hands over the hold on the Mods folder's lock, for the change that
    /// follows the unpacking.
    ///
    /// @return the hold; null when none is held
    [[nodiscard]] ModsHold take_hold() noexcept;

    /// Returns the discard folders a failure or a cancel made, for deletion.
    ///
    /// @return their paths
    [[nodiscard]] const std::vector<std::filesystem::path>& discards() const noexcept;

  private:

    struct State;

    /// Moves the staging folder aside to be deleted, as a failed or stopped
    /// unpacking leaves it, closing the file being written.
    void discard_staging() noexcept;

    std::unique_ptr<State> state_;
};

/// A change to a folder of Mods.
enum class Change : uint8_t {
    install,   ///< the staged files become the target, which is missing
    replace,   ///< the staged files replace the target, which becomes its .backup
    reinstall, ///< the staged files replace the target, which is dropped; its .backup stays
    roll_back, ///< the target and its .backup swap places
};

/// Stand-ins for the change's renames and its waits between tries.
struct ChangeHooks {
    void* context{}; ///< passed back to each function
    /// Renames `from` to `to`, failing when `to` exists, and sets `error`
    /// when it fails; null renames exclusively (rename_exclusively).
    void (*rename)(
        void* context,
        const std::filesystem::path& from,
        const std::filesystem::path& to,
        std::error_code& error
    ){};
    /// Waits a while before a rename is tried again, as another program,
    /// such as a file sync or a virus scanner, may hold its files; null sleeps.
    void (*wait)(void* context, uint32_t milliseconds){};
};

/// How a change runs.
struct ChangeOptions {
    ChangeHooks hooks{};
    /// The first wait before a refused rename is tried again, in
    /// milliseconds: each wait doubles, up to ten times this, until
    /// most_wait_ms have passed. 0 tries again at once, up to eight times.
    uint32_t first_retry_ms{100};
    /// The most a rename waits in all, in milliseconds.
    uint32_t most_wait_ms{10'000};
    /// What the target must still hold; nothing checks nothing.
    std::optional<InstalledMod> expected{};
};

/// What a change did.
struct ChangeResult {
    bool changed{};     ///< the new files, or the kept version, are in the target
    bool backup_kept{}; ///< target/.backup holds the version the target held before
    /// A folder the change could not settle, which holds the version the
    /// target held: the player is told where it is; empty for none.
    std::filesystem::path left_over{};
    Refusal refusal{Refusal::none}; ///< why it changed nothing: not_placed, changed or busy
    std::string detail{};           ///< English, for the log
    std::vector<std::filesystem::path> discards{}; ///< folders to delete (Discarder)
};

/// Renames a file or folder, failing when the new name exists, as one step
/// of the file system: never replacing what is there.
///
/// @param from the path
/// @param to the new path
/// @param[out] error set when it fails; on Windows the system's own error
void rename_exclusively(
    const std::filesystem::path& from, const std::filesystem::path& to, std::error_code& error
) noexcept;

/// Puts a change in place with renames on the same volume, each tried again
/// for a while when another program holds the files. Install moves the
/// staged files to the target. Replace moves the target aside, the staged
/// files in, drops the old version's own .backup and moves the old version
/// into the target's .backup. Reinstall moves the target aside, the staged
/// files in, the old version's .backup over and drops the old version.
/// Roll back swaps the target and its .backup. A failure before the new
/// files are in place undoes what was done; after it, the rest is tried
/// once more, and what it could not settle is renamed to a visible
/// <target>-left-over folder. Roll back refuses a .backup that is a link
/// or junction. Once the files are in place, the folders the renames
/// changed are synced, as the staging folder's files were.
///
/// @param mods the Mods folder
/// @param target the folder in Mods
/// @param change what to do
/// @param options the hooks, the waits and what the target must hold
/// @return what it did
[[nodiscard]] ChangeResult commit_change(
    const std::filesystem::path& mods,
    std::string_view target,
    Change change,
    const ChangeOptions& options = {}
);

/// What recovery did.
struct Recovery {
    std::vector<std::string> lines{};              ///< what it did, for the log
    std::vector<std::filesystem::path> discards{}; ///< folders to delete (Discarder)
    bool skipped{}; ///< another copy of the game held the folder's lock: nothing was done
};

/// Settles every change a stop left in a Mods folder to the state before it
/// or after it, by the folders it left, and lists the folders to delete. A
/// folder it cannot settle is renamed to a visible <target>-left-over
/// folder. Nothing is done while another copy of the game holds the lock.
///
/// @param mods the Mods folder
/// @param options the hooks and the waits
/// @return what it did
[[nodiscard]] Recovery
recover_changes(const std::filesystem::path& mods, const ChangeOptions& options = {});

/// Deletes discard folders a little at a time: each step removes at most
/// 64 entries, or works 4 ms. Links and junctions, a discard folder that is
/// one included, are removed, never entered; read-only files are made
/// writable first. An entry that cannot be removed is left, and logged once.
class Discarder {
  public:

    /// Adds folders to delete.
    ///
    /// @param folders their paths, inside one Mods folder
    void add(std::span<const std::filesystem::path> folders);

    /// Deletes a little more, under the Mods folder's lock.
    ///
    /// @return true while anything is left to delete
    bool step();

    /// Tells whether anything is left to delete.
    ///
    /// @return true while it is
    [[nodiscard]] bool busy() const noexcept;

  private:

    std::vector<std::filesystem::path> folders_;
    std::vector<std::filesystem::path> stack_;   ///< the folders being emptied, the deepest last
    std::vector<std::filesystem::path> skipped_; ///< entries that could not be removed
};

/// A file made anew, never over another, written and synced to the disk
/// before it is closed: on macOS, to the drive, whose own cache the
/// unpacking has written to storage once its last file is made.
class NewFile {
  public:

    NewFile() = default;
    NewFile(const NewFile&) = delete;
    NewFile& operator=(const NewFile&) = delete;

    /// Closes the file when it is open.
    ~NewFile();

    /// Makes the file, failing when anything of its name exists.
    ///
    /// @param file the path
    /// @param[out] error set when it fails; std::errc::file_exists when it exists
    /// @return true when it was made
    [[nodiscard]] bool create(const std::filesystem::path& file, std::error_code& error) noexcept;

    /// Writes bytes at its end.
    ///
    /// @param bytes the bytes
    /// @return true when all were written
    [[nodiscard]] bool write(std::span<const uint8_t> bytes) noexcept;

    /// Flushes and syncs the file to the disk and closes it.
    ///
    /// @return true when it reached the disk
    [[nodiscard]] bool finish() noexcept;

    /// Closes the file as it is, when it is open, without syncing it.
    void close() noexcept;

  private:

#ifdef _WIN32
    void* handle_{};
#else
    int descriptor_{-1};
#endif
};

} // namespace oa::app::mod_install
