// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The import's state file, the commit, recovery at the next start, discarding
// a copy, removing a folder the player confirmed, device backups, and the
// files the player copied themselves (game_files_import.hpp).
//
// Every step that changes the game folder is a rename, so a start that finds
// a commit cut short finishes it: each rename either happened or did not.
// An earlier game folder is set aside as "Total Annihilation (old)", never
// deleted; only removals the player confirmed delete anything, and they wait
// for the next start because the running game has the archives open.
#include "oa/app/game_files_import.hpp"

#include "oa/formats/zip.hpp"

#include <algorithm>
#include <array>
#include <cerrno>
#include <charconv>
#include <cstdio>
#include <fstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#include <io.h>
#include <share.h>
#else
#include <unistd.h>
#endif

namespace oa::app::game_files {
namespace {

/// The suffix of the file a new state is written to before it is renamed over the state.
constexpr std::string_view new_state_suffix = ".new";
/// The removal that takes every game file.
constexpr std::string_view remove_everything = "*";
/// What a commit says when the checked copy it was to put in place has gone.
constexpr std::string_view copy_missing = "the checked copy is no longer there";

/// The state file's names of the phases, by ImportPhase.
constexpr std::array<std::string_view, 3> phase_names{"copying", "checked", "committing"};
/// The state file's names of the modes, by ImportMode.
constexpr std::array<std::string_view, 4> mode_names{"replace", "add", "mod", "remove"};
/// The state file's names of the sources, by SourceKind.
constexpr std::array<std::string_view, 4> kind_names{
    "game-folder", "demo-installer", "additions-folder", "archives"
};
/// The state file's names of the parts' switches, by Part.
constexpr std::array<std::string_view, part_count> part_names{
    "game-archives",
    "update-31c",
    "core-contingency",
    "battle-tactics",
    "extra",
    "music",
    "movies",
    "mods",
    "other",
    "demo",
};
/// The state file's keys.
constexpr std::string_view phase_key = "phase";
constexpr std::string_view mode_key = "mode";
constexpr std::string_view kind_key = "kind";
constexpr std::string_view location_key = "location";
constexpr std::string_view bytes_key = "bytes";
constexpr std::string_view files_key = "files";
constexpr std::string_view part_key_prefix = "part.";
constexpr std::string_view mod_off_key = "mod-off";
constexpr std::string_view mod_id_key = "mod-id";
constexpr std::string_view remove_key = "remove";
constexpr std::string_view move_source_key = "move-source";

/// Lower-cases one ASCII letter, as the engine matches names without case.
///
/// @param c a byte of UTF-8 text
/// @return the byte, with A-Z made a-z
constexpr char lower_ascii(char c) noexcept {
    return c >= 'A' && c <= 'Z' ? static_cast<char>(c - 'A' + 'a') : c;
}

/// Lower-cases the ASCII letters of a name.
///
/// @param text UTF-8 text
/// @return the text with A-Z made a-z
std::string folded(std::string_view text) {
    std::string out(text);
    for (char& c : out)
        c = lower_ascii(c);
    return out;
}

/// Tells whether two names are the same without case.
///
/// @param left a name
/// @param right another name
/// @return true when they differ at most in ASCII capital letters
bool same_folded(std::string_view left, std::string_view right) noexcept {
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [](char a, char b) {
               return lower_ascii(a) == lower_ascii(b);
           });
}

/// Writes a value so that it fits on one line: backslashes and line breaks escaped.
///
/// @param text the value
/// @return the escaped value
std::string escaped(std::string_view text) {
    std::string out;
    for (const char c : text) {
        if (c == '\\')
            out += "\\\\";
        else if (c == '\n')
            out += "\\n";
        else if (c == '\r')
            out += "\\r";
        else
            out += c;
    }
    return out;
}

/// Reads a value escaped().
///
/// @param text the escaped value
/// @return the value
std::string unescaped(std::string_view text) {
    std::string out;
    for (std::size_t index = 0; index < text.size(); ++index) {
        if (text[index] != '\\' || index + 1 == text.size()) {
            out += text[index];
            continue;
        }
        const char next = text[++index];
        out += next == 'n' ? '\n' : next == 'r' ? '\r' : next;
    }
    return out;
}

/// Finds a name in a table of names.
///
/// @param names the table
/// @param name the name
/// @return its index; nothing when it is not there
template <std::size_t count>
std::optional<std::size_t>
index_of(const std::array<std::string_view, count>& names, std::string_view name) {
    const auto found = std::find(names.begin(), names.end(), name);
    if (found == names.end())
        return std::nullopt;
    return static_cast<std::size_t>(found - names.begin());
}

/// Reads a whole number.
///
/// @param text decimal digits
/// @return the number; 0 when it is not one
uint64_t number_of(std::string_view text) {
    uint64_t value = 0;
    const auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), value);
    return error == std::errc{} && end == text.data() + text.size() ? value : 0;
}

