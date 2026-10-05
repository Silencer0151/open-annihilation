// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A change put in place by renames on one volume, the recovery that settles
// one a stop left half done, and the deletion of the folders a change drops.

#include "files.hpp"

#include "oa/app/mod_install.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/base/threads.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace oa::app::mod_install {

namespace fs = std::filesystem;

namespace {

/// The tries a rename makes without waiting, when the options wait none.
constexpr int unwaited_tries = 8;
/// The most each later wait grows to, as a multiple of the first.
constexpr uint32_t longest_wait_factor = 10;
/// The most entries a Discarder step removes.
constexpr int entries_a_step = 64;
/// The most time a Discarder step works.
constexpr std::chrono::milliseconds time_a_step{4};

/// Tells whether anything, a link included, is at a path.
///
/// @param path the path
/// @return true when something is there
bool present(const fs::path& path) {
    std::error_code error;
    return fs::exists(fs::symlink_status(path, error));
}

/// Renames a file or folder, never over another, trying again for a while
/// when another program holds its files.
///
/// @param options the hooks and the waits
/// @param from the path
/// @param to the new path
/// @return the error; empty when it was renamed
std::error_code move(const ChangeOptions& options, const fs::path& from, const fs::path& to) {
    const ChangeHooks& hooks = options.hooks;
    uint32_t waited = 0;
    uint32_t delay = options.first_retry_ms;
    for (int tries = 1;; ++tries) {
        std::error_code error;
        if (hooks.rename != nullptr)
            hooks.rename(hooks.context, from, to, error);
        else
            rename_exclusively(from, to, error);
        if (!error)
            return {};
        if (!detail::retryable(error))
            return error;
        if (options.first_retry_ms == 0) {
            if (tries >= unwaited_tries)
                return error;
            continue;
        }
        if (waited >= options.most_wait_ms)
            return error;
        const uint32_t wait = std::min(delay, options.most_wait_ms - waited);
        if (hooks.wait != nullptr)
            hooks.wait(hooks.context, wait);
        else
            base::threads::sleep_ms(wait);
        waited += wait;
        delay = std::min(delay * 2, options.first_retry_ms * longest_wait_factor);
    }
}

/// Returns a folder's name for a role of a target's change.
///
/// @param mods the Mods folder
/// @param prefix the role's prefix
/// @param target the target
/// @return the folder
fs::path role_folder(const fs::path& mods, std::string_view prefix, std::string_view target) {
    return mods / detail::path_of(std::string(prefix) + std::string(target));
}

/// Returns the first discard folder name free for a target.
///
/// @param mods the Mods folder
/// @param target the target
/// @return the folder, which does not exist
fs::path free_discard(const fs::path& mods, std::string_view target) {
    for (uint32_t number = 1;; ++number) {
        const fs::path folder = mods / detail::path_of(
                                           std::string(discard_prefix) + std::string(target) + "-" +
                                           std::to_string(number)
                                       );
        if (!present(folder))
            return folder;
    }
}

/// Moves a folder aside to be deleted.
///
/// @param options the hooks and the waits
/// @param mods the Mods folder
/// @param target the target it belonged to
/// @param folder the folder
/// @param[in,out] discards receives the discard folder
/// @param[in,out] lines receives what failed
void discard(
    const ChangeOptions& options,
    const fs::path& mods,
    std::string_view target,
    const fs::path& folder,
    std::vector<fs::path>& discards,
    std::vector<std::string>& lines
) {
    if (!present(folder))
        return;
    const fs::path to = free_discard(mods, target);
    if (const std::error_code error = move(options, folder, to); error)
        lines.push_back(
            "cannot move " + detail::utf8_of(folder) + " aside to be deleted: " + error.message()
        );
    else
        discards.push_back(to);
}

/// Renames a folder no step could settle to a visible name after its
/// target, <target>-left-over or -left-over-2 and so on, so that it never
/// blocks a later change and shows on the Mods page.
///
/// @param options the hooks and the waits
/// @param mods the Mods folder
/// @param target the target
/// @param folder the folder
/// @param[in,out] lines receives what was done
/// @return where the folder is now
fs::path keep_visible(
    const ChangeOptions& options,
    const fs::path& mods,
    std::string_view target,
    const fs::path& folder,
    std::vector<std::string>& lines
) {
    for (uint32_t number = 1; number <= alongside_tries; ++number) {
        std::string name = std::string(target) + std::string(left_over_suffix);
        if (number > 1)
            name += "-" + std::to_string(number);
        const fs::path to = mods / detail::path_of(name);
        if (present(to))
            continue;
        if (const std::error_code error = move(options, folder, to); error) {
            lines.push_back(
                "cannot rename " + detail::utf8_of(folder) + " to " + detail::utf8_of(to) + ": " +
                error.message()
            );
            return folder;
        }
        lines.push_back("kept " + detail::utf8_of(folder) + " as " + detail::utf8_of(to));
        return to;
    }
    return folder;
}

/// Returns the .backup a folder holds, matched without case.
///
/// @param folder the folder
/// @return its path; nothing when there is none
std::optional<fs::path> backup_in(const fs::path& folder) {
    return entry_without_case(folder, backup_folder_name);
}

/// Returns a failed change's result.
///
/// @param refusal why
/// @param detail English, for the log
/// @return the result
ChangeResult failed(Refusal refusal, std::string detail) {
    ChangeResult result{};
    result.refusal = refusal;
    result.detail = std::move(detail);
    return result;
}

} // namespace

