// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The renderer records' two files, each case in a scratch folder of its own:
// a folder with neither file; the records and the sentinel written and read
// back by the next start; writes made only when a record changes; a garbled
// records file ignored and rewritten, and a garbled sentinel file read as a
// left-over of no known stage; the trial written with the records as they
// stand in the file after Off then On and during a match, and with the last
// run's strike otherwise; a clean exit erasing the trial and writing a
// match's records; under 2 GiB no trial written and what a larger machine
// left kept; writes that fail in a read-only folder and where a file stands
// in place of the folder, each logged once, with the records kept in memory
// and a trial that cannot be written reported; records in memory and
// disabled; and left-over trials across simulated starts, two in a row or
// the first where it counts.
#include "oa/app/renderer_state.hpp"

#include "oa/platform/preferences.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>
#ifndef _WIN32
#include <unistd.h>
#endif

namespace rs = oa::app::renderer_state;
namespace preferences = oa::platform::preferences;

namespace {

constexpr std::string_view version = "0.6.2";
/// A machine from 2 GiB where two left-over trials in a row make a record.
constexpr rs::RecordRules two_in_a_row{rs::CrashEvidence::two_in_a_row, false};
/// A machine from 2 GiB where the first left-over trial is a record.
constexpr rs::RecordRules first_counts{rs::CrashEvidence::first_counts, false};
/// A machine under 2 GiB, on a platform where the first would count.
constexpr rs::RecordRules below_two_gib{rs::CrashEvidence::first_counts, true};

/// The log lines a test collects.
struct LogLines {
    std::vector<std::string> lines{};
};

/// Keeps a log line.
void keep_line(void* context, std::string_view text) {
    static_cast<LogLines*>(context)->lines.emplace_back(text);
}

/// Returns hooks that keep log lines in `lines`.
rs::LogHooks log_into(LogLines& lines) {
    return rs::LogHooks{&lines, &keep_line};
}

/// A scratch folder of the test's, removed when the case ends.
struct Scratch {
    std::filesystem::path folder{oa::test::make_scratch_directory("oa-renderer-state")};

