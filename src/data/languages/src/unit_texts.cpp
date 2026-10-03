// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/languages/unit_texts.hpp"

#include <algorithm>
#include <cstring>
#include <string>
#include <string_view>
#include <vector>

namespace oa::data::languages {

namespace {

/// Views a field's text: its bytes up to its first NUL or its end.
///
/// @param field the field
/// @return the text
template <std::size_t Size>
std::string_view field_text(const char (&field)[Size]) noexcept {
    const void* end = std::memchr(field, '\0', Size);
    return {
        field,
        end != nullptr ? static_cast<std::size_t>(static_cast<const char*>(end) - field) : Size
    };
}

/// Returns the text the first word has in a map of texts by word.
///
/// @param texts the texts, by word
/// @param words the words, in order
/// @return the text; null when no word has one
template <typename Texts>
const std::string* first_text(const Texts& texts, std::span<const std::string> words) {
    for (const std::string& word : words) {
        const auto found = texts.find(std::string_view(word));
        if (found != texts.end())
            return &found->second;
    }
    return nullptr;
}

/// Lowers an ASCII letter; any other byte is kept.
///
/// @param letter the byte
/// @return the byte, A to Z lowered
constexpr char lowered(char letter) noexcept {
    return letter >= 'A' && letter <= 'Z' ? static_cast<char>(letter - 'A' + 'a') : letter;
}

/// The table and words the interface reads, and the sink that fills it.
struct Installed {
    const UnitTexts* texts{};                   ///< null for the types' own texts
    std::vector<std::string> words{};           ///< the words tried, in order
    const oa::data::defs::UnitTextSink* sink{}; ///< null for none
};

/// The one installation the interface reads.
Installed& installed() {
    static Installed state{};
    return state;
}

} // namespace

std::vector<std::string> data_words(const Language& language, std::string_view word) {
    std::vector<std::string> words;
    if (!word.empty()) {
        words.emplace_back(word);
        return words;
    }
    for (const Language* step : fallback_chain(language).view())
        if (!step->game_name.empty() && (step != &english() || &language == &english()))
            words.emplace_back(step->game_name);
    return words;
}

void UnitTexts::add(
    std::string_view unit_name, std::string_view word, const char* name, const char* description
) {
    if (unit_name.empty() || word.empty() || (name == nullptr && description == nullptr))
        return;
    auto unit = units_.find(unit_name);
    if (unit == units_.end())
        unit = units_.emplace(std::string(unit_name), Entry{}).first;
    Entry& entry = unit->second;
    const auto store = [word](ByWord& texts, const char* text) {
        const auto found = texts.find(word);
        if (found != texts.end())
            found->second = text;
        else
            texts.emplace(std::string(word), text);
    };
    if (name != nullptr)
        store(entry.names, name);
    if (description != nullptr)
        store(entry.descriptions, description);
}

void UnitTexts::clear() noexcept {
    units_.clear();
}

std::string_view UnitTexts::name(const UnitDef& def, std::span<const std::string> words) const {
    if (!words.empty()) {
        const auto found = units_.find(field_text(def.unit_name));
        if (found != units_.end())
            if (const std::string* text = first_text(found->second.names, words))
                return *text;
    }
    return field_text(def.name);
}

std::string_view UnitTexts::name(
    std::string_view unit_name, std::string_view own, std::span<const std::string> words
) const {
    if (!words.empty()) {
        const auto found = units_.find(unit_name);
        if (found != units_.end())
            if (const std::string* text = first_text(found->second.names, words))
                return *text;
    }
    return own;
}

std::string_view
UnitTexts::description(const UnitDef& def, std::span<const std::string> words) const {
    if (!words.empty()) {
        const auto found = units_.find(field_text(def.unit_name));
        if (found != units_.end())
            if (const std::string* text = first_text(found->second.descriptions, words))
                return *text;
    }
    return field_text(def.description);
}

bool UnitTexts::NoCaseLess::operator()(
    std::string_view left, std::string_view right
) const noexcept {
    return std::lexicographical_compare(
        left.begin(), left.end(), right.begin(), right.end(), [](char a, char b) {
            return static_cast<unsigned char>(lowered(a)) < static_cast<unsigned char>(lowered(b));
        }
    );
}

std::size_t UnitTexts::size() const noexcept {
    return units_.size();
}

void set_unit_texts(const UnitTexts* texts, std::span<const std::string> words) {
    Installed& state = installed();
    state.texts = texts;
    state.words.assign(words.begin(), words.end());
}

std::string_view unit_display_name(const UnitDef& def) {
    const Installed& state = installed();
    if (state.texts == nullptr)
        return field_text(def.name);
    return state.texts->name(def, state.words);
}

std::string_view unit_display_description(const UnitDef& def) {
    const Installed& state = installed();
    if (state.texts == nullptr)
        return field_text(def.description);
    return state.texts->description(def, state.words);
}

std::string_view unit_display_name(std::string_view unit_name, std::string_view own) {
    const Installed& state = installed();
    if (state.texts == nullptr)
        return own;
    return state.texts->name(unit_name, own, state.words);
}

void set_unit_text_sink(const oa::data::defs::UnitTextSink* sink) noexcept {
    installed().sink = sink;
}

const oa::data::defs::UnitTextSink* unit_text_sink() noexcept {
    return installed().sink;
}

} // namespace oa::data::languages
