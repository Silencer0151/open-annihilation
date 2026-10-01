// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/base/float_precision.hpp"

#if defined(__i386__) && defined(_WIN32)
#include <float.h>
#elif defined(__i386__) && defined(__linux__)
#include <fpu_control.h>
#endif

namespace oa::base::float_precision {

void use_double_precision() noexcept {
#if defined(__i386__) && defined(_WIN32)
    (void)_controlfp(_PC_53, _MCW_PC);
#elif defined(__i386__) && defined(__linux__)
    fpu_control_t word{};
    _FPU_GETCW(word);
    word = static_cast<fpu_control_t>((word & ~_FPU_EXTENDED) | _FPU_DOUBLE);
    _FPU_SETCW(word);
#endif
}

bool rounds_to_double() noexcept {
#if defined(__i386__) && defined(_WIN32)
    return (_controlfp(0, 0) & _MCW_PC) == _PC_53;
#elif defined(__i386__) && defined(__linux__)
    fpu_control_t word{};
    _FPU_GETCW(word);
    return (word & _FPU_EXTENDED) == _FPU_DOUBLE;
#else
    return true;
#endif
}

namespace {

#if defined(__GNUC__) || defined(__clang__)
/// Sets the first thread's precision before any static is initialised and
/// before main(); 101 is the earliest priority a program may use.
__attribute__((constructor(101))) void start_with_double_precision() {
    use_double_precision();
}
#endif

} // namespace

} // namespace oa::base::float_precision
