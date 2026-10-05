// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A package's files unpacked into a staging folder inside Mods, a piece at
// a time.

#include "files.hpp"
#include "package_file.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/app/mod_install.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <chrono>
#include <exception>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app::mod_install {

namespace fs = std::filesystem;
namespace zip = oa::formats::zip;

struct Unpacking::State {
    const Package* package{};
    fs::path mods{};
    std::string target{};
    fs::path staging{};
    UnpackHooks hooks{};
    ModsHold hold{};
    detail::PackageFile file{};
    std::size_t next_folder{};    ///< the next of package->folders to make
    std::size_t next{};           ///< the next of package->files to unpack
    std::size_t next_sync{};      ///< the next folder to sync: 0 staging, then package->folders
    bool file_open{};             ///< an entry is being written
    zip::EntryStream entry{};     ///< the entry being read
    NewFile out{};                ///< the file being written
    fs::path out_path{};          ///< its path
    uint64_t done_bytes{};        ///< uncompressed bytes written
    uint64_t entry_done_before{}; ///< done_bytes when the entry started
    std::vector<fs::path> discards{};
    bool finished{}; ///< every file is in staging, or it stopped
    bool failed{};   ///< it stopped
};

Unpacking::Unpacking() = default;
Unpacking::~Unpacking() = default;

namespace {

/// The most time a step works.
constexpr std::chrono::milliseconds time_a_step{4};
/// The most package bytes one read of an entry takes, so that a step's time
/// is looked at often.
constexpr uint64_t read_piece_bytes = uint64_t{64} << 10;

/// Returns a problem.
///
/// @param refusal why
/// @param subject what it names
/// @param detail English, for the log
/// @return the problem
Problem problem_of(Refusal refusal, std::string subject = {}, std::string detail = {}) {
    Problem problem{};
    problem.refusal = refusal;
    problem.subject = std::move(subject);
    problem.detail = std::move(detail);
    return problem;
}

/// Returns the bytes free on the disk holding a folder.
///
/// @param hooks the stand-in
/// @param folder the folder
/// @return the bytes; 0 when the system does not say
uint64_t free_bytes(const UnpackHooks& hooks, const fs::path& folder) {
    if (hooks.available != nullptr)
        return hooks.available(hooks.context, folder);
    std::error_code error;
    const fs::space_info space = fs::space(folder, error);
    return error ? 0 : static_cast<uint64_t>(space.available);
}

} // namespace

void Unpacking::discard_staging() noexcept {
    if (!state_)
        return;
    State& state = *state_;
    try {
        state.file_open = false;
        state.out.close();
        std::error_code error;
        if (state.staging.empty() || !fs::exists(fs::symlink_status(state.staging, error)))
            return;
        for (uint32_t number = 1;; ++number) {
            const fs::path to = state.mods / detail::path_of(
                                                 std::string(discard_prefix) + state.target + "-" +
                                                 std::to_string(number)
                                             );
            if (fs::exists(fs::symlink_status(to, error)))
                continue;
            rename_exclusively(state.staging, to, error);
            if (!error)
                state.discards.push_back(to);
            else
                detail::log_line(
                    "cannot move " + detail::utf8_of(state.staging) +
                    " aside to be deleted: " + error.message()
                );
            return;
        }
    } catch (const std::exception& failure) {
        detail::log_line(std::string("cannot move the staging folder aside: ") + failure.what());
    }
}

