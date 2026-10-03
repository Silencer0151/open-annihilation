// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/platform/locale.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <array>
#include <cstdlib>
#include <string>
#include <string_view>
#include <vector>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace oa::platform::locale {

namespace {

/// The variables that each name one locale, in the order they are read
/// after LANGUAGE's list.
constexpr std::array<const char*, 3> kSingleLocaleVariables{"LC_ALL", "LC_MESSAGES", "LANG"};

/// The variable that names a colon-separated list of languages.
constexpr const char* kLanguageListVariable = "LANGUAGE";

/// Adds a locale to a list unless it is empty, "C", "POSIX" or there already.
///
/// @param[in,out] locales the list
/// @param locale the locale
void add_locale(std::vector<std::string>& locales, std::string_view locale) {
    while (!locale.empty() && (locale.front() == ' ' || locale.front() == '\t'))
        locale.remove_prefix(1);
    while (!locale.empty() && (locale.back() == ' ' || locale.back() == '\t'))
        locale.remove_suffix(1);
    const std::string_view base = locale.substr(0, locale.find_first_of(".@"));
    if (base.empty() || base == "C" || base == "POSIX" || locales.size() >= most_locales)
        return;
    if (std::find(locales.begin(), locales.end(), locale) == locales.end())
        locales.emplace_back(locale);
}

/// Returns the system's own preferred locales, for when SDL gives none.
///
/// @return the locales, most preferred first; empty when the system names none
std::vector<std::string> system_locales() {
#if defined(_WIN32)
    // The user's interface language, as Windows 2000 and later give it.
    std::vector<std::string> locales;
    const LCID locale = MAKELCID(GetUserDefaultUILanguage(), SORT_DEFAULT);
    std::array<char, 16> language{};
    std::array<char, 16> country{};
    if (GetLocaleInfoA(
            locale, LOCALE_SISO639LANGNAME, language.data(), static_cast<int>(language.size())
        ) > 0) {
        std::string tag(language.data());
        if (GetLocaleInfoA(
                locale, LOCALE_SISO3166CTRYNAME, country.data(), static_cast<int>(country.size())
            ) > 0 &&
            country[0] != '\0')
            tag += "-" + std::string(country.data());
        add_locale(locales, tag);
    }
    return locales;
#else
    const EnvironmentReader environment{nullptr, [](void*, const char* name) -> const char* {
                                            return std::getenv(name);
                                        }};
    return environment_locales(environment);
#endif
}

} // namespace

std::vector<std::string> environment_locales(const EnvironmentReader& environment) {
    std::vector<std::string> locales;
    if (environment.read == nullptr)
        return locales;
    if (const char* list = environment.read(environment.context, kLanguageListVariable)) {
        std::string_view rest(list);
        while (!rest.empty()) {
            const auto colon = rest.find(':');
            add_locale(locales, rest.substr(0, colon));
            rest = colon == std::string_view::npos ? std::string_view{} : rest.substr(colon + 1);
        }
    }
    for (const char* variable : kSingleLocaleVariables)
        if (const char* value = environment.read(environment.context, variable))
            add_locale(locales, value);
    return locales;
}

std::vector<std::string> preferred_locales() {
    std::vector<std::string> locales;
    int count = 0;
    if (SDL_Locale** listed = SDL_GetPreferredLocales(&count)) {
        for (int index = 0; index < count && listed[index] != nullptr; ++index) {
            const SDL_Locale& entry = *listed[index];
            if (entry.language == nullptr)
                continue;
            std::string tag(entry.language);
            if (entry.country != nullptr && entry.country[0] != '\0')
                tag += "-" + std::string(entry.country);
            add_locale(locales, tag);
        }
        SDL_free(static_cast<void*>(listed));
    }
    if (locales.empty())
        locales = system_locales();
    return locales;
}

} // namespace oa::platform::locale
