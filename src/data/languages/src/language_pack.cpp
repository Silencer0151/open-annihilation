// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/data/languages/language_pack.hpp"

#include "oa/formats/oamod.hpp"
#include "oa/formats/tdf.hpp"

#include <algorithm>
#include <string>
#include <string_view>

namespace oa::data::languages {

namespace {

namespace tdf = oa::formats::tdf;
namespace oamod = oa::formats::oamod;

/// The most nested sections a picture's path names.
constexpr uint32_t most_picture_depth = 4;

/// Lowers the ASCII letters of a text.
///
/// @param text the text
/// @return the text, A to Z lowered
std::string lowered(std::string_view text) {
    std::string folded(text);
    for (char& letter : folded)
        if (letter >= 'A' && letter <= 'Z')
            letter = static_cast<char>(letter - 'A' + 'a');
    return folded;
}

/// Returns a mission file's name as missions.tdf is looked up by: lowered,
/// without a ".ota" extension.
///
/// @param mission_file the mission's file, as the campaign names it
/// @return the key
std::string mission_key(std::string_view mission_file) {
    std::string key = lowered(mission_file);
    constexpr std::string_view extension = ".ota";
    if (key.size() > extension.size() && key.ends_with(extension))
        key.resize(key.size() - extension.size());
    return key;
}

/// Parses a table, refusing one larger than most_pack_table_bytes.
///
/// @param file the file's bytes
/// @param[out] document the parsed file
/// @param[out] error why the file was refused; may be null
/// @return true when it parsed
bool parse_table(std::string_view file, tdf::OwnedDocument& document, std::string* error) {
    if (file.size() > most_pack_table_bytes) {
        if (error != nullptr)
            *error = "the table is larger than " + std::to_string(most_pack_table_bytes) + " bytes";
        return false;
    }
    tdf::ParseError parse_error{};
    if (!document.parse(file, &parse_error)) {
        if (error != nullptr)
            *error = tdf::describe(parse_error);
        return false;
    }
    return true;
}

/// Returns a section's value under a key, any case.
///
/// @param section the section
/// @param key the key
/// @return the value; empty without one
std::string_view value_of(const tdf::Block* section, const char* key) {
    const char* value = tdf::find_value(section, key);
    return value != nullptr ? std::string_view(value) : std::string_view{};
}

/// Adds a section's captions, and those of the sections nested in it.
///
/// @param section the section
/// @param path the path of the section's parent; empty at the top
/// @param depth how deep the section is, 1 at the top
/// @param[in,out] captions the captions, by lower-case path
void add_captions(
    const tdf::Block* section,
    const std::string& path,
    uint32_t depth,
    std::map<std::string, PictureCaptions::Keys, std::less<>>& captions
) {
    if (section == nullptr || section->name == nullptr || section->name[0] == '\0' ||
        depth > most_picture_depth)
        return;
    const std::string own =
        path.empty() ? lowered(section->name) : path + '/' + lowered(section->name);
    const uint32_t children = tdf::child_count(section);
    const uint32_t keys = tdf::property_count(section);
    // A section that only groups others is not a caption itself.
    if (keys != 0 || children == 0) {
        PictureCaptions::Keys values;
        for (uint32_t index = 0; index < keys; ++index) {
            const char* key = tdf::property_key_at(section, static_cast<int32_t>(index));
            const char* value = tdf::find_value(section, key);
            if (key != nullptr && value != nullptr)
                values[lowered(key)] = value;
        }
        captions[own] = std::move(values);
    }
    for (uint32_t index = 0; index < children; ++index)
        add_captions(tdf::child_at(section, index), own, depth + 1, captions);
}

/// Reads a manifest's text field.
///
/// @param node the field's node
/// @param[out] text its text
/// @return false when it is not a string
bool read_text(const oamod::Node& node, std::string& text) {
    if (node.kind != oamod::NodeKind::string)
        return false;
    text = node.text;
    return true;
}

/// Reads a manifest's list of tags.
///
/// @param node the field's node
/// @param[out] tags its strings
/// @return false when it is not a sequence of strings
bool read_tags(const oamod::Node& node, std::vector<std::string>& tags) {
    if (node.kind != oamod::NodeKind::sequence)
        return false;
    tags.clear();
    for (const oamod::Node& item : node.children) {
        if (item.kind != oamod::NodeKind::string || item.text.empty())
            return false;
        tags.push_back(item.text);
    }
    return true;
}

/// Reads a manifest's text: entry, { needs: game-fonts | modern-fonts }.
///
/// @param node the field's node
/// @param[out] needs what drawing the language needs
/// @param[out] error why it was refused
/// @return true when it was read
bool read_text_needs(const oamod::Node& node, TextNeeds& needs, std::string& error) {
    if (node.kind != oamod::NodeKind::mapping) {
        error = "text must be a mapping";
        return false;
    }
    for (const oamod::Node& entry : node.children) {
        if (entry.key.text != "needs") {
            error = "text has no key " + entry.key.text;
            return false;
        }
        if (entry.kind == oamod::NodeKind::string && entry.text == "game-fonts")
            needs = TextNeeds::game_fonts;
        else if (entry.kind == oamod::NodeKind::string && entry.text == "modern-fonts")
            needs = TextNeeds::modern_fonts;
        else {
            error = "text needs must be game-fonts or modern-fonts";
            return false;
        }
    }
    return true;
}

} // namespace

bool read_manifest(std::span<const uint8_t> bytes, PackManifest& manifest, std::string* error) {
    oamod::Node root;
    oamod::ReadError read_error{};
    std::string failure;
    PackManifest read{};
    bool has_format = false;
    if (!oamod::read_document(bytes, root, read_error)) {
        failure = std::string(oamod::rule_message(read_error.rule)) + " at line " +
                  std::to_string(read_error.position.line) + ", column " +
                  std::to_string(read_error.position.column);
    } else {
        for (const oamod::Node& entry : root.children) {
            const std::string& key = entry.key.text;
            bool ok = true;
            if (key == "oalang") {
                ok = entry.kind == oamod::NodeKind::number &&
                     oamod::integer_value(entry.number, read.format);
                has_format = ok;
            } else if (key == "tag") {
                ok = read_text(entry, read.tag);
            } else if (key == "name") {
                ok = read_text(entry, read.name);
            } else if (key == "english-name") {
                ok = read_text(entry, read.english_name);
            } else if (key == "word") {
                ok = read_text(entry, read.word);
            } else if (key == "version") {
                ok = read_text(entry, read.version);
            } else if (key == "locales") {
                ok = read_tags(entry, read.locales);
            } else if (key == "fallbacks") {
                ok = read_tags(entry, read.fallbacks);
            } else if (key == "text") {
                if (!read_text_needs(entry, read.needs, failure))
                    break;
            } else if (key == "unicode") {
                ok = entry.kind == oamod::NodeKind::boolean;
                read.unicode = ok && entry.boolean;
            } else {
                failure = "the manifest has no key " + key;
                break;
            }
            if (!ok) {
                failure = "the manifest's " + key + " is not of its kind";
                break;
            }
        }
        if (failure.empty() && !has_format)
            failure = "the manifest names no oalang";
        else if (failure.empty() && read.format != pack_format_version)
            failure = "the manifest's oalang " + std::to_string(read.format) + " is not " +
                      std::to_string(pack_format_version);
        else if (failure.empty() && read.tag.empty())
            failure = "the manifest names no tag";
        else if (failure.empty() && read.word.empty())
            failure = "the manifest names no word";
        else if (failure.empty() && normalised_locale(read.tag) != read.tag)
            failure = "the manifest's tag " + read.tag + " is not a tag";
    }
    if (!failure.empty()) {
        if (error != nullptr)
            *error = failure;
        return false;
    }
    manifest = std::move(read);
    return true;
}

std::string_view pack_table_file(PackTable table) noexcept {
    switch (table) {
    case PackTable::translate:
        return "translate.tdf";
    case PackTable::units:
        return "units.tdf";
    case PackTable::missions:
        return "missions.tdf";
    case PackTable::pictures:
        return "pictures.tdf";
    }
    return {};
}

bool PictureCaptions::add(std::string_view file, std::string* error) {
    tdf::OwnedDocument document;
    if (!parse_table(file, document, error))
        return false;
    const tdf::Block* root = document.root();
    std::map<std::string, Keys, std::less<>> added;
    for (uint32_t index = 0; index < tdf::child_count(root); ++index)
        add_captions(tdf::child_at(root, index), {}, 1, added);
    for (auto& [path, keys] : added)
        captions_[path] = std::move(keys);
    return true;
}

void PictureCaptions::add_missing(const PictureCaptions& lower) {
    for (const auto& [path, keys] : lower.captions_)
        captions_.try_emplace(path, keys);
}

const PictureCaptions::Keys* PictureCaptions::find(std::string_view path) const {
    const auto found = captions_.find(lowered(path));
    if (found == captions_.end() || found->second.empty())
        return nullptr;
    return &found->second;
}

bool translates(std::string_view from, std::string_view english, std::size_t field_bytes) noexcept {
    if (from.empty() || from == english)
        return true;
    // A field filled to its last byte may hold the English cut short.
    return field_bytes != 0 && english.size() + 1 >= field_bytes && from.starts_with(english);
}

bool LanguagePack::add(PackTable table, std::string_view file, std::string* error) {
    if (table == PackTable::pictures)
        return pictures_.add(file, error);
    tdf::OwnedDocument document;
    if (!parse_table(file, document, error))
        return false;
    const tdf::Block* root = document.root();
    for (uint32_t index = 0; index < tdf::child_count(root); ++index) {
        const tdf::Block* section = tdf::child_at(root, index);
        if (section == nullptr || section->name == nullptr || section->name[0] == '\0')
            continue;
        const std::string_view name = section->name;
        switch (table) {
        case PackTable::translate: {
            std::string_view text = value_of(section, manifest_.word.c_str());
            if (text.empty())
                text = value_of(section, manifest_.tag.c_str());
            if (!text.empty())
                translations_[std::string(name)] = std::string(text);
            break;
        }
        case PackTable::units: {
            PackUnitText& unit = units_[std::string(name)];
            const auto keep = [section](std::string& field, const char* key) {
                if (const std::string_view value = value_of(section, key); !value.empty())
                    field = std::string(value);
            };
            keep(unit.name, "name");
            keep(unit.name_from, "name-from");
            keep(unit.description, "description");
            keep(unit.description_from, "description-from");
            break;
        }
        case PackTable::missions: {
            auto& texts = missions_[mission_key(name)];
            for (const char* key : {"missionname", "missiondescription", "missionhint"})
                if (const std::string_view value = value_of(section, key); !value.empty())
                    texts[key] = std::string(value);
            break;
        }
        case PackTable::pictures:
            break;
        }
    }
    return true;
}

const std::string* LanguagePack::translation(std::string_view english) const {
    const auto found = translations_.find(english);
    return found != translations_.end() ? &found->second : nullptr;
}

const std::string*
LanguagePack::unit_name(std::string_view unit_name, std::string_view english) const {
    const auto found = units_.find(unit_name);
    if (found == units_.end() || found->second.name.empty() ||
        !translates(found->second.name_from, english, sizeof(UnitDef{}.name)))
        return nullptr;
    return &found->second.name;
}

const std::string*
LanguagePack::unit_description(std::string_view unit_name, std::string_view english) const {
    const auto found = units_.find(unit_name);
    if (found == units_.end() || found->second.description.empty() ||
        !translates(found->second.description_from, english, sizeof(UnitDef{}.description)))
        return nullptr;
    return &found->second.description;
}

const std::string*
LanguagePack::mission_text(std::string_view mission_file, std::string_view key) const {
    const auto found = missions_.find(mission_key(mission_file));
    if (found == missions_.end())
        return nullptr;
    const auto text = found->second.find(lowered(key));
    return text != found->second.end() ? &text->second : nullptr;
}

const PackLayer* layer_of(std::span<const PackLayer> layers, std::string_view word) {
    const UnitTexts::NoCaseLess less{};
    for (const PackLayer& layer : layers)
        if (!less(layer.word, word) && !less(word, layer.word))
            return &layer;
    return nullptr;
}

const char* layered_translation(
    std::span<const PackLayer> layers,
    std::span<const std::string> words,
    std::string_view english,
    const std::function<const char*(std::size_t)>& data
) {
    for (std::size_t index = 0; index < words.size(); ++index) {
        const PackLayer* layer = layer_of(layers, words[index]);
        if (layer != nullptr)
            for (const LanguagePack* pack : layer->before_data)
                if (const std::string* text = pack->translation(english))
                    return text->c_str();
        if (data)
            if (const char* text = data(index); text != nullptr && text[0] != '\0')
                return text;
        if (layer != nullptr)
            for (const LanguagePack* pack : layer->after_data)
                if (const std::string* text = pack->translation(english))
                    return text->c_str();
    }
    return nullptr;
}

const char* layered_mission_text(
    std::span<const PackLayer> layers,
    std::span<const std::string> words,
    std::string_view mission_file,
    std::string_view key,
    const char* data_text
) {
    for (std::size_t index = 0; index < words.size(); ++index) {
        const PackLayer* layer = layer_of(layers, words[index]);
        if (layer != nullptr)
            for (const LanguagePack* pack : layer->before_data)
                if (const std::string* text = pack->mission_text(mission_file, key))
                    return text->c_str();
        if (index == 0 && data_text != nullptr && data_text[0] != '\0')
            return data_text;
        if (layer != nullptr)
            for (const LanguagePack* pack : layer->after_data)
                if (const std::string* text = pack->mission_text(mission_file, key))
                    return text->c_str();
    }
    return data_text != nullptr && data_text[0] != '\0' ? data_text : nullptr;
}

PictureCaptions
layered_pictures(std::span<const PackLayer> layers, std::span<const std::string> words) {
    PictureCaptions captions;
    for (const std::string& word : words) {
        const PackLayer* layer = layer_of(layers, word);
        if (layer == nullptr)
            continue;
        for (const LanguagePack* pack : layer->before_data)
            captions.add_missing(pack->pictures());
        for (const LanguagePack* pack : layer->after_data)
            captions.add_missing(pack->pictures());
    }
    return captions;
}

} // namespace oa::data::languages
