// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/defs/unit_texts.hpp"

#include "oa/data/defs/locale.hpp"

#include <cstring>

namespace oa::data::defs {

namespace {

/// Builds a key from a language's word and a key's own name.
///
/// @param[out] key the key's buffer, locale_text_capacity bytes
/// @param language the language's word
/// @param name the key's own name
/// @return false when the key does not fit
bool language_key(char (&key)[locale_text_capacity], const char* language, const char* name) {
    const std::size_t language_length = std::strlen(language);
    const std::size_t name_length = std::strlen(name);
    if (language_length + name_length >= sizeof key)
        return false;
    std::memcpy(key, language, language_length);
    std::memcpy(key + language_length, name, name_length + 1);
    return true;
}

} // namespace

void read_unit_texts(
    const formats::tdf::Block* block, const char* unit_name, const UnitTextSink* sink
) noexcept {
    if (block == nullptr || sink == nullptr || sink->text == nullptr || sink->languages == nullptr)
        return;
    for (uint32_t index = 0; index < sink->language_count; ++index) {
        const char* language = sink->languages[index];
        if (language == nullptr || language[0] == '\0')
            continue;
        char key[locale_text_capacity];
        char name[unit_text_name_bytes];
        char description[unit_text_description_bytes];
        const bool has_name = language_key(key, language, "name") &&
                              formats::tdf::get_string(block, key, name, sizeof name, nullptr);
        const bool has_description =
            language_key(key, language, "description") &&
            formats::tdf::get_string(block, key, description, sizeof description, nullptr);
        if (has_name || has_description)
            sink->text(
                sink->context,
                unit_name != nullptr ? unit_name : "",
                index,
                has_name ? name : nullptr,
                has_description ? description : nullptr
            );
    }
}

} // namespace oa::data::defs
