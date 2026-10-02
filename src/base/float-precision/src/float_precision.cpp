// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/float_precision.hpp"

// The older unit is read and set through its own control word, which a 32-bit
// x86 compiler that takes inline assembly reaches directly. The C library's
// control-word functions on some systems read and set the vector unit's
// settings with it, and may drop the flags the vector unit has raised.
#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define OA_FLOAT_OLDER_UNIT 1
#endif

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define OA_FLOAT_VECTOR_UNIT 1
#include <xmmintrin.h>
#endif

// An ARM64 compiler without inline assembly reaches the control register
// through its status-register intrinsics.
#if defined(_M_ARM64) && !defined(__aarch64__)
#define OA_FLOAT_ARM64_INTRINSICS 1
#include <intrin.h>
#endif

namespace oa::base::float_precision {

namespace {

#if defined(OA_FLOAT_OLDER_UNIT)
/// The older unit's precision field.
constexpr uint16_t older_precision_field = 0x0300U;
/// The precision field's value that rounds each result to a double's 53-bit significand.
constexpr uint16_t older_double_precision = 0x0200U;
/// The older unit's rounding-mode field.
constexpr uint16_t older_rounding_field = 0x0c00U;
/// The older unit's six exception masks: invalid operation, denormal operand,
/// division by zero, overflow, underflow and inexact result.
constexpr uint16_t older_exception_masks = 0x003fU;
/// The older unit's settings kept: precision, rounding mode and every exception mask.
constexpr uint16_t older_unit_settings =
    older_precision_field | older_rounding_field | older_exception_masks;

/// Returns the older unit's whole control word.
uint16_t read_older_word() noexcept {
    uint16_t word = 0;
    __asm__ volatile("fnstcw %0" : "=m"(word));
    return word;
}

/// Sets the older unit's whole control word.
///
/// The unit's status, the vector unit and the flags either has raised are
/// left as they are.
///
/// @param word the control word to set
void write_older_word(uint16_t word) noexcept {
    __asm__ volatile("fldcw %0" : : "m"(word) : "memory");
}
#endif

#if defined(OA_FLOAT_VECTOR_UNIT)
/// The vector unit's flags that record which exceptions operations raised;
/// every other bit is a setting.
constexpr uint32_t vector_raised_flags = _MM_EXCEPT_MASK;
#endif

#if defined(__arm__) && defined(__ARM_FP)
/// The 32-bit ARM unit's comparison result flags.
constexpr uint32_t arm_comparison_flags = 0xf0000000U;
/// The 32-bit ARM unit's flag recording a saturated vector operation.
constexpr uint32_t arm_saturation_flag = 0x08000000U;
/// The 32-bit ARM unit's flags recording which exceptions operations raised.
constexpr uint32_t arm_exception_flags = 0x0000009fU;
/// The 32-bit ARM unit's flags that record results rather than set them;
/// every other bit is a setting.
constexpr uint32_t arm_raised_flags =
    arm_comparison_flags | arm_saturation_flag | arm_exception_flags;
#endif

#if defined(OA_FLOAT_ARM64_INTRINSICS)
/// The ARM64 control register's number as the status-register intrinsics
/// take it: op0 3, op1 3, CRn 4, CRm 4, op2 0.
constexpr int arm64_control_register = 0x5a20;
#endif

/// Returns the older unit's settings, or 0 where the build has none.
uint32_t read_older_unit() noexcept {
#if defined(OA_FLOAT_OLDER_UNIT)
    return read_older_word() & older_unit_settings;
#else
    return 0;
#endif
}

/// Sets the older unit's settings, leaving the rest of its state, and the
/// vector unit, as they are.
///
/// @param settings settings from read_older_unit
void write_older_unit([[maybe_unused]] uint32_t settings) noexcept {
#if defined(OA_FLOAT_OLDER_UNIT)
    write_older_word(
        static_cast<uint16_t>(
            (read_older_word() & ~older_unit_settings) | (settings & older_unit_settings)
        )
    );
#endif
}

/// Returns the vector unit's settings, or 0 where the build does not compute with it.
uint32_t read_vector_unit() noexcept {
#if defined(OA_FLOAT_VECTOR_UNIT)
    return static_cast<uint32_t>(_mm_getcsr()) & ~vector_raised_flags;
#else
    return 0;
#endif
}

/// Sets the vector unit's settings, leaving the flags operations raised as they are.
///
/// @param settings settings from read_vector_unit
void write_vector_unit([[maybe_unused]] uint32_t settings) noexcept {
#if defined(OA_FLOAT_VECTOR_UNIT)
    const auto raised = static_cast<uint32_t>(_mm_getcsr()) & vector_raised_flags;
    _mm_setcsr((settings & ~vector_raised_flags) | raised);
#endif
}

/// Returns the ARM unit's settings, or 0 where the build has none.
uint64_t read_arm_unit() noexcept {
#if defined(__aarch64__)
    uint64_t control = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(control));
    return control;
#elif defined(__arm__) && defined(__ARM_FP)
    uint32_t control = 0;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(control));
    return control & ~arm_raised_flags;
#elif defined(OA_FLOAT_ARM64_INTRINSICS)
    return static_cast<uint64_t>(_ReadStatusReg(arm64_control_register));
#else
    return 0;
#endif
}

