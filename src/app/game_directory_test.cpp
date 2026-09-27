// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game directory resolution order with a scripted dialog host, installation
// checks on synthetic installs, and the stored folder's preference round trip.
// With --install it checks that the installation OA_GAME_DIR names is usable.
#include "oa/app/game_directory.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/test/game_data.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oa::app;

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

struct Pick {
    FolderPick outcome{};
    fs::path folder;
    std::string error;
};

// Answers the dialog from a script and records what resolution asked.
struct ScriptedHost {
    std::vector<Pick> picks;
    std::map<fs::path, bool> installs;
    // Folders whose inspection throws this text.
    std::map<fs::path, std::string> unreadable;
    std::vector<fs::path> starts;
    std::vector<Notice> kinds;
    std::vector<std::string> notices;
    std::size_t inspections = 0;

    GameDirectoryHost host() { return {this, pick_folder, tell_user, inspect}; }

    static FolderPick
    pick_folder(void* context, const fs::path& start, fs::path* chosen, std::string* error) {
        auto& self = *static_cast<ScriptedHost*>(context);
        if (self.starts.size() >= self.picks.size()) {
            self.starts.push_back(start);
            return FolderPick::cancelled;
        }
        const auto& pick = self.picks[self.starts.size()];
        self.starts.push_back(start);
        *chosen = pick.folder;
        *error = pick.error;
        return pick.outcome;
    }

    static void tell_user(void* context, Notice kind, std::string_view text) {
        auto& self = *static_cast<ScriptedHost*>(context);
        self.kinds.push_back(kind);
        self.notices.emplace_back(text);
    }

    static GameInstall inspect(void* context, const fs::path& folder) {
        auto& self = *static_cast<ScriptedHost*>(context);
        ++self.inspections;
        if (const auto failure = self.unreadable.find(folder); failure != self.unreadable.end())
            throw std::runtime_error(failure->second);
        GameInstall install;
        const auto found = self.installs.find(folder);
        install.folder = found != self.installs.end();
        if (install.folder && found->second)
            install.archives.push_back(folder / "totala1.hpi");
        return install;
    }
};

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

const fs::path kGog = "/games/Total Annihilation (GOG)";
const fs::path kCd = "/games/cd-install";
const fs::path kDownloads = "/home/player/Downloads";
const fs::path kMoved = "/games/moved";

ScriptedHost with_installs() {
    ScriptedHost host;
    host.installs = {{kGog, true}, {kCd, true}, {kDownloads, false}};
    return host;
}

