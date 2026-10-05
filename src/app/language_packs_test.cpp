// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The files a language pack answers for the game data's language folders
// (language_packs.hpp): a briefing under camps/briefs-<word>, as a mission
// asks for it, is read from the pack's files folder whatever the case of its
// names; another word's folder, a path with no language folder, a folder
// itself and a path that climbs out of the files folder are answered by
// none, so the game data's own files in another language stay as they are.
//
// The command line names the folder that holds the pseudo-language's pack.

#include "language_packs.hpp"

#include "oa/test/check.hpp"

#include <filesystem>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace {

namespace fs = std::filesystem;
using oa::app::LoadedLanguagePack;

/// The folder that holds the pseudo-language's pack, from the command line.
fs::path packs_folder;

/// Reads the pseudo-language's pack.
///
/// @return the pack; the check fails without it
std::unique_ptr<LoadedLanguagePack> pseudo_pack() {
    std::vector<std::unique_ptr<LoadedLanguagePack>> packs;
    oa::app::read_language_packs(packs_folder, packs, nullptr);
    OA_CHECK(packs.size() == 1);
    if (packs.empty())
        return nullptr;
    OA_CHECK(packs.front()->pack.manifest().word == "Pseudo");
    return std::move(packs.front());
}

/// A briefing in the pack's own word is answered at any depth of its path.
void own_briefing_is_read() {
    const auto pack = pseudo_pack();
    if (pack == nullptr)
        return;
    const fs::path briefing = oa::app::language_pack_file(*pack, "camps/briefs-Pseudo/brief.TXT");
    OA_CHECK(!briefing.empty());
    std::string failure;
    const auto bytes = oa::app::read_pack_file(briefing, failure);
    OA_CHECK(bytes.has_value() && !bytes->empty() && failure.empty());
    OA_CHECK(!oa::app::language_pack_file(*pack, "CAMPS\\BRIEFS-PSEUDO\\BRIEF.TXT").empty());
}

/// Every other path is the game data's alone.
void other_paths_are_not_answered() {
    const auto pack = pseudo_pack();
    if (pack == nullptr)
        return;
    const auto answered = [&](std::string_view path) {
        return !oa::app::language_pack_file(*pack, path).empty();
    };
    OA_CHECK(!answered("camps/briefs-German/brief.txt"));
    OA_CHECK(!answered("camps/brief.txt"));
    OA_CHECK(!answered("brief.txt"));
    OA_CHECK(!answered("camps/briefs-Pseudo"));
    OA_CHECK(!answered("camps/briefs-Pseudo/missing.txt"));
    OA_CHECK(!answered("camps/briefs-Pseudo/../briefs-Pseudo/brief.txt"));
    OA_CHECK(!answered("camps/-Pseudo/brief.txt"));
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 2) {
        std::cerr
            << "usage: oa-app-language-packs-test <folder holding the pseudo-language's pack>\n";
        return 2;
    }
    packs_folder = argv[1];
    own_briefing_is_read();
    other_paths_are_not_answered();
    return oa::test::check_exit_status();
}