/// Opens a file for writing, by its wide path on Windows.
///
/// @param path the file
/// @return the open file; null when it cannot be made
std::FILE* create_file(const fs::path& path) {
#if defined(_WIN32)
    return _wfsopen(path.c_str(), L"wb", _SH_DENYNO);
#else
    return std::fopen(path.c_str(), "wb");
#endif
}

/// Writes a file whole through to the disk.
///
/// @param path the file
/// @param text its contents
/// @param[out] error why it could not be written
/// @return true when it was written and synced
bool write_synced(const fs::path& path, std::string_view text, std::string& error) {
    std::FILE* file = create_file(path);
    if (file == nullptr) {
        error = std::generic_category().message(errno);
        return false;
    }
    bool written =
        std::fwrite(text.data(), 1, text.size(), file) == text.size() && std::fflush(file) == 0;
#if defined(_WIN32)
    written = written && _commit(_fileno(file)) == 0;
#else
    written = written && ::fsync(::fileno(file)) == 0;
#endif
    if (!written)
        error = std::generic_category().message(errno);
    if (std::fclose(file) != 0 && written) {
        error = std::generic_category().message(errno);
        written = false;
    }
    return written;
}

/// Renames a file over another, replacing it.
///
/// @param from the file to rename
/// @param to its new name, replaced when it exists
/// @param[out] error why it could not be renamed
/// @return true when it was renamed
bool replace_file(const fs::path& from, const fs::path& to, std::string& error) {
    std::error_code failure;
    fs::rename(from, to, failure);
    if (failure) {
        // Some systems rename only onto a free name.
        std::error_code ignored;
        fs::remove(to, ignored);
        failure.clear();
        fs::rename(from, to, failure);
    }
    if (failure)
        error = failure.message();
    return !failure;
}

/// Tells whether something is at a path, a symbolic link included.
///
/// @param path the path
/// @return true when a file, folder or link is there
bool present(const fs::path& path) noexcept {
    std::error_code error;
    return fs::exists(fs::symlink_status(path, error));
}

/// Returns a free name to set an earlier game folder aside under.
///
/// @param paths the import's folders
/// @return "Total Annihilation (old)", or the first of " 2", " 3" and on that is free
fs::path free_old_folder(const ImportPaths& paths) {
    for (uint32_t number = 1;; ++number) {
        auto candidate = paths.documents / path_from_utf8(old_folder_name(number));
        if (!present(candidate))
            return candidate;
    }
}

/// Finds a path below a folder, each component matched without case where its own spelling
/// is not there.
///
/// @param root the folder
/// @param relative the path below it, '/'-separated
/// @param[out] found true when something is there
/// @return the path, spelled as it is on disk as far as it exists
fs::path find_without_case(const fs::path& root, std::string_view relative, bool& found) {
    fs::path at = root;
    found = true;
    std::string_view rest = relative;
    while (!rest.empty()) {
        const auto slash = rest.find('/');
        const auto part = rest.substr(0, slash);
        rest = slash == std::string_view::npos ? std::string_view{} : rest.substr(slash + 1);
        if (part.empty())
            continue;
        const auto exact = at / path_from_utf8(part);
        if (!found || present(exact)) {
            at = exact;
            continue;
        }
        std::error_code error;
        fs::path match;
        for (fs::directory_iterator entry{at, error}, end; !error && entry != end;
             entry.increment(error))
            if (same_folded(path_to_utf8(entry->path().filename()), part)) {
                match = entry->path();
                break;
            }
        if (match.empty()) {
            found = false;
            at = exact;
        } else {
            at = match;
        }
    }
    return at;
}

