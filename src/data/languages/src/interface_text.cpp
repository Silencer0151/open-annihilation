// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/languages/interface_text.hpp"

#include "oa/formats/tdf.hpp"

#include <cstdint>
#include <string>
#include <string_view>

namespace oa::data::languages {

namespace {

/// Lowers the ASCII letters of a tag and reads '_' as '-'.
///
/// @param tag the tag
/// @return the folded tag
std::string folded_tag(std::string_view tag) {
    std::string folded(tag);
    for (char& letter : folded) {
        if (letter >= 'A' && letter <= 'Z')
            letter = static_cast<char>(letter - 'A' + 'a');
        else if (letter == '_')
            letter = '-';
    }
    return folded;
}

/// The catalogue and language the interface's own words show in.
struct Installed {
    const InterfaceText* catalogue{}; ///< null for English alone
    const Language* language{};       ///< null before any is installed
};

/// The one installation the interface reads.
Installed& installed() noexcept {
    static Installed state{};
    return state;
}

} // namespace

bool InterfaceText::add(std::string_view file, std::string* error) {
    if (file.size() > most_catalogue_bytes) {
        if (error != nullptr)
            *error = "the catalogue file is larger than " + std::to_string(most_catalogue_bytes) +
                     " bytes";
        return false;
    }
    oa::formats::tdf::OwnedDocument document;
    oa::formats::tdf::ParseError parse_error{};
    if (!document.parse(file, &parse_error)) {
        if (error != nullptr)
            *error = oa::formats::tdf::describe(parse_error);
        return false;
    }
    const oa::formats::tdf::Block* root = document.root();
    for (uint32_t index = 0; index < oa::formats::tdf::child_count(root); ++index) {
        const oa::formats::tdf::Block* section = oa::formats::tdf::child_at(root, index);
        if (section == nullptr || section->name == nullptr || section->name[0] == '\0')
            continue;
        const uint32_t keys = oa::formats::tdf::property_count(section);
        for (uint32_t key_index = 0; key_index < keys; ++key_index) {
            const char* key =
                oa::formats::tdf::property_key_at(section, static_cast<int32_t>(key_index));
            const char* value = oa::formats::tdf::find_value(section, key);
            if (key == nullptr || value == nullptr || value[0] == '\0')
                continue;
            texts_[std::string(section->name)][folded_tag(key)] = value;
        }
    }
    return true;
}

std::string_view InterfaceText::text(std::string_view english, const Language& language) const {
    const auto found = texts_.find(english);
    if (found == texts_.end())
        return english;
    const FallbackChain chain = fallback_chain(language);
    for (const Language* step : chain.view()) {
        const auto translation = found->second.find(folded_tag(step->tag));
        if (translation != found->second.end())
            return translation->second;
    }
    return english;
}

std::size_t InterfaceText::size() const noexcept {
    std::size_t count = 0;
    for (const auto& [english, texts] : texts_)
        count += texts.size();
    return count;
}

void set_interface_language(const InterfaceText* catalogue, const Language& language) noexcept {
    installed() = {catalogue, &language};
}

const Language& interface_language() noexcept {
    const Language* language = installed().language;
    return language != nullptr ? *language : english();
}

std::string_view interface_text(std::string_view english) {
    const Installed& state = installed();
    if (state.catalogue == nullptr)
        return english;
    return state.catalogue->text(english, interface_language());
}

} // namespace oa::data::languages
