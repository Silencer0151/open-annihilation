// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/locale.hpp"
#include "oa/base/text.hpp"

#include <cstdint>
#include <cstdlib>
#include <cstring>

namespace oa::data::defs {
namespace {

constexpr uint32_t max_locale_entries = 65536;

char* duplicate(const char* text) noexcept {
    const std::size_t length = std::strlen(text);
    auto* copy = static_cast<char*>(std::malloc(length + 1));
    if (copy != nullptr)
        std::memcpy(copy, text, length + 1);
    return copy;
}

uint32_t lower_bound_source(const LocaleTable* table, const char* source) noexcept {
    uint32_t first = 0;
    uint32_t length = table->count;
    while (length > 0) {
        const uint32_t half = length / 2;
        if (std::strcmp(table->entries[first + half].source, source) < 0) {
            first += half + 1;
            length -= half + 1;
        } else {
            length = half;
        }
    }
    return first;
}

void clear_entries(LocaleTable* table) noexcept {
    for (uint32_t index = 0; index < table->count; ++index) {
        std::free(table->entries[index].source);
        std::free(table->entries[index].translation);
    }
    std::free(table->entries);
    table->entries = nullptr;
    table->count = 0;
    table->capacity = 0;
}

/// Stores a translation for source text, replacing an existing one.
///
/// A new entry is inserted in case-sensitive source order; the table grows
/// from 64 entries by doubling, up to 65536.
///
/// @param[in,out] table table to update
/// @param source source text; copied for a new entry
/// @param translation translated text; copied
/// @return false when the table is full or an allocation fails
bool set_translation(LocaleTable* table, const char* source, const char* translation) noexcept {
    const uint32_t position = lower_bound_source(table, source);
    char* copy = duplicate(translation);
    if (copy == nullptr)
        return false;
    if (position != table->count && std::strcmp(table->entries[position].source, source) == 0) {
        std::free(table->entries[position].translation);
        table->entries[position].translation = copy;
        return true;
    }
    char* key = duplicate(source);
    if (key == nullptr || table->count >= max_locale_entries) {
        std::free(key);
        std::free(copy);
        return false;
    }
    if (table->count == table->capacity) {
        const uint32_t grown = table->capacity == 0 ? 64u : table->capacity * 2u;
        auto* entries =
            static_cast<LocaleEntry*>(std::realloc(table->entries, sizeof(LocaleEntry) * grown));
        if (entries == nullptr) {
            std::free(key);
            std::free(copy);
            return false;
        }
        table->entries = entries;
        table->capacity = grown;
    }
    LocaleEntry* slot = &table->entries[position];
    std::memmove(slot + 1, slot, sizeof(LocaleEntry) * (table->count - position));
    slot->source = key;
    slot->translation = copy;
    ++table->count;
    return true;
}

/// Adds each top-level section whose value under the table's language key is non-empty.
///
/// @param[in,out] table table to fill; its language selects the key
/// @param[in,out] document parsed translation file; its cursor is moved
/// @return false when an entry cannot be stored
bool parse_entries(LocaleTable* table, formats::tdf::Document* document) noexcept {
    for (uint32_t index = 0;; ++index) {
        formats::tdf::reset_cursor(document);
        if (!formats::tdf::step_entry(document, index))
            return true;
        const formats::tdf::Block* section = formats::tdf::cursor(document);
        char source[locale_text_capacity];
        oa::base::text::copy_padded(source, section->name, sizeof source - 1);
        source[sizeof source - 1] = '\0';
        char translation[locale_text_capacity];
        formats::tdf::get_string(section, table->language, translation, sizeof translation, "");
        if (translation[0] != '\0' && !set_translation(table, source, translation))
            return false;
    }
}

} // namespace

void locale_table_init(LocaleTable* table) noexcept {
    table->language[0] = '\0';
    table->entries = nullptr;
    table->count = 0;
    table->capacity = 0;
}

void locale_table_free(LocaleTable* table) noexcept {
    clear_entries(table);
    table->language[0] = '\0';
}

bool locale_table_load(
    LocaleTable* table, formats::tdf::Document* document, const char* language
) noexcept {
    if (formats::tdf::compare_nocase(language, table->language) == 0)
        return true;
    clear_entries(table);
    oa::base::text::copy_padded(table->language, language, sizeof table->language - 1);
    table->language[sizeof table->language - 1] = '\0';
    return parse_entries(table, document);
}

bool load_locale_table(
    const Files* files, LocaleTable* table, const char* path, const char* language
) noexcept {
    if (formats::tdf::compare_nocase(language, table->language) == 0)
        return true;
    formats::tdf::Document document;
    formats::tdf::document_init(&document);
    const bool loaded = load_tdf_file(files, path, &document, nullptr);
    // A missing file still switches language, leaving the table empty.
    const bool ok = locale_table_load(table, &document, language);
    formats::tdf::document_free(&document);
    return loaded && ok;
}

const char* locale_translate(const LocaleTable* table, const char* source) noexcept {
    if (source == nullptr)
        return nullptr;
    const uint32_t position = lower_bound_source(table, source);
    if (position != table->count && std::strcmp(source, table->entries[position].source) >= 0)
        return table->entries[position].translation;
    return source;
}

const char* locale_find_source(const LocaleTable* table, const char* translation) noexcept {
    if (translation == nullptr)
        return nullptr;
    for (uint32_t index = 0; index < table->count; ++index)
        if (formats::tdf::compare_nocase(table->entries[index].translation, translation) == 0)
            return table->entries[index].source;
    return nullptr;
}

bool get_localized_string(
    const formats::tdf::Block* block,
    const char* language,
    const char* key,
    char* out,
    std::size_t size,
    const char* fallback
) noexcept {
    char localized[locale_text_capacity];
    oa::base::text::copy_padded(localized, language, sizeof localized - 1);
    localized[sizeof localized - 1] = '\0';
    oa::base::text::append_terminated(localized, key);
    const char* first_fallback = fallback != nullptr ? fallback : "";
    if (formats::tdf::get_string(block, localized, out, size, first_fallback))
        return true;
    return formats::tdf::get_string(block, key, out, size, first_fallback);
}

} // namespace oa::data::defs