/// Removes the folders a removal left empty, from a path's parent up to the game folder.
///
/// @param game_folder the game folder, which stays
/// @param removed the path removed
void prune_empty_folders(const fs::path& game_folder, const fs::path& removed) {
    std::error_code error;
    const auto root = fs::weakly_canonical(game_folder, error);
    for (fs::path at = removed.parent_path(); !at.empty() && at != at.parent_path();
         at = at.parent_path()) {
        std::error_code status;
        if (fs::weakly_canonical(at, status) == root || !fs::is_directory(at, status) ||
            !fs::is_empty(at, status))
            break;
        fs::remove(at, status);
    }
}

/// Removes the paths the player confirmed removing from the game folder.
///
/// @param paths the import's folders
/// @param removals relative paths; "*" removes the game folder with everything in it
/// @param[out] error why one could not be removed
/// @return true when every one is gone
bool apply_removals(
    const ImportPaths& paths, const std::vector<std::string>& removals, std::string& error
) {
    bool removed = true;
    for (const auto& removal : removals) {
        std::error_code failure;
        if (removal == remove_everything) {
            fs::remove_all(paths.game_folder, failure);
        } else {
            // A removal only ever names a path inside the game folder.
            if (!oa::formats::zip::name_is_safe(removal))
                continue;
            bool found = false;
            const auto target = find_without_case(paths.game_folder, removal, found);
            // A link itself may go, but nothing is removed through one.
            if (!found || passes_through_link(paths.game_folder, target.parent_path()))
                continue;
            fs::remove_all(target, failure);
            if (!failure)
                prune_empty_folders(paths.game_folder, target);
        }
        if (failure) {
            error = removal + ": " + failure.message();
            removed = false;
        }
    }
    return removed;
}

/// Ends an import's state once its change is done: removals still waiting are kept for the
/// next start, otherwise the state is removed.
///
/// @param paths the import's folders
/// @param state the state
/// @param[out] error why the state could not be written
/// @return true when it was
bool finish_state(const ImportPaths& paths, const ImportState& state, std::string& error) {
    if (!state.removals.empty()) {
        ImportState waiting;
        waiting.phase = ImportPhase::checked;
        waiting.mode = ImportMode::remove;
        waiting.parts.fill(true);
        waiting.removals = state.removals;
        return write_import_state(paths.state_file, waiting, &error);
    }
    std::error_code failure;
    fs::remove(paths.state_file, failure);
    if (failure)
        error = failure.message();
    return !failure;
}

/// Sets the backup setting on a folder, when the platform manages backups and it exists.
///
/// @param hooks the platform's hooks
/// @param folder the folder
/// @param backed_up the player's backup setting
void set_backed_up(const GameFilesHooks& hooks, const fs::path& folder, bool backed_up) {
    std::error_code error;
    if (hooks.set_backed_up != nullptr && fs::is_directory(folder, error))
        static_cast<void>(
            hooks.set_backed_up(hooks.context, path_to_utf8(folder).c_str(), backed_up)
        );
}