    ~Scratch() {
        std::error_code ignored;
        std::filesystem::permissions(
            folder, std::filesystem::perms::owner_all, std::filesystem::perm_options::add, ignored
        );
        std::filesystem::remove_all(folder, ignored);
    }
};

/// Writes text into a file, as a crash or another program could leave it.
void write_text(const std::filesystem::path& file, const std::string& text) {
    std::ofstream(file, std::ios::binary) << text;
}

/// Returns a sentinel of a stage and driver.
rs::Sentinel sentinel_of(rs::SentinelStage stage, std::string driver) {
    rs::Sentinel sentinel;
    sentinel.stage = stage;
    sentinel.driver = std::move(driver);
    return sentinel;
}

/// Returns the probe trial of a driver.
rs::Trial probe_trial(std::string driver) {
    return rs::Trial{rs::StrikeStage::probe, rs::AcceleratedPath::magnify, std::move(driver)};
}

void a_folder_with_neither_file() {
    Scratch scratch;
    LogLines log;
    rs::RendererState state = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    OA_CHECK(state.storage() == rs::Storage::disk);
    OA_CHECK(!state.found().records_unreadable);
    OA_CHECK(state.found().sentinel == rs::LeftoverSentinel::none);
    OA_CHECK(state.records().drivers.empty());
    const rs::LeftoverOutcome outcome = state.resolve_leftovers();
    OA_CHECK(!outcome.unclean_exit && !outcome.change.changed);
    // Nothing changed, so nothing is written.
    OA_CHECK(state.write_records());
    OA_CHECK(!std::filesystem::exists(scratch.folder / rs::records_file_name));
    OA_CHECK(log.lines.empty());
    // A folder that does not exist yet reads the same, and is made by the
    // first write.
    const auto nested = scratch.folder / "not-yet";
    rs::RendererState later =
        rs::RendererState::open_folder(nested, std::string(version), two_in_a_row, log_into(log));
    OA_CHECK(later.found().sentinel == rs::LeftoverSentinel::none);
    (void)rs::note_adapter(later.records(), "Example Graphics 3000");
    OA_CHECK(later.write_records());
    OA_CHECK(std::filesystem::exists(nested / rs::records_file_name));
    OA_CHECK(log.lines.empty());
}

void records_are_written_only_when_one_changes() {
    Scratch scratch;
    LogLines log;
    const auto records_file = scratch.folder / rs::records_file_name;
    {
        rs::RendererState state = rs::RendererState::open_folder(
            scratch.folder, std::string(version), two_in_a_row, log_into(log)
        );
        (void)rs::note_adapter(state.records(), "Example Graphics 3000");
        rs::DriverRecords& alpha = rs::driver_entry(state.records(), "alpha");
        alpha.failed_driver = rs::Record{rs::RecordedFailure::stopped, false};
        alpha.scale_level = rs::ScaleLevel{2, 450};
        state.records().native_density = rs::NativeDensity{"beta", std::string(version)};
        OA_CHECK(state.write_records());
        OA_CHECK(std::filesystem::exists(records_file));
        // The same records again write nothing: the file, removed behind the
        // state's back, stays away.
        std::filesystem::remove(records_file);
        OA_CHECK(state.write_records());
        OA_CHECK(!std::filesystem::exists(records_file));
        // A change writes the file again.
        OA_CHECK(rs::mark_told(state.records(), "alpha").changed);
        OA_CHECK(state.write_records());
        OA_CHECK(std::filesystem::exists(records_file));
        // No temporary file is left beside it.
        size_t entries = 0;
        for ([[maybe_unused]] const auto& entry :
             std::filesystem::directory_iterator(scratch.folder))
            ++entries;
        OA_CHECK(entries == 1);
    }
    // The next start reads them back.
    rs::RendererState next = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    OA_CHECK(next.found().dropped == 0);
    OA_CHECK(next.records().adapter == "Example Graphics 3000");
    const rs::DriverRecords* alpha = rs::find_driver(next.records(), "alpha");
    OA_CHECK(alpha != nullptr && alpha->failed_driver.told && alpha->scale_level->rung == 2);
    OA_CHECK(next.records().native_density && next.records().native_density->driver == "beta");
    OA_CHECK(rs::skips_driver(next.records(), "alpha"));
    // A start of a new engine version drops them, and rewrites the file
    // without them at its first write.
    rs::RendererState newer =
        rs::RendererState::open_folder(scratch.folder, "0.7.0", two_in_a_row, log_into(log));
    OA_CHECK(newer.records().drivers.empty());
    OA_CHECK(newer.found().dropped == 2); // the record and the remembered rung
    OA_CHECK(newer.write_records());
    OA_CHECK(preferences::load(records_file).count("failed-driver.alpha") == 0);
    OA_CHECK(log.lines.empty());
}

void the_sentinel_is_rewritten_in_place_and_deleted() {
    Scratch scratch;
    LogLines log;
    const auto sentinel_file = scratch.folder / rs::sentinel_file_name;
    rs::RendererState state = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    state.set_sentinel(sentinel_of(rs::SentinelStage::create, "alpha"));
    OA_CHECK(preferences::load(sentinel_file) == (rs::Values{{"starting", "create alpha"}}));
    // A second name for the file sees each new stage: the file is rewritten
    // where it is, never replaced.
    const auto second_name = scratch.folder / "second-name.conf";
    std::filesystem::create_hard_link(sentinel_file, second_name);
    rs::Sentinel software = sentinel_of(rs::SentinelStage::standard, "software");
    software.via = {"alpha", "beta"};
    state.set_sentinel(software);
    OA_CHECK(
        preferences::load(second_name) ==
        (rs::Values{{"starting", "standard software via alpha,beta"}})
    );
    OA_CHECK(state.sentinel() && rs::same_sentinel(*state.sentinel(), software));
    // The records file is not touched by the sentinel.
    OA_CHECK(!std::filesystem::exists(scratch.folder / rs::records_file_name));
    // A start after a crash here finds it left over.
    rs::RendererState next = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    OA_CHECK(next.found().sentinel == rs::LeftoverSentinel::read);
    OA_CHECK(rs::same_sentinel(next.found().leftover, software));
    // A clean exit deletes it, and the start after that finds none.
    state.delete_sentinel();
    OA_CHECK(!state.sentinel());
    OA_CHECK(!std::filesystem::exists(sentinel_file));
    state.delete_sentinel(); // deleting it again is no failure
    rs::RendererState clean = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    OA_CHECK(clean.found().sentinel == rs::LeftoverSentinel::none);
    OA_CHECK(log.lines.empty());
}

void garbled_files_are_never_fatal() {
    Scratch scratch;
    LogLines log;
    const auto records_file = scratch.folder / rs::records_file_name;
    const auto sentinel_file = scratch.folder / rs::sentinel_file_name;
    // Files a crash or another program could leave: cut short, empty, not
    // in the format, or holding other keys.
    const char* garbled_records[] = {
        "",
        "open-annihilation-preferences 1\n\"strike.alpha\" \"create",
        "\x01\x02\x03 not a records file",
        "open-annihilation-preferences 1\n\"trial\" \"a\"\n\"trial\" \"b\"\n",
    };
    for (const char* text : garbled_records) {
        write_text(records_file, text);
        log.lines.clear();
        rs::RendererState state = rs::RendererState::open_folder(
            scratch.folder, std::string(version), two_in_a_row, log_into(log)
        );
        OA_CHECK(state.found().records_unreadable);
        // The last run ended cleanly, so the start may accelerate.
        OA_CHECK(!state.records_unreadable_after_unclean_start());
        OA_CHECK(state.records().drivers.empty() && !state.records().trial);
        OA_CHECK(log.lines.size() == 1);
        // The next write replaces it, even with nothing to write.
        OA_CHECK(state.write_records());
        OA_CHECK(preferences::load(records_file).empty());
    }
    // Values that cannot be read are dropped one by one, and the rest kept.
    preferences::save(
        records_file,
        {{"adapter", "Example Graphics 3000"},
         {"failed-driver.alpha", "stopped Example Graphics 3000 0.6.2"},
         {"failed-driver.beta", "exploded Example Graphics 3000 0.6.2"},
         {"trial", "probe"}}
    );
    rs::RendererState partial = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    OA_CHECK(!partial.found().records_unreadable);
    OA_CHECK(partial.found().dropped == 2);
    OA_CHECK(rs::skips_driver(partial.records(), "alpha"));
    OA_CHECK(!partial.records().trial);
    OA_CHECK(partial.write_records());
    OA_CHECK(preferences::load(records_file).size() == 2);

    // A garbled records file after a start that did not end cleanly keeps
    // this start standard, whatever the sentinel holds.
    for (const char* sentinel_text :
         {"open-annihilation-preferences 1\n\"starting\" \"probe alpha\"\n", "garbled"}) {
        write_text(records_file, "open-annihilation-preferences 1\n\"trial\" \"pro");
        write_text(sentinel_file, sentinel_text);
        rs::RendererState state = rs::RendererState::open_folder(
            scratch.folder, std::string(version), two_in_a_row, log_into(log)
        );
        OA_CHECK(state.records_unreadable_after_unclean_start());
    }
    std::filesystem::remove(sentinel_file);
    OA_CHECK(
        rs::RendererState::open_folder(
            scratch.folder, std::string(version), two_in_a_row, log_into(log)
        )
            .found()
            .records_unreadable
    );
    preferences::save(records_file, {{"adapter", "Example Graphics 3000"}});

    const char* garbled_sentinels[] = {
        "",
        "open-annihilation-preferences 1\n\"starting\" \"stan",
        "open-annihilation-preferences 1\n\"starting\" \"standing alpha\"\n",
        "open-annihilation-preferences 1\n\"other\" \"create alpha\"\n",
        "open-annihilation-preferences 1\n",
        "open-annihilation-preferences 1\n\"starting\" \"create alpha\"\n\"extra\" \"1\"\n",
    };
    for (const char* text : garbled_sentinels) {
        write_text(sentinel_file, text);
        rs::RendererState state = rs::RendererState::open_folder(
            scratch.folder, std::string(version), first_counts, log_into(log)
        );
        OA_CHECK(state.found().sentinel == rs::LeftoverSentinel::unreadable);
        OA_CHECK(!state.records_unreadable_after_unclean_start());
        // It is only logged: nothing is struck or recorded.
        log.lines.clear();
        const rs::LeftoverOutcome outcome = state.resolve_leftovers();
        OA_CHECK(outcome.unclean_exit && !outcome.change.changed);
        OA_CHECK(log.lines.size() == 1);
    }
}

void the_trial_carries_the_records_as_they_stand_in_the_file() {
    Scratch scratch;
    LogLines log;
    const auto records_file = scratch.folder / rs::records_file_name;
    rs::RendererState state = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    (void)rs::note_adapter(state.records(), "Example Graphics 3000");
    rs::driver_entry(state.records(), "alpha").accelerated_unusable =
        rs::Record{rs::RecordedFailure::stopped, false};
    OA_CHECK(state.write_records());
    // Off then On clears the records in memory; the file keeps them until
    // they are written.
    OA_CHECK(state.clear().changed);
    OA_CHECK(rs::acceleration_allowed(state.records(), "alpha", false));
    OA_CHECK(preferences::load(records_file).count("accelerated-unusable.alpha") == 1);
    // The retry's trial is written with the records the file holds.
    OA_CHECK(state.write_trial(probe_trial("alpha")));
    rs::Values on_disk = preferences::load(records_file);
    OA_CHECK(on_disk.at("trial") == "probe alpha");
    OA_CHECK(on_disk.count("accelerated-unusable.alpha") == 1);
    OA_CHECK(state.records().trial && state.records().trial->driver == "alpha");
    // The stage passes: the trial is erased, and the file still keeps the
    // records until OK writes the cleared ones.
    OA_CHECK(state.erase_trial());
    on_disk = preferences::load(records_file);
    OA_CHECK(on_disk.count("trial") == 0);
    OA_CHECK(on_disk.count("accelerated-unusable.alpha") == 1);
    OA_CHECK(state.write_records());
    OA_CHECK(preferences::load(records_file).count("accelerated-unusable.alpha") == 0);
    // Erasing a trial that is not there writes nothing.
    std::filesystem::remove(records_file);
    OA_CHECK(state.erase_trial());
    OA_CHECK(!std::filesystem::exists(records_file));
    // A trial for a name that cannot be a driver's is never written.
    OA_CHECK(!state.write_trial(probe_trial("not a driver")));
    OA_CHECK(log.lines.empty());
}

void records_from_a_match_are_written_when_it_ends() {
    Scratch scratch;
    LogLines log;
    const auto records_file = scratch.folder / rs::records_file_name;
    rs::RendererState state = rs::RendererState::open_folder(
        scratch.folder, std::string(version), two_in_a_row, log_into(log)
    );
    state.set_match_running(true);
    rs::Strike lost;
    lost.stage = rs::StrikeStage::lost;
    OA_CHECK(rs::note_running_failure(state.records(), "alpha", lost, {}, two_in_a_row).new_record);
    OA_CHECK(state.write_records());
    OA_CHECK(!std::filesystem::exists(records_file));
    // A path's trial during the match still reaches the disk, without the
    // match's records.
    OA_CHECK(
        state.write_trial(rs::Trial{rs::StrikeStage::path, rs::AcceleratedPath::magnify, "alpha"})
    );
    OA_CHECK(preferences::load(records_file) == (rs::Values{{"trial", "path magnify alpha"}}));
    OA_CHECK(state.erase_trial());
    state.set_match_running(false);
    OA_CHECK(state.write_records());
    const rs::Values on_disk = preferences::load(records_file);
    OA_CHECK(on_disk.at("accelerated-unusable.alpha") == "lost unknown 0.6.2");
    OA_CHECK(on_disk.at("strike.alpha") == "lost unknown 0.6.2");
}

void the_last_runs_strike_goes_with_the_trial() {
    Scratch scratch;
    LogLines log;
    const auto folder = scratch.folder / "profile";
    std::filesystem::create_directories(folder);
    preferences::save(folder / rs::sentinel_file_name, {{"starting", "standard alpha"}});
    rs::RendererState state =
        rs::RendererState::open_folder(folder, std::string(version), two_in_a_row, log_into(log));
    const rs::LeftoverOutcome outcome = state.resolve_leftovers();
    OA_CHECK(outcome.driver == "alpha" && outcome.change.changed);
    // For a moment nothing can be written there: a file stands in the
    // folder's place, as another program can hold a file.
    std::filesystem::remove_all(folder);
    write_text(folder, "a file, not a folder");
    OA_CHECK(!state.write_records());
    // The trial cannot go without the strike, so it is refused and the
    // caller skips the stage.
    OA_CHECK(!state.write_trial(probe_trial("alpha")));
    OA_CHECK(!state.records().trial);
    // Once the folder can be written again, the trial carries the strike, so
    // a crash in the stage it covers is the second strike in a row.
    std::filesystem::remove(folder);
    std::filesystem::create_directories(folder);
    OA_CHECK(state.write_trial(probe_trial("alpha")));
    const rs::Values on_disk = preferences::load(folder / rs::records_file_name);
    OA_CHECK(on_disk.at("trial") == "probe alpha");
    OA_CHECK(on_disk.at("strike.alpha") == "standard unknown 0.6.2");
    // The unclean exit, and the failed write once.
    OA_CHECK(log.lines.size() == 2);
}

void a_clean_exit_counts_nothing_against_the_driver() {
    Scratch scratch;
    LogLines log;
    const auto records_file = scratch.folder / rs::records_file_name;
    const auto sentinel_file = scratch.folder / rs::sentinel_file_name;
    {
        // The player quits in the first accelerated frames, before the
        // start-up stage passes, with a match running.
        rs::RendererState state = rs::RendererState::open_folder(
            scratch.folder, std::string(version), first_counts, log_into(log)
        );
        (void)state.resolve_leftovers();
        state.set_sentinel(sentinel_of(rs::SentinelStage::create, "alpha"));
        state.set_sentinel(sentinel_of(rs::SentinelStage::standard, "alpha"));
        OA_CHECK(state.write_trial(probe_trial("alpha")));
        state.set_sentinel(sentinel_of(rs::SentinelStage::probe, "alpha"));
        state.set_sentinel(sentinel_of(rs::SentinelStage::accelerated, "alpha"));
        state.set_match_running(true);
        rs::Strike present;
        present.stage = rs::StrikeStage::present;
        present.call = "present-frame";
        (void)rs::note_running_failure(state.records(), "alpha", present, {}, state.rules());
        OA_CHECK(state.write_records());
        OA_CHECK(preferences::load(records_file).count("strike.alpha") == 0);
        state.clean_exit();
        // The trial is erased, the match's strike written and the sentinel
        // deleted.
        OA_CHECK(!state.records().trial && !state.sentinel());
        OA_CHECK(!std::filesystem::exists(sentinel_file));
        const rs::Values on_disk = preferences::load(records_file);
        OA_CHECK(on_disk.count("trial") == 0);
        OA_CHECK(on_disk.at("strike.alpha") == "present present-frame unknown 0.6.2");
    }
    // The next start finds no unclean exit and counts nothing, even where
    // the first left-over trial is a record.
    rs::RendererState next = rs::RendererState::open_folder(
        scratch.folder, std::string(version), first_counts, log_into(log)
    );
    OA_CHECK(next.found().sentinel == rs::LeftoverSentinel::none);
    const rs::LeftoverOutcome outcome = next.resolve_leftovers();
    OA_CHECK(!outcome.unclean_exit && !outcome.change.changed);
    OA_CHECK(rs::acceleration_allowed(next.records(), "alpha", false));
    OA_CHECK(rs::find_driver(next.records(), "alpha")->strike.stage == rs::StrikeStage::present);
    OA_CHECK(log.lines.empty());
    // A clean exit with nothing to write writes nothing.
    std::filesystem::remove(records_file);
    next.set_sentinel(sentinel_of(rs::SentinelStage::create, "alpha"));
    next.clean_exit();
    OA_CHECK(!std::filesystem::exists(records_file));
    OA_CHECK(!std::filesystem::exists(sentinel_file));
}

void under_two_gib_what_a_larger_machine_left_stays() {
    Scratch scratch;
    LogLines log;
    const auto records_file = scratch.folder / rs::records_file_name;
    // A run with more memory crashed in its function test, and had kept an
    // accelerated-unusable record, a remembered rung and the native-density
    // key.
    preferences::save(
        records_file,
        {{"trial", "probe alpha"},
         {"accelerated-unusable.beta", "call unknown 0.6.2"},
         {"scale-level.beta", "2 unknown 0.6.2 0.450"},
         {"native-density", "gamma 0.6.2"}}
    );
    preferences::save(scratch.folder / rs::sentinel_file_name, {{"starting", "probe alpha"}});
    const rs::Values left = preferences::load(records_file);
    {
        rs::RendererState state = rs::RendererState::open_folder(
            scratch.folder, std::string(version), below_two_gib, log_into(log)
        );
        // The trial decides nothing, and the probe sentinel is only logged.
        const rs::LeftoverOutcome outcome = state.resolve_leftovers();
        OA_CHECK(outcome.unclean_exit && !outcome.change.changed);
        OA_CHECK(state.records().trial.has_value());
        OA_CHECK(state.write_records());
        // No trial is written: the stage it would cover is skipped.
        OA_CHECK(!state.write_trial(probe_trial("beta")));
        // The start-up stage passing and a clean exit leave the trial too.
        OA_CHECK(state.erase_trial());
        state.set_sentinel(sentinel_of(rs::SentinelStage::running, "beta"));
        state.clean_exit();
        OA_CHECK(state.records().trial.has_value());
        OA_CHECK(preferences::load(records_file) == left);
    }
    // With no sentinel left over, the trial is no unclean exit there.
    rs::RendererState quiet = rs::RendererState::open_folder(
        scratch.folder, std::string(version), below_two_gib, log_into(log)
    );
    OA_CHECK(!quiet.resolve_leftovers().unclean_exit);
    // A start from 2 GiB judges it.
    rs::RendererState later = rs::RendererState::open_folder(
        scratch.folder, std::string(version), first_counts, log_into(log)
    );
    const rs::LeftoverOutcome outcome = later.resolve_leftovers();
    OA_CHECK(outcome.unclean_exit && outcome.change.new_record && outcome.driver == "alpha");
    OA_CHECK(!later.records().trial);
    OA_CHECK(!rs::acceleration_allowed(later.records(), "alpha", false));
}

/// Checks that every write into an unwritable folder fails, is logged once
/// and leaves the records in memory.
///
/// @param folder the unwritable folder
void writes_fail_and_are_logged_once(const std::filesystem::path& folder) {
    LogLines log;
    rs::RendererState state =
        rs::RendererState::open_folder(folder, std::string(version), two_in_a_row, log_into(log));
    OA_CHECK(!state.found().records_unreadable);
    (void)rs::note_adapter(state.records(), "Example Graphics 3000");
    OA_CHECK(!state.write_records());
    OA_CHECK(!state.write_records());
    // The records stay in memory for the run.
    OA_CHECK(state.records().adapter == "Example Graphics 3000");
    // A trial that cannot be written is reported, so the caller skips the
    // stage, and it is not kept.
    OA_CHECK(!state.write_trial(probe_trial("alpha")));
    OA_CHECK(!state.records().trial);
    // The sentinel is kept in memory.
    state.set_sentinel(sentinel_of(rs::SentinelStage::create, "alpha"));
    state.set_sentinel(sentinel_of(rs::SentinelStage::standard, "alpha"));
    OA_CHECK(state.sentinel() && state.sentinel()->stage == rs::SentinelStage::standard);
    state.delete_sentinel();
    // One line for the records file and one for the sentinel's.
    OA_CHECK(log.lines.size() == 2);
    // A retry tries the trial again, and fails again without a new line.
    OA_CHECK(!state.write_trial(probe_trial("alpha")));
    OA_CHECK(log.lines.size() == 2);
}

void writes_that_fail_are_logged_once() {
    {
        // A file stands where the folder should be, so nothing can be made
        // in it on any system.
        Scratch scratch;
        const auto blocker = scratch.folder / "blocker";
        write_text(blocker, "a file, not a folder");
        writes_fail_and_are_logged_once(blocker / "profile");
    }
#ifndef _WIN32
    {
        // A read-only folder; the superuser writes into it anyway.
        Scratch scratch;
        const auto folder = scratch.folder / "read-only";
        std::filesystem::create_directories(folder);
        // A records file and a sentinel already there can still be read.
        preferences::save(
            folder / rs::records_file_name,
            {{"adapter", "Example Graphics 3000"}, {"trial", "probe alpha"}}
        );
        preferences::save(folder / rs::sentinel_file_name, {{"starting", "probe alpha"}});
        std::filesystem::permissions(
            folder,
            std::filesystem::perms::owner_write | std::filesystem::perms::group_write |
                std::filesystem::perms::others_write,
            std::filesystem::perm_options::remove
        );
        if (::geteuid() != 0) {
            LogLines log;
            rs::RendererState state = rs::RendererState::open_folder(
                folder, std::string(version), two_in_a_row, log_into(log)
            );
            OA_CHECK(state.found().sentinel == rs::LeftoverSentinel::read);
            OA_CHECK(state.records().trial.has_value());
            const rs::LeftoverOutcome outcome = state.resolve_leftovers();
            OA_CHECK(outcome.driver == "alpha" && outcome.change.changed);
            OA_CHECK(log.lines.size() == 1); // the unclean exit
            // The strike cannot be written: logged once, kept in memory.
            OA_CHECK(!state.write_records());
            OA_CHECK(!state.write_records());
            OA_CHECK(log.lines.size() == 2);
            OA_CHECK(state.records().drivers.size() == 1);
            OA_CHECK(!state.write_trial(probe_trial("alpha")));
            OA_CHECK(log.lines.size() == 2);
        }
        std::filesystem::permissions(
            folder, std::filesystem::perms::owner_all, std::filesystem::perm_options::add
        );
    }
#endif
}

void records_in_memory_and_disabled() {
    Scratch scratch;
    LogLines log;
    // In memory, as with a named --preferences-file: nothing reaches the
    // disk, a trial cannot fail and the sentinel is kept.
    rs::RendererState memory =
        rs::RendererState::in_memory(std::string(version), two_in_a_row, log_into(log));
    OA_CHECK(memory.storage() == rs::Storage::memory);
    (void)rs::note_adapter(memory.records(), "Example Graphics 3000");
    OA_CHECK(memory.write_records());
    OA_CHECK(memory.write_trial(probe_trial("alpha")));
    OA_CHECK(memory.records().trial.has_value());
    memory.set_sentinel(sentinel_of(rs::SentinelStage::probe, "alpha"));
    OA_CHECK(memory.sentinel().has_value());
    OA_CHECK(memory.erase_trial());
    OA_CHECK(!memory.records().trial);
    memory.delete_sentinel();
    OA_CHECK(!memory.sentinel());
    OA_CHECK(!memory.resolve_leftovers().unclean_exit);
    // A clean exit erases a trial that stands.
    OA_CHECK(memory.write_trial(probe_trial("alpha")));
    memory.set_sentinel(sentinel_of(rs::SentinelStage::accelerated, "alpha"));
    memory.clean_exit();
    OA_CHECK(!memory.records().trial && !memory.sentinel());
    // Under 2 GiB no trial is kept even in memory.
    rs::RendererState small =
        rs::RendererState::in_memory(std::string(version), below_two_gib, log_into(log));
    OA_CHECK(!small.write_trial(probe_trial("alpha")));
    OA_CHECK(!small.records().trial);

    // Disabled, as under SDL_RENDER_DRIVER: no sentinel and no trial, and a
    // trial still lets the stage run.
    rs::RendererState disabled = rs::RendererState::disabled();
    OA_CHECK(disabled.storage() == rs::Storage::disabled);
    disabled.set_sentinel(sentinel_of(rs::SentinelStage::create, "alpha"));
    OA_CHECK(!disabled.sentinel());
    OA_CHECK(disabled.write_trial(probe_trial("alpha")));
    OA_CHECK(!disabled.records().trial);
    OA_CHECK(disabled.write_records());
    OA_CHECK(log.lines.empty());
    size_t entries = 0;
    for ([[maybe_unused]] const auto& entry : std::filesystem::directory_iterator(scratch.folder))
        ++entries;
    OA_CHECK(entries == 0);
}

/// Simulates one start that crashes in the function test: it reads what the
/// last start left, applies it, writes the records, and, unless the records
/// keep the driver off the accelerated tier, writes the trial and the probe
/// sentinel and stops there.
///
/// @param folder the profile's folder
/// @param rules how the platform counts a left-over trial
/// @param log where log lines go
/// @return what the start made of what it found
rs::LeftoverOutcome start_that_crashes_in_the_test(
    const std::filesystem::path& folder, const rs::RecordRules& rules, LogLines& log
) {
    rs::RendererState state =
        rs::RendererState::open_folder(folder, std::string(version), rules, log_into(log));
    const rs::LeftoverOutcome outcome = state.resolve_leftovers();
    OA_CHECK(state.write_records());
    state.set_sentinel(sentinel_of(rs::SentinelStage::create, "alpha"));
    state.set_sentinel(sentinel_of(rs::SentinelStage::standard, "alpha"));
    if (rs::acceleration_allowed(state.records(), "alpha", false)) {
        OA_CHECK(state.write_trial(probe_trial("alpha")));
        state.set_sentinel(sentinel_of(rs::SentinelStage::probe, "alpha"));
    }
    return outcome; // the crash: nothing is deleted
}

void leftover_trials_across_starts() {
    struct Row {
        const char* name;
        rs::RecordRules rules;
        bool lose_sentinel; // a system crash took the unflushed sentinel with it
        bool record_at_first;
    };

    const Row rows[] = {
        {"two in a row", two_in_a_row, false, false},
        {"two in a row, sentinel lost", two_in_a_row, true, false},
        {"the first counts", first_counts, false, true},
        {"the first counts, sentinel lost", first_counts, true, true},
    };
    for (const Row& row : rows) {
        const int failures_before = oa::test::failed_checks();
        Scratch scratch;
        LogLines log;
        const auto sentinel_file = scratch.folder / rs::sentinel_file_name;
        const auto lose = [&] {
            if (row.lose_sentinel)
                write_text(sentinel_file, ""); // cut short, or never written
        };
        (void)start_that_crashes_in_the_test(scratch.folder, row.rules, log);
        lose();
        const rs::LeftoverOutcome second =
            start_that_crashes_in_the_test(scratch.folder, row.rules, log);
        OA_CHECK(second.driver == "alpha");
        OA_CHECK(second.change.new_record == row.record_at_first);
        lose();
        const rs::LeftoverOutcome third =
            start_that_crashes_in_the_test(scratch.folder, row.rules, log);
        // Where the first counts, the second start already kept the driver
        // standard, so the third finds only its standard sentinel left over:
        // a strike, and no new record.
        OA_CHECK(third.change.new_record == !row.record_at_first);
        rs::RendererState after = rs::RendererState::open_folder(
            scratch.folder, std::string(version), two_in_a_row, log_into(log)
        );
        OA_CHECK(!rs::acceleration_allowed(after.records(), "alpha", false));
        OA_CHECK(!rs::skips_driver(after.records(), "alpha"));
        OA_CHECK(rs::has_untold_record(after.records()));
        // Off then On gives the driver a fresh try.
        OA_CHECK(after.clear().changed);
        OA_CHECK(after.write_records());
        rs::RendererState retried = rs::RendererState::open_folder(
            scratch.folder, std::string(version), two_in_a_row, log_into(log)
        );
        OA_CHECK(rs::acceleration_allowed(retried.records(), "alpha", false));
        if (oa::test::failed_checks() != failures_before)
            std::fprintf(stderr, "in the row \"%s\"\n", row.name);
    }
}

} // namespace

int main() {
    try {
        a_folder_with_neither_file();
        records_are_written_only_when_one_changes();
        the_sentinel_is_rewritten_in_place_and_deleted();
        garbled_files_are_never_fatal();
        the_trial_carries_the_records_as_they_stand_in_the_file();
        records_from_a_match_are_written_when_it_ends();
        the_last_runs_strike_goes_with_the_trial();
        a_clean_exit_counts_nothing_against_the_driver();
        under_two_gib_what_a_larger_machine_left_stays();
        writes_that_fail_are_logged_once();
        records_in_memory_and_disabled();
        leftover_trials_across_starts();
    } catch (const std::exception& error) {
        std::fprintf(stderr, "renderer state test stopped: %s\n", error.what());
        return 1;
    }
    return oa::test::check_exit_status();
}