void check_resolution_order() {
    {
        auto host = with_installs();
        auto h = host.host();
        const auto result = resolve_game_directory({kCd, path_to_utf8(kGog), true, false}, h);
        expect(
            result && result->path == kCd && result->installation == kCd &&
                result->source == GameDirectorySource::argument && result->archives.size() == 1,
            "--game-dir wins over the stored folder and --choose-game-dir"
        );
        expect(
            host.starts.empty() && host.notices.empty() && host.inspections == 1,
            "--game-dir is inspected, never asked for or told about"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        const auto result =
            resolve_game_directory({"/elsewhere", path_to_utf8(kGog), false, false, true}, h);
        expect(
            result && result->path == "/elsewhere" && result->installation == "/elsewhere" &&
                result->archives.empty() && host.inspections == 0,
            "--game-dir with --archive is taken as given"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        std::string refusal;
        try {
            (void)resolve_game_directory({"/elsewhere", std::nullopt, false, false}, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "game directory does not exist: /elsewhere") && host.notices.empty(),
            "a missing --game-dir folder is refused"
        );
        refusal.clear();
        try {
            (void)resolve_game_directory({kDownloads, std::nullopt, false, false}, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "/home/player/Downloads") &&
                contains(refusal, "no Total Annihilation archives") && host.starts.empty(),
            "a --game-dir folder without archives is refused and says why"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(kGog), false, true}, h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored &&
                result->archives.size() == 1,
            "an unattended run starts the usable stored folder"
        );
        expect(host.starts.empty() && host.notices.empty(), "an unattended run shows no dialog");
    }
    {
        auto host = with_installs();
        auto h = host.host();
        std::string refusal;
        try {
            (void)resolve_game_directory({{}, path_to_utf8(kMoved), false, true}, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "/games/moved") && contains(refusal, "--game-dir PATH") &&
                host.starts.empty() && host.notices.empty(),
            "an unattended run refuses an unusable stored folder and names --game-dir"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        std::string refusal;
        try {
            (void)resolve_game_directory({{}, std::nullopt, false, true}, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "--game-dir PATH") && host.starts.empty() && host.notices.empty() &&
                host.inspections == 0,
            "an unattended first run names --game-dir without asking"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(kGog), false, false}, h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored &&
                result->archives.size() == 1,
            "a usable stored folder starts the game with its archives"
        );
        expect(
            host.starts.empty() && host.notices.empty(), "a usable stored folder shows no dialog"
        );
    }
    {
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kCd, {}}};
        auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(kMoved), false, false}, h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::chosen,
            "a moved stored folder is chosen again"
        );
        expect(
            host.starts.size() == 1 && host.starts[0] == kMoved,
            "the dialog opens at the moved folder"
        );
        expect(
            host.notices.size() == 1 && contains(host.notices[0], "chosen earlier") &&
                contains(host.notices[0], "/games/moved"),
            "the user is told the stored folder no longer works"
        );
    }
    {
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kCd, {}}};
        auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(kGog), true, false}, h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::chosen,
            "--choose-game-dir replaces a usable stored folder"
        );
        expect(
            host.starts.size() == 1 && host.starts[0] == kGog && host.notices.empty(),
            "--choose-game-dir opens the dialog at the stored folder without a notice"
        );
    }
    {
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kDownloads, {}}, {FolderPick::chosen, kGog, {}}};
        auto h = host.host();
        const auto result = resolve_game_directory({}, h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::chosen &&
                result->archives.size() == 1,
            "the first run ends with the usable folder"
        );
        expect(
            host.starts.size() == 2 && host.starts[0].empty() && host.starts[1] == kDownloads,
            "an unusable choice asks again from that folder"
        );
        expect(
            host.notices.size() == 2 && host.kinds[0] == Notice::information &&
                contains(host.notices[0], "totala1.hpi") && host.kinds[1] == Notice::warning &&
                contains(host.notices[1], "/home/player/Downloads") &&
                contains(host.notices[1], "no Total Annihilation archives"),
            "the first run explains the dialog, then why the choice was refused"
        );
    }
    {
        auto host = with_installs();
        host.picks = {{FolderPick::cancelled, {}, {}}};
        auto h = host.host();
        const auto result = resolve_game_directory({{}, std::string{}, true, false}, h);
        expect(!result, "a cancelled dialog does not start the game");
        expect(
            host.notices.size() == 1 && contains(host.notices[0], "--game-dir PATH"),
            "a cancel explains --game-dir and nothing else"
        );
    }
    {
        auto host = with_installs();
        host.picks = {
            {FolderPick::unavailable, {}, "File dialog driver unsupported"},
            {FolderPick::unavailable, {}, "File dialog driver unsupported"},
        };
        auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(kMoved), false, false}, h);
        expect(!result, "no dialog does not start the game");
        expect(host.starts.size() == 2, "a failed dialog is tried once more");
        expect(
            host.notices.size() == 2 && host.kinds[1] == Notice::warning &&
                contains(host.notices[1], "File dialog driver unsupported") &&
                contains(host.notices[1], "--game-dir PATH"),
            "no dialog explains why and names --game-dir"
        );
    }
    {
        auto host = with_installs();
        host.picks = {
            {FolderPick::unavailable, {}, "no file system path"},
            {FolderPick::chosen, kDownloads, {}},
            {FolderPick::unavailable, {}, "no file system path"},
            {FolderPick::chosen, kGog, {}},
        };
        auto h = host.host();
        const auto result = resolve_game_directory({}, h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::chosen,
            "a choice the dialog cannot return as a folder asks again"
        );
        expect(
            host.starts.size() == 4 && host.starts[1].empty() && host.starts[3] == kDownloads,
            "the dialog reopens where it was after a failed choice"
        );
        expect(
            host.notices.size() == 2 && contains(host.notices[1], "/home/player/Downloads"),
            "a failed choice is asked again without a notice"
        );
    }
    {
        auto host = with_installs();
        const fs::path unreadable = "/home/player/Desktop";
        host.unreadable = {{unreadable, "No mapping for the Unicode character"}};
        host.picks = {{FolderPick::chosen, unreadable, {}}, {FolderPick::chosen, kCd, {}}};
        auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(unreadable), false, false}, h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::chosen,
            "a folder the check cannot read is refused and the dialog asked again"
        );
        expect(
            host.starts.size() == 2 && host.starts[0] == unreadable && host.starts[1] == unreadable,
            "an unreadable folder reopens the dialog there"
        );
        expect(
            host.notices.size() == 2 && contains(host.notices[0], "chosen earlier") &&
                contains(host.notices[0], "No mapping for the Unicode character") &&
                host.kinds[1] == Notice::warning &&
                contains(host.notices[1], "No mapping for the Unicode character"),
            "the user is told why an unreadable folder was refused"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        std::string refusal;
        try {
            (void)resolve_game_directory({{}, path_to_utf8(kGog), true, true}, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "--choose-game-dir opens a dialog") && host.starts.empty(),
            "--choose-game-dir is refused where nobody can answer the dialog"
        );
    }
}

void check_environment() {
    expect(!unattended_environment("", ""), "a desktop session may ask");
    expect(!unattended_environment("", "cocoa"), "a named desktop driver may ask");
    expect(unattended_environment("true", ""), "CI never asks");
    expect(unattended_environment("", "dummy"), "the dummy driver never asks");
    expect(unattended_environment("", "Offscreen"), "the offscreen driver never asks");
    expect(unattended_environment("", "dummy,x11"), "a driver list headed by dummy never asks");
    expect(!unattended_environment("", "x11,dummy"), "dummy only as a fallback may ask");
}

