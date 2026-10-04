// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Listing a source and the SourceScan worker (game_files_import.hpp): the
// listing through the platform's hooks or a walk of the folder, the name
// check, the plan and the engine's check on a local source.
//
// A folder is first read by name only (its top, then two levels below), so
// that choosing a whole drive is answered after a few directory reads
// instead of a long listing. Only a folder that looks like a game folder is
// listed fully. Nothing is read from a file held in the cloud before the
// player taps COPY: the engine's check on the source runs only when every
// planned file is on the device.
#include "oa/app/game_files_import.hpp"

#include <algorithm>
#include <limits>
#include <system_error>
#include <utility>

namespace oa::app::game_files {
namespace {

namespace threads = oa::base::threads;

/// The separator a display location puts between folders (U+203A, UTF-8).
constexpr std::string_view location_separator = " \xe2\x80\xba ";
/// Listing depth that has no limit.
constexpr uint32_t whole_tree = std::numeric_limits<uint32_t>::max();

/// What a listing through the platform's hooks gathers.
struct HookListing {
    std::vector<SourceEntry>* entries{};                                ///< where entries go
    const std::atomic<bool>* stop{};                                    ///< ends it early
    void (*progress)(void* userdata, uint32_t files, uint64_t bytes){}; ///< told the count
    void* userdata{};                                                   ///< for progress
    uint32_t files{};                                                   ///< files so far
    uint64_t bytes{};                                                   ///< their known sizes
};

/// Takes one entry the platform listed.
///
/// @param userdata the HookListing
/// @param entry the entry
void take_listed_entry(void* userdata, const SourceEntry& entry) {
    auto& listing = *static_cast<HookListing*>(userdata);
    if (listing.stop != nullptr && listing.stop->load())
        return;
    listing.entries->push_back(entry);
    if (entry.folder || entry.link)
        return;
    ++listing.files;
    if (entry.size_known)
        listing.bytes += entry.size;
    if (listing.progress != nullptr)
        listing.progress(listing.userdata, listing.files, listing.bytes);
}

/// Walks a folder with std::filesystem: symbolic links are reported and never followed.
///
/// @param source the folder
/// @param max_depth levels below `source` listed; 0 lists its own entries only
/// @param[out] entries the entries found, appended
/// @param[out] error why it could not be listed
/// @param stop ends it early; may be null
/// @param progress told the count and bytes so far; may be null
/// @param userdata passed to `progress`
/// @return true when the whole listing was read
bool walk_folder(
    const fs::path& source,
    uint32_t max_depth,
    std::vector<SourceEntry>& entries,
    std::string& error,
    const std::atomic<bool>* stop,
    void (*progress)(void* userdata, uint32_t files, uint64_t bytes),
    void* userdata
) {
    struct Pending {
        fs::path folder{};      ///< the folder to read
        std::string relative{}; ///< its path below the source
        uint32_t depth{};       ///< 0 for the source itself
    };

    std::error_code failure;
    if (!fs::is_directory(source, failure)) {
        error = failure ? failure.message() : "it is not a folder";
        return false;
    }
    uint32_t files = 0;
    uint64_t bytes = 0;
    std::vector<Pending> pending{{source, {}, 0}};
    while (!pending.empty()) {
        const Pending at = std::move(pending.back());
        pending.pop_back();
        fs::directory_iterator entry{at.folder, failure};
        for (const fs::directory_iterator end; !failure && entry != end; entry.increment(failure)) {
            if (stop != nullptr && stop->load()) {
                error.clear();
                return false;
            }
            const auto name = path_to_utf8(entry->path().filename());
            SourceEntry listed;
            listed.path = at.relative.empty() ? name : at.relative + "/" + name;
            std::error_code status_error;
            const auto status = entry->symlink_status(status_error);
            if (status_error) {
                error = listed.path + ": " + status_error.message();
                return false;
            }
            if (fs::is_symlink(status)) {
                listed.link = true;
                entries.push_back(std::move(listed));
                continue;
            }
            if (fs::is_directory(status)) {
                listed.folder = true;
                if (at.depth < max_depth)
                    pending.push_back({entry->path(), listed.path, at.depth + 1});
                entries.push_back(std::move(listed));
                continue;
            }
            // Devices, sockets and pipes are no game data and are not listed.
            if (!fs::is_regular_file(status))
                continue;
            listed.size = entry->file_size(status_error);
            if (!status_error)
                listed.modified = seconds_since_1970(entry->last_write_time(status_error));
            if (status_error) {
                error = listed.path + ": " + status_error.message();
                return false;
            }
            ++files;
            bytes += listed.size;
            entries.push_back(std::move(listed));
            if (progress != nullptr)
                progress(userdata, files, bytes);
        }
        if (failure) {
            error = (at.relative.empty() ? path_to_utf8(at.folder.filename()) : at.relative) +
                    ": " + failure.message();
            return false;
        }
    }
    return true;
}

/// Drops a trailing separator from a folder's path, so that its name is its last component.
///
/// @param folder the folder, as a picker may give it
/// @return the same folder without a trailing separator
[[nodiscard]] fs::path without_trailing_separator(fs::path folder) {
    while (!folder.has_filename() && folder.has_relative_path())
        folder = folder.parent_path();
    return folder;
}

/// Splits a '/'-separated path into its components.
///
/// @param path the path
/// @return its components, empty ones left out
std::vector<std::string_view> components_of(std::string_view path) {
    std::vector<std::string_view> parts;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (!part.empty())
            parts.push_back(part);
        if (slash == std::string_view::npos)
            break;
        path.remove_prefix(slash + 1);
    }
    return parts;
}

/// Joins a display location and a folder below it.
///
/// @param location the display location
/// @param relative a folder below it, '/'-separated
/// @return the location with each folder after the separator
std::string location_below(std::string_view location, std::string_view relative) {
    std::string out(location);
    for (const auto part : components_of(relative)) {
        if (!out.empty())
            out += location_separator;
        out += part;
    }
    return out;
}

/// Returns the longest folder that holds every one of some files.
///
/// @param files absolute files
/// @return their common folder
fs::path common_folder(const std::vector<fs::path>& files) {
    if (files.empty())
        return {};
    fs::path common = files.front().parent_path();
    for (const auto& file : files) {
        const fs::path parent = file.parent_path();
        fs::path shared;
        auto left = common.begin();
        auto right = parent.begin();
        while (left != common.end() && right != parent.end() && *left == *right) {
            shared /= *left;
            ++left;
            ++right;
        }
        common = shared;
    }
    return common;
}

/// Plans the files a picker chose one by one: the demo's installer, or archives whose common
/// folder is the source, each copied to the top of the game folder.
///
/// @param kind SourceKind::archives or SourceKind::demo_installer
/// @param files the chosen files, absolute
/// @param location the display location; the file's or their folder's name when empty
/// @param installed what is installed; null for none
/// @param[out] error why a file could not be read
/// @return the plan; nothing when a file could not be read
std::optional<ImportPlan> plan_chosen_files(
    SourceKind kind,
    const std::vector<fs::path>& files,
    std::string location,
    const InstalledSummary* installed,
    std::string& error
) {
    const bool installer = kind == SourceKind::demo_installer;
    const fs::path source = installer ? files.front() : common_folder(files);
    const fs::path base = installer ? source.parent_path() : source;
    std::vector<SourceEntry> entries;
    for (const auto& file : files) {
        SourceEntry entry;
        entry.path = path_to_utf8(file.lexically_relative(base));
        if (entry.path.empty() || entry.path == ".")
            entry.path = path_to_utf8(file.filename());
        std::replace(entry.path.begin(), entry.path.end(), '\\', '/');
        std::error_code failure;
        const auto status = fs::symlink_status(file, failure);
        entry.link = !failure && fs::is_symlink(status);
        if (!failure && !entry.link && !fs::is_regular_file(status))
            failure = std::make_error_code(std::errc::no_such_file_or_directory);
        if (!failure && !entry.link)
            entry.size = fs::file_size(file, failure);
        if (!failure && !entry.link)
            entry.modified = seconds_since_1970(fs::last_write_time(file, failure));
        if (failure) {
            error = path_to_utf8(file.filename()) + ": " + failure.message();
            return std::nullopt;
        }
        entries.push_back(std::move(entry));
    }
    if (location.empty())
        location = path_to_utf8(installer ? source.filename() : base.filename());
    return plan_import(kind, source, std::move(location), entries, installed);
}

/// Tells whether a plan holds a file that is downloaded when it is read.
///
/// @param plan the plan
/// @return true when one is held in the cloud
bool holds_remote_files(const ImportPlan& plan) {
    return std::any_of(plan.files.begin(), plan.files.end(), [](const PlannedFile& file) {
        return file.remote;
    });
}

} // namespace

