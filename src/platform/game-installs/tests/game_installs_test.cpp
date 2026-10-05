// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The search for Total Annihilation folders over synthetic home folders
// built in a temporary folder (Steam with an SD-card library and a Flatpak
// Steam, Heroic native and Flatpak, a Lutris prefix and a Bottles bottle),
// and the readers of each library file, malformed ones included.
#include "oa/platform/game_installs.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <string>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace installs = oa::platform::game_installs;

/// Returns a folder's canonical path, as the search reports it.
///
/// @param folder the folder
/// @return its canonical path, or the folder itself when it cannot be found
fs::path canonical_of(const fs::path& folder) {
    std::error_code error;
    const fs::path path = fs::canonical(folder, error);
    return error ? folder : path;
}

/// Writes a file, making its folders.
///
/// @param file the file
/// @param text what it holds
void write_file(const fs::path& file, const std::string& text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary);
    out << text;
}

/// Makes a game folder: a folder holding the game's first archive under a name.
///
/// @param folder the folder
/// @param archive the archive's name, in any case
void make_game_folder(const fs::path& folder, const std::string& archive = "totala1.hpi") {
    write_file(folder / archive, "HAPI");
}

/// Returns UTF-8 text for a path, for the files the test writes.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8(const fs::path& path) {
    const std::u8string text = path.u8string();
    return {text.begin(), text.end()};
}

/// Returns a path as it stands inside a quoted string of Steam's and Heroic's files: UTF-8, each
/// backslash and quote escaped, as both programs write them. A Windows path's backslashes
/// would otherwise read as escapes (\u, \n, \t) or break the string.
///
/// @param path the path
/// @return its escaped UTF-8 spelling
std::string quoted_text(const fs::path& path) {
    std::string text;
    for (const char character : utf8(path)) {
        if (character == '\\' || character == '"')
            text += '\\';
        text += character;
    }
    return text;
}

/// Returns a Steam manifest of an app.
///
/// @param app the app number
/// @param install_dir the folder's name under steamapps/common
/// @return the manifest's text
std::string manifest(uint32_t app, const std::string& install_dir) {
    return "\"AppState\"\n{\n\t\"appid\"\t\t\"" + std::to_string(app) +
           "\"\n\t\"Universe\"\t\t\"1\"\n\t\"name\"\t\t\"Total Annihilation\"\n"
           "\t\"installdir\"\t\t\"" +
           install_dir + "\"\n\t\"UserConfig\"\n\t{\n\t\t\"language\"\t\t\"english\"\n\t}\n}\n";
}

/// Returns the file name Steam gives an app's manifest.
///
/// @param app the app number
/// @return the manifest's file name
std::string manifest_file(uint32_t app) {
    return "appmanifest_" + std::to_string(app) + ".acf";
}

/// Checks the words for where a folder was found.
void check_source_words() {
    OA_CHECK(installs::source_words(installs::Source::steam, false) == "your Steam library");
    OA_CHECK(
        installs::source_words(installs::Source::steam, true) == "your Steam library on the SD card"
    );
    OA_CHECK(installs::source_words(installs::Source::heroic, false) == "Heroic");
    OA_CHECK(installs::source_words(installs::Source::lutris, false) == "Lutris");
    OA_CHECK(installs::source_words(installs::Source::bottles, true) == "Bottles");
}

/// Checks the places under a home folder.
void check_search_roots() {
    const fs::path home = "/home/deck";
    const installs::SearchRoots roots = installs::search_roots_under(home);
    OA_CHECK(roots.home == home);
    OA_CHECK(roots.steam_roots.size() == 3);
    OA_CHECK(roots.steam_roots[0] == home / ".local" / "share" / "Steam");
    OA_CHECK(roots.steam_roots[1] == home / ".steam" / "steam");
    OA_CHECK(
        roots.steam_roots[2] ==
        home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam"
    );
    OA_CHECK(roots.heroic_configs.size() == 2);
    OA_CHECK(roots.heroic_configs[0] == home / ".config" / "heroic");
    OA_CHECK(
        roots.heroic_configs[1] ==
        home / ".var" / "app" / "com.heroicgameslauncher.hgl" / "config" / "heroic"
    );
    OA_CHECK(roots.prefix_parents.size() == roots.prefix_sources.size());
    OA_CHECK(!roots.prefix_parents.empty() && roots.prefix_parents[0] == home / "Games");
    OA_CHECK(!roots.prefix_sources.empty() && roots.prefix_sources[0] == installs::Source::lutris);
    const auto flatpak_bottles =
        home / ".var" / "app" / "com.usebottles.bottles" / "data" / "bottles" / "bottles";
    const auto bottles =
        std::find(roots.prefix_parents.begin(), roots.prefix_parents.end(), flatpak_bottles);
    OA_CHECK(bottles != roots.prefix_parents.end());
    if (bottles != roots.prefix_parents.end())
        OA_CHECK(
            roots
                .prefix_sources[static_cast<std::size_t>(bottles - roots.prefix_parents.begin())] ==
            installs::Source::bottles
        );
    OA_CHECK(roots.removable_mounts.size() == 1 && roots.removable_mounts[0] == "/run/media");
#if !defined(__linux__)
    OA_CHECK(installs::default_search_roots().steam_roots.empty());
#endif
}

