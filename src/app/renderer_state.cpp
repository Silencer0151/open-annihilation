// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/renderer_state.hpp"

#include "oa/platform/preferences.hpp"

#include <algorithm>
#include <exception>
#include <iostream>
#include <system_error>
#include <utility>

namespace oa::app::renderer_state {

namespace preferences = oa::platform::preferences;

namespace {

/// Returns the drivers' entries a clear() took away, with what was struck or
/// recorded since laid over them: a strike, a record or a remembered rung
/// found since replaces the driver's own, and a driver first noted since is
/// added, up to max_drivers.
///
/// @param before every driver's entry as the clear found it
/// @param since every driver's entry since the clear
/// @return the entries
std::vector<DriverRecords>
laid_over(std::vector<DriverRecords> before, const std::vector<DriverRecords>& since) {
    for (const DriverRecords& entry : since) {
        const bool struck = entry.strike.stage != StrikeStage::none;
        const bool failed = entry.failed_driver.failure != RecordedFailure::none;
        const bool unusable = entry.accelerated_unusable.failure != RecordedFailure::none;
        const bool full_unusable = entry.full_unusable.failure != RecordedFailure::none;
        if (!struck && !failed && !unusable && !full_unusable && !entry.scale_level)
            continue;
        const auto kept =
            std::find_if(before.begin(), before.end(), [&](const DriverRecords& candidate) {
                return candidate.driver == entry.driver;
            });
        if (kept == before.end()) {
            if (before.size() < max_drivers)
                before.push_back(entry);
            continue;
        }
        if (struck) {
            kept->strike = entry.strike;
            kept->struck_this_run = entry.struck_this_run;
        }
        if (failed)
            kept->failed_driver = entry.failed_driver;
        if (unusable)
            kept->accelerated_unusable = entry.accelerated_unusable;
        if (full_unusable)
            kept->full_unusable = entry.full_unusable;
        if (entry.scale_level)
            kept->scale_level = entry.scale_level;
    }
    return before;
}

} // namespace

RendererState::RendererState(
    Storage storage, std::string engine_version, const RecordRules& rules, LogHooks log
)
    : storage_(storage), engine_version_(std::move(engine_version)), rules_(rules), log_(log) {
}

RendererState RendererState::open_folder(
    const std::filesystem::path& folder,
    std::string engine_version,
    const RecordRules& rules,
    LogHooks log
) {
    RendererState state(Storage::disk, std::move(engine_version), rules, log);
    state.folder_ = folder;
    const auto records_file = folder / records_file_name;
    try {
        Values values = preferences::load(records_file);
        ParsedRecords parsed = parse_records(values, state.engine_version_);
        state.records_ = std::move(parsed.records);
        state.found_.dropped = parsed.dropped;
        state.written_ = std::move(values);
    } catch (const std::exception& error) {
        state.found_.records_unreadable = true;
        state.file_unreadable_ = true;
        state.log(
            std::string(records_file_name) + " cannot be read and is ignored: " + error.what()
        );
    }
    const auto sentinel_file = folder / sentinel_file_name;
    try {
        const Values values = preferences::load(sentinel_file);
        std::error_code error;
        if (values.empty() && !std::filesystem::exists(sentinel_file, error) && !error)
            return state; // the last run ended cleanly
        const auto starting = values.find(std::string(sentinel_key));
        std::optional<Sentinel> sentinel;
        if (values.size() == 1 && starting != values.end())
            sentinel = parse_sentinel(starting->second);
        if (sentinel) {
            state.found_.sentinel = LeftoverSentinel::read;
            state.found_.leftover = std::move(*sentinel);
        } else {
            state.found_.sentinel = LeftoverSentinel::unreadable;
        }
    } catch (const std::exception&) {
        state.found_.sentinel = LeftoverSentinel::unreadable;
    }
    return state;
}

RendererState
RendererState::in_memory(std::string engine_version, const RecordRules& rules, LogHooks log) {
    return RendererState(Storage::memory, std::move(engine_version), rules, log);
}

RendererState RendererState::disabled() {
    return RendererState(Storage::disabled, std::string(), RecordRules{}, LogHooks{});
}

void RendererState::log(std::string_view text) const {
    if (log_.line != nullptr) {
        log_.line(log_.context, text);
        return;
    }
    std::cerr << "open-annihilation: " << text << '\n';
}

LeftoverOutcome RendererState::resolve_leftovers() {
    if (resolved_ || storage_ == Storage::disabled)
        return {};
    resolved_ = true;
    LeftoverOutcome outcome = note_leftover(records_, found_.sentinel, found_.leftover, rules_);
    if (!outcome.unclean_exit)
        return outcome;
    std::string line = "the last run ended without a clean exit";
    if (found_.sentinel == LeftoverSentinel::read)
        line += " (" + format_sentinel(found_.leftover) + ")";
    else if (found_.sentinel == LeftoverSentinel::unreadable)
        line += " (" + std::string(sentinel_file_name) + " cannot be read)";
    if (outcome.change.new_record)
        line += "; recorded against " + outcome.driver;
    else if (!outcome.driver.empty())
        line += "; a strike against " + outcome.driver;
    log(line);
    return outcome;
}

bool RendererState::save_records_file(const Values& values) {
    try {
        preferences::save(folder_ / records_file_name, values, preferences::SyncFolder::yes);
    } catch (const std::exception& error) {
        if (!records_failure_logged_) {
            records_failure_logged_ = true;
            log("cannot write " + std::string(records_file_name) + ": " + error.what() +
                "; the renderer records are kept in memory for this run");
        }
        return false;
    }
    written_ = values;
    file_unreadable_ = false;
    return true;
}

Values RendererState::records_values() const {
    if (!clear_pending_)
        return format_records(records_, engine_version_);
    Records kept = records_;
    kept.drivers = laid_over(cleared_, records_.drivers);
    return format_records(kept, engine_version_);
}

bool RendererState::save_records() {
    const Values values = records_values();
    return (values == written_ && !file_unreadable_) || save_records_file(values);
}

bool RendererState::write_records() {
    if (storage_ != Storage::disk || match_running_)
        return true;
    return save_records();
}

bool RendererState::write_trial(const Trial& trial) {
    if (!valid_driver_name(trial.driver) || rules_.below_two_gib)
        return false;
    if (storage_ == Storage::disabled)
        return true;
    if (storage_ == Storage::memory) {
        records_.trial = trial;
        own_trial_ = true;
        return true;
    }
    // The records as they stand, the last run's strike among them, unless a
    // match's must wait.
    Values values;
    if (match_running_) {
        if (!file_unreadable_)
            values = written_;
    } else {
        values = records_values();
    }
    values.insert_or_assign(std::string(trial_key), format_trial(trial));
    if (!save_records_file(values))
        return false;
    records_.trial = trial;
    own_trial_ = true;
    return true;
}

bool RendererState::erase_trial() {
    if (!own_trial_)
        return true;
    own_trial_ = false;
    records_.trial.reset();
    if (storage_ != Storage::disk)
        return true;
    if (file_unreadable_ || written_.count(std::string(trial_key)) == 0)
        return true;
    Values values = written_;
    values.erase(std::string(trial_key));
    return save_records_file(values);
}

void RendererState::set_sentinel(const Sentinel& sentinel) {
    if (storage_ == Storage::disabled)
        return;
    sentinel_ = sentinel;
    if (storage_ != Storage::disk)
        return;
    try {
        preferences::overwrite(
            folder_ / sentinel_file_name,
            Values{{std::string(sentinel_key), format_sentinel(sentinel)}}
        );
    } catch (const std::exception& error) {
        if (!sentinel_failure_logged_) {
            sentinel_failure_logged_ = true;
            log("cannot write " + std::string(sentinel_file_name) + ": " + error.what());
        }
    }
}

void RendererState::delete_sentinel() {
    sentinel_.reset();
    if (storage_ != Storage::disk)
        return;
    std::error_code error;
    std::filesystem::remove(folder_ / sentinel_file_name, error);
    if (error && !sentinel_failure_logged_) {
        sentinel_failure_logged_ = true;
        log("cannot delete " + std::string(sentinel_file_name) + ": " + error.message());
    }
}

void RendererState::clean_exit() {
    if (storage_ == Storage::disabled)
        return;
    restore_failures();
    if (own_trial_) {
        own_trial_ = false;
        records_.trial.reset();
    }
    if (storage_ == Storage::disk)
        (void)save_records();
    delete_sentinel();
}

void RendererState::restore_failures() noexcept {
    if (!clear_pending_)
        return;
    std::vector<DriverRecords> restored;
    try {
        restored = laid_over(cleared_, records_.drivers);
    } catch (const std::exception&) {
        // Short of memory, what the clear took away comes back alone.
        restored = std::move(cleared_);
    }
    records_.drivers = std::move(restored);
    cleared_.clear();
    clear_pending_ = false;
}

Change RendererState::clear() {
    std::vector<DriverRecords> before;
    if (!clear_pending_)
        before = records_.drivers;
    const Change change = clear_failures(records_);
    if (change.changed && !clear_pending_) {
        cleared_ = std::move(before);
        clear_pending_ = true;
    }
    return change;
}

bool RendererState::confirm_clear() {
    cleared_.clear();
    clear_pending_ = false;
    return write_records();
}

} // namespace oa::app::renderer_state