bool list_source(
    const GameFilesHooks& hooks,
    const fs::path& source,
    uint32_t max_depth,
    std::vector<SourceEntry>* entries,
    std::string* error,
    const std::atomic<bool>* stop,
    void (*progress)(void* userdata, uint32_t files, uint64_t bytes),
    void* userdata
) {
    std::vector<SourceEntry> ignored_entries;
    std::string ignored_error;
    auto& out = entries != nullptr ? *entries : ignored_entries;
    auto& why = error != nullptr ? *error : ignored_error;
    why.clear();
    if (hooks.list_source == nullptr)
        return walk_folder(source, max_depth, out, why, stop, progress, userdata);
    HookListing listing{&out, stop, progress, userdata, 0, 0};
    const bool listed = hooks.list_source(
        hooks.context, path_to_utf8(source).c_str(), max_depth, take_listed_entry, &listing, &why
    );
    if (stop != nullptr && stop->load()) {
        why.clear();
        return false;
    }
    if (!listed && why.empty())
        why = "the folder could not be listed";
    return listed;
}

namespace {

/// What a scan's worker and the screen share.
struct ScanState {
    mutable threads::Mutex mutex{};   ///< guards snapshot
    ScanSnapshot snapshot{};          ///< the latest state
    std::atomic<bool> stop{false};    ///< Cancel
    std::atomic<bool> running{false}; ///< the worker runs
    GameFilesHooks hooks{};           ///< the platform's hooks
    ImportPaths paths{};              ///< the import's folders
    ScanRequest request{};            ///< what to scan
    fs::path folder{};                ///< the folder scanned: the chosen one, or one below it
    std::string location{};           ///< its display location
};

/// Changes a scan's snapshot under its lock.
///
/// @param[in,out] shared the scan
/// @param change what to change
template <typename Change>
void publish(ScanState& shared, Change change) {
    threads::LockGuard lock(shared.mutex);
    change(shared.snapshot);
}

/// Tells the screen how many files are listed so far.
///
/// @param userdata the scan's ScanState
/// @param files files listed
/// @param bytes their known sizes
void publish_count(void* userdata, uint32_t files, uint64_t bytes) {
    publish(*static_cast<ScanState*>(userdata), [&](ScanSnapshot& snapshot) {
        snapshot.files = files;
        snapshot.bytes = bytes;
    });
}

/// Ends a scan as failed.
///
/// @param[in,out] shared the scan
/// @param failure why
/// @param error what to tell
void fail_scan(ScanState& shared, ScanSnapshot::Failure failure, std::string error) {
    publish(shared, [&](ScanSnapshot& snapshot) {
        snapshot.stage = ScanStage::failed;
        snapshot.failure = failure;
        snapshot.error = std::move(error);
    });
}

/// Scans files a picker chose one by one: the demo's installer, or archives.
///
/// @param[in,out] shared the scan
void scan_chosen_files(ScanState& shared) {
    const auto& request = shared.request;
    std::vector<fs::path> files;
    for (const auto& path : request.paths)
        files.push_back(path_from_utf8(path));
    if (files.empty()) {
        fail_scan(shared, ScanSnapshot::Failure::unreadable, "no file was chosen");
        return;
    }
    std::optional<InstalledSummary> installed;
    if (request.kind == SourceKind::archives)
        installed = summarize_installed(shared.hooks, shared.paths);
    std::string error;
    auto planned = plan_chosen_files(
        request.kind, files, request.location, installed ? &*installed : nullptr, error
    );
    if (!planned) {
        fail_scan(shared, ScanSnapshot::Failure::unreadable, std::move(error));
        return;
    }
    planned->movable = request.movable;
    const auto plan = std::make_shared<const ImportPlan>(std::move(*planned));
    const bool installer = request.kind == SourceKind::demo_installer;
    // The demo's installer is recognised by its size here; the check after the copy reads it.
    const bool recognised = !installer || (files.size() == 1 && plan->files.size() == 1 &&
                                           plan->files.front().size == demo_1997.installer_size);
    publish(shared, [&](ScanSnapshot& snapshot) {
        snapshot.plan = plan;
        snapshot.files = static_cast<uint32_t>(plan->files.size());
        snapshot.bytes = plan->total_bytes;
        if (recognised) {
            snapshot.stage = ScanStage::planned;
            return;
        }
        snapshot.stage = ScanStage::failed;
        snapshot.failure = ScanSnapshot::Failure::not_demo_installer;
        snapshot.error = path_to_utf8(files.front().filename()) +
                         " is not the release of the Total Annihilation demo (1997) that Open "
                         "Annihilation recognises.";
    });
}

/// Scans a chosen folder: the name check, the listing, the plan and the engine's check.
///
/// @param[in,out] shared the scan
void scan_folder(ScanState& shared) {
    const auto& request = shared.request;
    const auto cancelled = [&] {
        if (!shared.stop.load())
            return false;
        fail_scan(shared, ScanSnapshot::Failure::none, {});
        return true;
    };
    auto names = check_names(shared.hooks, shared.paths, shared.folder);
    const auto look = names.look;
    const auto read_error = names.error;
    publish(shared, [&](ScanSnapshot& snapshot) { snapshot.names = std::move(names); });
    if (cancelled())
        return;
    if (!read_error.empty()) {
        fail_scan(shared, ScanSnapshot::Failure::unreadable, read_error);
        return;
    }
    switch (look) {
    case TopLook::already_there:
        publish(shared, [](ScanSnapshot& snapshot) { snapshot.stage = ScanStage::already_there; });
        return;
    case TopLook::nested:
        publish(shared, [](ScanSnapshot& snapshot) { snapshot.stage = ScanStage::nested; });
        return;
    case TopLook::nothing: {
        DemoSetup none;
        none.outcome = DemoOutcome::no_installer;
        fail_scan(shared, ScanSnapshot::Failure::not_a_game, describe_archive_problem(none));
        return;
    }
    case TopLook::game:
    case TopLook::in_documents:
        break;
    }
    std::vector<SourceEntry> entries;
    std::string error;
    const bool listed = list_source(
        shared.hooks,
        shared.folder,
        whole_tree,
        &entries,
        &error,
        &shared.stop,
        publish_count,
        &shared
    );
    if (cancelled())
        return;
    if (!listed) {
        fail_scan(shared, ScanSnapshot::Failure::unreadable, std::move(error));
        return;
    }
    std::optional<InstalledSummary> installed;
    if (request.kind == SourceKind::additions_folder)
        installed = summarize_installed(shared.hooks, shared.paths);
    auto planned = plan_import(
        request.kind, shared.folder, shared.location, entries, installed ? &*installed : nullptr
    );
    planned.movable = request.movable;
    planned.move_in_place =
        look == TopLook::in_documents && request.kind == SourceKind::game_folder;
    if (cancelled())
        return;

    // The engine's own check, when every planned file is on the device.
    std::optional<GameInstall> source_check;
    const bool remote = holds_remote_files(planned);
    std::error_code failure;
    const bool game_folder_there = fs::is_directory(shared.paths.game_folder, failure);
    if (!remote && request.inspect_local) {
        switch (import_mode_of(planned)) {
        case ImportMode::replace:
            // A folder that holds only the demo's installer is checked once copied, when its
            // archive is unpacked.
            if (planned.has_archives)
                source_check = inspect_game_install(
                    shared.folder, shared.paths.data_folder, demo_1997, request.mod
                );
            break;
        case ImportMode::add:
            // An archive already installed is kept, not copied: the source's copy of it, laid
            // over the game folder, would stand in for the one the game plays.
            if (game_folder_there &&
                std::none_of(planned.kept.begin(), planned.kept.end(), [](const std::string& kept) {
                    const auto slash = kept.rfind('/');
                    return is_archive_name(
                        slash == std::string::npos ? kept : kept.substr(slash + 1)
                    );
                }))
                source_check = inspect_game_install(
                    shared.paths.game_folder,
                    shared.paths.data_folder,
                    demo_1997,
                    request.mod,
                    shared.folder
                );
            break;
        case ImportMode::mod:
            if (game_folder_there) {
                ModChoice choice = request.mod;
                choice.folder = shared.folder;
                source_check = inspect_game_install(
                    shared.paths.game_folder, shared.paths.data_folder, demo_1997, choice
                );
            }
            break;
        case ImportMode::remove:
            break;
        }
    }
    if (cancelled())
        return;
    const auto plan = std::make_shared<const ImportPlan>(std::move(planned));
    publish(shared, [&](ScanSnapshot& snapshot) {
        snapshot.plan = plan;
        snapshot.source_check = std::move(source_check);
        snapshot.source_check_skipped = remote;
        // The plan is whole; the screen asks "Is this the right folder?" before Ready to copy.
        snapshot.failure = plan->total_bytes > far_too_large_bytes
                               ? ScanSnapshot::Failure::too_large
                               : ScanSnapshot::Failure::none;
        snapshot.stage = ScanStage::planned;
    });
}

/// Runs a scan on its worker thread.
///
/// @param argument the scan's ScanState
void run_scan(void* argument) {
    auto& shared = *static_cast<ScanState*>(argument);
    if (shared.request.kind == SourceKind::demo_installer ||
        shared.request.kind == SourceKind::archives)
        scan_chosen_files(shared);
    else
        scan_folder(shared);
    shared.running.store(false);
}

} // namespace

