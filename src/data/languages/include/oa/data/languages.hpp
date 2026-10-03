// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The languages the game shows its text in: for each, its BCP-47 tag, its
// name in itself, the word 3.1c's game data knows it by, the languages its
// text falls back to and what drawing its text needs. The operating
// system's preferred locales and the player's choice in the settings are
// matched against them here; the text of each language comes from the game
// data through 3.1c's own keys (oa/data/defs/locale.hpp) and, for the Open
// Annihilation interface's own words, from the interface catalogue
// (oa/data/languages/interface_text.hpp). Nothing here reaches the
// simulation, a saved game or what a shared game sends.
//
// Adding a language is adding its entry to src/registry.inc. Where the game
// data holds the language's text under 3.1c's keys (Translate.tdf's
// "<Language> = ..." entries, a unit file's <Language>Name and
// <Language>Description, the <directory>-<Language> folders), nothing else
// changes: the settings offer it, the operating system's locale chooses it
// and every lookup reads it. A language whose letters the game cannot draw
// yet waits, unoffered, until the drawing it needs (TextNeeds) is built.
#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <string>
#include <string_view>

namespace oa::data::languages {

/// What drawing a language's text needs, beyond the game's own fonts.
enum class TextNeeds : uint8_t {
    /// Nothing more: every letter is in the game's 8-bit code page
    /// (Windows-1252), which the game's own GUI and FNT fonts draw. English,
    /// French, Italian, German and Spanish.
    game_fonts,
    /// The modern fonts (oa/platform/text_font.hpp), which hold the
    /// letters outside the code page, and game data that holds its text as
    /// UTF-8 (a mod profile's ui.text-rendering, unicode). Simplified
    /// Chinese is one: the bundled Noto Sans CJK SC cut holds GB 2312.
    modern_fonts,
    /// A modern font face the game does not bundle: Traditional Chinese and
    /// Japanese draw their characters in their own regional forms, which
    /// the Simplified Chinese face draws differently, and the bundled cut
    /// keeps only the commonest Traditional and Japanese characters and
    /// 2,350 Hangul syllables of Korean's 11,172.
    more_font_faces,
    /// Complex text shaping: letters that join, stack and change order, as
    /// Devanagari's do in Hindi, which the FreeType-only drawing cannot lay
    /// out. It needs a shaping library (HarfBuzz) and a face for the
    /// script.
    text_shaping,
};

/// One language the game knows.
struct Language {
    /// Its BCP-47 tag, as the settings keep it: "en", "fr", "zh-Hans".
    std::string_view tag{};
    /// Its name in itself, in UTF-8, as the settings list it: "Français".
    std::string_view endonym{};
    /// Its name in English, for logs and documents: "French".
    std::string_view english_name{};
    /// The word 3.1c's game data knows the language by: the key of its
    /// entries in Translate.tdf, the prefix of a unit file's Name and
    /// Description keys (GermanName) and the suffix of its language
    /// folders (bitmaps-German). English has one too, "English", which 3.1c
    /// runs in by default; its data holds no English entries, so English
    /// shows the data's own text, but a mod's may.
    std::string_view game_name{};
    /// The operating system's locales that choose it, each matched as a
    /// whole tag or as the leading subtags of one ("de" matches "de-AT").
    std::span<const std::string_view> locales{};
    /// The tags its text falls back to, in order, where the data has no
    /// text in it; English, the data's own text, comes last whether listed
    /// or not.
    std::span<const std::string_view> fallbacks{};
    /// What drawing its text needs.
    TextNeeds needs{TextNeeds::game_fonts};
};

/// The word the settings keep for the operating system's choice of language.
inline constexpr std::string_view system_choice = "system";

/// The most languages a fallback chain holds: a language, the fallbacks an
/// entry may list and English.
inline constexpr std::size_t most_chain_languages = 6;

/// The most bytes of a locale read: longer text is not a locale.
inline constexpr std::size_t most_locale_bytes = 64;

/// The languages a text is looked up in, in order: a language, its
/// fallbacks, then English.
struct FallbackChain {
    std::array<const Language*, most_chain_languages> languages{}; ///< the first `count` hold one
    std::size_t count{}; ///< 1 or more; the last is always English

    /// Returns the chain's languages.
    ///
    /// @return the first `count` entries of `languages`
    [[nodiscard]] std::span<const Language* const> view() const noexcept {
        return {languages.data(), count};
    }
};

/// Returns every language the game knows: English first, then the others
/// in the order of their own names, as the settings list them.
///
/// @return the languages; never empty
[[nodiscard]] std::span<const Language> known_languages() noexcept;

/// Returns English, the game data's own language.
///
/// @return English's entry
[[nodiscard]] const Language& english() noexcept;

/// Tells whether this build draws a language's text, and so offers it.
///
/// @param language the language
/// @return true for TextNeeds::game_fonts and TextNeeds::modern_fonts; the
///     other needs are not built yet
[[nodiscard]] bool drawable(const Language& language) noexcept;

/// Finds a known language by its tag, matched without regard to the case of
/// its letters, '_' read as '-'.
///
/// @param tag a BCP-47 tag
/// @return the language; null for a tag no entry has
[[nodiscard]] const Language* find_by_tag(std::string_view tag) noexcept;

/// Finds a known language by the word 3.1c's command line and game data
/// name it by ("german", "english"), or its English name, matched without
/// regard to the case of its letters.
///
/// @param name the word
/// @return the language; null for a word no entry has
[[nodiscard]] const Language* find_by_game_name(std::string_view name) noexcept;

/// Returns the chain a language's text is looked up in: the language, the
/// known fallbacks its entry lists, then English, each once.
///
/// @param language the language
/// @return the chain; English's is English alone
[[nodiscard]] FallbackChain fallback_chain(const Language& language) noexcept;

/// Writes a locale as the operating system gives it in BCP-47's form: the
/// encoding (".UTF-8") and modifier ("@euro") dropped and '_' read as '-'.
///
/// @param locale a locale, as "de_DE.UTF-8", "fr-CA" or "en"
/// @return the tag, as "de-DE"; empty for "C", "POSIX", text past
///     most_locale_bytes or text that is not a locale
[[nodiscard]] std::string normalised_locale(std::string_view locale);

/// Finds the known language a locale chooses: the entry with the longest
/// of its locales that is the locale's whole tag or its leading subtags.
/// Only drawable languages are chosen.
///
/// @param locale a locale, in any form normalised_locale reads
/// @return the language; null when none is chosen
[[nodiscard]] const Language* match_locale(std::string_view locale);

/// Finds the language the operating system's preferred locales choose: the
/// first locale, in the order given, that chooses a known language.
///
/// @param locales the preferred locales, most preferred first
/// @return the language; English when none chooses one
[[nodiscard]] const Language& preferred_language(std::span<const std::string> locales);

/// Returns the language a settings choice names.
///
/// @param choice system_choice, or a tag; anything else, a tag of a
///     language not drawable included, reads as system_choice
/// @param system the language the operating system chooses
/// @return the language
[[nodiscard]] const Language&
chosen_language(std::string_view choice, const Language& system) noexcept;

} // namespace oa::data::languages
