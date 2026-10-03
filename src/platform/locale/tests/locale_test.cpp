// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The user's preferred locales: the locale environment variables read in
// their order, each locale once and "C" left out, and SDL's list, which a
// hint the test sets stands in for the system's settings.

#include "oa/platform/locale.hpp"
#include "oa/test/check.hpp"

#include <SDL3/SDL.h>

#include <map>
#include <string>
#include <vector>

namespace {

namespace locale = oa::platform::locale;

/// An environment of the test's own.
using Variables = std::map<std::string, std::string>;

/// Reads a variable from a test's environment.
///
/// @param context the Variables
/// @param name the variable
/// @return its value; null when it is not set
const char* read_variable(void* context, const char* name) {
    const auto& variables = *static_cast<const Variables*>(context);
    const auto found = variables.find(name);
    return found == variables.end() ? nullptr : found->second.c_str();
}

/// Returns the locales an environment names.
///
/// @param variables the environment
/// @return the locales
std::vector<std::string> locales_of(const Variables& variables) {
    const locale::EnvironmentReader reader{const_cast<Variables*>(&variables), read_variable};
    return locale::environment_locales(reader);
}

/// LANGUAGE's list comes first, then LC_ALL, LC_MESSAGES and LANG.
void environment_variables_are_read_in_their_order() {
    const Variables variables{
        {"LANGUAGE", "fr_CA:fr::en"},
        {"LC_MESSAGES", "de_DE.UTF-8"},
        {"LANG", "it_IT.UTF-8"},
    };
    OA_CHECK(
        locales_of(variables) ==
        std::vector<std::string>({"fr_CA", "fr", "en", "de_DE.UTF-8", "it_IT.UTF-8"})
    );
    const Variables all{{"LC_ALL", "es_ES"}, {"LC_MESSAGES", "de_DE"}, {"LANG", "es_ES"}};
    OA_CHECK(locales_of(all) == std::vector<std::string>({"es_ES", "de_DE"}));
}

/// "C", "POSIX" and empty values name no language.
void c_and_empty_locales_are_left_out() {
    const Variables variables{{"LANGUAGE", ""}, {"LC_ALL", "C.UTF-8"}, {"LANG", "POSIX"}};
    OA_CHECK(locales_of(variables).empty());
    OA_CHECK(locales_of({}).empty());
    OA_CHECK(locale::environment_locales({}).empty());
}

/// A long list keeps its first most_locales.
void long_lists_are_cut() {
    std::string list;
    for (int index = 0; index < 40; ++index)
        list += "x" + std::to_string(index) + ":";
    const auto locales = locales_of({{"LANGUAGE", list}});
    OA_CHECK(locales.size() == locale::most_locales);
    OA_CHECK(!locales.empty() && locales.front() == "x0");
}

/// SDL's list, here from its hint, comes as language and country joined by '-'.
void sdl_list_joins_language_and_country() {
    OA_CHECK(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "it_IT,es,de_AT"));
    OA_CHECK(locale::preferred_locales() == std::vector<std::string>({"it-IT", "es", "de-AT"}));
    OA_CHECK(SDL_SetHint(SDL_HINT_PREFERRED_LOCALES, "fr_CA"));
    OA_CHECK(locale::preferred_locales() == std::vector<std::string>({"fr-CA"}));
}

} // namespace

int main() {
    environment_variables_are_read_in_their_order();
    c_and_empty_locales_are_left_out();
    long_lists_are_cut();
    sdl_list_joins_language_and_country();
    return oa::test::check_exit_status();
}
