// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Each double operation rounds to a 53-bit significand on the program's
// first thread and on a thread the engine starts. Each floating-point setting
// the build keeps (the precision and rounding of 32-bit x86's older unit; the
// rounding, flush-to-zero and denormals-are-zero of the vector unit where the
// build computes with SSE; the rounding and flush-to-zero of ARM's unit) is
// found when changed and put back, while the flags that only record what
// operations raised are ignored; a guard reports the first change only.

#include "oa/base/float_precision.hpp"
#include "oa/base/threads.hpp"
#include "oa/test/check.hpp"

#include <cfloat>
#include <cstdio>

#if defined(__SSE__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 1)
#define OA_TEST_VECTOR_UNIT 1
#include <xmmintrin.h>
#endif

#if defined(__i386__) && (defined(__GNUC__) || defined(__clang__))
#define OA_TEST_OLDER_UNIT 1
#endif

#if defined(__aarch64__) || defined(_M_ARM64) || (defined(__arm__) && defined(__ARM_FP))
#define OA_TEST_ARM_UNIT 1
#endif

#if defined(_M_ARM64) && !defined(__aarch64__)
#define OA_TEST_ARM64_INTRINSICS 1
#include <intrin.h>
#endif

namespace {

namespace fp = oa::base::float_precision;

#if defined(OA_TEST_VECTOR_UNIT) && (defined(__x86_64__) || defined(_M_X64))
/// The vector unit's denormals-are-zero setting, which every x86-64
/// processor has; some 32-bit x86 processors with SSE fault on it.
constexpr unsigned int vector_denormals_are_zero = 0x0040U;
#define OA_TEST_DENORMALS_ARE_ZERO 1
#endif

#if defined(OA_TEST_OLDER_UNIT)
/// The older unit's precision field.
constexpr uint16_t older_precision_field = 0x0300U;
/// The precision field's value that keeps a 64-bit significand.
constexpr uint16_t older_extended_precision = 0x0300U;
/// The older unit's rounding-mode field.
constexpr uint16_t older_rounding_field = 0x0c00U;
/// The older unit's rounding mode toward positive infinity.
constexpr uint16_t older_round_upward = 0x0800U;

/// Returns the older unit's whole control word.
uint16_t read_older_word() {
    uint16_t word = 0;
    __asm__ volatile("fnstcw %0" : "=m"(word));
    return word;
}

/// Sets the older unit's whole control word.
///
/// @param word the word to set
void write_older_word(uint16_t word) {
    __asm__ volatile("fldcw %0" : : "m"(word) : "memory");
}
#endif

#if defined(OA_TEST_ARM_UNIT)
/// The ARM unit's rounding-mode field.
constexpr uint64_t arm_rounding_field = 0x00c00000U;
/// The ARM unit's rounding mode toward positive infinity.
constexpr uint64_t arm_round_upward = 0x00400000U;
/// The ARM unit's flush-to-zero setting.
constexpr uint64_t arm_flush_to_zero = 0x01000000U;
/// The ARM unit's settings these checks change.
constexpr uint64_t arm_changed_settings = arm_rounding_field | arm_flush_to_zero;

#if defined(__arm__)
/// The 32-bit ARM unit's flag recording an inexact result.
constexpr uint64_t arm_inexact_flag = 0x00000010U;
/// The 32-bit ARM unit's comparison flag recording an equal result.
constexpr uint64_t arm_zero_flag = 0x40000000U;
/// The raised flags these checks set.
constexpr uint64_t arm_set_flags = arm_inexact_flag | arm_zero_flag;
#endif

#if defined(OA_TEST_ARM64_INTRINSICS)
/// The ARM64 control register's number as the status-register intrinsics
/// take it: op0 3, op1 3, CRn 4, CRm 4, op2 0.
constexpr int arm64_control_register = 0x5a20;
#endif

/// Returns the ARM unit's whole state word.
uint64_t read_arm_word() {
#if defined(__aarch64__)
    uint64_t word = 0;
    __asm__ volatile("mrs %0, fpcr" : "=r"(word));
    return word;
#elif defined(OA_TEST_ARM64_INTRINSICS)
    return static_cast<uint64_t>(_ReadStatusReg(arm64_control_register));
#else
    uint32_t word = 0;
    __asm__ volatile("vmrs %0, fpscr" : "=r"(word));
    return word;
#endif
}

/// Sets the ARM unit's whole state word.
///
/// @param word the word to set
void write_arm_word(uint64_t word) {
#if defined(__aarch64__)
    __asm__ volatile("msr fpcr, %0" : : "r"(word) : "memory");
#elif defined(OA_TEST_ARM64_INTRINSICS)
    _WriteStatusReg(arm64_control_register, static_cast<long long>(word));
#else
    const auto narrow = static_cast<uint32_t>(word);
    __asm__ volatile("vmsr fpscr, %0" : : "r"(narrow) : "memory");
#endif
}
#endif

/// 1 + 2^-53 + 2^-53 in one expression. Rounded to a double at each step the
/// first sum is a tie that rounds to 1, and so is the second; kept to a
/// 64-bit significand between the steps it is 1 + 2^-52.
double sum_of_two_half_steps() {
    volatile double one = 1.0;
    volatile double half_step = 0x1p-53;
    const double first = one;
    const double step = half_step;
    return first + step + step;
}

/// 1 + 2^-60 as a double: 1 rounding to nearest, the next double up rounding upward.
double double_one_and_a_bit() {
    volatile double one = 1.0;
    volatile double bit = 0x1p-60;
    volatile double sum = one + bit;
    return sum;
}

/// 1 + 2^-30 as a float: 1 rounding to nearest, the next float up rounding upward.
float float_one_and_a_bit() {
    volatile float one = 1.0F;
    volatile float bit = 0x1p-30F;
    volatile float sum = one + bit;
    return sum;
}

/// Sets every floating-point unit the build keeps to round toward positive infinity.
void round_upward() {
#if defined(OA_TEST_OLDER_UNIT)
    write_older_word(
        static_cast<uint16_t>((read_older_word() & ~older_rounding_field) | older_round_upward)
    );
#endif
#if defined(OA_TEST_VECTOR_UNIT)
    _MM_SET_ROUNDING_MODE(_MM_ROUND_UP);
#endif
#if defined(OA_TEST_ARM_UNIT)
    write_arm_word((read_arm_word() & ~arm_rounding_field) | arm_round_upward);
#endif
}

/// What a guard's hook saw.
struct Reports {
    int count{};
    fp::FloatControl found{};
    fp::FloatControl saved{};
};

/// Counts a reported change and keeps what it found.
///
/// @param context the Reports to count in
/// @param found the settings the guard found
/// @param saved the settings it kept
void count_report(void* context, const fp::FloatControl& found, const fp::FloatControl& saved) {
    auto& reports = *static_cast<Reports*>(context);
    ++reports.count;
    reports.found = found;
    reports.saved = saved;
}

/// Returns whether two sets of settings are the same.
///
/// @param left one set
/// @param right the other
/// @return true when every field agrees
bool same(const fp::FloatControl& left, const fp::FloatControl& right) {
    return left.older_unit == right.older_unit && left.vector_unit == right.vector_unit &&
           left.arm_unit == right.arm_unit;
}

/// What the started thread found.
struct ThreadResult {
    bool rounds_to_double{};
    double sum{};
};

/// Measures the started thread's precision and its rounding of each step.
///
/// @param argument the ThreadResult to fill
void measure(void* argument) {
    auto* result = static_cast<ThreadResult*>(argument);
    result->rounds_to_double = fp::rounds_to_double();
    result->sum = sum_of_two_half_steps();
}

/// Checks the double precision of the first thread and of a started thread.
void double_precision() {
    OA_CHECK(fp::rounds_to_double());
    OA_CHECK(sum_of_two_half_steps() == 1.0);

    fp::use_double_precision();
    OA_CHECK(fp::rounds_to_double());

    ThreadResult result{};
    oa::base::threads::Thread thread{};
    OA_CHECK(oa::base::threads::start_thread(thread, measure, &result));
    oa::base::threads::join_thread(thread);
    OA_CHECK(result.rounds_to_double);
    OA_CHECK(result.sum == 1.0);
}

/// Checks that the program saved its settings at start and that they are unchanged.
void program_settings() {
    const auto& guard = fp::program_float_control();
    OA_CHECK(fp::float_control_matches(guard.saved));
    OA_CHECK(&guard == &fp::program_float_control());
    OA_CHECK(!guard.reported);
    OA_CHECK(!fp::restore_program_float_control());
    const auto saved = fp::save_float_control();
#if !defined(OA_TEST_OLDER_UNIT)
    OA_CHECK(saved.older_unit == 0);
#endif
#if !defined(OA_TEST_VECTOR_UNIT)
    OA_CHECK(saved.vector_unit == 0);
#endif
#if !defined(OA_TEST_ARM_UNIT)
    OA_CHECK(saved.arm_unit == 0);
#endif
    OA_CHECK(same(saved, guard.saved));
}

/// Checks that a rounding mode changed on every unit is found and put back,
/// and that the results round to nearest again.
void rounding_restored() {
    const auto saved = fp::save_float_control();
    OA_CHECK(double_one_and_a_bit() == 1.0);
    OA_CHECK(float_one_and_a_bit() == 1.0F);
#if defined(OA_TEST_OLDER_UNIT) || defined(OA_TEST_VECTOR_UNIT) || defined(OA_TEST_ARM_UNIT)
    round_upward();
    OA_CHECK(!fp::float_control_matches(saved));
    OA_CHECK(!same(fp::save_float_control(), saved));
    // Whichever unit computes them, both sums now round upward.
    OA_CHECK(double_one_and_a_bit() == 1.0 + 0x1p-52);
    OA_CHECK(float_one_and_a_bit() == 1.0F + 0x1p-23F);
    fp::restore_float_control(saved);
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK(same(fp::save_float_control(), saved));
    OA_CHECK(double_one_and_a_bit() == 1.0);
    OA_CHECK(float_one_and_a_bit() == 1.0F);
#endif
    fp::restore_float_control(saved);
}

/// Checks the older unit's precision on 32-bit x86: a wider one is found, and
/// the guard puts back a double's; and that the older unit's settings and the
/// vector unit's are kept apart.
void older_unit_restored() {
#if defined(OA_TEST_OLDER_UNIT)
    const auto saved = fp::save_float_control();
    write_older_word(static_cast<uint16_t>(read_older_word() | older_extended_precision));
    OA_CHECK((read_older_word() & older_precision_field) == older_extended_precision);
    OA_CHECK(!fp::rounds_to_double());
    OA_CHECK(!fp::float_control_matches(saved));
    OA_CHECK(fp::save_float_control().older_unit != saved.older_unit);
#if FLT_EVAL_METHOD == 2
    // Only where a double expression's intermediate results stay on the
    // older unit does its wider precision show in the result.
    OA_CHECK(sum_of_two_half_steps() != 1.0);
#endif
    fp::FloatControlGuard guard{saved};
    OA_CHECK(fp::restore_changed_float_control(guard));
    OA_CHECK(fp::rounds_to_double());
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK(sum_of_two_half_steps() == 1.0);

#if defined(OA_TEST_VECTOR_UNIT)
    // A change to one unit's rounding shows in its own field only.
    _MM_SET_ROUNDING_MODE(_MM_ROUND_UP);
    OA_CHECK(fp::save_float_control().older_unit == saved.older_unit);
    OA_CHECK(fp::save_float_control().vector_unit != saved.vector_unit);
    fp::restore_float_control(saved);
    write_older_word(
        static_cast<uint16_t>((read_older_word() & ~older_rounding_field) | older_round_upward)
    );
    OA_CHECK(fp::save_float_control().older_unit != saved.older_unit);
    OA_CHECK(fp::save_float_control().vector_unit == saved.vector_unit);
#endif
    fp::restore_float_control(saved);
#endif
}

/// Checks the vector unit's flush-to-zero and denormals-are-zero, and that
/// its exception flags are ignored and left as they are.
void vector_unit_restored() {
#if defined(OA_TEST_VECTOR_UNIT)
    const auto saved = fp::save_float_control();
    const unsigned int start = _mm_getcsr();

    _mm_setcsr(start | _MM_FLUSH_ZERO_ON);
    OA_CHECK(!fp::float_control_matches(saved));
    fp::restore_float_control(saved);
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK((_mm_getcsr() & _MM_FLUSH_ZERO_MASK) == (start & _MM_FLUSH_ZERO_MASK));

#if defined(OA_TEST_DENORMALS_ARE_ZERO)
    _mm_setcsr(start | vector_denormals_are_zero);
    OA_CHECK(!fp::float_control_matches(saved));
    fp::restore_float_control(saved);
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK((_mm_getcsr() & vector_denormals_are_zero) == (start & vector_denormals_are_zero));
#endif

    // A raised flag is no change, and putting the settings back keeps it.
    _mm_setcsr(start | _MM_EXCEPT_INEXACT);
    OA_CHECK(fp::float_control_matches(saved));
    _MM_SET_ROUNDING_MODE(_MM_ROUND_TOWARD_ZERO);
    fp::restore_float_control(saved);
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK((_mm_getcsr() & _MM_EXCEPT_INEXACT) != 0);
    OA_CHECK((_mm_getcsr() & _MM_ROUND_MASK) == (start & _MM_ROUND_MASK));
    _mm_setcsr(start);
#endif
}

/// Checks the ARM unit's flush-to-zero and rounding mode, and on 32-bit ARM
/// that its raised flags are ignored and left as they are.
void arm_unit_restored() {
#if defined(OA_TEST_ARM_UNIT)
    const auto saved = fp::save_float_control();
    const uint64_t start = read_arm_word();

    write_arm_word(start | arm_flush_to_zero);
    OA_CHECK(!fp::float_control_matches(saved));
    OA_CHECK(fp::save_float_control().arm_unit != saved.arm_unit);
    fp::restore_float_control(saved);
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK((read_arm_word() & arm_changed_settings) == (start & arm_changed_settings));

    write_arm_word((start & ~arm_rounding_field) | arm_round_upward);
    OA_CHECK(!fp::float_control_matches(saved));
    fp::FloatControlGuard guard{saved};
    OA_CHECK(fp::restore_changed_float_control(guard));
    OA_CHECK((read_arm_word() & arm_changed_settings) == (start & arm_changed_settings));

#if defined(__arm__)
    // On 32-bit ARM the raised flags share the settings' word: a raised flag
    // is no change, and putting the settings back keeps it.
    write_arm_word(start | arm_set_flags);
    OA_CHECK(fp::float_control_matches(saved));
    write_arm_word((read_arm_word() & ~arm_rounding_field) | arm_round_upward);
    OA_CHECK(!fp::float_control_matches(saved));
    fp::restore_float_control(saved);
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK((read_arm_word() & arm_changed_settings) == (start & arm_changed_settings));
    OA_CHECK((read_arm_word() & arm_set_flags) == arm_set_flags);
#endif
    write_arm_word(start);
#endif
}

/// Checks that a guard puts back each change, reports the first only, and
/// changes nothing while the settings are unchanged.
void guard_reports_once() {
    const auto saved = fp::save_float_control();
    Reports reports{};
    fp::FloatControlGuard guard{saved, {&reports, count_report}};

    OA_CHECK(!fp::restore_changed_float_control(guard));
    OA_CHECK(reports.count == 0);
    OA_CHECK(!guard.reported);
#if defined(OA_TEST_OLDER_UNIT) || defined(OA_TEST_VECTOR_UNIT) || defined(OA_TEST_ARM_UNIT)
    round_upward();
    const auto changed = fp::save_float_control();
    OA_CHECK(fp::restore_changed_float_control(guard));
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK(reports.count == 1);
    OA_CHECK(guard.reported);
    OA_CHECK(same(reports.found, changed));
    OA_CHECK(same(reports.saved, saved));

    OA_CHECK(!fp::restore_changed_float_control(guard));
    round_upward();
    OA_CHECK(fp::restore_changed_float_control(guard));
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK(reports.count == 1);

    // A guard with no hook puts the settings back all the same.
    fp::FloatControlGuard silent{saved};
    round_upward();
    OA_CHECK(fp::restore_changed_float_control(silent));
    OA_CHECK(fp::float_control_matches(saved));
    OA_CHECK(silent.reported);
#endif
    fp::restore_float_control(saved);
}

/// Checks that the program's guard puts back a change and reports it once.
void program_guard_reports_once() {
    auto& guard = fp::program_float_control();
    Reports reports{};
    guard.hooks = {&reports, count_report};
#if defined(OA_TEST_OLDER_UNIT) || defined(OA_TEST_VECTOR_UNIT) || defined(OA_TEST_ARM_UNIT)
    round_upward();
    OA_CHECK(fp::restore_program_float_control());
    OA_CHECK(fp::float_control_matches(guard.saved));
    round_upward();
    OA_CHECK(fp::restore_program_float_control());
    OA_CHECK(reports.count == 1);
#endif
    OA_CHECK(!fp::restore_program_float_control());
    guard.hooks = {};
}

} // namespace

int main() {
    program_settings();
    double_precision();
    rounding_restored();
    older_unit_restored();
    vector_unit_restored();
    arm_unit_restored();
    guard_reports_once();
    program_guard_reports_once();
    const int status = oa::test::check_exit_status();
    if (status == 0)
        std::puts("base-float-precision: ok");
    return status;
}