ChangeResult commit_change(
    const fs::path& mods, std::string_view target, Change change, const ChangeOptions& options
) {
    try {
        const ModsHold hold = hold_mods_folder(mods);
        if (!hold)
            return failed(Refusal::busy, "another copy of the game holds the Mods folder");
        const fs::path folder = mods / detail::path_of(target);
        const fs::path staged = role_folder(mods, staging_prefix, target);
        const fs::path old = role_folder(mods, old_prefix, target);
        const fs::path replaced = role_folder(mods, replaced_prefix, target);
        const fs::path restore = role_folder(mods, restore_prefix, target);
        const fs::path kept = folder / std::string(backup_folder_name);
        std::vector<std::string> lines;
        ChangeResult result{};
        const auto fail = [&](Refusal refusal, std::string detail) {
            if (change != Change::roll_back)
                discard(options, mods, target, staged, result.discards, lines);
            result.refusal = refusal;
            result.detail = std::move(detail);
            for (const auto& line : lines)
                result.detail += "; " + line;
            detail::log_line(detail::utf8_of(folder) + ": " + result.detail);
            return result;
        };
        // The folder must hold what the plan found, and no earlier change's
        // folder may stand in the way.
        if (options.expected && !same_mod(read_installed_mod(folder), *options.expected))
            return fail(Refusal::changed, "the folder changed since the install was planned");
        for (const fs::path& role : {old, replaced, restore})
            if (present(role))
                return fail(
                    Refusal::not_placed,
                    "an earlier change's folder is in the way: " + detail::utf8_of(role)
                );
        // What the version leaving the target becomes when a step after the
        // new files are in place fails: a visible folder the player is told of.
        const auto left_over = [&](const fs::path& leaving) {
            result.left_over = keep_visible(options, mods, target, leaving, lines);
        };
        std::error_code error;
        switch (change) {
        case Change::install:
            if ((error = move(options, staged, folder)))
                return fail(
                    Refusal::not_placed, "cannot put the files in place: " + error.message()
                );
            result.changed = true;
            break;
        case Change::replace:
            if ((error = move(options, folder, old)))
                return fail(
                    Refusal::not_placed,
                    "cannot move the version installed aside: " + error.message()
                );
            if ((error = move(options, staged, folder))) {
                if (const std::error_code undo = move(options, old, folder); undo)
                    result.left_over = old;
                return fail(
                    Refusal::not_placed, "cannot put the new files in place: " + error.message()
                );
            }
            result.changed = true;
            // One version back: the old version's own kept version goes.
            if (const auto older = backup_in(old)) {
                const fs::path to = free_discard(mods, target);
                if ((error = move(options, *older, to))) {
                    lines.push_back("cannot drop the earlier kept version: " + error.message());
                    left_over(old);
                    break;
                }
                result.discards.push_back(to);
            }
            if ((error = move(options, old, kept))) {
                lines.push_back("cannot keep the replaced version: " + error.message());
                left_over(old);
                break;
            }
            result.backup_kept = true;
            break;
        case Change::reinstall:
            if ((error = move(options, folder, replaced)))
                return fail(
                    Refusal::not_placed,
                    "cannot move the version installed aside: " + error.message()
                );
            if ((error = move(options, staged, folder))) {
                if (const std::error_code undo = move(options, replaced, folder); undo)
                    result.left_over = replaced;
                return fail(
                    Refusal::not_placed, "cannot put the files in place: " + error.message()
                );
            }
            result.changed = true;
            // The kept version stays: it moves over to the new files.
            if (const auto older = backup_in(replaced); older && !present(kept)) {
                if ((error = move(options, *older, kept))) {
                    lines.push_back("cannot keep the earlier version: " + error.message());
                    left_over(replaced);
                    break;
                }
            }
            result.backup_kept = present(kept);
            discard(options, mods, target, replaced, result.discards, lines);
            break;
        case Change::roll_back: {
            const auto backup = backup_in(folder);
            if (!backup)
                return fail(Refusal::changed, "the folder keeps no earlier version");
            // A link or junction is never put in the target's place.
            if (is_link_or_junction(*backup))
                return fail(Refusal::changed, "the kept version is a link");
            if ((error = move(options, *backup, restore)))
                return fail(
                    Refusal::not_placed, "cannot move the kept version aside: " + error.message()
                );
            if ((error = move(options, folder, old))) {
                if (const std::error_code undo = move(options, restore, *backup); undo)
                    result.left_over = restore;
                return fail(
                    Refusal::not_placed,
                    "cannot move the version installed aside: " + error.message()
                );
            }
            if ((error = move(options, restore, folder))) {
                const std::error_code back = move(options, old, folder);
                const std::error_code again = back ? back : move(options, restore, kept);
                if (back)
                    result.left_over = old;
                else if (again)
                    result.left_over = restore;
                return fail(
                    Refusal::not_placed, "cannot bring the kept version back: " + error.message()
                );
            }
            result.changed = true;
            if ((error = move(options, old, kept))) {
                lines.push_back("cannot keep the version rolled back from: " + error.message());
                left_over(old);
                break;
            }
            result.backup_kept = true;
            break;
        }
        }
        for (const auto& line : lines) {
            result.detail += result.detail.empty() ? line : "; " + line;
            detail::log_line(detail::utf8_of(folder) + ": " + line);
        }
        // The renames reach storage before the player is told of them.
        detail::sync_folder(folder);
        detail::flush_to_storage(mods);
        return result;
    } catch (const std::exception& failure) {
        return failed(Refusal::not_placed, failure.what());
    }
}

