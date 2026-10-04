// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game directory resolution order with a scripted dialog host, with and
// without the platform's default folder and advice and with no dialog at all,
// installation checks on synthetic installs, and the stored folder's
// preference round trip.
// With --install it checks that the installation OA_GAME_DIR names is usable.
#include "oa/app/game_directory.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/platform/preferences.hpp"
#include "oa/test/game_data.hpp"
#include <chrono>
#include <cstdio>
#include <fstream>
#include <functional>
#include <initializer_list>
#include <map>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
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
    // The look-again notice: the texts and labels it was shown with, what
    // each press changes before resolution looks again, and how many times
    // it can be shown at all.
    std::vector<std::string> asked;
    std::vector<std::string> labels;
    std::vector<std::function<void(ScriptedHost&)>> presses;
    std::size_t shown_at_most = 1000;
    // What looking for the platform's default folder again finds, and how
    // many times it was looked for.
    std::optional<fs::path> platform_found;
    std::size_t platform_lookups = 0;

    GameDirectoryHost host() { return {this, pick_folder, tell_user, inspect}; }

    // The host with the look-again notice and the platform's search.
    GameDirectoryHost asking_host() {
        auto h = host();
        h.ask = ask;
        h.find_platform_default = find_platform_default;
        return h;
    }

    static bool ask(void* context, Notice kind, std::string_view text, std::string_view button) {
        auto& self = *static_cast<ScriptedHost*>(context);
        if (self.asked.size() >= self.shown_at_most)
            return false;
        self.kinds.push_back(kind);
        self.asked.emplace_back(text);
        self.labels.emplace_back(button);
        if (self.asked.size() <= self.presses.size())
            self.presses[self.asked.size() - 1](self);
        return true;
    }

    static bool find_platform_default(void* context, fs::path* folder) {
        auto& self = *static_cast<ScriptedHost*>(context);
        ++self.platform_lookups;
        if (!self.platform_found)
            return false;
        *folder = *self.platform_found;
        return true;
    }

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

const fs::path kPlatform = "/container/Documents/Total Annihilation";
constexpr const char* kAdvice =
    "Copy your Total Annihilation folder into the game's Documents folder.";

// A request with the platform's default folder and advice, as
// find_game_directory fills it where the platform's hooks give them.
GameDirectoryRequest platform_request(
    const fs::path& platform,
    std::optional<std::string> stored,
    bool unattended = false,
    std::string advice = kAdvice
) {
    GameDirectoryRequest request;
    request.stored = std::move(stored);
    request.unattended = unattended;
    request.platform_default = platform;
    request.platform_advice = std::move(advice);
    return request;
}

// The scripted host as a build without the folder dialog gives it: no
// dialog to pick with.
GameDirectoryHost without_dialog(ScriptedHost& host) {
    auto h = host.host();
    h.pick_folder = nullptr;
    return h;
}

