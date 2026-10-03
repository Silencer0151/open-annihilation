// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/app/memory_guard.hpp"

namespace oa::app::render_policy {
namespace {

using platform::MemoryPressure;
using platform::SystemMemorySample;

/// Returns the thresholds the guard judges a sample by: its own, or, while
/// physical memory is not known, those of the physical memory the sample
/// reports.
///
/// @param guard the guard
/// @param sample the sample
/// @return the thresholds
MemoryGuardThresholds thresholds_for(const MemoryGuard& guard, const SystemMemorySample& sample) {
    if (guard.thresholds.physical == 0 && sample.physical != 0)
        return memory_guard_thresholds(sample.physical);
    return guard.thresholds;
}

/// Tells whether hard page faults ran above the threshold over an interval.
///
/// @param faults the hard page faults over the interval
/// @param interval_ns the interval's length, in nanoseconds; above 0
/// @return true when the faults exceed memory_guard_hard_faults_per_second
///     over the interval
bool hard_faults_above_threshold(uint64_t faults, uint64_t interval_ns) {
    // The faults the threshold allows over the interval, rounded down, split
    // so that no product can overflow.
    const uint64_t allowed =
        memory_guard_hard_faults_per_second * (interval_ns / nanoseconds_per_second) +
        memory_guard_hard_faults_per_second * (interval_ns % nanoseconds_per_second) /
            nanoseconds_per_second;
    return faults > allowed;
}

/// Takes the sample's hard page faults: the rate since the previous sample
/// that reported them, then the count to measure the next rate from. A count
/// that went back is taken afresh, with no rate.
///
/// @param[in,out] guard the guard
/// @param sample the sample
/// @param now_ns the sample's time
/// @return when the interval the rate was measured over began
uint64_t take_hard_faults(MemoryGuard& guard, const SystemMemorySample& sample, uint64_t now_ns) {
    const uint64_t interval_start_ns = guard.last_hard_faults_ns;
    if (!sample.hard_faults_known) {
        guard.hard_faults_seen = false;
        guard.hard_faults_high = false;
        return interval_start_ns;
    }
    guard.hard_faults_high =
        guard.hard_faults_seen && sample.hard_faults >= guard.last_hard_faults &&
        now_ns > guard.last_hard_faults_ns &&
        hard_faults_above_threshold(
            sample.hard_faults - guard.last_hard_faults, now_ns - guard.last_hard_faults_ns
        );
    guard.last_hard_faults = sample.hard_faults;
    guard.last_hard_faults_ns = now_ns;
    guard.hard_faults_seen = true;
    return interval_start_ns;
}

/// Judges free memory by the first sign the sample gives.
///
/// @param guard the guard, its hard page faults already taken from the sample
/// @param sample the sample
/// @param thresholds the thresholds to judge by
/// @return the sign that shows free memory low, or MemoryGuardCause::none
///     when free memory is not low or the sample gives no sign
MemoryGuardCause low_free_memory(
    const MemoryGuard& guard,
    const SystemMemorySample& sample,
    const MemoryGuardThresholds& thresholds
) {
    if (sample.available_known && thresholds.free_floor != 0)
        return sample.available < thresholds.free_floor ? MemoryGuardCause::free_memory
                                                        : MemoryGuardCause::none;
    if (sample.pressure != MemoryPressure::unknown)
        return sample.pressure >= memory_guard_low_pressure ? MemoryGuardCause::pressure
                                                            : MemoryGuardCause::none;
    if (sample.hard_faults_known && guard.hard_faults_high)
        return MemoryGuardCause::hard_faults;
    return MemoryGuardCause::none;
}

/// Forgets the spell of low free memory and the hard page faults.
///
/// @param[in,out] guard the guard
void forget_watch(MemoryGuard& guard) {
    guard.low_cause = MemoryGuardCause::none;
    guard.low_since_ns = 0;
    guard.hard_faults_seen = false;
    guard.hard_faults_high = false;
}

} // namespace

MemoryGuardThresholds memory_guard_thresholds(uint64_t physical) noexcept {
    MemoryGuardThresholds thresholds;
    thresholds.physical = physical;
    thresholds.committed_limit = physical / memory_guard_committed_divisor;
    thresholds.free_floor = physical / memory_guard_free_divisor;
    return thresholds;
}

MemoryGuard make_memory_guard(uint64_t physical) noexcept {
    MemoryGuard guard;
    guard.thresholds = memory_guard_thresholds(physical);
    return guard;
}

bool memory_guard_sample_due(const MemoryGuard& guard, uint64_t now_ns) noexcept {
    if (guard.tripped != MemoryGuardCause::none)
        return false;
    return !guard.sampled || now_ns < guard.last_sample_ns ||
           now_ns - guard.last_sample_ns >= memory_guard_sample_interval_ns;
}

MemoryGuardAction observe_memory(
    MemoryGuard& guard, const platform::SystemMemorySample& sample, uint64_t now_ns
) noexcept {
    if (guard.tripped != MemoryGuardCause::none)
        return MemoryGuardAction::drop_acceleration;
    guard.thresholds = thresholds_for(guard, sample);
    if (guard.sampled && now_ns < guard.last_sample_ns)
        forget_watch(guard);
    guard.sampled = true;
    guard.last_sample_ns = now_ns;

    if (sample.committed_known && guard.thresholds.committed_limit != 0 &&
        sample.committed > guard.thresholds.committed_limit) {
        guard.tripped = MemoryGuardCause::committed;
        return MemoryGuardAction::drop_acceleration;
    }

    const uint64_t faults_interval_start_ns = take_hard_faults(guard, sample, now_ns);
    const MemoryGuardCause low = low_free_memory(guard, sample, guard.thresholds);
    if (low == MemoryGuardCause::none) {
        guard.low_cause = MemoryGuardCause::none;
        return MemoryGuardAction::none;
    }
    if (guard.low_cause == MemoryGuardCause::none)
        // A rate of hard page faults covers the interval since the previous
        // count, so its spell begins there.
        guard.low_since_ns =
            low == MemoryGuardCause::hard_faults ? faults_interval_start_ns : now_ns;
    guard.low_cause = low;
    if (now_ns - guard.low_since_ns < memory_guard_low_hold_ns)
        return MemoryGuardAction::none;
    guard.tripped = low;
    return MemoryGuardAction::drop_acceleration;
}

void resume_memory_guard(MemoryGuard& guard) noexcept {
    guard.sampled = false;
    guard.last_sample_ns = 0;
    forget_watch(guard);
}

void retry_memory_guard(MemoryGuard& guard) noexcept {
    guard.tripped = MemoryGuardCause::none;
    resume_memory_guard(guard);
}

bool memory_guard_allows(
    const MemoryGuard& guard, const platform::SystemMemorySample& sample, uint64_t bytes
) noexcept {
    if (guard.tripped != MemoryGuardCause::none)
        return false;
    const MemoryGuardThresholds thresholds = thresholds_for(guard, sample);
    if (thresholds.physical == 0)
        return false;
    if (sample.committed_known && (bytes > thresholds.committed_limit ||
                                   sample.committed > thresholds.committed_limit - bytes))
        return false;
    if (sample.available_known)
        return bytes <= sample.available && sample.available - bytes >= thresholds.free_floor;
    if (sample.pressure != MemoryPressure::unknown)
        return sample.pressure < memory_guard_low_pressure;
    if (sample.hard_faults_known)
        return !guard.hard_faults_high;
    return false;
}

} // namespace oa::app::render_policy