Recovery recover_changes(const fs::path& mods, const ChangeOptions& options) {
    Recovery recovery{};
    try {
        std::error_code error;
        if (!fs::is_directory(mods, error))
            return recovery;
        const ModsHold hold = hold_mods_folder(mods);
        if (!hold) {
            recovery.skipped = true;
            recovery.lines.push_back(
                "another copy of the game holds " + detail::utf8_of(mods) + "; nothing is settled"
            );
            return recovery;
        }

        // The folders each target's changes left, by their roles.
        struct Left {
            bool staged{};
            bool old{};
            bool replaced{};
            bool restore{};
        };

        std::map<std::string, Left> targets;
        std::vector<fs::path> discards;
        for (fs::directory_iterator entry{mods, error}, end; !error && entry != end;
             entry.increment(error)) {
            const std::string name = detail::utf8_of(entry->path().filename());
            if (!name.starts_with(reserved_prefix))
                continue;
            // A discard folder is deleted whatever it is: a link or
            // junction is removed, never entered (Discarder).
            if (name.starts_with(discard_prefix)) {
                discards.push_back(entry->path());
                continue;
            }
            std::error_code status;
            if (is_link_or_junction(entry->path()) || !entry->is_directory(status))
                continue;
            const auto rest = [&](std::string_view prefix) {
                return std::string(std::string_view(name).substr(prefix.size()));
            };
            if (name.starts_with(staging_prefix))
                targets[rest(staging_prefix)].staged = true;
            else if (name.starts_with(old_prefix))
                targets[rest(old_prefix)].old = true;
            else if (name.starts_with(replaced_prefix))
                targets[rest(replaced_prefix)].replaced = true;
            else if (name.starts_with(restore_prefix))
                targets[rest(restore_prefix)].restore = true;
        }
        const auto note = [&](const std::string& line) { recovery.lines.push_back(line); };
        for (const auto& [target, left] : targets) {
            if (target.empty())
                continue;
            const fs::path folder = mods / detail::path_of(target);
            const fs::path kept = folder / std::string(backup_folder_name);
            const auto step = [&](const fs::path& from, const fs::path& to) {
                if (const std::error_code failed_move = move(options, from, to); failed_move) {
                    note(
                        "cannot move " + detail::utf8_of(from) + " to " + detail::utf8_of(to) +
                        ": " + failed_move.message()
                    );
                    return false;
                }
                note("moved " + detail::utf8_of(from) + " to " + detail::utf8_of(to));
                return true;
            };
            const auto to_discard = [&](const fs::path& from) {
                const fs::path to = free_discard(mods, target);
                if (step(from, to))
                    discards.push_back(to);
            };
            if (left.replaced) {
                // A reinstall: before its new files were in place, the
                // version replaced goes back; after, it goes, its kept
                // version moved over first.
                const fs::path replaced = role_folder(mods, replaced_prefix, target);
                if (!present(folder)) {
                    step(replaced, folder);
                } else {
                    const auto older = backup_in(replaced);
                    if (older && !backup_in(folder) && !step(*older, kept))
                        keep_visible(options, mods, target, replaced, recovery.lines);
                    else
                        to_discard(replaced);
                }
            }
            if (left.old) {
                // A replace or a roll back: before the new files were in
                // place, the version installed goes back; after, it becomes
                // the kept version, its own kept version dropped.
                const fs::path old = role_folder(mods, old_prefix, target);
                if (!present(folder)) {
                    step(old, folder);
                } else {
                    if (const auto older = backup_in(old))
                        to_discard(*older);
                    if (!backup_in(folder)) {
                        if (!step(old, kept))
                            keep_visible(options, mods, target, old, recovery.lines);
                    } else {
                        keep_visible(options, mods, target, old, recovery.lines);
                    }
                }
            }
            if (left.restore) {
                // A roll back: the kept version goes back to being kept.
                const fs::path restore = role_folder(mods, restore_prefix, target);
                if (present(folder) && !backup_in(folder)) {
                    if (!step(restore, kept))
                        keep_visible(options, mods, target, restore, recovery.lines);
                } else if (!present(folder)) {
                    step(restore, folder);
                } else {
                    keep_visible(options, mods, target, restore, recovery.lines);
                }
            }
            if (left.staged)
                to_discard(role_folder(mods, staging_prefix, target));
        }
        recovery.discards = std::move(discards);
        for (const auto& line : recovery.lines)
            detail::log_line(line);
    } catch (const std::exception& failure) {
        recovery.lines.push_back(std::string("recovery stopped: ") + failure.what());
        detail::log_line(recovery.lines.back());
    }
    return recovery;
}

