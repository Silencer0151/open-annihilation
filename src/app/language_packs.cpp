// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "language_packs.hpp"

#include "oa/app/game_directory.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/formats/tdf.hpp"

#include <algorithm>
#include <cstdint>
#include <fstream>
#include <iostream>
#include <iterator>
#include <optional>
#include <string>
#include <system_error>

namespace oa::app {

namespace {

namespace languages = oa::data::languages;

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

/// Reads a file of at most a number of bytes.
///
/// @param file the file
/// @param most_bytes the most bytes read
/// @param[out] failure why it was not read, when it is there
/// @return its bytes; nothing when it is not there, is larger or does not read
std::optional<std::string>
read_bounded(const fs::path& file, std::size_t most_bytes, std::string& failure) {
    std::error_code error;
    if (!fs::is_regular_file(file, error))
        return std::nullopt;
    const auto size = fs::file_size(file, error);
    if (error || size > most_bytes) {
        failure = "it is larger than " + std::to_string(most_bytes) + " bytes";
        return std::nullopt;
    }
    std::ifstream input(file, std::ios::binary);
    std::string text{std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (!input && !input.eof()) {
        failure = "it does not read";
        return std::nullopt;
    }
    return text;
}

/// Reads one pack's folder.
///
/// @param folder the pack's folder
/// @param[out] interface_text its interface.tdf; empty without one
/// @param[out] failure why the pack was left out
/// @return the pack; null when it is left out
std::unique_ptr<LoadedLanguagePack>
read_pack(const fs::path& folder, std::string& interface_text, std::string& failure) {
    const auto manifest_text = read_bounded(
        folder / path_from_utf8(languages::pack_manifest_file),
        oa::formats::oamod::max_input_bytes,
        failure
    );
    if (!manifest_text) {
        if (failure.empty())
            failure = "its manifest is missing";
        return nullptr;
    }
    languages::PackManifest manifest;
    if (!languages::read_manifest(
            {reinterpret_cast<const uint8_t*>(manifest_text->data()), manifest_text->size()},
            manifest,
            &failure
        ))
        return nullptr;
    auto loaded = std::make_unique<LoadedLanguagePack>(
        LoadedLanguagePack{languages::LanguagePack(std::move(manifest)), folder}
    );
    for (const languages::PackTable table : languages::pack_tables) {
        const fs::path file = folder / path_from_utf8(languages::pack_table_file(table));
        std::string table_failure;
        const auto text = read_bounded(file, languages::most_pack_table_bytes, table_failure);
        if (!text && table_failure.empty())
            continue;
        if (!text || !loaded->pack.add(table, *text, &table_failure)) {
            failure = std::string(languages::pack_table_file(table)) + ": " + table_failure;
            return nullptr;
        }
    }
    std::string interface_failure;
    if (const auto text = read_bounded(
            folder / path_from_utf8(languages::pack_interface_file),
            languages::most_catalogue_bytes,
            interface_failure
        ))
        interface_text = *text;
    else if (!interface_failure.empty()) {
        failure = std::string(languages::pack_interface_file) + ": " + interface_failure;
        return nullptr;
    }
    return loaded;
}

/// Finds an entry of a folder by its name, the same name first, else one
/// that differs only in the case of its ASCII letters.
///
/// @param folder the folder
/// @param name the name
/// @return the entry; empty when there is none
fs::path entry_named(const fs::path& folder, std::string_view name) {
    std::error_code error;
    const fs::path same = folder / path_from_utf8(name);
    if (fs::exists(same, error))
        return same;
    const std::string wanted = lowered(name);
    for (fs::directory_iterator entry(folder, error), end; !error && entry != end;
         entry.increment(error))
        if (lowered(path_to_utf8(entry->path().filename())) == wanted)
            return entry->path();
    return {};
}

} // namespace

void read_language_packs(
    const fs::path& root,
    std::vector<std::unique_ptr<LoadedLanguagePack>>& packs,
    languages::InterfaceText* catalogue
) {
    std::error_code error;
    if (!fs::is_directory(root, error))
        return;
    std::vector<fs::path> folders;
    for (fs::directory_iterator entry(root, error), end; !error && entry != end;
         entry.increment(error))
        if (entry->is_directory(error))
            folders.push_back(entry->path());
    std::sort(folders.begin(), folders.end());
    for (const fs::path& folder : folders) {
        std::error_code missing;
        if (!fs::exists(folder / path_from_utf8(languages::pack_manifest_file), missing))
            continue;
        std::string interface_text;
        std::string failure;
        auto pack = read_pack(folder, interface_text, failure);
        if (pack != nullptr && catalogue != nullptr && !interface_text.empty() &&
            !catalogue->add(interface_text, &failure))
            pack.reset();
        if (pack == nullptr) {
            std::cerr << "open-annihilation: the language pack " << path_to_utf8(folder)
                      << " was not read: " << failure << '\n';
            continue;
        }
        packs.push_back(std::move(pack));
    }
}

std::optional<std::string> read_pack_file(const fs::path& file, std::string& failure) {
    auto bytes = read_bounded(file, oa::formats::tdf::max_input_bytes, failure);
    if (!bytes && failure.empty())
        failure = "it is not there";
    return bytes;
}

fs::path language_pack_file(const LoadedLanguagePack& pack, std::string_view path) {
    // Only files under a language folder of the pack's word, at any depth:
    // .../<folder>-<word>/..., as camps/briefs-Chinese/<briefing>.
    std::vector<std::string_view> names;
    std::size_t start = 0;
    while (start <= path.size()) {
        const std::size_t end = std::min(path.find_first_of("/\\", start), path.size());
        if (end > start)
            names.push_back(path.substr(start, end - start));
        start = end + 1;
    }
    if (names.size() < 2)
        return {};
    const std::string suffix = '-' + lowered(pack.pack.manifest().word);
    const auto language_folder = [&suffix](std::string_view name) {
        const std::string folder = lowered(name);
        return folder.size() > suffix.size() && folder.ends_with(suffix);
    };
    if (std::none_of(names.begin(), names.end() - 1, language_folder))
        return {};
    fs::path found = pack.folder / path_from_utf8(languages::pack_files_folder);
    for (const std::string_view name : names) {
        if (name == "." || name == "..")
            return {};
        found = entry_named(found, name);
        if (found.empty())
            return {};
    }
    std::error_code error;
    return fs::is_regular_file(found, error) ? found : fs::path{};
}

} // namespace oa::app