/// Checks the reader of Steam's library list, malformed lists included.
void check_library_folders() {
    const std::string today =
        "\"libraryfolders\"\n{\n"
        "\t\"0\"\n\t{\n\t\t\"path\"\t\t\"/home/deck/.local/share/Steam\"\n"
        "\t\t\"label\"\t\t\"\"\n\t\t\"apps\"\n\t\t{\n\t\t\t\"298030\"\t\t\"1234\"\n\t\t}\n\t}\n"
        "\t\"1\"\n\t{\n\t\t\"path\"\t\t\"/run/media/mmcblk0p1\"\n\t}\n}\n";
    const auto folders = installs::library_folders(today);
    OA_CHECK(folders.size() == 2);
    OA_CHECK(folders.size() == 2 && folders[0] == "/home/deck/.local/share/Steam");
    OA_CHECK(folders.size() == 2 && folders[1] == "/run/media/mmcblk0p1");

    // Escapes are undone; a backslash before another character is kept.
    const auto escaped = installs::library_folders(
        "\"libraryfolders\" { \"0\" { \"path\" \"D:\\\\SteamLibrary\" } "
        "\"1\" { \"PATH\" \"/with \\\"quote\\\"\" } \"2\" { \"path\" \"C:\\x\" } }"
    );
    OA_CHECK(escaped.size() == 3);
    OA_CHECK(escaped.size() == 3 && escaped[0] == fs::path("D:\\SteamLibrary"));
    OA_CHECK(escaped.size() == 3 && escaped[1] == fs::path("/with \"quote\""));
    OA_CHECK(escaped.size() == 3 && escaped[2] == fs::path("C:\\x"));

    // Older lists name each library by a number directly under the top block.
    const auto older = installs::library_folders(
        "\"LibraryFolders\"\n{\n\t\"TimeNextStatsReport\"\t\t\"1700000000\"\n"
        "\t\"ContentStatsID\"\t\t\"-123\"\n\t\"1\"\t\t\"/mnt/games\"\n}\n"
    );
    OA_CHECK(older.size() == 1 && older[0] == "/mnt/games");

    // Comments are skipped.
    const auto commented = installs::library_folders(
        "// a comment\n\"libraryfolders\" { \"0\" { \"path\" \"/a\" // and another\n } }"
    );
    OA_CHECK(commented.size() == 1 && commented[0] == "/a");

    // A list that breaks off, or breaks the format, gives what was read before the fault.
    const auto cut = installs::library_folders(
        "\"libraryfolders\" { \"0\" { \"path\" \"/a\" } \"1\" { \"path\" \"/b"
    );
    OA_CHECK(cut.size() == 1 && cut[0] == "/a");
    const auto extra_close = installs::library_folders(
        "\"libraryfolders\" { \"0\" { \"path\" \"/a\" } } } \"path\" \"/c\""
    );
    OA_CHECK(extra_close.size() == 1 && extra_close[0] == "/a");
    const auto no_value =
        installs::library_folders("\"libraryfolders\" { \"0\" { \"path\" \"/a\" \"path\" } }");
    OA_CHECK(no_value.size() == 1 && no_value[0] == "/a");
    std::string deep;
    for (std::size_t level = 0; level <= installs::most_nesting; ++level)
        deep += "\"k\" { ";
    deep += "\"path\" \"/deep\"";
    OA_CHECK(installs::library_folders(deep).empty());
    OA_CHECK(installs::library_folders("").empty());
    OA_CHECK(installs::library_folders("{{{").empty());
}

