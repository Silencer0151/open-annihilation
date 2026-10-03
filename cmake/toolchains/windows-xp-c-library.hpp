// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Included before every C++ source of a build for Windows XP whose
// cross-compiler was switched from the C library Windows 10 added to the one
// every Windows release includes (mingw-w64-common.cmake). The C++ library
// was built against the newer library's headers, and its <cstdlib> names
// quick_exit and at_quick_exit, which the older library's headers do not
// declare. They are declared here so that <cstdlib> compiles. Nothing defines
// them: a program that calls either fails to link, rather than importing a
// function Windows XP lacks.

#if defined(__cplusplus) && !defined(_UCRT)
extern "C" {
/// Registers a function for quick_exit to run; declared, never defined.
///
/// @param function the function to run
/// @return 0 when the function was registered
int __cdecl at_quick_exit(void(__cdecl* function)(void));

/// Ends the program after running what at_quick_exit registered; declared, never defined.
///
/// @param status the program's exit status
[[noreturn]] void __cdecl quick_exit(int status);
}
#endif