void check_platform_default() {
    {
        auto host = with_installs();
        host.installs[kPlatform] = true;
        auto h = host.host();
        auto request = platform_request(kPlatform, path_to_utf8(kGog));
        request.argument = kCd;
        const auto result = resolve_game_directory(request, h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::argument &&
                host.inspections == 1,
            "--game-dir wins over the platform's default folder, which is not looked at"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = true;
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kGog)), h);
        expect(
            result && result->path == kPlatform && result->installation == kPlatform &&
                result->source == GameDirectorySource::platform && result->archives.size() == 1,
            "a usable platform default wins over a usable stored folder"
        );
        expect(
            host.starts.empty() && host.notices.empty() && host.inspections == 1,
            "a usable platform default asks nothing and looks at nothing else"
        );
        expect(
            result && result->source != GameDirectorySource::chosen,
            "a platform default is not remembered as the chosen folder"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kGog)), h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored &&
                host.starts.empty() && host.notices.empty() && host.inspections == 2,
            "a missing platform default falls back to the usable stored folder without a notice"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = false;
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kGog)), h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored,
            "a platform default without archives falls back to the stored folder"
        );
    }
    {
        auto host = with_installs();
        host.unreadable = {{kPlatform, "No mapping for the Unicode character"}};
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kGog)), h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored,
            "a platform default the check cannot read falls back to the stored folder"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = true;
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kMoved), true), h);
        expect(
            result && result->path == kPlatform &&
                result->source == GameDirectorySource::platform && host.notices.empty(),
            "an unattended run takes a usable platform default before the stored folder"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kGog), true), h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored,
            "an unattended run falls back from a missing platform default to the stored folder"
        );
    }
    {
        auto host = with_installs();
        auto h = host.host();
        std::string refusal;
        try {
            (void)resolve_game_directory(platform_request(kPlatform, std::nullopt, true), h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, path_to_utf8(kPlatform)) &&
                contains(refusal, "The folder does not exist.") &&
                contains(refusal, "--game-dir PATH") && host.starts.empty() && host.notices.empty(),
            "an unattended run with neither folder names the platform default and --game-dir"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = true;
        host.picks = {{FolderPick::chosen, kCd, {}}};
        auto h = host.host();
        auto request = platform_request(kPlatform, path_to_utf8(kGog));
        request.choose = true;
        const auto result = resolve_game_directory(request, h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::chosen &&
                host.starts.size() == 1 && host.starts[0] == kGog,
            "--choose-game-dir passes over a usable platform default to the dialog"
        );
    }
    {
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kCd, {}}};
        auto h = host.host();
        const auto result = resolve_game_directory(platform_request(kPlatform, std::nullopt), h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::chosen,
            "a first run with a missing platform default asks with the dialog"
        );
        expect(
            host.starts.size() == 1 && host.starts[0] == kPlatform,
            "the dialog opens where the platform keeps the game"
        );
        expect(
            host.notices.size() == 1 && host.kinds[0] == Notice::information &&
                contains(host.notices[0], path_to_utf8(kPlatform)) &&
                contains(host.notices[0], "The folder does not exist.") &&
                contains(host.notices[0], "totala1.hpi"),
            "the first run says where the game was looked for before the dialog"
        );
    }
    {
        auto host = with_installs();
        host.picks = {
            {FolderPick::unavailable, {}, "File dialog driver unsupported"},
            {FolderPick::unavailable, {}, "File dialog driver unsupported"},
        };
        auto h = host.host();
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kMoved)), h);
        expect(!result, "no usable folder and no dialog does not start the game");
        expect(
            host.notices.size() == 2 && host.kinds[1] == Notice::warning &&
                contains(host.notices[1], "File dialog driver unsupported") &&
                contains(host.notices[1], kAdvice) && !contains(host.notices[1], "--game-dir"),
            "an unavailable dialog gives the platform's advice in place of --game-dir"
        );
    }
}

void check_without_dialog() {
    {
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kCd, {}}};
        const auto h = without_dialog(host);
        const auto result = resolve_game_directory(platform_request(kPlatform, std::nullopt), h);
        expect(!result, "without a dialog, no usable folder does not start the game");
        expect(host.starts.empty(), "a build without the folder dialog never asks for a folder");
        expect(
            host.notices.size() == 1 && host.kinds[0] == Notice::warning &&
                contains(host.notices[0], path_to_utf8(kPlatform)) &&
                contains(host.notices[0], "The folder does not exist.") &&
                contains(host.notices[0], kAdvice) && !contains(host.notices[0], "--game-dir"),
            "without a dialog, one notice says where the game was looked for and the advice"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = false;
        host.picks = {{FolderPick::chosen, kCd, {}}};
        const auto h = without_dialog(host);
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kMoved)), h);
        expect(!result && host.starts.empty(), "without a dialog, a moved folder is not asked for");
        expect(
            host.notices.size() == 1 && contains(host.notices[0], path_to_utf8(kPlatform)) &&
                contains(host.notices[0], "no Total Annihilation archives") &&
                contains(host.notices[0], "chosen earlier") &&
                contains(host.notices[0], "/games/moved") && contains(host.notices[0], kAdvice),
            "without a dialog, the notice gives why each folder cannot be played"
        );
    }
    {
        auto host = with_installs();
        const auto h = without_dialog(host);
        const auto result = resolve_game_directory({{}, path_to_utf8(kMoved), false, false}, h);
        expect(!result && host.starts.empty(), "without a dialog or a platform default, nothing");
        expect(
            host.notices.size() == 1 && contains(host.notices[0], "chosen earlier") &&
                contains(host.notices[0], "--game-dir PATH"),
            "without the platform's advice, the notice names --game-dir"
        );
    }
    {
        auto host = with_installs();
        const auto h = without_dialog(host);
        const auto result = resolve_game_directory({}, h);
        expect(
            !result && host.notices.size() == 1 &&
                contains(host.notices[0], "needs your Total Annihilation installation") &&
                contains(host.notices[0], "--game-dir PATH"),
            "a first run without a dialog or platform names --game-dir"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = true;
        const auto h = without_dialog(host);
        const auto result = resolve_game_directory(platform_request(kPlatform, std::nullopt), h);
        expect(
            result && result->source == GameDirectorySource::platform && host.notices.empty(),
            "without a dialog, a usable platform default starts the game"
        );
    }
    {
        auto host = with_installs();
        const auto h = without_dialog(host);
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kGog)), h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored &&
                host.notices.empty(),
            "without a dialog, a usable stored folder still starts the game"
        );
    }
    {
        auto host = with_installs();
        const auto h = without_dialog(host);
        auto request = platform_request(kPlatform, path_to_utf8(kGog));
        request.choose = true;
        std::string refusal;
        try {
            (void)resolve_game_directory(request, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "--choose-game-dir") && contains(refusal, "does not offer") &&
                host.notices.empty(),
            "--choose-game-dir is refused where the build offers no dialog"
        );
    }
}