bool Unpacking::start(
    const Package& package,
    const fs::path& mods,
    std::string_view target,
    const InstalledMod& expected,
    Problem& problem,
    const UnpackHooks& hooks
) {
    problem = Problem{};
    state_.reset();
    try {
        auto state = std::make_unique<State>();
        state->package = &package;
        state->mods = mods;
        state->target = std::string(target);
        state->hooks = hooks;
        std::error_code error;
        fs::create_directories(mods, error);
        if (error) {
            problem = problem_of(
                Refusal::not_placed, {}, "cannot make the Mods folder: " + error.message()
            );
            return false;
        }
        state->hold = hold_mods_folder(mods);
        if (!state->hold) {
            problem = problem_of(Refusal::busy);
            return false;
        }
        const fs::path folder = mods / detail::path_of(target);
        if (!same_mod(read_installed_mod(folder), expected)) {
            problem = problem_of(Refusal::changed, std::string(target));
            return false;
        }
        const uint64_t needed = package.unpacked_bytes + free_space_margin;
        const uint64_t available = free_bytes(hooks, mods);
        if (available < needed) {
            problem = problem_of(Refusal::no_space);
            problem.size_bytes = needed;
            problem.limit_bytes = available;
            return false;
        }
        state->staging = mods / detail::path_of(std::string(staging_prefix) + std::string(target));
        // The longest path a file of the package takes in staging, which is
        // longer than the target's, keeps path_room to spare.
        std::size_t longest_name = 0;
        for (const PackagedFile& packaged : package.files)
            longest_name = std::max(longest_name, detail::path_of(packaged.name).native().size());
        if (const std::string why = path_length_problem(
                state->staging, longest_name + 1 + static_cast<std::size_t>(path_room)
            );
            !why.empty()) {
            problem = problem_of(Refusal::path_too_long, {}, why);
            return false;
        }
        // An earlier staging folder goes, and this one is made afresh.
        state_ = std::move(state);
        if (fs::exists(fs::symlink_status(state_->staging, error)))
            discard_staging();
        State* made = state_.get();
        if (!fs::create_directory(made->staging, error) || error) {
            problem = problem_of(
                Refusal::not_placed,
                {},
                "cannot make " + detail::utf8_of(made->staging) + ": " + error.message()
            );
            made->finished = true;
            made->failed = true;
            made->hold.reset();
            return false;
        }
        if (!made->file.open(package.file)) {
            discard_staging();
            problem = problem_of(Refusal::unreadable, {}, "the file cannot be opened again");
            made->finished = true;
            made->failed = true;
            made->hold.reset();
            return false;
        }
        return true;
    } catch (const std::exception& failure) {
        problem = problem_of(Refusal::not_placed, {}, failure.what());
        return false;
    }
}