/// Checks the reader of Steam's app manifests.
void check_manifest() {
    const std::string text = manifest(installs::total_annihilation_steam_app, "Total Annihilation");
    const auto install_dir =
        installs::manifest_install_dir(text, installs::total_annihilation_steam_app);
    OA_CHECK(install_dir && *install_dir == "Total Annihilation");
    OA_CHECK(!installs::manifest_install_dir(text, 1));
    OA_CHECK(!installs::manifest_install_dir(manifest(42, "Other"), 298030));
    OA_CHECK(!installs::manifest_install_dir("\"AppState\" { \"appid\" \"298030\" }", 298030));
    OA_CHECK(!installs::manifest_install_dir(
        "\"AppState\" { \"appid\" \"298030x\" \"installdir\" \"TA\" }", 298030
    ));
    // Keys inside a nested block are not the manifest's.
    OA_CHECK(!installs::manifest_install_dir(
        "\"AppState\" { \"UserConfig\" { \"appid\" \"298030\" \"installdir\" \"TA\" } }", 298030
    ));
    // A manifest cut short after both keys still names the folder.
    const auto cut = installs::manifest_install_dir(
        "\"AppState\" { \"appid\" \"298030\" \"installdir\" \"TA\" \"name\" \"Total", 298030
    );
    OA_CHECK(cut && *cut == "TA");
    OA_CHECK(!installs::manifest_install_dir("\"AppState\" { \"appid\" \"2980", 298030));
}

/// Checks the reader of Heroic's installed list, malformed lists included.
void check_heroic_list() {
    const std::string text =
        "{\n  \"installed\": [\n    {\n      \"platform\": \"windows\",\n"
        "      \"executable\": \"Launcher.exe\",\n"
        "      \"install_path\": \"/home/deck/Games/Heroic/Total Annihilation\",\n"
        "      \"install_size\": \"1.1 GiB\",\n      \"is_dlc\": false,\n"
        "      \"version\": \"3.1\",\n      \"appName\": \"1207658782\",\n"
        "      \"installedDLCs\": [],\n      \"cyberpunk\": {\"install_path\": \"/no\"}\n    },\n"
        "    {\"install_path\": \"/home/deck/Games/Heroic/Caf\\u00e9 \\ud83d\\ude00\", "
        "\"pinned\": null, \"size\": -1.5e3}\n  ],\n"
        "  \"other\": [{\"install_path\": \"/elsewhere\"}]\n}\n";
    const auto paths = installs::heroic_install_paths(text);
    OA_CHECK(paths.size() == 2);
    OA_CHECK(paths.size() == 2 && paths[0] == "/home/deck/Games/Heroic/Total Annihilation");
    OA_CHECK(
        paths.size() == 2 &&
        utf8(paths[1]) == "/home/deck/Games/Heroic/Caf\xc3\xa9 \xf0\x9f\x98\x80"
    );
    // A Windows path, its backslashes escaped as Heroic writes them.
    const auto windows = installs::heroic_install_paths(
        "{\"installed\": [{\"install_path\": \"C:\\\\Games\\\\Total Annihilation\"}]}"
    );
    OA_CHECK(windows.size() == 1 && windows[0] == fs::path("C:\\Games\\Total Annihilation"));
    // The list "installed" must be the top object's.
    OA_CHECK(
        installs::heroic_install_paths("{\"other\": {\"installed\": [{\"install_path\": \"/x\"}]}}")
            .empty()
    );
    // A list that breaks off, or breaks the format, gives what was read before the fault.
    const auto cut = installs::heroic_install_paths(
        "{\"installed\": [{\"install_path\": \"/a\"}, {\"install_path\": \"/b\""
    );
    OA_CHECK(cut.size() == 2 && cut[0] == "/a" && cut[1] == "/b");
    const auto no_comma = installs::heroic_install_paths(
        "{\"installed\": [{\"install_path\": \"/a\"} {\"install_path\": \"/c\"}]}"
    );
    OA_CHECK(no_comma.size() == 1 && no_comma[0] == "/a");
    const auto bad_escape = installs::heroic_install_paths(
        "{\"installed\": [{\"install_path\": \"/a\"}, {\"install_path\": \"/b\\q\"}]}"
    );
    OA_CHECK(bad_escape.size() == 1 && bad_escape[0] == "/a");
    const auto lone_surrogate = installs::heroic_install_paths(
        "{\"installed\": [{\"install_path\": \"/a\"}, {\"install_path\": \"/\\udc00\"}]}"
    );
    OA_CHECK(lone_surrogate.size() == 1);
    const auto number = installs::heroic_install_paths(
        "{\"installed\": [{\"install_path\": 5}, {\"install_path\": \"/c\"}]}"
    );
    OA_CHECK(number.size() == 1 && number[0] == "/c");
    std::string deep = "{\"installed\": [";
    for (std::size_t level = 0; level <= installs::most_nesting; ++level)
        deep += "[";
    OA_CHECK(installs::heroic_install_paths(deep).empty());
    OA_CHECK(installs::heroic_install_paths("").empty());
    OA_CHECK(installs::heroic_install_paths("[1, 2]").empty());
    OA_CHECK(installs::heroic_install_paths("{\"installed\": []}").empty());
}

