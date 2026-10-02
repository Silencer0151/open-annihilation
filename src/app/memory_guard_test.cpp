// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The memory guard of the accelerated tier, by table: its thresholds scaled
// to physical memory; when a sample is due; scripted runs of samples that
// trip it or leave it be: committed memory above half of physical memory at
// once, free memory under a sixteenth of it for 3 s, the memory pressure and
// the rate of hard page faults standing in where free memory is not
// reported, spells that end, a clock that goes back, physical memory taken
// from a sample, and a tripped guard that stays tripped; resuming the
// watch; and the check before a new buffer of the tier's own. The samples
// are made up; the guard reads no system.

#include "oa/app/memory_guard.hpp"

#include "oa/test/check.hpp"

#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <limits>
#include <vector>

namespace {

using namespace oa::app::render_policy;
using oa::platform::MemoryPressure;
using oa::platform::SystemMemorySample;

constexpr uint64_t mebibyte = uint64_t{1024} * 1024;
constexpr uint64_t gibibyte = 1024 * mebibyte;

/// Nanoseconds in a millisecond, for writing the scripts' times.
constexpr uint64_t ns_per_ms = 1'000'000;

/// The physical memory of the scripted machine: committed memory above
/// 8 GiB trips the guard, and free memory under 1 GiB is low.
constexpr uint64_t machine_memory = 16 * gibibyte;
constexpr uint64_t machine_committed_limit = 8 * gibibyte;
constexpr uint64_t machine_free_floor = 1 * gibibyte;

/// A figure the sample does not report.
constexpr uint64_t unknown = std::numeric_limits<uint64_t>::max();

/// One sample of a script, each figure given or unknown.
struct Reading {
    uint64_t available{unknown};
    uint64_t committed{unknown};
    uint64_t hard_faults{unknown};
    MemoryPressure pressure{MemoryPressure::unknown};
    uint64_t physical{};
};

/// Builds the sample a reading describes.
///
/// @param reading the reading
/// @return the sample, its flags set for the figures given
SystemMemorySample sample_of(const Reading& reading) {
    SystemMemorySample sample;
    sample.physical = reading.physical;
    sample.pressure = reading.pressure;
    sample.available_known = reading.available != unknown;
    sample.available = sample.available_known ? reading.available : 0;
    sample.committed_known = reading.committed != unknown;
    sample.committed = sample.committed_known ? reading.committed : 0;
    sample.hard_faults_known = reading.hard_faults != unknown;
    sample.hard_faults = sample.hard_faults_known ? reading.hard_faults : 0;
    return sample;
}

/// A reading of free memory alone.
Reading free_memory(uint64_t available) {
    Reading reading;
    reading.available = available;
    return reading;
}

/// A reading of plenty of free memory and the given committed memory.
Reading committed_memory(uint64_t committed) {
    Reading reading;
    reading.available = 8 * gibibyte;
    reading.committed = committed;
    return reading;
}

/// A reading of the memory pressure alone.
Reading pressure(MemoryPressure level) {
    Reading reading;
    reading.pressure = level;
    return reading;
}

/// A reading of the hard page faults alone.
Reading hard_faults(uint64_t count) {
    Reading reading;
    reading.hard_faults = count;
    return reading;
}

/// A reading of plenty of memory.
Reading plenty() {
    return committed_memory(1 * gibibyte);
}

/// One step of a script: a sample at a time, and what the guard must ask.
struct Step {
    uint64_t ms{};
    Reading reading{};
    MemoryGuardAction expected{MemoryGuardAction::none};
};

/// A scripted run of samples.
struct Script {
    const char* name{};
    uint64_t physical{};
    std::vector<Step> steps{};
    MemoryGuardCause tripped{MemoryGuardCause::none}; ///< what tripped the guard at the end
};

constexpr MemoryGuardAction keep = MemoryGuardAction::none;
constexpr MemoryGuardAction drop = MemoryGuardAction::drop_acceleration;

/// Builds the steps of samples one second apart from 0 s, all asking `keep`
/// but the last, which asks `last`.
///
/// @param reading every sample's reading
/// @param count the samples
/// @param last what the last sample asks
std::vector<Step> every_second(const Reading& reading, uint64_t count, MemoryGuardAction last) {
    std::vector<Step> steps;
    for (uint64_t index = 0; index < count; ++index)
        steps.push_back({index * 1000, reading, index + 1 == count ? last : keep});
    return steps;
}

void test_thresholds() {
    struct Case {
        uint64_t physical;
        uint64_t committed_limit;
        uint64_t free_floor;
    };

    const Case cases[] = {
        {0, 0, 0},
        {256 * mebibyte, 128 * mebibyte, 16 * mebibyte},
        {1 * gibibyte, 512 * mebibyte, 64 * mebibyte},
        {2 * gibibyte, 1 * gibibyte, 128 * mebibyte},
        {machine_memory, machine_committed_limit, machine_free_floor},
        {100, 50, 6},
    };
    for (const Case& test : cases) {
        const MemoryGuardThresholds thresholds = memory_guard_thresholds(test.physical);
        OA_CHECK(thresholds.physical == test.physical);
        OA_CHECK(thresholds.committed_limit == test.committed_limit);
        OA_CHECK(thresholds.free_floor == test.free_floor);
        const MemoryGuard guard = make_memory_guard(test.physical);
        OA_CHECK(guard.thresholds.committed_limit == test.committed_limit);
        OA_CHECK(guard.tripped == MemoryGuardCause::none && !guard.sampled);
    }
}

void test_sample_due() {
    MemoryGuard guard = make_memory_guard(machine_memory);
    OA_CHECK(memory_guard_sample_due(guard, 0));
    OA_CHECK(memory_guard_sample_due(guard, 5000 * ns_per_ms));
    (void)observe_memory(guard, sample_of(plenty()), 5000 * ns_per_ms);
    OA_CHECK(!memory_guard_sample_due(guard, 5000 * ns_per_ms));
    OA_CHECK(!memory_guard_sample_due(guard, 5999 * ns_per_ms));
    OA_CHECK(memory_guard_sample_due(guard, 6000 * ns_per_ms));
    OA_CHECK(memory_guard_sample_due(guard, 60000 * ns_per_ms));
    OA_CHECK(memory_guard_sample_due(guard, 4000 * ns_per_ms)); // the clock went back

    // Resuming makes a sample due at once.
    resume_memory_guard(guard);
    OA_CHECK(memory_guard_sample_due(guard, 5001 * ns_per_ms));

    // A tripped guard asks for no more samples, even resumed.
    (void)observe_memory(guard, sample_of(committed_memory(9 * gibibyte)), 6000 * ns_per_ms);
    OA_CHECK(guard.tripped == MemoryGuardCause::committed);
    OA_CHECK(!memory_guard_sample_due(guard, 9000 * ns_per_ms));
    resume_memory_guard(guard);
    OA_CHECK(!memory_guard_sample_due(guard, 9000 * ns_per_ms));
}

void test_scripts() {
    Reading low_free = free_memory(machine_free_floor - 1);
    Reading low_free_with_plenty_committed = low_free;
    low_free_with_plenty_committed.committed = 1 * gibibyte;
    Reading critical_but_free = free_memory(4 * gibibyte);
    critical_but_free.pressure = MemoryPressure::critical;
    Reading normal_with_faults = pressure(MemoryPressure::normal);
    Reading physical_reported = committed_memory(9 * gibibyte);
    physical_reported.physical = machine_memory;
    Reading physical_unknown_low = committed_memory(unknown - 1);
    physical_unknown_low.available = 0;

    const std::vector<Script> scripts = {
        {"plenty of memory never trips", machine_memory, every_second(plenty(), 10, keep)},
        {"committed memory above half trips at once",
         machine_memory,
         {{0, committed_memory(machine_committed_limit + 1), drop}},
         MemoryGuardCause::committed},
        {"committed memory at half does not trip",
         machine_memory,
         every_second(committed_memory(machine_committed_limit), 5, keep)},
        {"committed memory rising past half trips on that sample",
         machine_memory,
         {{0, committed_memory(4 * gibibyte), keep},
          {1000, committed_memory(7 * gibibyte), keep},
          {2000, committed_memory(9 * gibibyte), drop}},
         MemoryGuardCause::committed},
        {"free memory under a sixteenth for 3 s trips",
         machine_memory,
         every_second(low_free_with_plenty_committed, 4, drop),
         MemoryGuardCause::free_memory},
        {"free memory under a sixteenth for 2 s does not trip",
         machine_memory,
         every_second(low_free, 3, keep)},
        {"free memory at a sixteenth is not low",
         machine_memory,
         every_second(free_memory(machine_free_floor), 6, keep)},
        {"a sample with free memory restarts the 3 s",
         machine_memory,
         {{0, low_free, keep},
          {1000, low_free, keep},
          {2000, low_free, keep},
          {3000, free_memory(machine_free_floor), keep},
          {4000, low_free, keep},
          {5000, low_free, keep},
          {6000, low_free, keep},
          {7000, low_free, drop}},
         MemoryGuardCause::free_memory},
        {"samples further apart still wait 3 s",
         machine_memory,
         {{0, low_free, keep},
          {2500, low_free, keep},
          {2999, low_free, keep},
          {3000, low_free, drop}},
         MemoryGuardCause::free_memory},
        {"a sample with no sign of free memory ends the spell",
         machine_memory,
         {{0, low_free, keep},
          {1000, low_free, keep},
          {2000, Reading{}, keep},
          {3000, low_free, keep},
          {5000, low_free, keep},
          {6000, low_free, drop}},
         MemoryGuardCause::free_memory},
        {"the clock going back restarts the spell",
         machine_memory,
         {{10000, low_free, keep},
          {12000, low_free, keep},
          {1000, low_free, keep},
          {3000, low_free, keep},
          {4000, low_free, drop}},
         MemoryGuardCause::free_memory},
        {"memory pressure at the warning level never trips",
         machine_memory,
         every_second(pressure(MemoryPressure::warning), 6, keep)},
        {"critical memory pressure for 3 s trips",
         machine_memory,
         every_second(pressure(MemoryPressure::critical), 4, drop),
         MemoryGuardCause::pressure},
        {"normal memory pressure never trips",
         machine_memory,
         every_second(pressure(MemoryPressure::normal), 6, keep)},
        {"free memory in bytes is judged before the pressure",
         machine_memory,
         every_second(critical_but_free, 6, keep)},
        {"a known pressure is judged before hard page faults",
         machine_memory,
         [&] {
             std::vector<Step> steps;
             for (uint64_t second = 0; second < 6; ++second) {
                 normal_with_faults.hard_faults = second * 10000;
                 steps.push_back({second * 1000, normal_with_faults, keep});
             }
             return steps;
         }()},
        {"hard page faults above the rate for 3 s trip",
         machine_memory,
         {{0, hard_faults(0), keep},
          {1000, hard_faults(1000), keep},
          {2000, hard_faults(2000), keep},
          {3000, hard_faults(3000), drop}},
         MemoryGuardCause::hard_faults},
        {"hard page faults at the rate are not high",
         machine_memory,
         [] {
             std::vector<Step> steps;
             for (uint64_t second = 0; second < 6; ++second)
                 steps.push_back(
                     {second * 1000,
                      hard_faults(second * memory_guard_hard_faults_per_second),
                      keep}
                 );
             return steps;
         }()},
        {"one fault over the rate across 1.5 s is high",
         machine_memory,
         {{0, hard_faults(0), keep},
          {1500, hard_faults(193), keep},
          {3000, hard_faults(386), drop}},
         MemoryGuardCause::hard_faults},
        {"a hard-fault count that goes back is taken afresh",
         machine_memory,
         {{0, hard_faults(0), keep},
          {1000, hard_faults(1000), keep},
          {2000, hard_faults(2000), keep},
          {3000, hard_faults(10), keep},
          {4000, hard_faults(2000), keep},
          {5000, hard_faults(4000), keep},
          {6000, hard_faults(6000), drop}},
         MemoryGuardCause::hard_faults},
        {"a sample without hard page faults forgets the count",
         machine_memory,
         {{0, hard_faults(0), keep},
          {1000, hard_faults(1000), keep},
          {2000, Reading{}, keep},
          {3000, hard_faults(3000), keep},
          {4000, hard_faults(4000), keep},
          {5000, hard_faults(5000), keep},
          {6000, hard_faults(6000), drop}},
         MemoryGuardCause::hard_faults},
        {"no figure at all never trips", machine_memory, every_second(Reading{}, 6, keep)},
        {"with physical memory unknown, bytes are never judged",
         0,
         every_second(physical_unknown_low, 6, keep)},
        {"with physical memory unknown, the pressure is still judged",
         0,
         every_second(pressure(MemoryPressure::critical), 4, drop),
         MemoryGuardCause::pressure},
        {"physical memory is taken from the first sample that reports it",
         0,
         {{0, committed_memory(9 * gibibyte), keep}, {1000, physical_reported, drop}},
         MemoryGuardCause::committed},
        {"a tripped guard stays tripped",
         machine_memory,
         {{0, committed_memory(9 * gibibyte), drop},
          {1000, plenty(), drop},
          {2000, plenty(), drop}},
         MemoryGuardCause::committed},
    };

    for (const Script& script : scripts) {
        MemoryGuard guard = make_memory_guard(script.physical);
        int step_number = 0;
        for (const Step& step : script.steps) {
            ++step_number;
            const MemoryGuardAction action =
                observe_memory(guard, sample_of(step.reading), step.ms * ns_per_ms);
            if (action != step.expected)
                std::fprintf(stderr, "script \"%s\", step %d:\n", script.name, step_number);
            OA_CHECK(action == step.expected);
        }
        if (guard.tripped != script.tripped)
            std::fprintf(stderr, "script \"%s\": tripped by the wrong cause\n", script.name);
        OA_CHECK(guard.tripped == script.tripped);
    }
}

void test_resume() {
    // Resuming ends a spell of low free memory, so the 3 s start again.
    const SystemMemorySample low = sample_of(free_memory(machine_free_floor - 1));
    MemoryGuard guard = make_memory_guard(machine_memory);
    OA_CHECK(observe_memory(guard, low, 0) == keep);
    OA_CHECK(observe_memory(guard, low, 2000 * ns_per_ms) == keep);
    resume_memory_guard(guard);
    OA_CHECK(!guard.sampled && guard.low_cause == MemoryGuardCause::none);
    OA_CHECK(observe_memory(guard, low, 3000 * ns_per_ms) == keep);
    OA_CHECK(observe_memory(guard, low, 5000 * ns_per_ms) == keep);
    OA_CHECK(observe_memory(guard, low, 6000 * ns_per_ms) == drop);

    // Resuming forgets the hard-fault count, so no rate spans the pause.
    MemoryGuard faults = make_memory_guard(machine_memory);
    OA_CHECK(observe_memory(faults, sample_of(hard_faults(0)), 0) == keep);
    resume_memory_guard(faults);
    OA_CHECK(observe_memory(faults, sample_of(hard_faults(100000)), 1000 * ns_per_ms) == keep);
    OA_CHECK(!faults.hard_faults_high);
}

void test_allows() {
    struct Case {
        const char* name;
        uint64_t physical;
        Reading reading;
        uint64_t bytes;
        bool expected;
    };

    Reading free_and_committed = free_memory(6 * gibibyte);
    free_and_committed.committed = 7 * gibibyte;
    Reading physical_reported = free_memory(2 * gibibyte);
    physical_reported.physical = machine_memory;
    Reading critical_but_free = free_memory(2 * gibibyte);
    critical_but_free.pressure = MemoryPressure::critical;
    Reading warning_and_low = free_memory(machine_free_floor + 32 * mebibyte);
    warning_and_low.pressure = MemoryPressure::warning;
    const Case cases[] = {
        {"free memory stays above the floor",
         machine_memory,
         free_memory(2 * gibibyte),
         512 * mebibyte,
         true},
        {"free memory left exactly at the floor",
         machine_memory,
         free_memory(2 * gibibyte),
         1 * gibibyte,
         true},
        {"free memory would fall under the floor",
         machine_memory,
         free_memory(2 * gibibyte),
         1 * gibibyte + 1,
         false},
        {"a buffer larger than free memory",
         machine_memory,
         free_memory(512 * mebibyte),
         1 * gibibyte,
         false},
        {"committed memory would pass half",
         machine_memory,
         free_and_committed,
         1 * gibibyte + 1,
         false},
        {"committed memory would reach half exactly",
         machine_memory,
         free_and_committed,
         1 * gibibyte,
         true},
        {"a buffer larger than the committed limit",
         machine_memory,
         free_and_committed,
         machine_committed_limit + 1,
         false},
        {"normal memory pressure",
         machine_memory,
         pressure(MemoryPressure::normal),
         64 * mebibyte,
         true},
        {"memory pressure at the warning level",
         machine_memory,
         pressure(MemoryPressure::warning),
         64 * mebibyte,
         true},
        {"critical memory pressure",
         machine_memory,
         pressure(MemoryPressure::critical),
         64 * mebibyte,
         false},
        {"free memory in bytes is judged before the pressure",
         machine_memory,
         critical_but_free,
         64 * mebibyte,
         true},
        {"free memory in bytes is judged with the pressure at warning",
         machine_memory,
         warning_and_low,
         64 * mebibyte,
         false},
        {"hard page faults not running high", machine_memory, hard_faults(10), 64 * mebibyte, true},
        {"no sign of free memory", machine_memory, Reading{}, 64 * mebibyte, false},
        {"physical memory unknown", 0, free_memory(8 * gibibyte), 64 * mebibyte, false},
        {"physical memory taken from the sample", 0, physical_reported, 1 * gibibyte, true},
    };
    for (const Case& test : cases) {
        const MemoryGuard guard = make_memory_guard(test.physical);
        const bool allowed = memory_guard_allows(guard, sample_of(test.reading), test.bytes);
        if (allowed != test.expected)
            std::fprintf(stderr, "case \"%s\":\n", test.name);
        OA_CHECK(allowed == test.expected);
    }

    // Hard page faults running high at the last sample refuse the buffer.
    MemoryGuard faults = make_memory_guard(machine_memory);
    (void)observe_memory(faults, sample_of(hard_faults(0)), 0);
    (void)observe_memory(faults, sample_of(hard_faults(1000)), 1000 * ns_per_ms);
    OA_CHECK(faults.hard_faults_high);
    OA_CHECK(!memory_guard_allows(faults, sample_of(hard_faults(1000)), 64 * mebibyte));

    // A tripped guard allows nothing.
    MemoryGuard tripped = make_memory_guard(machine_memory);
    (void)observe_memory(tripped, sample_of(committed_memory(9 * gibibyte)), 0);
    OA_CHECK(!memory_guard_allows(tripped, sample_of(free_memory(8 * gibibyte)), 1));
}

} // namespace

int main() {
    test_thresholds();
    test_sample_due();
    test_scripts();
    test_resume();
    test_allows();
    return oa::test::check_exit_status();
}