// The missing-folder notice's look-again button (Phase 0 of bringing the
// game files in): the notice stays up, looking again after each press,
// until a folder can be played.
void check_look_again() {
    constexpr const char* kLabel = "Check again";
    {
        // The folder is copied while the notice shows: missing at the start,
        // there without its archives after the first press, whole after the
        // second.
        auto host = with_installs();
        host.presses = {
            [](ScriptedHost& self) {
                self.platform_found = kPlatform;
                self.installs[kPlatform] = false;
            },
            [](ScriptedHost& self) { self.installs[kPlatform] = true; },
        };
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        h.find_platform_default = ScriptedHost::find_platform_default;
        auto request = platform_request({}, std::nullopt);
        request.check_again_label = kLabel;
        const auto result = resolve_game_directory(request, h);
        expect(
            result && result->path == kPlatform &&
                result->source == GameDirectorySource::platform && result->archives.size() == 1,
            "the look-again notice goes on into the game once the folder can be played"
        );
        expect(
            host.asked.size() == 2 && host.labels.size() == 2 && host.labels[0] == kLabel &&
                host.labels[1] == kLabel && host.kinds[0] == Notice::warning,
            "the notice is shown with the platform's one button until the folder is usable"
        );
        expect(
            host.platform_lookups == 2 && host.notices.empty() && host.starts.empty(),
            "each press looks for the platform's folder again, and nothing else is told"
        );
        expect(
            host.asked.size() == 2 &&
                host.asked[0] ==
                    std::string("Open Annihilation needs your Total Annihilation files.\n\n") +
                        kAdvice,
            "the look-again notice asks for the files, then gives the platform's advice"
        );
        expect(
            host.asked.size() == 2 && !contains(host.asked[0], "It looks for it first in") &&
                contains(host.asked[0], kAdvice) &&
                contains(
                    host.asked[1], "It looks for it first in:\n\n" + path_to_utf8(kPlatform)
                ) &&
                contains(host.asked[1], "no Total Annihilation archives") &&
                contains(host.asked[1], kAdvice),
            "the notice names the platform's folder and why once the folder is there"
        );
    }
    {
        // The folder chosen earlier comes back usable: it is taken after
        // the platform's, which is still not there.
        auto host = with_installs();
        host.installs.erase(kGog);
        host.presses = {[](ScriptedHost& self) { self.installs[kGog] = true; }};
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        h.find_platform_default = ScriptedHost::find_platform_default;
        auto request = platform_request(kPlatform, path_to_utf8(kGog));
        request.check_again_label = kLabel;
        const auto result = resolve_game_directory(request, h);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored &&
                host.asked.size() == 1 && host.platform_lookups == 1,
            "a press takes the stored folder when the platform's is not there"
        );
        expect(
            host.asked.size() == 1 &&
                contains(
                    host.asked[0], "It looks for it first in:\n\n" + path_to_utf8(kPlatform)
                ) &&
                contains(host.asked[0], "chosen earlier") &&
                contains(host.asked[0], path_to_utf8(kGog)),
            "the first notice says why each folder looked at cannot be played"
        );
    }
    {
        // Without the platform's search the start's folder is looked at again.
        auto host = with_installs();
        host.installs[kPlatform] = false;
        host.presses = {[](ScriptedHost& self) { self.installs[kPlatform] = true; }};
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        auto request = platform_request(kPlatform, std::nullopt);
        request.check_again_label = kLabel;
        const auto result = resolve_game_directory(request, h);
        expect(
            result && result->path == kPlatform && host.asked.size() == 1 &&
                host.platform_lookups == 0,
            "without the platform's search the start's platform folder is looked at again"
        );
    }
    {
        // A notice that cannot be shown ends the start as before.
        auto host = with_installs();
        host.shown_at_most = 0;
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        h.find_platform_default = ScriptedHost::find_platform_default;
        auto request = platform_request(kPlatform, std::nullopt);
        request.check_again_label = kLabel;
        const auto result = resolve_game_directory(request, h);
        expect(
            !result && host.asked.empty() && host.platform_lookups == 0 && host.notices.empty(),
            "a look-again notice that cannot be shown ends the start, looking no further"
        );
    }
    {
        // A press after which the notice can no longer be shown ends it too.
        auto host = with_installs();
        host.shown_at_most = 1;
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        h.find_platform_default = ScriptedHost::find_platform_default;
        auto request = platform_request(kPlatform, std::nullopt);
        request.check_again_label = kLabel;
        const auto result = resolve_game_directory(request, h);
        expect(
            !result && host.asked.size() == 1 && host.platform_lookups == 1,
            "the notice ends the start once it cannot be shown again"
        );
    }
    {
        // Without the label, or without a host that can ask, the notice is
        // today's, word for word.
        const std::string expected = "Open Annihilation needs your Total Annihilation "
                                     "installation.\n\nIt looks for it first in:\n\n" +
                                     path_to_utf8(kPlatform) +
                                     "\n\nThe folder does not exist.\n\nThe Total Annihilation "
                                     "folder chosen earlier can no longer be used:\n\n" +
                                     path_to_utf8(kMoved) + "\n\nThe folder does not exist.\n\n" +
                                     kAdvice;
        auto host = with_installs();
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        h.find_platform_default = ScriptedHost::find_platform_default;
        const auto result =
            resolve_game_directory(platform_request(kPlatform, path_to_utf8(kMoved)), h);
        expect(
            !result && host.asked.empty() && host.notices.size() == 1 &&
                host.notices[0] == expected && host.kinds[0] == Notice::warning,
            "without the look-again label the notice is told once, as before"
        );
        auto unasked = with_installs();
        auto plain = without_dialog(unasked);
        auto request = platform_request(kPlatform, path_to_utf8(kMoved));
        request.check_again_label = kLabel;
        const auto told = resolve_game_directory(request, plain);
        expect(
            !told && unasked.notices.size() == 1 && unasked.notices[0] == expected,
            "a host that cannot ask tells the notice once, as before"
        );
    }
    {
        // A first run without the platform's folder or a stored one gives
        // the --game-dir advice word for word without the label.
        auto host = with_installs();
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        const auto result = resolve_game_directory({}, h);
        expect(
            !result && host.asked.empty() && host.notices.size() == 1 &&
                host.notices[0] ==
                    "Open Annihilation needs your Total Annihilation installation.\n\nName "
                    "your Total Annihilation folder on the command line:\n\n"
                    "open-annihilation --game-dir PATH",
            "the first run's notice without a platform is today's"
        );
    }
    {
        // An unattended run never shows the notice: it stops as before.
        auto host = with_installs();
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        auto request = platform_request(kPlatform, std::nullopt, true);
        request.check_again_label = kLabel;
        std::string refusal;
        try {
            (void)resolve_game_directory(request, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        expect(
            contains(refusal, "--game-dir PATH") && host.asked.empty(),
            "an unattended run with the label still stops with the --game-dir advice"
        );
    }
    {
        // A build with the folder dialog asks with it, label or not.
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kCd, {}}};
        auto h = host.asking_host();
        auto request = platform_request(kPlatform, std::nullopt);
        request.check_again_label = kLabel;
        const auto result = resolve_game_directory(request, h);
        expect(
            result && result->path == kCd && result->source == GameDirectorySource::chosen &&
                host.asked.empty(),
            "the folder dialog is used where the build offers it"
        );
    }
}

