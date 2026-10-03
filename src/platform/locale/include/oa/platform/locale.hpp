// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The languages the user prefers, as the operating system gives them: SDL's
// list of preferred locales, which reads the system's own settings on macOS,
// on Windows (Windows XP's user locale included) and on Linux, and where SDL
// gives none, the system's own query: on Windows the user's interface
// language, elsewhere the locale environment variables. Which of them the
// game can show is decided in oa/data/languages.hpp.
#pragma once

#include <cstddef>
#include <string>
#include <vector>

namespace oa::platform::locale {

/// Reads an environment variable.
struct EnvironmentReader {
    void* context{}; ///< passed back to read
    /// Returns a variable's value; null for one that is not set. Null reads
    /// every variable as unset.
    const char* (*read)(void* context, const char* name){};
};

/// The most locales a list keeps; the rest are dropped.
inline constexpr std::size_t most_locales = 16;

/// Returns the locales the locale environment variables name, most
/// preferred first: the colon-separated list LANGUAGE holds, then LC_ALL,
/// LC_MESSAGES and LANG, as programs that follow POSIX and GNU read them.
/// Each locale is listed once; empty entries, "C" and "POSIX" are left out.
///
/// @param environment the variables
/// @return the locales as the variables write them, as "de_DE.UTF-8"; at
///     most most_locales of them
[[nodiscard]] std::vector<std::string> environment_locales(const EnvironmentReader& environment);

/// Returns the user's preferred locales, most preferred first: SDL's list,
/// each as its language and country joined by '-' ("de-AT", or "de" without
/// a country), else the system's own query. SDL may be used before it is
/// initialised.
///
/// @return the locales; empty when the system names none
[[nodiscard]] std::vector<std::string> preferred_locales();

} // namespace oa::platform::locale
