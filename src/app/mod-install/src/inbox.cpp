// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The packages waiting for the main menu, the change waiting for its run to
// end and its outcome, kept for the whole process.

#include "files.hpp"

#include "oa/app/mod_install/inbox.hpp"
#include "oa/base/threads.hpp"

#include <algorithm>
#include <deque>
#include <exception>
#include <system_error>
#include <utility>

namespace oa::app::mod_install {

namespace fs = std::filesystem;

namespace {

/// What the process keeps between runs.
struct Kept {
    base::threads::Mutex lock{};
    std::deque<fs::path> files{};      ///< waiting, oldest first
    std::optional<fs::path> current{}; ///< the one being installed
    std::optional<PendingChange> pending{};
    std::optional<ChangeOutcome> outcome{};
    std::vector<fs::path> discards{};
    std::optional<fs::path> handoff{}; ///< the hand-off folder, while the lock is held
};

/// Returns the process's kept state.
///
/// @return the state
Kept& kept() {
    static Kept state;
    return state;
}

/// Returns the form a path is compared in: weakly canonical, else absolute
/// and normal.
///
/// @param file the path
/// @return its form
fs::path compared(const fs::path& file) {
    std::error_code error;
    fs::path form = fs::weakly_canonical(file, error);
    if (!error)
        return form;
    form = fs::absolute(file, error);
    return (error ? file : form).lexically_normal();
}

} // namespace

void post_mod_file(const fs::path& file) {
    const fs::path form = compared(file);
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    if ((state.current && *state.current == form) ||
        std::find(state.files.begin(), state.files.end(), form) != state.files.end())
        return;
    state.files.push_back(form);
}

std::optional<fs::path> take_mod_file() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    if (state.files.empty())
        return std::nullopt;
    fs::path file = std::move(state.files.front());
    state.files.pop_front();
    state.current = file;
    return file;
}

void return_mod_file(const fs::path& file) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    const fs::path form = compared(file);
    state.current.reset();
    if (std::find(state.files.begin(), state.files.end(), form) == state.files.end())
        state.files.push_front(form);
}

void finish_mod_file() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.current.reset();
}

bool mod_files_waiting() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return !state.files.empty();
}

void set_pending_change(PendingChange change) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.pending = std::move(change);
}

bool pending_change_waiting() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return state.pending.has_value();
}

void finish_pending_change(const ChangeOptions& options) {
    std::optional<PendingChange> pending;
    {
        Kept& state = kept();
        const base::threads::LockGuard guard(state.lock);
        pending = std::move(state.pending);
        state.pending.reset();
    }
    if (!pending)
        return;
    ChangeOptions checked = options;
    checked.expected = pending->replaced;
    ChangeOutcome outcome{};
    outcome.result = commit_change(pending->mods, pending->target, pending->change, checked);
    detail::log_line(
        pending->target + (outcome.result.changed ? ": changed before the run" : ": not changed")
    );
    // The hold goes once the change is in place; then the folder is settled.
    pending->hold.reset();
    const Recovery recovery = recover_changes(pending->mods, options);
    std::vector<fs::path> discards = outcome.result.discards;
    discards.insert(discards.end(), recovery.discards.begin(), recovery.discards.end());
    outcome.change = std::move(*pending);
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.outcome = std::move(outcome);
    for (fs::path& folder : discards)
        if (std::find(state.discards.begin(), state.discards.end(), folder) == state.discards.end())
            state.discards.push_back(std::move(folder));
}

std::optional<ChangeOutcome> take_change_outcome() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    std::optional<ChangeOutcome> outcome = std::move(state.outcome);
    state.outcome.reset();
    return outcome;
}

void keep_discards(const std::vector<fs::path>& folders) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    for (const fs::path& folder : folders)
        if (std::find(state.discards.begin(), state.discards.end(), folder) == state.discards.end())
            state.discards.push_back(folder);
}

std::vector<fs::path> take_discards() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return std::exchange(state.discards, {});
}

void set_handoff_folder(const fs::path& folder) {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    state.handoff = folder;
}

std::optional<fs::path> handoff_folder() {
    Kept& state = kept();
    const base::threads::LockGuard guard(state.lock);
    return state.handoff;
}

} // namespace oa::app::mod_install
