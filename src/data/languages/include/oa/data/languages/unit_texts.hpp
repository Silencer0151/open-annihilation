// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The names and descriptions players see for unit types, in the language
// they chose: a unit file's "<Language>Name" and "<Language>Description"
// (oa/data/defs/unit_texts.hpp) where it gives them, else its Name and
// Description, which UnitDef keeps in every language so that the
// simulation, a saved game and what a shared game sends never change with
// the language. Every place the interface shows a unit type's name or
// description reads it here (unit_display_name, unit_display_description).
#pragma once

#include "oa/core/unit_def.h"
#include "oa/data/defs/unit_texts.hpp"
#include "oa/data/languages.hpp"

#include <cstddef>
#include <map>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::languages {

/// The words a language's text is looked up by in the game data, in order:
/// the game_name of each language of its fallback chain. English's comes
/// only for English itself: elsewhere English is the data's own text,
/// which ends every lookup, as 3.1c shows a text its data leaves
/// untranslated.
///
/// @param language the language
/// @param word the word 3.1c's command line names the language by, when no
///     known language has it; it is looked up alone. Empty for none.
/// @return the words, the language's own first
[[nodiscard]] std::vector<std::string> data_words(const Language& language, std::string_view word);

/// Unit types' names and descriptions in the languages their files give.
class UnitTexts {
  public:

    /// Records a unit's name and description in a language. A null text
    /// keeps the one recorded before.
    ///
    /// @param unit_name the unit's name (UnitDef.unit_name), matched later
    ///     without regard to case
    /// @param word the language's word in the game data, as "German"
    /// @param name the unit's name in the language; may be null
    /// @param description the unit's description in the language; may be null
    void add(
        std::string_view unit_name, std::string_view word, const char* name, const char* description
    );

    /// Forgets every text.
    void clear() noexcept;

    /// Returns the name players see for a unit type: in the first word's
    /// language its file gives a name in, else the type's own name.
    ///
    /// @param def the unit type
    /// @param words the words to try, in order (data_words)
    /// @return the name; a view of this table's copy or of `def`
    [[nodiscard]] std::string_view
    name(const UnitDef& def, std::span<const std::string> words) const;

    /// Returns the description players see for a unit type: in the first
    /// word's language its file gives a description in, else the type's own.
    ///
    /// @param def the unit type
    /// @param words the words to try, in order (data_words)
    /// @return the description; a view of this table's copy or of `def`
    [[nodiscard]] std::string_view
    description(const UnitDef& def, std::span<const std::string> words) const;

    /// Returns the name players see for a unit known by its names alone, as
    /// name(def, words) does.
    ///
    /// @param unit_name the unit's name (UnitDef.unit_name)
    /// @param own the type's own name (UnitDef.name)
    /// @param words the words to try, in order (data_words)
    /// @return the name; a view of this table's copy or of `own`
    [[nodiscard]] std::string_view name(
        std::string_view unit_name, std::string_view own, std::span<const std::string> words
    ) const;

    /// Returns how many unit types the table holds texts for.
    ///
    /// @return the count
    [[nodiscard]] std::size_t size() const noexcept;

    /// Orders texts without regard to the case of their ASCII letters.
    struct NoCaseLess {
        using is_transparent = void; ///< compares any text with any

        /// Tells whether a text comes before another.
        ///
        /// @param left a text
        /// @param right a text
        /// @return true when `left` sorts first
        [[nodiscard]] bool operator()(std::string_view left, std::string_view right) const noexcept;
    };

  private:

    /// Texts by the language's word.
    using ByWord = std::map<std::string, std::string, NoCaseLess>;

    /// One unit's texts.
    struct Entry {
        ByWord names{};        ///< its names, by word
        ByWord descriptions{}; ///< its descriptions, by word
    };

    /// The units' texts by unit name.
    std::map<std::string, Entry, NoCaseLess> units_{};
};

/// Installs the table and the words the interface shows unit types' names
/// and descriptions by. The views unit_display_name and
/// unit_display_description give stay valid until the table changes or the
/// next call. Only the thread that draws the interface calls it and them.
///
/// @param texts the table; null shows every type's own name and description
/// @param words the words to try, in order (data_words); copied
void set_unit_texts(const UnitTexts* texts, std::span<const std::string> words);

/// Returns the name players see for a unit type, in the installed language.
///
/// @param def the unit type
/// @return its name in the language, else UnitDef.name
[[nodiscard]] std::string_view unit_display_name(const UnitDef& def);

/// Returns the description players see for a unit type, in the installed
/// language.
///
/// @param def the unit type
/// @return its description in the language, else UnitDef.description
[[nodiscard]] std::string_view unit_display_description(const UnitDef& def);

/// Returns the name players see for a unit known by its names alone, in the
/// installed language.
///
/// @param unit_name the unit's name (UnitDef.unit_name)
/// @param own the type's own name (UnitDef.name)
/// @return its name in the language, else `own`
[[nodiscard]] std::string_view unit_display_name(std::string_view unit_name, std::string_view own);

/// Installs the sink the unit loaders hand each unit's names and
/// descriptions in other languages to, which fills the installed table.
///
/// @param sink the sink, kept until the next call; null for none
void set_unit_text_sink(const oa::data::defs::UnitTextSink* sink) noexcept;

/// Returns the sink the unit loaders hand their texts to.
///
/// @return the installed sink; null when none is installed
[[nodiscard]] const oa::data::defs::UnitTextSink* unit_text_sink() noexcept;

} // namespace oa::data::languages