/// Commits a replacement: the game folder set aside, the copy renamed into its place.
///
/// @param hooks the platform's hooks
/// @param paths the import's folders
/// @param state the import's state
/// @param backed_up the player's backup setting
/// @return what the commit did
CommitResult commit_replacement(
    const GameFilesHooks& hooks, const ImportPaths& paths, ImportState state, bool backed_up
) {
    CommitResult result;
    const auto source =
        state.move_source.empty() ? paths.staging : path_from_utf8(state.move_source);
    std::error_code error;
    if (!fs::is_directory(source, error)) {
        // A commit cut short after its last rename only has its state left to finish.
        if (state.phase == ImportPhase::committing && fs::is_directory(paths.game_folder, error)) {
            set_backed_up(hooks, paths.game_folder, backed_up);
            result.ok = finish_state(paths, state, result.error);
            return result;
        }
        result.error = std::string(copy_missing);
        return result;
    }
    const auto phase = state.phase;
    state.phase = ImportPhase::committing;
    state.mode = ImportMode::replace;
    if (!write_import_state(paths.state_file, state, &result.error))
        return result;
    const auto put_back = [&] {
        state.phase = phase == ImportPhase::committing ? ImportPhase::checked : phase;
        std::string ignored;
        static_cast<void>(write_import_state(paths.state_file, state, &ignored));
    };
    if (present(paths.game_folder)) {
        const auto old = free_old_folder(paths);
        fs::rename(paths.game_folder, old, error);
        if (error) {
            result.error = error.message();
            put_back();
            return result;
        }
        result.old_folder = old;
    }
    fs::create_directories(paths.documents, error);
    error.clear();
    fs::rename(source, paths.game_folder, error);
    if (error) {
        result.error = error.message();
        if (!result.old_folder.empty()) {
            std::error_code ignored;
            fs::rename(result.old_folder, paths.game_folder, ignored);
            result.old_folder.clear();
        }
        put_back();
        return result;
    }
    if (!state.move_source.empty()) {
        // Nothing was staged for a folder moved into place; its empty staging folder goes.
        std::error_code ignored;
        if (fs::is_empty(paths.staging, ignored))
            fs::remove(paths.staging, ignored);
    }
    set_backed_up(hooks, paths.game_folder, backed_up);
    result.ok = finish_state(paths, state, result.error);
    return result;
}

/// Commits an addition: each staged file moved into the game folder, never over a file of the
/// same name (matched without case), and never one waiting to be removed.
///
/// @param hooks the platform's hooks
/// @param paths the import's folders
/// @param state the import's state
/// @param mode ImportMode::add or ImportMode::mod
/// @param backed_up the player's backup setting
/// @return what the commit did
CommitResult commit_addition(
    const GameFilesHooks& hooks,
    const ImportPaths& paths,
    ImportState state,
    ImportMode mode,
    bool backed_up
) {
    CommitResult result;
    state.phase = ImportPhase::committing;
    state.mode = mode;
    if (!write_import_state(paths.state_file, state, &result.error))
        return result;
    std::vector<std::string> staged;
    std::error_code error;
    if (fs::is_directory(paths.staging, error))
        for (fs::recursive_directory_iterator entry{paths.staging, error}, end;
             !error && entry != end;
             entry.increment(error)) {
            std::error_code status;
            if (!entry->is_regular_file(status) || entry->is_symlink(status))
                continue;
            auto relative = path_to_utf8(entry->path().lexically_relative(paths.staging));
            std::replace(relative.begin(), relative.end(), '\\', '/');
            if (relative.size() > part_suffix.size() &&
                relative.compare(
                    relative.size() - part_suffix.size(), part_suffix.size(), part_suffix
                ) == 0)
                continue;
            staged.push_back(std::move(relative));
        }
    if (error) {
        result.error = error.message();
        return result;
    }
    std::sort(staged.begin(), staged.end());
    const auto waits_removal = [&](const std::string& relative) {
        const auto lower = folded(relative);
        return std::any_of(
            state.removals.begin(), state.removals.end(), [&](const std::string& removal) {
                const auto gone = folded(removal);
                return removal == remove_everything || gone == lower ||
                       (lower.size() > gone.size() && lower.compare(0, gone.size(), gone) == 0 &&
                        lower[gone.size()] == '/');
            }
        );
    };
    fs::create_directories(paths.game_folder, error);
    for (const auto& relative : staged) {
        if (waits_removal(relative))
            continue;
        bool found = false;
        const auto target = find_without_case(paths.game_folder, relative, found);
        // A file already there stays, and one whose place lies through a
        // link is left out, so that nothing is written outside the folder.
        if (found || passes_through_link(paths.game_folder, target))
            continue;
        fs::create_directories(target.parent_path(), error);
        error.clear();
        fs::rename(paths.staging / path_from_utf8(relative), target, error);
        if (error) {
            // The state stays at committing: the next start moves the files left.
            result.error = relative + ": " + error.message();
            return result;
        }
    }
    fs::remove_all(paths.staging, error);
    set_backed_up(hooks, paths.game_folder, backed_up);
    result.ok = finish_state(paths, state, result.error);
    return result;
}