zip::StreamStep Unpacking::step(uint64_t budget, Problem& problem) {
    problem = Problem{};
    if (!state_ || state_->failed)
        return zip::StreamStep::failed;
    if (state_->finished)
        return zip::StreamStep::done;
    State& state = *state_;
    const Package& package = *state.package;
    const auto stop = [&](Problem why) {
        problem = std::move(why);
        discard_staging();
        state.finished = true;
        state.failed = true;
        state.hold.reset();
        detail::log_line(
            package.file_name +
            ": unpacking stopped: " + (problem.detail.empty() ? problem.subject : problem.detail)
        );
        return zip::StreamStep::failed;
    };
    try {
        const auto started = std::chrono::steady_clock::now();
        const uint64_t allowed = std::max<uint64_t>(budget, 1);
        uint64_t used = 0;
        // At least one piece of work, then on while the budget and the time last.
        while (used < allowed &&
               (used == 0 || std::chrono::steady_clock::now() - started < time_a_step)) {
            if (state.next_folder < package.folders.size()) {
                // The folders first, each made anew: a folder whose name the
                // file system folds to one made already is refused.
                const std::string& name = package.folders[state.next_folder];
                const fs::path folder = state.staging / detail::path_of(name);
                std::error_code error;
                if (!fs::create_directory(folder, error))
                    return stop(
                        error
                            ? problem_of(
                                  Refusal::not_placed,
                                  name,
                                  "cannot make " + detail::utf8_of(folder) + ": " + error.message()
                              )
                            : problem_of(Refusal::case_clash, name)
                    );
                ++state.next_folder;
                used += unpack_entry_cost;
            } else if (state.file_open || state.next < package.files.size()) {
                const PackagedFile& packaged = package.files[state.next];
                if (!state.file_open) {
                    const zip::StreamEntry& entry = package.directory.entries[packaged.entry];
                    if (entry.directory) {
                        // Made with the folders.
                        ++state.next;
                        continue;
                    }
                    const fs::path path = state.staging / detail::path_of(packaged.name);
                    if (state.hooks.before_file != nullptr)
                        state.hooks.before_file(state.hooks.context, path);
                    std::error_code error;
                    if (!state.out.create(path, error))
                        return stop(
                            error == std::errc::file_exists
                                ? problem_of(Refusal::case_clash, packaged.name)
                                : problem_of(
                                      Refusal::not_placed,
                                      packaged.name,
                                      "cannot make " + detail::utf8_of(path) + ": " +
                                          error.message()
                                  )
                        );
                    zip::ZipError zip_error{};
                    if (!state.entry.open(state.file.source(), state.file.bytes, entry, zip_error))
                        return stop(problem_of(
                            Refusal::damaged,
                            packaged.name,
                            zip::zip_status_message(zip_error.status)
                        ));
                    state.out_path = path;
                    state.file_open = true;
                    state.entry_done_before = state.done_bytes;
                    used += unpack_entry_cost;
                    continue;
                }
                const zip::SinkHooks sink{
                    &state, [](void* context, std::span<const uint8_t> bytes) {
                        auto& self = *static_cast<State*>(context);
                        if (!self.out.write(bytes))
                            return false;
                        self.done_bytes += bytes.size();
                        return true;
                    }
                };
                zip::ZipError zip_error{};
                const uint64_t before = state.entry.input_done();
                const zip::StreamStep read =
                    state.entry.step(sink, std::min(read_piece_bytes, allowed - used), zip_error);
                used += std::max<uint64_t>(state.entry.input_done() - before, 1);
                if (read == zip::StreamStep::failed)
                    return stop(
                        zip_error.status == zip::ZipStatus::write_failed
                            ? problem_of(
                                  Refusal::not_placed,
                                  packaged.name,
                                  "cannot write " + detail::utf8_of(state.out_path)
                              )
                            : problem_of(
                                  Refusal::damaged,
                                  packaged.name,
                                  zip::zip_status_message(zip_error.status)
                              )
                    );
                if (read == zip::StreamStep::done) {
                    if (!state.out.finish())
                        return stop(problem_of(
                            Refusal::not_placed,
                            packaged.name,
                            "cannot finish " + detail::utf8_of(state.out_path)
                        ));
                    state.file_open = false;
                    state.entry = zip::EntryStream();
                    ++state.next;
                }
            } else if (state.next_sync <= package.folders.size()) {
                // Every folder reaches the disk before the files are put in
                // place.
                detail::sync_folder(
                    state.next_sync == 0
                        ? state.staging
                        : state.staging / detail::path_of(package.folders[state.next_sync - 1])
                );
                ++state.next_sync;
                used += unpack_entry_cost;
            } else {
                detail::flush_to_storage(state.staging);
                state.finished = true;
                return zip::StreamStep::done;
            }
        }
        return zip::StreamStep::more;
    } catch (const std::exception& failure) {
        return stop(problem_of(Refusal::not_placed, {}, failure.what()));
    }
}

void Unpacking::cancel() noexcept {
    if (!state_)
        return;
    discard_staging();
    state_->finished = true;
    state_->failed = true;
    state_->hold.reset();
}

uint64_t Unpacking::done_bytes() const noexcept {
    return state_ ? state_->done_bytes : 0;
}

uint64_t Unpacking::total_bytes() const noexcept {
    return state_ && state_->package != nullptr ? state_->package->unpacked_bytes : 0;
}

fs::path Unpacking::staging() const {
    return state_ ? state_->staging : fs::path();
}

ModsHold Unpacking::take_hold() noexcept {
    return state_ ? std::move(state_->hold) : nullptr;
}

const std::vector<fs::path>& Unpacking::discards() const noexcept {
    static const std::vector<fs::path> none;
    return state_ ? state_->discards : none;
}

} // namespace oa::app::mod_install