/// What the scan's worker and the screen share.
struct SourceScan::Shared : ScanState {};

SourceScan::~SourceScan() {
    if (shared_)
        shared_->stop.store(true);
    threads::join_thread(thread_);
}

bool SourceScan::start(
    const GameFilesHooks& hooks, const ImportPaths& paths, ScanRequest request, std::string* error
) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    if (busy()) {
        why = "a scan is already running";
        return false;
    }
    if (request.paths.empty()) {
        why = "nothing was chosen";
        return false;
    }
    threads::join_thread(thread_);
    auto shared = std::make_shared<Shared>();
    shared->hooks = hooks;
    shared->paths = paths;
    shared->folder = without_trailing_separator(path_from_utf8(request.paths.front()));
    shared->location =
        request.location.empty() ? path_to_utf8(shared->folder.filename()) : request.location;
    shared->request = std::move(request);
    shared->running.store(true);
    shared_ = shared;
    if (!threads::start_thread(thread_, run_scan, static_cast<ScanState*>(shared.get()))) {
        shared->running.store(false);
        fail_scan(*shared, ScanSnapshot::Failure::unreadable, "the scan could not start");
        why = "the scan could not start";
        return false;
    }
    return true;
}

bool SourceScan::use_nested(std::size_t index, std::string* error) {
    std::string ignored;
    auto& why = error != nullptr ? *error : ignored;
    if (!shared_ || busy()) {
        why = "no scan waits for a folder below";
        return false;
    }
    const auto snapshot = this->snapshot();
    if (snapshot.stage != ScanStage::nested || index >= snapshot.names.nested.size()) {
        why = "no such folder below";
        return false;
    }
    threads::join_thread(thread_);
    const auto& relative = snapshot.names.nested[index];
    shared_->folder /= path_from_utf8(relative);
    shared_->location = location_below(shared_->location, relative);
    shared_->stop.store(false);
    publish(*shared_, [](ScanSnapshot& state) { state = ScanSnapshot{}; });
    shared_->running.store(true);
    if (!threads::start_thread(thread_, run_scan, static_cast<ScanState*>(shared_.get()))) {
        shared_->running.store(false);
        fail_scan(*shared_, ScanSnapshot::Failure::unreadable, "the scan could not start");
        why = "the scan could not start";
        return false;
    }
    return true;
}

void SourceScan::cancel() noexcept {
    if (!shared_)
        return;
    shared_->stop.store(true);
    if (!busy())
        publish(*shared_, [](ScanSnapshot& snapshot) {
            if (snapshot.stage == ScanStage::planned || snapshot.stage == ScanStage::failed)
                return;
            snapshot.stage = ScanStage::failed;
            snapshot.failure = ScanSnapshot::Failure::none;
            snapshot.error.clear();
        });
}

ScanSnapshot SourceScan::snapshot() const {
    if (!shared_)
        return {};
    threads::LockGuard lock(shared_->mutex);
    return shared_->snapshot;
}

bool SourceScan::busy() const noexcept {
    return shared_ && shared_->running.load();
}

} // namespace oa::app::game_files
