// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Language packs read from their folders (oa/data/languages/language_pack.hpp):
// the engine's own, in the languages folder beside the game's other files;
// the player's, in Languages in their own folder; and a mod's, in its
// languages folder. Each pack is a folder named by its tag, holding
// language.yaml and its tables.

#pragma once

#include "oa/data/languages/interface_text.hpp"
#include "oa/data/languages/language_pack.hpp"

#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

/// The folder beside the game's other files that holds the engine's
/// interface catalogue files and its language packs.
inline constexpr std::string_view engine_languages_folder = "languages";

/// The folder of the player's own folder that holds their language packs.
inline constexpr std::string_view player_languages_folder = "Languages";

/// A language pack read from its folder.
struct LoadedLanguagePack {
    /// The pack's tables.
    oa::data::languages::LanguagePack pack;
    /// The folder it was read from; its files folder answers the game
    /// data's language folders.
    std::filesystem::path folder{};
};

/// Reads every language pack in a folder: each folder in it that holds
/// language.yaml. A pack whose manifest or a table does not read is
/// reported and left out whole; tables larger than
/// oa::data::languages::most_pack_table_bytes are refused.
///
/// @param root the folder that holds the packs' folders; one that is not
///     there holds none
/// @param[in,out] packs the packs read are added at the end, in the order
///     of their folders' names
/// @param[in,out] catalogue each pack's interface.tdf is added to it; null
///     reads none
void read_language_packs(
    const std::filesystem::path& root,
    std::vector<std::unique_ptr<LoadedLanguagePack>>& packs,
    oa::data::languages::InterfaceText* catalogue
);

/// Reads a file of a pack's files folder, of at most the bytes the TDF
/// reader takes.
///
/// @param file the file
/// @param[out] failure why it was not read
/// @return its bytes; nothing when it is not there, is larger or does not read
[[nodiscard]] std::optional<std::string>
read_pack_file(const std::filesystem::path& file, std::string& failure);

/// Finds a file of the game data's language folders in a pack's files
/// folder, matching each name without regard to case.
///
/// @param pack the pack
/// @param path the file's path in the game data, its names parted by '/'
///     or '\\', as "camps/briefs-Chinese/arm01.txt"
/// @return the file on this machine; empty when the pack has none, or when
///     no folder of the path is one of the pack's word's language folders
///     (<folder>-<word>)
[[nodiscard]] std::filesystem::path
language_pack_file(const LoadedLanguagePack& pack, std::string_view path);

} // namespace oa::app
