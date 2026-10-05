// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The languages the game knows, included by languages.cpp: English first,
// then the others in the order of their own names, which is the order the
// settings list them in.
//
// Adding a language is adding its locales and its entry here:
//
// - tag: its BCP-47 tag, which the settings keep;
// - endonym: its name in itself, in UTF-8 written as escaped bytes;
// - english_name: its name in English;
// - game_name: the word the game data knows it by, which keys its text in
//   Translate.tdf, prefixes a unit file's Name and Description keys and
//   names its language folders (bitmaps-<word>, CAMPS\Briefs-<word>), and
//   which 3.1c's command line names it by;
// - locales: the operating system's locale tags that choose it;
// - fallbacks: the tags its text falls back to before English;
// - needs: what drawing its text needs (TextNeeds).
//
// The game data of 3.1c holds German, French, Italian and Spanish.
// Simplified Chinese draws in the modern fonts, its text in UTF-8, from the
// game data under its word or from a language pack (language_pack.hpp).
// Among the languages a later entry might add:
//
// - Portuguese ("pt", Português) or Dutch ("nl", Nederlands): every
//   letter is in the game's code page, so TextNeeds::game_fonts; game data
//   that holds the language under its word is all they need.
// - Traditional Chinese ("zh-Hant", locales zh-Hant, zh-TW, zh-HK, zh-MO)
//   and Japanese ("ja"): TextNeeds::more_font_faces, for the Traditional
//   Chinese and Japanese faces of Noto Sans CJK. Until Traditional Chinese
//   has an entry, its locales are kUnofferedLocales.
// - Korean ("ko"): TextNeeds::more_font_faces while the cut CJK face keeps
//   only KS X 1001's syllables.
// - Hindi ("hi"): TextNeeds::text_shaping, for Devanagari's shaping and a
//   face that holds it.
//
// An entry whose needs this build cannot meet is kept but not offered
// (drawable).

/// The locales that choose English.
constexpr std::array<std::string_view, 1> kEnglishLocales{"en"};
/// The locales that choose German.
constexpr std::array<std::string_view, 1> kGermanLocales{"de"};
/// The locales that choose Spanish.
constexpr std::array<std::string_view, 1> kSpanishLocales{"es"};
/// The locales that choose French.
constexpr std::array<std::string_view, 1> kFrenchLocales{"fr"};
/// The locales that choose Italian.
constexpr std::array<std::string_view, 1> kItalianLocales{"it"};
/// The locales that choose Simplified Chinese: its script, the places that
/// write it, and Chinese with neither named.
constexpr std::array<std::string_view, 5> kSimplifiedChineseLocales{
    "zh-Hans", "zh-CN", "zh-SG", "zh-MY", "zh"
};

/// The locales of languages the game does not offer yet, which choose no
/// language: Traditional Chinese's script and the places that write it. A
/// locale whose longest match is one of these chooses none, so that
/// "zh-TW" or "zh-Hant-HK" never reaches "zh" and Simplified Chinese.
constexpr std::array<std::string_view, 4> kUnofferedLocales{"zh-Hant", "zh-TW", "zh-HK", "zh-MO"};

/// Every language the game knows.
constexpr std::array<Language, 6> kLanguages{{
    {"en", "English", "English", "English", kEnglishLocales, {}, TextNeeds::game_fonts},
    {"de", "Deutsch", "German", "German", kGermanLocales, {}, TextNeeds::game_fonts},
    // Español
    {"es", "Espa\xC3\xB1" "ol", "Spanish", "Spanish", kSpanishLocales, {}, TextNeeds::game_fonts},
    // Français
    {"fr", "Fran\xC3\xA7" "ais", "French", "French", kFrenchLocales, {}, TextNeeds::game_fonts},
    {"it", "Italiano", "Italian", "Italian", kItalianLocales, {}, TextNeeds::game_fonts},
    // 简体中文
    {"zh-Hans",
     "\347\256\200\344\275\223\344\270\255\346\226\207",
     "Chinese (Simplified)",
     "Chinese",
     kSimplifiedChineseLocales,
     {},
     TextNeeds::modern_fonts},
}};