/// Commits a state by its mode.
///
/// @param hooks the platform's hooks
/// @param paths the import's folders
/// @param state the import's state
/// @param mode what the import does
/// @param backed_up the player's backup setting
/// @return what the commit did
CommitResult commit_state(
    const GameFilesHooks& hooks,
    const ImportPaths& paths,
    ImportState state,
    ImportMode mode,
    bool backed_up
) {
    switch (mode) {
    case ImportMode::replace:
        return commit_replacement(hooks, paths, std::move(state), backed_up);
    case ImportMode::add:
    case ImportMode::mod:
        return commit_addition(hooks, paths, std::move(state), mode, backed_up);
    case ImportMode::remove:
        break;
    }
    CommitResult result;
    result.ok = apply_removals(paths, state.removals, result.error);
    if (result.ok) {
        std::error_code error;
        fs::remove(paths.state_file, error);
    }
    return result;
}

/// Tells whether a folder's top says it is a game folder: an archive name, oamod.yaml or a
/// file of the demo installer's size.
///
/// @param folder the folder
/// @return true when it does
bool folder_marks_game(const fs::path& folder) {
    std::error_code error;
    for (fs::directory_iterator entry{folder, error}, end; !error && entry != end;
         entry.increment(error)) {
        std::error_code status;
        if (entry->is_symlink(status) || !entry->is_regular_file(status))
            continue;
        const auto name = path_to_utf8(entry->path().filename());
        if (is_archive_name(name) || same_folded(name, mod_profile_name))
            return true;
        const auto size = entry->file_size(status);
        if (!status && size == demo_1997.installer_size)
            return true;
    }
    return false;
}

} // namespace

std::optional<ImportState> read_import_state(const fs::path& file, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    why.clear();
    if (!present(file))
        return std::nullopt;
    std::ifstream in(file, std::ios::binary);
    if (!in) {
        why = "the import's state cannot be read";
        return std::nullopt;
    }
    ImportState state;
    state.parts.fill(true);
    bool phase_read = false;
    std::string line;
    while (std::getline(in, line)) {
        if (!line.empty() && line.back() == '\r')
            line.pop_back();
        const auto equals = line.find('=');
        if (equals == std::string::npos)
            continue;
        std::string_view key(line.data(), equals);
        while (!key.empty() && key.back() == ' ')
            key.remove_suffix(1);
        while (!key.empty() && key.front() == ' ')
            key.remove_prefix(1);
        std::string_view raw(line);
        raw.remove_prefix(equals + 1);
        if (!raw.empty() && raw.front() == ' ')
            raw.remove_prefix(1);
        const auto value = unescaped(raw);
        if (key == phase_key) {
            const auto phase = index_of(phase_names, value);
            if (!phase) {
                why = "the import's state names a phase it does not know: " + value;
                return std::nullopt;
            }
            state.phase = static_cast<ImportPhase>(*phase);
            phase_read = true;
        } else if (key == mode_key) {
            const auto mode = index_of(mode_names, value);
            if (!mode) {
                why = "the import's state names a mode it does not know: " + value;
                return std::nullopt;
            }
            state.mode = static_cast<ImportMode>(*mode);
        } else if (key == kind_key) {
            if (const auto kind = index_of(kind_names, value))
                state.kind = static_cast<SourceKind>(*kind);
        } else if (key == location_key) {
            state.location = value;
        } else if (key == bytes_key) {
            state.bytes = number_of(value);
        } else if (key == files_key) {
            state.files = static_cast<uint32_t>(number_of(value));
        } else if (key.substr(0, part_key_prefix.size()) == part_key_prefix) {
            if (const auto part = index_of(part_names, key.substr(part_key_prefix.size())))
                state.parts[*part] = value != "0";
        } else if (key == mod_off_key) {
            state.mods_off.push_back(value);
        } else if (key == mod_id_key) {
            state.mod_id = value;
        } else if (key == remove_key) {
            state.removals.push_back(value);
        } else if (key == move_source_key) {
            state.move_source = value;
        }
    }
    if (!phase_read) {
        why = "the import's state names no phase";
        return std::nullopt;
    }
    return state;
}

