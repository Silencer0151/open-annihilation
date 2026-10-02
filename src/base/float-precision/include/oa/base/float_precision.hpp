// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The precision each double operation rounds to, and the processor's other
// floating-point settings. On 32-bit x86 a double operation done on the
// processor's older floating-point unit keeps its result to the precision
// that unit is set to, which some C libraries start a program at 64 bits; the
// simulation needs each double result rounded to a 53-bit significand, as on
// every other processor (docs/development/conventions.md). Every processor
// also keeps a rounding mode, and most a flush-to-zero setting, that change
// what an operation gives. Code the program does not control, such as a
// graphics driver, may change them on the program's thread, so the program
// saves them as it starts and puts back any that changed.

#include <stdint.h>

namespace oa::base::float_precision {

/// Makes the calling thread round every double operation to a 53-bit significand.
///
/// On 32-bit x86 it sets the floating-point unit's precision to a double's,
/// leaving its rounding and exception settings as they are; elsewhere each
/// operation already rounds to its own type and it does nothing. Each engine
/// program calls it on its first thread before main() runs, and each thread
/// started through oa/base/threads.hpp calls it first.
void use_double_precision() noexcept;

/// Returns whether the calling thread rounds double operations to a 53-bit significand.
///
/// @return false only on 32-bit x86 while its floating-point unit keeps more
///         than a double's precision; true everywhere else
[[nodiscard]] bool rounds_to_double() noexcept;

/// The floating-point settings of one thread that decide what an operation gives.
///
/// Each field holds one unit's settings, without the flags that only record
/// what operations have raised, and is 0 where the build has no such unit.
struct FloatControl {
    /// On 32-bit x86, the older floating-point unit's precision, rounding
    /// mode and exception masks.
    uint32_t older_unit{};
    /// Where the build computes with SSE (x86-64, and a 32-bit x86 build that
    /// targets it), that unit's rounding mode, flush-to-zero,
    /// denormals-are-zero and exception masks.
    uint32_t vector_unit{};
    /// On ARM64, and on 32-bit ARM with a floating-point unit, its rounding
    /// mode, flush-to-zero, default-NaN and exception enables.
    uint64_t arm_unit{};
};

/// Reports a change to the settings a guard keeps.
struct FloatControlHooks {
    void* context{};
    /// Reports that the calling thread's settings differed from the saved
    /// ones and have been put back; null reports nothing.
    void (*changed)(void* context, const FloatControl& found, const FloatControl& saved){};
};

/// The settings a thread keeps, and whether a change to them has been reported.
struct FloatControlGuard {
    FloatControl saved{}; ///< the settings to keep
    FloatControlHooks hooks{};
    bool reported{}; ///< whether hooks.changed has been called
};

/// Returns the calling thread's floating-point settings.
///
/// @return the settings, without the flags operations have raised
[[nodiscard]] FloatControl save_float_control() noexcept;

/// Returns whether the calling thread's floating-point settings are the saved ones.
///
/// The flags that record what operations have raised are not compared.
///
/// @param saved settings from save_float_control
/// @return true when every setting is as saved
[[nodiscard]] bool float_control_matches(const FloatControl& saved) noexcept;

/// Sets the calling thread's floating-point settings to the saved ones.
///
/// The flags that record what operations have raised are left as they are.
///
/// @param saved settings from save_float_control
void restore_float_control(const FloatControl& saved) noexcept;

/// Puts back any floating-point setting of the calling thread that differs from the guard's.
///
/// Checks the settings against guard.saved, and the precision against a
/// double's (rounds_to_double). When any differs it restores them, calls
/// guard.hooks.changed the first time only and marks the guard reported;
/// when none differs it changes nothing.
///
/// @param[in,out] guard the settings to keep and whether a change was reported
/// @return true when a setting had changed and has been put back
bool restore_changed_float_control(FloatControlGuard& guard) noexcept;

/// Returns the guard holding the settings the program's first thread started with.
///
/// The settings are saved before main() runs, after use_double_precision.
/// Only the first thread checks against the guard or sets its hooks.
///
/// @return the program's guard, the same object on every call
[[nodiscard]] FloatControlGuard& program_float_control() noexcept;

/// Puts back any floating-point setting of the first thread that changed since the program started.
///
/// Calls restore_changed_float_control on program_float_control(), whose
/// hooks report the first change of the run. The game calls it on its first
/// thread after it creates a renderer, after the intro movies, after each
/// present, at the start of each match frame's drawing and before the
/// simulation ticks.
///
/// @return true when a setting had changed and has been put back
bool restore_program_float_control() noexcept;

} // namespace oa::base::float_precision
