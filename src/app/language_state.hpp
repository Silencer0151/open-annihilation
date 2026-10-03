// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The language the game shows its text in (Runtime::LanguageState): the
// operating system's choice, the player's setting, the word the game data
// knows the language by, the units' names and descriptions in every
// language their files give, and the interface catalogue of the engine's
// own words. runtime_language.cpp chooses the language at start and each
// time the setting changes, and puts it in effect.
#pragma once

#include "oa/app/runtime.hpp"
#include "oa/data/languages.hpp"
#include "oa/data/languages/interface_text.hpp"
#include "oa/data/languages/unit_texts.hpp"
#include "oa/data/defs/locale.hpp"

#include <memory>
#include <string>
#include <vector>

namespace oa::app {

struct Runtime::LanguageState {
    /// The language the operating system's preferred locales choose, which
    /// the setting's System default shows in.
    const oa::data::languages::Language* system{&oa::data::languages::english()};
    /// The setting: oa::data::languages::system_choice or a tag.
    std::string choice{oa::data::languages::system_choice};
    /// The language the game shows its text in.
    const oa::data::languages::Language* shown{&oa::data::languages::english()};
    /// The word the game data's lookups use: 3.1c's command-line word as
    /// typed when no known language has it, else the shown language's
    /// game_name. It points into the registry or the command line, which
    /// live as long as the game runs, so a copy of it never dangles.
    const char* data_word{"English"};
    /// The words a unit's name and description are looked up by, in order
    /// (oa::data::languages::data_words).
    std::vector<std::string> words;
    /// The words the unit loaders read each unit's texts in: every known
    /// language's, and the command line's word when no known language has
    /// it. They are set once, so that the loaders' pointers to them hold.
    std::vector<std::string> sink_words;
    /// sink_words as the loaders take them.
    std::vector<const char*> sink_word_pointers;
    /// The sink the unit loaders hand each unit's texts to, which fills
    /// unit_texts; installed for every loader (oa::data::languages::unit_text_sink).
    oa::data::defs::UnitTextSink sink{};
    /// The units' names and descriptions in every language their files give.
    oa::data::languages::UnitTexts unit_texts;
    /// The interface catalogue of the engine's own words.
    oa::data::languages::InterfaceText catalogue;
    /// The game data's translation table and fonts are loaded for a word.
    bool loaded{};
    /// The word they are loaded for.
    std::string loaded_word;

    /// A translation table of gamedata/translate.tdf for one word, freed
    /// with it.
    struct FallbackTable {
        oa::data::defs::LocaleTable table{};

        /// Starts with no language loaded.
        FallbackTable() noexcept { oa::data::defs::locale_table_init(&table); }

        /// Frees the loaded texts.
        ~FallbackTable() { oa::data::defs::locale_table_free(&table); }

        FallbackTable(const FallbackTable&) = delete;
        FallbackTable& operator=(const FallbackTable&) = delete;
    };

    /// The translation tables of the words after the first (words), tried
    /// in order after the game's own table: a language's fallbacks.
    std::vector<std::unique_ptr<FallbackTable>> fallback_tables;
};

} // namespace oa::app