/// Builds synthetic home folders and checks what the search finds in them, and in what order.
///
/// @param temporary a folder of the test's own
void check_search(const fs::path& temporary) {
    const fs::path home = temporary / "home" / "deck";
    const fs::path media = temporary / "run" / "media";
    const fs::path sd_card = media / "mmcblk0p1";
    installs::SearchRoots roots = installs::search_roots_under(home);
    roots.removable_mounts = {media};

    // Native Steam: its own library and an SD card's, both holding the game; the SD card is
    // named twice.
    const fs::path steam = home / ".local" / "share" / "Steam";
    const fs::path native_game = steam / "steamapps" / "common" / "Total Annihilation";
    const fs::path sd_game = sd_card / "steamapps" / "common" / "Total Annihilation";
    write_file(
        steam / "steamapps" / "libraryfolders.vdf",
        "\"libraryfolders\"\n{\n\t\"0\"\n\t{\n\t\t\"path\"\t\t\"" + quoted_text(steam) +
            "\"\n\t}\n\t\"1\"\n\t{\n\t\t\"path\"\t\t\"" + quoted_text(sd_card) +
            "\"\n\t}\n"
            "\t\"2\"\n\t{\n\t\t\"path\"\t\t\"" +
            quoted_text(sd_card) +
            "\"\n\t}\n"
            "\t\"3\"\n\t{\n\t\t\"path\"\t\t\"" +
            quoted_text(temporary / "unplugged") + "\"\n\t}\n}\n"
    );
    write_file(
        steam / "steamapps" / manifest_file(installs::total_annihilation_steam_app),
        manifest(298030, "Total Annihilation")
    );
    make_game_folder(native_game);
    write_file(
        sd_card / "steamapps" / manifest_file(installs::total_annihilation_steam_app),
        manifest(298030, "Total Annihilation")
    );
    make_game_folder(sd_game);
    // A library whose manifest names a folder outside steamapps/common is passed over, and a
    // folder that is there but has no manifest is never guessed.
    const fs::path sneaky = temporary / "sneaky";
    write_file(
        sneaky / "steamapps" / manifest_file(installs::total_annihilation_steam_app),
        manifest(298030, "../../outside")
    );
    make_game_folder(temporary / "outside");
    make_game_folder(temporary / "guessed" / "steamapps" / "common" / "Total Annihilation");
    write_file(
        steam / "config" / "libraryfolders.vdf",
        "\"libraryfolders\" { \"0\" { \"path\" \"" + quoted_text(sneaky) +
            "\" } \"1\" { \"path\" \"" + quoted_text(temporary / "guessed") + "\" } }"
    );
    // ~/.steam/steam is a link to the same Steam folder: its libraries are searched once.
    std::error_code link_error;
    fs::create_directories(home / ".steam", link_error);
    fs::create_directory_symlink(steam, home / ".steam" / "steam", link_error);

    // Flatpak Steam, with no library list: its own folder is its library.
    const fs::path flatpak_steam =
        home / ".var" / "app" / "com.valvesoftware.Steam" / ".local" / "share" / "Steam";
    const fs::path flatpak_game = flatpak_steam / "steamapps" / "common" / "TA Flatpak";
    write_file(
        flatpak_steam / "steamapps" / manifest_file(installs::total_annihilation_steam_app),
        manifest(298030, "TA Flatpak")
    );
    make_game_folder(flatpak_game, "TOTALA1.HPI");

    // Heroic, native and Flatpak: another game's folder and a folder named twice.
    const fs::path heroic_game = home / "Games" / "Heroic" / "Total Annihilation";
    const fs::path other_game = home / "Games" / "Heroic" / "Another Game";
    make_game_folder(heroic_game, "TotalA1.Hpi");
    write_file(other_game / "game.exe", "MZ");
    write_file(
        home / ".config" / "heroic" / "gog_store" / "installed.json",
        "{\"installed\": [{\"install_path\": \"" + quoted_text(other_game) +
            "\"}, {\"install_path\": \"" + quoted_text(heroic_game) + "\"}]}"
    );
    const fs::path flatpak_heroic_game = home / "Games" / "Heroic Flatpak" / "Total Annihilation";
    make_game_folder(flatpak_heroic_game);
    write_file(
        home / ".var" / "app" / "com.heroicgameslauncher.hgl" / "config" / "heroic" / "gog_store" /
            "installed.json",
        "{\"installed\": [{\"install_path\": \"" + quoted_text(flatpak_heroic_game) +
            "\"}, {\"install_path\": \"" + quoted_text(native_game) +
            "\"}, {\"install_path\": \"relative\"}"
    );

    // A Lutris prefix, with copies of the archive where the search never looks: under drive_c's
    // windows folder, deeper than the search goes, and below a game folder already found.
    const fs::path prefix = home / "Games" / "total-annihilation" / "drive_c";
    const fs::path lutris_game = prefix / "GOG Games" / "Total Annihilation";
    make_game_folder(lutris_game);
    make_game_folder(lutris_game / "nested copy");
    make_game_folder(prefix / "windows" / "system32");
    make_game_folder(prefix / "a" / "b" / "c" / "d" / "too deep");
    // A Bottles bottle, the game four folders below drive_c.
    const fs::path bottle = home / ".var" / "app" / "com.usebottles.bottles" / "data" / "bottles" /
                            "bottles" / "Total Annihilation" / "drive_c";
    const fs::path bottles_game =
        bottle / "Program Files (x86)" / "GOG Galaxy" / "Games" / "Total Annihilation";
    make_game_folder(bottles_game);

    const std::vector<installs::Candidate> found = installs::find_candidates(roots);
    const std::vector<std::pair<fs::path, installs::Source>> expected{
        {native_game, installs::Source::steam},
        {sd_game, installs::Source::steam},
        {flatpak_game, installs::Source::steam},
        {heroic_game, installs::Source::heroic},
        {flatpak_heroic_game, installs::Source::heroic},
        {lutris_game, installs::Source::lutris},
        {bottles_game, installs::Source::bottles},
    };
    OA_CHECK(found.size() == expected.size());
    for (std::size_t index = 0; index < std::min(found.size(), expected.size()); ++index) {
        OA_CHECK(found[index].folder == canonical_of(expected[index].first));
        OA_CHECK(found[index].source == expected[index].second);
        OA_CHECK(found[index].removable == (index == 1));
    }
    for (const installs::Candidate& candidate : found)
        if (candidate.folder == canonical_of(sd_game))
            OA_CHECK(
                installs::source_words(candidate.source, candidate.removable) ==
                "your Steam library on the SD card"
            );

    OA_CHECK(installs::holds_game_archive(flatpak_game));
    OA_CHECK(installs::holds_game_archive(heroic_game));
    OA_CHECK(!installs::holds_game_archive(other_game));
    OA_CHECK(!installs::holds_game_archive(temporary / "missing"));

    // Places that are not there are passed over.
    installs::SearchRoots nowhere = installs::search_roots_under(temporary / "nobody");
    nowhere.removable_mounts = {temporary / "no media"};
    OA_CHECK(installs::find_candidates(nowhere).empty());
    OA_CHECK(installs::find_candidates({}).empty());

    // At most most_candidates folders are returned.
    installs::SearchRoots many;
    many.prefix_parents = {temporary / "many"};
    for (std::size_t index = 0; index < installs::most_candidates + 3; ++index)
        make_game_folder(
            temporary / "many" / ("p" + std::to_string(100 + index)) / "drive_c" / "TA"
        );
    OA_CHECK(installs::find_candidates(many).size() == installs::most_candidates);
}

} // namespace

/// Runs the checks.
int main() {
    check_source_words();
    check_search_roots();
    check_library_folders();
    check_manifest();
    check_heroic_list();
    const fs::path temporary =
        fs::temp_directory_path() /
        ("oa-game-installs-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check_search(temporary);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "check failed: %s\n", error.what());
        ++oa::test::failed_checks();
    }
    std::error_code error;
    fs::remove_all(temporary, error);
    return oa::test::check_exit_status();
}