bool write_import_state(const fs::path& file, const ImportState& state, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    std::string text;
    const auto line = [&](std::string_view key, std::string_view value) {
        text += key;
        text += " = ";
        text += escaped(value);
        text += '\n';
    };
    line(phase_key, phase_names[static_cast<std::size_t>(state.phase)]);
    line(mode_key, mode_names[static_cast<std::size_t>(state.mode)]);
    line(kind_key, kind_names[static_cast<std::size_t>(state.kind)]);
    line(location_key, state.location);
    line(bytes_key, std::to_string(state.bytes));
    line(files_key, std::to_string(state.files));
    for (std::size_t index = 0; index < part_count; ++index)
        line(
            std::string(part_key_prefix) + std::string(part_names[index]),
            state.parts[index] ? "1" : "0"
        );
    for (const auto& folder : state.mods_off)
        line(mod_off_key, folder);
    if (!state.mod_id.empty())
        line(mod_id_key, state.mod_id);
    for (const auto& removal : state.removals)
        line(remove_key, removal);
    if (!state.move_source.empty())
        line(move_source_key, state.move_source);

    std::error_code failure;
    if (file.has_parent_path())
        fs::create_directories(file.parent_path(), failure);
    auto temporary = file;
    temporary += std::string(new_state_suffix);
    if (!write_synced(temporary, text, why)) {
        std::error_code removed;
        fs::remove(temporary, removed);
        return false;
    }
    return replace_file(temporary, file, why);
}

uint64_t staged_bytes(const fs::path& staging) noexcept {
    uint64_t bytes = 0;
    std::error_code error;
    if (!fs::is_directory(staging, error))
        return 0;
    for (fs::recursive_directory_iterator entry{staging, error}, end; !error && entry != end;
         entry.increment(error)) {
        std::error_code status;
        if (entry->is_symlink(status) || !entry->is_regular_file(status))
            continue;
        const auto name = entry->path().filename().native();
        const auto suffix = fs::path(part_suffix).native();
        if (name.size() > suffix.size() &&
            name.compare(name.size() - suffix.size(), suffix.size(), suffix) == 0)
            continue;
        const auto size = entry->file_size(status);
        if (!status)
            bytes += size;
    }
    return bytes;
}

CommitResult commit_import(
    const GameFilesHooks& hooks, const ImportPaths& paths, ImportMode mode, bool backed_up
) {
    std::string error;
    auto state = read_import_state(paths.state_file, &error);
    if (!state) {
        if (!error.empty()) {
            CommitResult result;
            result.error = error;
            return result;
        }
        state = ImportState{};
        state->parts.fill(true);
        state->phase = ImportPhase::checked;
        state->mode = mode;
    }
    return commit_state(hooks, paths, std::move(*state), mode, backed_up);
}

