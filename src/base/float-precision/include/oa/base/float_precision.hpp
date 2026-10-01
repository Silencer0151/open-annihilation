// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The precision each double operation rounds to. On 32-bit x86 a double
// operation done on the processor's older floating-point unit keeps its
// result to the precision that unit is set to, which some C libraries start
// a program at 64 bits; the simulation needs each double result rounded to a
// 53-bit significand, as on every other processor (docs/development/conventions.md).

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
[[nodiscard]] bool rounds_to_double() noexcept;

} // namespace oa::base::float_precision