void Discarder::add(std::span<const fs::path> folders) {
    for (const fs::path& folder : folders)
        if (std::find(folders_.begin(), folders_.end(), folder) == folders_.end())
            folders_.push_back(folder);
}

bool Discarder::busy() const noexcept {
    return !folders_.empty();
}

bool Discarder::step() {
    if (folders_.empty())
        return false;
    // Under the lock of the Mods folder the discard folders lie in.
    const ModsHold hold = hold_mods_folder(folders_.front().parent_path());
    if (!hold)
        return true;
    const auto started = std::chrono::steady_clock::now();
    int removed = 0;
    const auto skipped = [this](const fs::path& path) {
        return std::find(skipped_.begin(), skipped_.end(), path) != skipped_.end();
    };
    const auto skip = [this](const fs::path& path, const std::error_code& error) {
        skipped_.push_back(path);
        detail::log_line("cannot delete " + detail::utf8_of(path) + ": " + error.message());
    };
    try {
        while (!folders_.empty() && removed < entries_a_step &&
               std::chrono::steady_clock::now() - started < time_a_step) {
            if (stack_.empty())
                stack_.push_back(folders_.front());
            const fs::path folder = stack_.back();
            // A folder is entered only while it is one, not a link or
            // junction: a discard folder that is a link, or a folder made a
            // link since it was found, is removed as it is.
            std::error_code status;
            const bool entered = !is_link_or_junction(folder) &&
                                 fs::is_directory(fs::symlink_status(folder, status));
            // The first entry of the folder not yet given up on.
            std::optional<fs::path> child;
            std::error_code error;
            if (entered)
                for (fs::directory_iterator entry{folder, error}, end; !error && entry != end;
                     entry.increment(error))
                    if (!skipped(entry->path())) {
                        child = entry->path();
                        break;
                    }
            if (!child) {
                std::error_code removal;
                if (!detail::remove_entry(folder, removal) && present(folder))
                    skip(folder, removal);
                ++removed;
                stack_.pop_back();
                if (stack_.empty())
                    folders_.erase(folders_.begin());
                continue;
            }
            // A link or junction is removed itself, never entered.
            if (!is_link_or_junction(*child) &&
                fs::is_directory(fs::symlink_status(*child, status))) {
                stack_.push_back(*child);
                continue;
            }
            std::error_code removal;
            if (!detail::remove_entry(*child, removal))
                skip(*child, removal);
            ++removed;
        }
    } catch (const std::exception& failure) {
        detail::log_line(std::string("deleting stopped: ") + failure.what());
        folders_.clear();
        stack_.clear();
    }
    return !folders_.empty();
}

} // namespace oa::app::mod_install