// Where the platform brings game files in, a start with no usable folder
// reports it for the Game files screen instead of a notice.
void check_game_files_offered() {
    const auto offered_request =
        [](const fs::path& platform, std::optional<std::string> stored, bool unattended = false) {
            auto request = platform_request(platform, std::move(stored), unattended);
            request.import_offered = true;
            request.check_again_label = "Check again";
            return request;
        };
    {
        auto host = with_installs();
        auto h = without_dialog(host);
        h.ask = ScriptedHost::ask;
        GameFilesNeeded needed;
        needed.problem = "left over";
        const auto result = resolve_game_directory(offered_request({}, std::nullopt), h, &needed);
        expect(
            !result && needed.needed && needed.folder.empty() && needed.problem.empty(),
            "no platform folder and no stored one: the screen is needed, naming no folder"
        );
        expect(
            host.notices.empty() && host.asked.empty() && host.starts.empty(),
            "the screen offered shows no notice and asks nothing"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = false;
        auto h = without_dialog(host);
        GameFilesNeeded needed;
        const auto result =
            resolve_game_directory(offered_request(kPlatform, path_to_utf8(kMoved)), h, &needed);
        expect(
            !result && needed.needed && needed.folder == kPlatform &&
                needed.problem == "It holds no Total Annihilation archives (.hpi, .ufo, .ccx or "
                                  "rev31.gp3 files)." &&
                host.notices.empty() && host.inspections == 2,
            "a platform folder that cannot be played is named with why, after the stored one"
        );
    }
    {
        auto host = with_installs();
        auto h = without_dialog(host);
        GameFilesNeeded needed;
        needed.needed = true;
        const auto result =
            resolve_game_directory(offered_request(kPlatform, path_to_utf8(kGog)), h, &needed);
        expect(
            result && result->path == kGog && result->source == GameDirectorySource::stored &&
                !needed.needed && host.notices.empty(),
            "the stored folder is still tried first, and the report is cleared"
        );
    }
    {
        auto host = with_installs();
        host.installs[kPlatform] = true;
        auto h = without_dialog(host);
        GameFilesNeeded needed;
        const auto result =
            resolve_game_directory(offered_request(kPlatform, path_to_utf8(kGog)), h, &needed);
        expect(
            result && result->source == GameDirectorySource::platform && !needed.needed &&
                host.inspections == 1,
            "a usable platform folder is taken first with the screen offered"
        );
    }
    {
        auto host = with_installs();
        auto h = without_dialog(host);
        GameFilesNeeded needed;
        bool threw = false;
        std::optional<GameDirectory> result;
        try {
            result = resolve_game_directory(
                offered_request(kPlatform, path_to_utf8(kMoved), true), h, &needed
            );
        } catch (const std::runtime_error&) {
            threw = true;
        }
        expect(
            !threw && !result && needed.needed && needed.folder.empty(),
            "an unattended run that checks the screen does not stop for lack of a folder"
        );
    }
    {
        auto host = with_installs();
        host.picks = {{FolderPick::chosen, kCd, {}}};
        auto h = host.host();
        GameFilesNeeded needed;
        const auto result = resolve_game_directory(offered_request({}, std::nullopt), h, &needed);
        expect(
            !result && needed.needed && host.starts.empty() && host.notices.empty(),
            "with the screen offered the folder dialog does not open"
        );
    }
    {
        // Without the report to fill, the request plays as without the screen.
        auto host = with_installs();
        auto h = without_dialog(host);
        const auto result = resolve_game_directory(offered_request(kPlatform, std::nullopt), h);
        expect(
            !result && host.notices.size() == 1 && contains(host.notices[0], kAdvice),
            "the screen is offered only with a report to fill"
        );
    }
    {
        // --game-dir still wins, and a refused one still stops the start.
        auto host = with_installs();
        auto h = without_dialog(host);
        GameFilesNeeded needed;
        auto request = offered_request({}, std::nullopt);
        request.argument = kCd;
        const auto result = resolve_game_directory(request, h, &needed);
        expect(
            result && result->source == GameDirectorySource::argument && !needed.needed,
            "--game-dir wins with the screen offered"
        );
    }
}

// The texts resolution shows for a folder that cannot be played.
void check_problem_texts() {
    GameInstall install;
    install.problem = "No mapping for the Unicode character";
    expect(
        describe_install_problem(install) == "It could not be read: No mapping for the Unicode "
                                             "character",
        "a folder that could not be read says why"
    );
    install = GameInstall{};
    install.folder = true;
    install.profile_errors = {"oamod.yaml: unknown hack", "oamod.yaml: bad limit"};
    expect(
        describe_install_problem(install) ==
            "Its mod profile cannot be used:\n  oamod.yaml: unknown hack\n  oamod.yaml: bad limit",
        "a profile that cannot be used lists its errors"
    );
    install = GameInstall{};
    expect(
        describe_install_problem(install) == "The folder does not exist.",
        "a missing folder says so"
    );
    install.folder = true;
    expect(
        describe_install_problem(install) ==
            "It holds no Total Annihilation archives (.hpi, .ufo, .ccx or rev31.gp3 files).",
        "a folder without archives names the archives"
    );
    install.archives = {"/games/x/totala1.hpi"};
    install.missing = {"guis/mainmenu.gui", "gamedata/sound.tdf"};
    expect(
        describe_install_problem(install) ==
            "Its archives lack guis/mainmenu.gui, gamedata/sound.tdf.",
        "archives lacking resources name them"
    );
    DemoSetup demo;
    demo.outcome = DemoOutcome::no_installer;
    expect(
        describe_archive_problem(demo) == "It holds no Total Annihilation archives (.hpi, .ufo, "
                                          ".ccx or rev31.gp3 files) and no installer of the Total "
                                          "Annihilation demo (1997).",
        "a folder without the demo's installer says so"
    );
    demo.outcome = DemoOutcome::unrecognised;
    demo.rejected = {"/games/x/setup.exe", "/games/x/other.exe"};
    expect(
        describe_archive_problem(demo) ==
            "It holds no Total Annihilation archives (.hpi, .ufo, .ccx or rev31.gp3 files), and "
            "setup.exe and other.exe are not the release of the Total Annihilation demo (1997) "
            "that Open Annihilation recognises.",
        "unrecognised installers are named"
    );
    demo = DemoSetup{};
    demo.outcome = DemoOutcome::disk_full;
    demo.installer = "/games/x/TA_Demo.exe";
    demo.problem = "no space left on device";
    expect(
        describe_archive_problem(demo) ==
            "It holds the installer of the Total Annihilation demo (1997), TA_Demo.exe, but the "
            "disk is full: no space left on device.",
        "a full disk while unpacking says so"
    );
    install = GameInstall{};
    install.folder = true;
    install.demo = demo;
    expect(
        describe_install_problem(install) == describe_archive_problem(demo),
        "a folder without archives says what the search for the installer found"
    );
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

// Archives that cannot be mounted are recorded, and files laid over a
// folder come first.
void check_skipped_and_overlay(const fs::path& temporary) {
    const auto folder = temporary / "skipped";
    fs::create_directories(folder);
    write_file(
        folder / "totala1.hpi",
        archive_of({"guis/mainmenu.gui", "palettes/palette.pal", "gamedata/sidedata.tdf"})
    );
    write_file(folder / "ccdata.ccx", {'n', 'o', 't', ' ', 'a', 'n', ' ', 'a', 'r', 'c', 'h'});
    const auto install = inspect_game_install(folder);
    expect(
        install.skipped.size() == 1 && install.skipped[0].path.filename() == "ccdata.ccx" &&
            !install.skipped[0].error.empty(),
        "an archive that cannot be mounted is recorded with what the mount said"
    );
    expect(
        install.archives.size() == 1 && install.missing.size() == 1 &&
            install.missing[0] == "gamedata/sound.tdf",
        "the other archives are still taken"
    );
    const auto overlay = temporary / "overlay";
    fs::create_directories(overlay);
    write_file(overlay / "btdata.ccx", archive_of({"gamedata/sound.tdf"}));
    const auto layered = inspect_game_install(folder, {}, demo_1997, {}, overlay);
    expect(
        layered.folders.size() == 2 && layered.folders.front() == overlay &&
            layered.folders.back() == folder,
        "the overlay comes before the folder"
    );
    expect(
        usable(layered) && layered.archives.size() == 2,
        "the overlay's archives are checked with the folder's"
    );
    const auto plain = inspect_game_install(folder);
    expect(
        plain.folders.size() == 1 && plain.folders.front() == folder,
        "without an overlay the folder stands alone"
    );
}

void write_text(const fs::path& path, std::string_view text) {
    fs::create_directories(path.parent_path());
    std::ofstream(path, std::ios::binary) << text;
}

// A mod whose profile renames the revision archive and the GUI and game-data
// directories, played as a mod folder over a base folder and as a copied
// install.
void check_mod_folders(const fs::path& temporary) {
    constexpr std::string_view profile = "oamod: 1\n"
                                         "id: example\n"
                                         "name: Example mod\n"
                                         "version: \"1.0\"\n"
                                         "requires: {base: ta-3.1c, catalogue: 1}\n"
                                         "identity: {display-version: \"9.9\", "
                                         "network-version: [9, 9], side-names: [Red, Blue]}\n"
                                         "layout:\n"
                                         "  revision-archive: modrev.gp3\n"
                                         "  directories: {guis: guiM, gamedata: gamedatM}\n";
    const auto base = temporary / "base";
    const auto mod = base / "Mods" / "example";
    const auto copied = temporary / "copied";
    for (const auto& folder : {base, mod, copied})
        fs::create_directories(folder);
    write_file(
        base / "totala1.hpi",
        archive_of(
            {"guis/mainmenu.gui",
             "palettes/palette.pal",
             "gamedata/sidedata.tdf",
             "gamedata/sound.tdf"}
        )
    );
    write_file(base / "rev31.gp3", archive_of({"anims/base.gaf"}));
    write_file(
        mod / "modrev.gp3",
        archive_of({"guiM/mainmenu.gui", "gamedatM/sidedata.tdf", "gamedatM/sound.tdf"})
    );
    write_text(mod / "OAMod.yaml", profile);

    const auto plain = inspect_game_install(base);
    expect(usable(plain) && !plain.profile, "a base folder plays base 3.1c");
    expect(plain.folders == std::vector<fs::path>{base}, "a base folder is its only folder");

    const auto layered = inspect_game_install(base, {}, demo_1997, {mod, {}, false, nullptr});
    expect(usable(layered), "a mod folder over a base folder is usable");
    expect(
        layered.profile && layered.profile->id == "example" &&
            layered.profile->identity.display_version == "9.9",
        "the mod folder's profile is resolved"
    );
    expect(layered.folders == std::vector<fs::path>{mod, base}, "the mod folder layers first");
    std::vector<std::string> names;
    for (const auto& archive : layered.archives)
        names.push_back(archive.filename().string());
    expect(
        names == std::vector<std::string>{"modrev.gp3", "totala1.hpi"},
        "the profile's revision archive replaces rev31.gp3"
    );

    const auto offered = list_mod_folders(base);
    expect(
        offered.size() == 1 && offered.front().filename() == "example",
        "the base folder offers its mods folder's mod"
    );

    write_text(mod / "broken" / "oamod.yaml", "oamod: 1\nid: Broken\n");
    const auto broken =
        inspect_game_install(base, {}, demo_1997, {mod / "broken", {}, false, nullptr});
    expect(
        !usable(broken) && !broken.profile_errors.empty() && broken.archives.empty(),
        "a profile that cannot be used stops the folder before any archive"
    );

    write_file(
        copied / "modrev.gp3",
        archive_of({"guiM/mainmenu.gui", "gamedatM/sidedata.tdf", "gamedatM/sound.tdf"})
    );
    write_file(copied / "totala1.hpi", archive_of({"palettes/palette.pal"}));
    write_text(copied / "oamod.yaml", profile);
    const auto installed = inspect_game_install(copied);
    expect(
        usable(installed) && installed.profile && installed.profile->id == "example",
        "a copied install plays its own profile"
    );
    const auto over_copy = inspect_game_install(copied, {}, demo_1997, {mod, {}, false, nullptr});
    expect(
        !usable(over_copy) && !over_copy.profile_errors.empty(),
        "a copied install cannot carry a mod folder"
    );

    oa::platform::preferences::Values values;
    expect(chosen_mod_directory({}, false, values).empty(), "no mod folder before a choice");
    remember_mod_directory(values, mod);
    expect(
        chosen_mod_directory({}, false, values) == fs::absolute(mod).lexically_normal(),
        "the chosen mod folder is remembered"
    );
    expect(chosen_mod_directory({}, true, values).empty(), "--base-game ignores it");
    expect(chosen_mod_directory(copied, false, values) == copied, "--mod-dir wins over the choice");
    remember_mod_directory(values, {});
    expect(values.empty(), "the base game forgets the choice");
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
    check_platform_default();
    check_without_dialog();
    check_look_again();
    check_game_files_offered();
    check_problem_texts();
    check_environment();
    try {
        check_installs(temporary);
        check_skipped_and_overlay(temporary);
        check_mod_folders(temporary);
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
