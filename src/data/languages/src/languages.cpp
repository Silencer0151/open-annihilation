// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/languages.hpp"

#include <algorithm>
#include <array>
#include <string>
#include <string_view>

namespace oa::data::languages {

namespace {

#include "registry.inc"

static_assert(!kLanguages.empty() && kLanguages[0].tag == "en", "English comes first");

/// Lowers an ASCII letter; any other byte is kept.
///
/// @param letter the byte
/// @return the byte, A to Z lowered
constexpr char lower(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

/// Tells whether two texts are the same, ignoring the case of ASCII
/// letters and reading '_' as '-'.
///
/// @param left a text
/// @param right a text
/// @return true when they match
bool same_tag(std::string_view left, std::string_view right) noexcept {
    const auto fold = [](char letter) { return letter == '_' ? '-' : lower(letter); };
    return left.size() == right.size() &&
           std::equal(left.begin(), left.end(), right.begin(), [&](char a, char b) {
               return fold(a) == fold(b);
           });
}

/// Tells whether a pattern is a whole tag or its leading subtags.
///
/// @param tag a normalised tag, as "de-AT"
/// @param pattern an entry's locale, as "de"
/// @return true when the pattern is the tag, or the tag's start ending at a '-'
bool leads(std::string_view tag, std::string_view pattern) noexcept {
    if (pattern.empty() || pattern.size() > tag.size())
        return false;
    if (!same_tag(tag.substr(0, pattern.size()), pattern))
        return false;
    return pattern.size() == tag.size() || tag[pattern.size()] == '-';
}

/// Tells whether a byte may stand in a locale's tag.
///
/// @param letter the byte
/// @return true for ASCII letters, digits, '-' and '_'
constexpr bool tag_character(char letter) noexcept {
    return (letter >= 'a' && letter <= 'z') || (letter >= 'A' && letter <= 'Z') ||
           (letter >= '0' && letter <= '9') || letter == '-' || letter == '_';
}

} // namespace

std::span<const Language> known_languages() noexcept {
    return kLanguages;
}

const Language& english() noexcept {
    return kLanguages[0];
}

bool drawable(const Language& language) noexcept {
    return language.needs == TextNeeds::game_fonts || language.needs == TextNeeds::modern_fonts;
}

bool turns_unicode_chat_on(const Language& language, bool pack_asks) noexcept {
    return pack_asks || language.needs == TextNeeds::modern_fonts;
}

const Language* find_by_tag(std::string_view tag) noexcept {
    for (const Language& language : kLanguages)
        if (same_tag(language.tag, tag))
            return &language;
    return nullptr;
}

const Language* find_by_game_name(std::string_view name) noexcept {
    if (name.empty())
        return nullptr;
    for (const Language& language : kLanguages)
        if (same_tag(language.game_name, name) || same_tag(language.english_name, name))
            return &language;
    return nullptr;
}

FallbackChain fallback_chain(const Language& language) noexcept {
    FallbackChain chain{};
    const auto add = [&chain](const Language* entry) {
        if (entry == nullptr || chain.count >= chain.languages.size())
            return;
        const auto held = chain.view();
        if (std::find(held.begin(), held.end(), entry) == held.end())
            chain.languages[chain.count++] = entry;
    };
    add(&language);
    // Room is kept for English, which ends every chain.
    for (const std::string_view tag : language.fallbacks)
        if (chain.count + 1 < chain.languages.size())
            add(find_by_tag(tag));
    const Language* last = &english();
    const auto held = chain.view();
    if (std::find(held.begin(), held.end(), last) != held.end()) {
        // English listed among the fallbacks moves to the end.
        std::size_t kept = 0;
        for (std::size_t index = 0; index < chain.count; ++index)
            if (chain.languages[index] != last)
                chain.languages[kept++] = chain.languages[index];
        chain.count = kept;
    }
    chain.languages[chain.count++] = last;
    return chain;
}

std::string normalised_locale(std::string_view locale) {
    if (locale.size() > most_locale_bytes)
        return {};
    // The encoding and the modifier go: "de_DE.UTF-8@euro" is "de_DE".
    locale = locale.substr(0, locale.find_first_of(".@"));
    while (!locale.empty() && (locale.front() == ' ' || locale.front() == '\t'))
        locale.remove_prefix(1);
    while (!locale.empty() && (locale.back() == ' ' || locale.back() == '\t'))
        locale.remove_suffix(1);
    if (locale.empty() || locale == "C" || locale == "POSIX")
        return {};
    if (!std::all_of(locale.begin(), locale.end(), tag_character) || locale.front() == '-' ||
        locale.front() == '_')
        return {};
    std::string tag(locale);
    std::replace(tag.begin(), tag.end(), '_', '-');
    return tag;
}

const Language* match_locale(std::string_view locale) {
    const std::string tag = normalised_locale(locale);
    if (tag.empty())
        return nullptr;
    const Language* best = nullptr;
    std::size_t best_length = 0;
    // A longer match among the languages not offered chooses none.
    for (const std::string_view pattern : kUnofferedLocales)
        if (pattern.size() > best_length && leads(tag, pattern))
            best_length = pattern.size();
    for (const Language& language : kLanguages) {
        if (!drawable(language))
            continue;
        for (const std::string_view pattern : language.locales)
            if (pattern.size() > best_length && leads(tag, pattern)) {
                best = &language;
                best_length = pattern.size();
            }
    }
    return best;
}

const Language& preferred_language(std::span<const std::string> locales) {
    for (const std::string& locale : locales)
        if (const Language* language = match_locale(locale))
            return *language;
    return english();
}

const Language& chosen_language(std::string_view choice, const Language& system) noexcept {
    if (const Language* language = find_by_tag(choice); language != nullptr && drawable(*language))
        return *language;
    return system;
}

} // namespace oa::data::languages
