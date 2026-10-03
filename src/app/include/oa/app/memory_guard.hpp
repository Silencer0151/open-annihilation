// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The memory guard of the accelerated tier, as a pure state machine. On
// every machine running the accelerated tier, the host samples the system's
// memory about once a second (oa::platform::sample_system_memory) and hands
// each sample to the guard with the time. The guard asks the tier to drop
// acceleration for the rest of the run when the process's committed memory
// rises above its threshold, or when free physical memory stays below its
// threshold for 3 s; where the system reports no free memory, its memory
// pressure, or else the process's hard page faults, stands in for it. Both
// thresholds scale with physical memory, and the guard also tells the tier
// whether a new buffer of its own would leave free memory above the
// threshold. The constants are conservative, chosen without a measurement
// on period hardware. The guard reads no clock and calls no
// system: the accelerated tier's watch (runtime_tier_watch.cpp) samples the
// system and hands it each sample.
#pragma once

#include "oa/platform/memory_status.hpp"

#include <cstdint>

namespace oa::app::render_policy {

/// Time between two samples, in nanoseconds.
inline constexpr uint64_t memory_guard_sample_interval_ns = 1'000'000'000;

/// How long free memory must stay low before the guard trips, in
/// nanoseconds.
inline constexpr uint64_t memory_guard_low_hold_ns = 3'000'000'000;

/// Committed memory above physical memory divided by this trips the guard:
/// above half of it. A conservative figure, chosen without measurement.
inline constexpr uint64_t memory_guard_committed_divisor = 2;

/// Free physical memory below physical memory divided by this is low: under
/// a sixteenth of it. A conservative figure, chosen without measurement.
inline constexpr uint64_t memory_guard_free_divisor = 16;

/// Hard page faults a second above which free memory counts as low, where
/// the system reports neither free memory nor its memory pressure. A
/// conservative figure, chosen without measurement.
inline constexpr uint64_t memory_guard_hard_faults_per_second = 128;

/// Nanoseconds in a second, for the rate of hard page faults.
inline constexpr uint64_t nanoseconds_per_second = 1'000'000'000;

/// The least memory pressure that counts as low free memory, where the
/// system reports its pressure instead of free memory. The warning level
/// does not count: systems report it under ordinary load, with far more than
/// a sixteenth of physical memory free.
inline constexpr platform::MemoryPressure memory_guard_low_pressure =
    platform::MemoryPressure::critical;

/// What the guard asks of the tier.
enum class MemoryGuardAction : uint8_t {
    none, ///< carry on
    /// Drop acceleration for the rest of the run, freeing what only the
    /// accelerated tier made. Nothing is recorded for the next start, and
    /// switching the setting Off then On does not lift it.
    drop_acceleration,
};

/// What tripped the guard.
enum class MemoryGuardCause : uint8_t {
    none,        ///< it has not tripped
    committed,   ///< committed memory rose above its threshold
    free_memory, ///< free physical memory stayed below its threshold
    pressure,    ///< memory pressure stayed at memory_guard_low_pressure or above
    hard_faults, ///< hard page faults stayed above memory_guard_hard_faults_per_second
};

/// The guard's thresholds, scaled to physical memory.
struct MemoryGuardThresholds {
    uint64_t physical{}; ///< the physical memory they are scaled from, in bytes; 0 when not known
    /// Committed memory above this trips the guard, in bytes; 0 when
    /// physical memory is not known, and then committed memory is not judged.
    uint64_t committed_limit{};
    /// Free physical memory below this is low, in bytes; 0 when physical
    /// memory is not known, and then free memory is not judged in bytes.
    uint64_t free_floor{};
};

/// The guard's state between samples, its times in nanoseconds on a steady
/// clock. Make it with make_memory_guard.
struct MemoryGuard {
    MemoryGuardThresholds thresholds{};
    uint64_t last_sample_ns{};      ///< when the last sample was taken
    uint64_t low_since_ns{};        ///< when the current spell of low free memory began
    uint64_t last_hard_faults{};    ///< the hard page faults of the last sample that reported them
    uint64_t last_hard_faults_ns{}; ///< when that sample was taken
    /// The sign that showed free memory low at the last sample; none outside
    /// a spell of low free memory.
    MemoryGuardCause low_cause{MemoryGuardCause::none};
    /// What tripped the guard; none until it trips. A tripped guard stays
    /// tripped for the rest of the run.
    MemoryGuardCause tripped{MemoryGuardCause::none};
    bool sampled{};          ///< a sample was taken since the guard was made or resumed
    bool hard_faults_seen{}; ///< last_hard_faults holds a count
    /// Hard page faults ran above memory_guard_hard_faults_per_second between
    /// the last two samples that reported them.
    bool hard_faults_high{};
};

/// Scales the guard's thresholds to physical memory.
///
/// @param physical installed physical memory, in bytes; 0 when not known
/// @return committed memory above half of it trips the guard, and free
///     memory under a sixteenth of it is low; both 0 when it is not known
[[nodiscard]] MemoryGuardThresholds memory_guard_thresholds(uint64_t physical) noexcept;

/// Makes a guard that has taken no sample and has not tripped.
///
/// When physical memory is not known, the guard takes it from the first
/// sample that reports it.
///
/// @param physical installed physical memory, in bytes, as the machine's
///     traits report it; 0 when not known
/// @return the guard
[[nodiscard]] MemoryGuard make_memory_guard(uint64_t physical) noexcept;

/// Tells whether the host should sample the system's memory and hand the
/// sample to observe_memory.
///
/// @param guard the guard
/// @param now_ns the time, in nanoseconds on a steady clock
/// @return true when the guard has taken no sample since it was made or
///     resumed, when memory_guard_sample_interval_ns has passed since the
///     last, or when the clock went back; false once the guard has tripped
[[nodiscard]] bool memory_guard_sample_due(const MemoryGuard& guard, uint64_t now_ns) noexcept;

/// Judges one sample of the system's memory.
///
/// Committed memory above its threshold trips the guard at once. Free
/// physical memory below its threshold trips it once every sample has found
/// it low for memory_guard_low_hold_ns; a sample that finds it not low ends
/// the spell. Free memory is judged by the first sign the sample gives:
/// free physical memory in bytes (when physical memory is known), then the
/// memory pressure (low at memory_guard_low_pressure or above), then the
/// rate of hard page faults since the previous sample that reported them
/// (low above memory_guard_hard_faults_per_second, the spell counted from
/// that previous sample). A sample with none of them ends the spell. A
/// clock that went back since the last sample ends the spell and forgets the
/// hard page faults.
///
/// @param[in,out] guard the guard; it remembers the sample's time, the hard
///     page faults and the spell, and what tripped it
/// @param sample the system's memory, as oa::platform::sample_system_memory
///     reports it
/// @param now_ns the time the sample was taken, in nanoseconds on a steady
///     clock
/// @return MemoryGuardAction::drop_acceleration on the sample that trips the
///     guard and on every sample after it; MemoryGuardAction::none otherwise
MemoryGuardAction observe_memory(
    MemoryGuard& guard, const platform::SystemMemorySample& sample, uint64_t now_ns
) noexcept;

/// Starts the guard's watch afresh, as when the accelerated tier resumes
/// after a pause: the next sample is due at once, and the spell of low free
/// memory and the hard page faults are forgotten. A tripped guard stays
/// tripped.
///
/// @param[in,out] guard the guard
void resume_memory_guard(MemoryGuard& guard) noexcept;

/// Lets a tripped guard judge again, as when the Full tier it tripped
/// against has freed its pages and targets and the Basic tier draws on:
/// the trip is forgotten and the watch starts afresh (resume_memory_guard),
/// so that the tier left is judged on the memory then free, and trips the
/// guard again, for good, when memory stays short.
///
/// @param[in,out] guard the guard
void retry_memory_guard(MemoryGuard& guard) noexcept;

/// Tells whether the accelerated tier may make a new buffer of its own (the
/// scene, the overlay or a prescale target) without tripping the guard: free
/// physical memory stays at or above its threshold after it, and committed
/// memory stays at or under its threshold. Where the sample gives no free
/// memory in bytes, the memory pressure must be below
/// memory_guard_low_pressure, or else the hard page faults must not have
/// been running high at the last sample.
///
/// @param guard the guard
/// @param sample the system's memory, sampled just before
/// @param bytes the size of the new buffer, in bytes
/// @return false when the guard has tripped, when physical memory is not
///     known, when the sample gives no sign of free memory, or when the
///     buffer would bring either figure past its threshold
[[nodiscard]] bool memory_guard_allows(
    const MemoryGuard& guard, const platform::SystemMemorySample& sample, uint64_t bytes
) noexcept;

} // namespace oa::app::render_policy