bool schedule_removal(
    const ImportPaths& paths, std::span<const std::string> relative_paths, std::string* error
) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    auto state = read_import_state(paths.state_file, &why);
    if (!state && !why.empty())
        return false;
    if (state && state->phase == ImportPhase::checked && state->mode == ImportMode::replace) {
        why = "the new game files wait for the next start of Open Annihilation";
        return false;
    }
    if (!state) {
        state = ImportState{};
        state->parts.fill(true);
        state->phase = ImportPhase::checked;
        state->mode = ImportMode::remove;
    }
    auto& removals = state->removals;
    for (const auto& path : relative_paths) {
        if (path.empty())
            continue;
        const bool known =
            std::any_of(removals.begin(), removals.end(), [&](const std::string& waiting) {
                return same_folded(waiting, path);
            });
        if (!known)
            removals.push_back(path);
    }
    if (std::find(removals.begin(), removals.end(), remove_everything) != removals.end())
        removals = {std::string(remove_everything)};
    return write_import_state(paths.state_file, *state, &why);
}

bool schedule_replacement(const ImportPaths& paths, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    auto state = read_import_state(paths.state_file, &why);
    if (!state) {
        if (why.empty())
            why = "no checked copy waits";
        return false;
    }
    std::error_code failure;
    const bool copy_there = state->move_source.empty()
                                ? fs::is_directory(paths.staging, failure)
                                : fs::is_directory(path_from_utf8(state->move_source), failure);
    if (state->mode != ImportMode::replace || !copy_there) {
        why = "no checked copy waits";
        return false;
    }
    state->phase = ImportPhase::checked;
    return write_import_state(paths.state_file, *state, &why);
}

bool cancel_scheduled(const ImportPaths& paths, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    auto state = read_import_state(paths.state_file, &why);
    if (!state)
        return why.empty();
    std::error_code failure;
    if (state->mode == ImportMode::remove) {
        fs::remove(paths.state_file, failure);
        if (failure)
            why = failure.message();
        return !failure;
    }
    if (state->phase == ImportPhase::checked && state->mode == ImportMode::replace) {
        // A folder moved in place stays where the player put it; only the staging copy goes.
        fs::remove_all(paths.staging, failure);
        if (failure) {
            why = failure.message();
            return false;
        }
        return finish_state(paths, *state, why);
    }
    if (!state->removals.empty()) {
        state->removals.clear();
        return write_import_state(paths.state_file, *state, &why);
    }
    return true;
}

RecoveryResult
recover_import(const GameFilesHooks& hooks, const ImportPaths& paths, bool backed_up) {
    RecoveryResult result;
    auto state = read_import_state(paths.state_file, &result.error);
    if (!state) {
        result.outcome = result.error.empty() ? Recovery::nothing : Recovery::unreadable;
        return result;
    }
    // Removals the player confirmed apply at this start, before anything else.
    if (!state->removals.empty() || state->mode == ImportMode::remove) {
        std::string error;
        const bool removed = apply_removals(paths, state->removals, error);
        if (state->mode == ImportMode::remove) {
            result.outcome = Recovery::applied;
            result.commit.ok = removed;
            result.commit.error = error;
            std::error_code ignored;
            fs::remove(paths.state_file, ignored);
            result.state = std::move(state);
            return result;
        }
        state->removals.clear();
        static_cast<void>(write_import_state(paths.state_file, *state, &error));
    }
    result.state = state;
    switch (state->phase) {
    case ImportPhase::committing:
        result.outcome = Recovery::finished_commit;
        result.commit = commit_state(hooks, paths, *state, state->mode, backed_up);
        break;
    case ImportPhase::checked:
        result.outcome = Recovery::applied;
        result.commit = commit_state(hooks, paths, *state, state->mode, backed_up);
        break;
    case ImportPhase::copying:
        result.outcome = Recovery::copy_waiting;
        result.staged_bytes = staged_bytes(paths.staging);
        break;
    }
    // A commit that cannot find its copy any more would be retried at every start; its state
    // goes, and resolution finds what is there.
    if (result.outcome != Recovery::copy_waiting && !result.commit.ok &&
        result.commit.error == copy_missing) {
        std::error_code ignored;
        fs::remove(paths.state_file, ignored);
    }
    return result;
}