void write_file(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream(path, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
}

std::vector<uint8_t> archive_of(std::initializer_list<const char*> names) {
    std::vector<oa::HpiWriteFile> files;
    for (const char* name : names)
        files.push_back({name, {'T', 'A'}});
    return oa::write_hpi(files);
}

void check_installs(const fs::path& temporary) {
#if defined(_WIN32)
    // The asset store names files through the ANSI code page on Windows.
    const auto install = temporary / "Jeux video" / "Total Annihilation";
#else
    const auto install = temporary / fs::path(u8"Jeux vid\u00e9o") /
                         fs::path(u8"\u30c8\u30fc\u30bf\u30eb Annihilation");
#endif
    fs::create_directories(install);
    write_file(
        install / "TOTALA1.HPI",
        archive_of(
            {"guis/mainmenu.gui",
             "palettes/palette.pal",
             "gamedata/sidedata.tdf",
             "gamedata/sound.tdf"}
        )
    );
    const auto good = inspect_game_install(install);
    expect(
        usable(good) && good.archives.size() == 1, "an install with the required files is usable"
    );

    const auto empty = temporary / "empty folder";
    fs::create_directories(empty);
    const auto none = inspect_game_install(empty);
    expect(
        none.folder && none.archives.empty() && !usable(none),
        "a folder without archives is not usable"
    );

    const auto partial = temporary / "partial";
    fs::create_directories(partial);
    write_file(partial / "tactics1.hpi", archive_of({"gamedata/sound.tdf"}));
    const auto lacking = inspect_game_install(partial);
    expect(
        !usable(lacking) && lacking.archives.size() == 1 && lacking.missing.size() == 3,
        "an archive without the required files is not usable"
    );

    const auto missing = inspect_game_install(temporary / "missing");
    expect(!missing.folder && !usable(missing), "a missing folder is not usable");

    expect(dialog_location({}).empty(), "no start folder leaves the dialog's choice");
    const auto separator = static_cast<char>(fs::path::preferred_separator);
    const auto location = dialog_location(install);
    expect(
        !location.empty() && location.back() == separator &&
            path_from_utf8(location) == fs::path(install).make_preferred() / "",
        "the dialog opens inside an existing folder"
    );
    expect(
        dialog_location(install / "gone" / "deeper") == location,
        "the dialog opens at the nearest existing folder"
    );
}

void check_preferences(const fs::path& temporary) {
    std::vector<fs::path> folders{
        temporary / fs::path(u8"Jeux vid\u00e9o") /
            fs::path(u8"\u30c8\u30fc\u30bf\u30eb Annihilation (GOG)"),
        temporary / "Program Files (x86)" / "GOG Galaxy" / "Games" / "Total Annihilation",
    };
#if !defined(_WIN32)
    folders.push_back(temporary / "quote \" and back\\slash");
#endif
    const auto file = temporary / "profile" / "preferences.conf";
    for (const auto& folder : folders) {
        oa::platform::preferences::Values values{{"Total Annihilation|Game Speed", "10"}};
        remember_game_directory(values, folder);
        oa::platform::preferences::save(file, values);
        const auto loaded = oa::platform::preferences::load(file);
        const auto stored = stored_game_directory(loaded);
        expect(
            stored && path_from_utf8(*stored) == folder,
            "the stored folder round-trips through the preferences file"
        );
        expect(
            loaded.size() == 2 && loaded.at("Total Annihilation|Game Speed") == "10",
            "the stored folder leaves the game's preferences alone"
        );
    }
    expect(
        !stored_game_directory({{"Total Annihilation|Game Speed", "10"}}),
        "no stored folder before the first choice"
    );
    expect(
        path_to_utf8(path_from_utf8("Jeux vid\xc3\xa9o")) == "Jeux vid\xc3\xa9o" &&
            path_from_utf8("Jeux vid\xc3\xa9o") == fs::path(u8"Jeux vid\u00e9o"),
        "UTF-8 paths convert both ways"
    );
    expect(
        preference_file(fs::path("explicit.conf")) == "explicit.conf",
        "--preferences-file names the stored folder's file"
    );
}

} // namespace

int main(int argc, char** argv) {
    if (argc == 2 && std::string_view(argv[1]) == "--install") {
        const auto folder = oa::test::require_game_directory("the installation check");
        const auto install = inspect_game_install(folder);
        expect(usable(install), "the installation OA_GAME_DIR names is usable");
        return failures == 0 ? 0 : 1;
    }
    const auto temporary =
        fs::temp_directory_path() /
        ("oa-game-directory-test-" +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    check_resolution_order();
    check_environment();
    try {
        check_installs(temporary);
        check_preferences(temporary);
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        ++failures;
    }
    std::error_code error;
    fs::remove_all(temporary, error);
    if (failures != 0)
        return 1;
    std::puts("game directory checks passed");
    return 0;
}
