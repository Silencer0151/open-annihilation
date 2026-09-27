// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Translation table: each top-level TDF section name maps to its value for
// the active language key (e.g. [Single Player] { french=Solo; }).
#pragma once

#include "oa/data/defs/files.hpp"
#include "oa/formats/tdf.hpp"

#include <cstddef>
#include <cstdint>

namespace oa::data::defs {

inline constexpr std::size_t locale_text_capacity = 0x100;

struct LocaleEntry {
    char* source;
    char* translation;
};

// Sorted by case-sensitive source text.
struct LocaleTable {
    char language[locale_text_capacity];
    LocaleEntry* entries;
    uint32_t count;
    uint32_t capacity;
};

/// Empties a table and clears its language without freeing anything.
///
/// @param[out] table table to reset; its previous storage is not released
void locale_table_init(LocaleTable* table) noexcept;

/// Frees every entry and clears the language.
///
/// @param[in,out] table table to release; left empty and reusable
void locale_table_free(LocaleTable* table) noexcept;

/// Rebuilds the table from a document for a language, unless that language is already loaded.
///
/// Every top-level section whose value under the language key is non-empty
/// becomes an entry mapping the section name (up to 255 characters) to that
/// value; a later duplicate section replaces the earlier translation.
///
/// @param[in,out] table table to fill; its entries are dropped first
/// @param[in,out] document parsed translation file; its cursor is moved
/// @param language language key, compared case-insensitively with the loaded one
/// @return false when an entry cannot be stored (the table keeps the entries
///     added so far)
bool locale_table_load(
    LocaleTable* table, formats::tdf::Document* document, const char* language
) noexcept;

/// Loads a translation file for a language, unless that language is already loaded.
///
/// A missing or unparsable file still switches the table to the language,
/// leaving it empty.
///
/// @param files file boundary
/// @param[in,out] table table to fill
/// @param path translation file path
/// @param language language key
/// @return true when the language was already loaded, or the file loaded and
///     every entry was stored
bool load_locale_table(
    const Files* files, LocaleTable* table, const char* path, const char* language
) noexcept;

/// Translates source text by exact (case-sensitive) match.
///
/// @param table loaded table
/// @param source text to translate; may be null
/// @return the translation, `source` itself when there is none, or null for a null source
[[nodiscard]] const char* locale_translate(const LocaleTable* table, const char* source) noexcept;

/// Finds the source text of a translation.
///
/// @param table loaded table
/// @param translation translated text, compared case-insensitively; may be null
/// @return the first entry's source text in table order, or null when none
///     matches or `translation` is null
[[nodiscard]] const char*
locale_find_source(const LocaleTable* table, const char* translation) noexcept;

/// Reads a localized string value, "<language><key>" first, then the plain key.
///
/// @param block TDF block to read
/// @param language language prefix; the prefixed key is cut at 255 characters
/// @param key value name
/// @param[out] out destination buffer
/// @param size size of `out` in bytes
/// @param fallback text stored when neither key exists; null counts as ""
/// @return true when either key was found
bool get_localized_string(
    const formats::tdf::Block* block,
    const char* language,
    const char* key,
    char* out,
    std::size_t size,
    const char* fallback
) noexcept;

} // namespace oa::data::defs