bool discard_import(const ImportPaths& paths, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    why.clear();
    std::error_code failure;
    fs::remove_all(paths.staging, failure);
    if (failure) {
        why = failure.message();
        return false;
    }
    std::string state_error;
    const auto state = read_import_state(paths.state_file, &state_error);
    // Removals waiting for the next start are a change of their own, and stay.
    if (state && state->mode == ImportMode::remove)
        return true;
    if (state && !state->removals.empty())
        return finish_state(paths, *state, why);
    fs::remove(paths.state_file, failure);
    if (failure) {
        why = failure.message();
        return false;
    }
    return true;
}

bool remove_folder_now(const fs::path& folder, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    why.clear();
    if (folder.empty() || folder == folder.root_path() || !folder.has_filename()) {
        why = "no folder to remove";
        return false;
    }
    std::error_code failure;
    fs::remove_all(folder, failure);
    if (failure)
        why = failure.message();
    return !failure;
}

void apply_backup_setting(const GameFilesHooks& hooks, const ImportPaths& paths, bool backed_up) {
    if (hooks.set_backed_up == nullptr)
        return;
    set_backed_up(hooks, paths.game_folder, backed_up);
    set_backed_up(hooks, paths.staging, backed_up);
    if (!paths.data_folder.empty())
        set_backed_up(hooks, paths.data_folder / fs::path(demo_1997.folder_name), backed_up);
}

CopiedFiles find_copied_files(const ImportPaths& paths) {
    CopiedFiles found;
    std::error_code error;
    if (fs::is_directory(paths.game_folder, error)) {
        found.find = CopiedFind::game_folder;
        return found;
    }
    std::vector<std::pair<bool, std::string>> folders;
    std::vector<std::string> archives;
    for (fs::directory_iterator entry{paths.documents, error}, end; !error && entry != end;
         entry.increment(error)) {
        std::error_code status;
        if (entry->is_symlink(status))
            continue;
        const auto name = path_to_utf8(entry->path().filename());
        if (name.empty() || name.front() == '.')
            continue;
        if (entry->is_directory(status)) {
            if (folder_marks_game(entry->path()))
                folders.emplace_back(
                    name.compare(0, old_folder_name(1).size(), old_folder_name(1)) == 0, name
                );
        } else if (entry->is_regular_file(status) && is_archive_name(name)) {
            archives.push_back(name);
        }
    }
    // A folder the player copied comes before one the game set aside.
    std::sort(folders.begin(), folders.end());
    if (!folders.empty()) {
        found.find = CopiedFind::misnamed_folder;
        found.folder = folders.front().second;
        return found;
    }
    if (!archives.empty()) {
        std::sort(archives.begin(), archives.end());
        found.find = CopiedFind::loose_archives;
        found.archives = std::move(archives);
    }
    return found;
}

bool adopt_copied_files(const ImportPaths& paths, const CopiedFiles& found, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    why.clear();
    std::error_code failure;
    switch (found.find) {
    case CopiedFind::nothing:
    case CopiedFind::game_folder:
        return fs::is_directory(paths.game_folder, failure);
    case CopiedFind::misnamed_folder: {
        if (found.folder.empty() || found.folder.find('/') != std::string::npos ||
            present(paths.game_folder)) {
            why = "the folder cannot be put in place";
            return false;
        }
        fs::rename(paths.documents / path_from_utf8(found.folder), paths.game_folder, failure);
        if (failure)
            why = failure.message();
        return !failure;
    }
    case CopiedFind::loose_archives:
        break;
    }
    fs::create_directories(paths.game_folder, failure);
    if (failure) {
        why = failure.message();
        return false;
    }
    for (const auto& name : found.archives) {
        // Only archives, matched as the name check matches them, and only names of one part.
        if (!is_archive_name(name) || name.find('/') != std::string::npos ||
            !oa::formats::zip::name_is_safe(name))
            continue;
        const auto target = paths.game_folder / path_from_utf8(name);
        if (present(target))
            continue;
        fs::rename(paths.documents / path_from_utf8(name), target, failure);
        if (failure) {
            why = name + ": " + failure.message();
            return false;
        }
    }
    return true;
}

} // namespace oa::app::game_files