/// Sets the ARM unit's settings, leaving the flags operations raised as they are.
///
/// @param settings settings from read_arm_unit
void write_arm_unit([[maybe_unused]] uint64_t settings) noexcept {
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" : : "r"(settings) : "memory");
#elif defined(__arm__) && defined(__ARM_FP)
    uint32_t control = 0;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(control));
    control = (control & arm_raised_flags) | (static_cast<uint32_t>(settings) & ~arm_raised_flags);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(control) : "memory");
#elif defined(OA_FLOAT_ARM64_INTRINSICS)
    _WriteStatusReg(arm64_control_register, static_cast<long long>(settings));
#endif
}

/// Returns whether two sets of settings are the same.
///
/// @param left one set of settings
/// @param right the other
/// @return true when every unit's settings agree
bool same_settings(const FloatControl& left, const FloatControl& right) noexcept {
    return left.older_unit == right.older_unit && left.vector_unit == right.vector_unit &&
           left.arm_unit == right.arm_unit;
}

} // namespace

void use_double_precision() noexcept {
#if defined(OA_FLOAT_OLDER_UNIT)
    write_older_word(
        static_cast<uint16_t>((read_older_word() & ~older_precision_field) | older_double_precision)
    );
#endif
}

bool rounds_to_double() noexcept {
#if defined(OA_FLOAT_OLDER_UNIT)
    return (read_older_word() & older_precision_field) == older_double_precision;
#else
    return true;
#endif
}

FloatControl save_float_control() noexcept {
    return {read_older_unit(), read_vector_unit(), read_arm_unit()};
}

bool float_control_matches(const FloatControl& saved) noexcept {
    return same_settings(save_float_control(), saved);
}

void restore_float_control(const FloatControl& saved) noexcept {
    write_older_unit(saved.older_unit);
    write_vector_unit(saved.vector_unit);
    write_arm_unit(saved.arm_unit);
}

bool restore_changed_float_control(FloatControlGuard& guard) noexcept {
    const FloatControl found = save_float_control();
    if (same_settings(found, guard.saved) && rounds_to_double())
        return false;
    restore_float_control(guard.saved);
    use_double_precision();
    if (!guard.reported) {
        guard.reported = true;
        if (guard.hooks.changed != nullptr)
            guard.hooks.changed(guard.hooks.context, found, guard.saved);
    }
    return true;
}

FloatControlGuard& program_float_control() noexcept {
    static FloatControlGuard guard{save_float_control()};
    return guard;
}

bool restore_program_float_control() noexcept {
    return restore_changed_float_control(program_float_control());
}

namespace {

#if defined(__GNUC__) || defined(__clang__)
/// Sets the first thread's precision before any static is initialised and
/// before main(); 101 is the earliest priority a program may use.
__attribute__((constructor(101))) void start_with_double_precision() {
    use_double_precision();
}
#endif

/// Saves the first thread's settings while the program starts, before main()
/// and after start_with_double_precision, which runs before every static.
[[maybe_unused]] const bool saved_at_start = (static_cast<void>(program_float_control()), true);

} // namespace

} // namespace oa::base::float_precision
