// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Game directory resolution and installation checks for oa-game.
#include "oa/app/game_directory.hpp"
#include "oa/formats/hpi.hpp"
#include <cctype>
#include <iostream>
#include <stdexcept>
#include <system_error>

namespace oa::app {
namespace {

// Opened by the frontend before anything else draws; each 3.1 installation
// carries them in totala1.hpi. Each lies in a directory of the data layout,
// or in palettes when none is named.
struct RequiredResource {
    std::optional<data::defs::DataDirectory> directory;
    std::string_view name;
};

constexpr RequiredResource kRequiredResources[]{
    {data::defs::DataDirectory::guis, "mainmenu.gui"},
    {std::nullopt, "palettes/palette.pal"},
    {data::defs::DataDirectory::gamedata, "sidedata.tdf"},
    {data::defs::DataDirectory::gamedata, "sound.tdf"}
};

constexpr std::string_view kWhatToChoose =
    "Choose the Total Annihilation folder that holds totala1.hpi, or the folder that holds "
    "the installer of the Total Annihilation demo (1997).";

constexpr std::string_view kNoArchives =
    "It holds no Total Annihilation archives (.hpi, .ufo, .ccx or rev31.gp3 files)";

// Why a folder without archives cannot be played, from what the search for
// the demo's installer found.
[[nodiscard]] std::string archive_problem(const DemoSetup& demo) {
    switch (demo.outcome) {
    case DemoOutcome::not_searched:
        break;
    case DemoOutcome::no_installer:
        return std::string(kNoArchives) +
               " and no installer of the Total Annihilation demo (1997).";
    case DemoOutcome::unrecognised: {
        std::string names;
        for (std::size_t i = 0; i < demo.rejected.size(); ++i) {
            if (i != 0)
                names += i + 1 == demo.rejected.size() ? " and " : ", ";
            names += path_to_utf8(demo.rejected[i].filename());
        }
        return std::string(kNoArchives) + ", and " + names +
               (demo.rejected.size() == 1 ? " is not" : " are not") +
               " the release of the Total Annihilation demo (1997) that Open Annihilation "
               "recognises.";
    }
    case DemoOutcome::ready:
        return "The Total Annihilation demo (1997) unpacked to " + path_to_utf8(demo.archive) +
               " could not be opened.";
    case DemoOutcome::unpack_failed:
        return "It holds the installer of the Total Annihilation demo (1997), " +
               path_to_utf8(demo.installer.filename()) +
               ", but its game data could not be unpacked: " + demo.problem + '.';
    case DemoOutcome::disk_full:
        return "It holds the installer of the Total Annihilation demo (1997), " +
               path_to_utf8(demo.installer.filename()) + ", but the disk is full: " + demo.problem +
               '.';
    }
    return std::string(kNoArchives) + '.';
}

[[nodiscard]] std::string install_problem(const GameInstall& install) {
    if (!install.problem.empty())
        return "It could not be read: " + install.problem;
    if (!install.profile_errors.empty()) {
        std::string text = "Its mod profile cannot be used:";
        for (const auto& error : install.profile_errors)
            text += "\n  " + error;
        return text;
    }
    if (!install.folder)
        return "The folder does not exist.";
    if (install.archives.empty())
        return archive_problem(install.demo);
    std::string text = "Its archives lack ";
    for (std::size_t i = 0; i < install.missing.size(); ++i) {
        if (i != 0)
            text += ", ";
        text += install.missing[i];
    }
    return text + '.';
}

[[nodiscard]] bool equal_ignoring_case(std::string_view left, std::string_view right) {
    if (left.size() != right.size())
        return false;
    for (std::size_t i = 0; i < left.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(left[i])) !=
            std::tolower(static_cast<unsigned char>(right[i])))
            return false;
    return true;
}

// The scan throws on folders it cannot read, such as a name outside the
// Windows ANSI code page or two names differing only in case; the user is
// told why and asked again like for any other unusable folder.
[[nodiscard]] GameInstall inspect_folder(const GameDirectoryHost& host, const fs::path& folder) {
    try {
        return host.inspect(host.context, folder);
    } catch (const std::exception& error) {
        GameInstall install;
        install.problem = error.what();
        return install;
    }
}

// The resolved folder: where it lies, its archives and, when it held the
// demo's installer, where the archive was unpacked.
[[nodiscard]] GameDirectory
resolved(const fs::path& folder, GameInstall&& install, GameDirectorySource source) {
    auto installation = install.installation.empty() ? folder : std::move(install.installation);
    auto folders = std::move(install.folders);
    if (folders.empty())
        folders.push_back(installation);
    return GameDirectory{
        folder,
        std::move(install.archives),
        source,
        std::move(installation),
        std::move(install.demo),
        std::move(folders),
        std::move(install.profile),
        std::move(install.profile_warnings)
    };
}

// Records a probe store's archives as the installation's, and the required
// resources neither they nor the loose files in its folders hold, named in
// the layout of the installation's profile.
void take_mounted(const fs::path& root, const oa::AssetStore& probe, GameInstall& install) {
    const auto mounted = probe.mount_paths();
    install.archives.assign(mounted.begin(), mounted.end());
    install.missing.clear();
    const auto layout = data_layout_of(install.profile.get());
    for (const auto& resource : kRequiredResources) {
        const auto path = resource.directory
                              ? layout.directories[static_cast<std::size_t>(*resource.directory)] +
                                    '/' + std::string(resource.name)
                              : std::string(resource.name);
        if (probe.file_size(path) == 0)
            install.missing.push_back(path);
    }
    install.installation = root;
}

// Runs the archive discovery on the installation's folders into `install`,
// as its profile's layout names the archives.
void discover_archives(const fs::path& root, GameInstall& install) {
    // The game's discovery scan (AssetStore::discover) decides
    // both which archives are mounted and their lookup precedence: the
    // revision patch, then *.CCX, *.UFO and at most ten *.HPI, each group in
    // Windows name order, then every *.hpi on each CD-ROM root with no limit
    // but with already-mounted paths rejected. The game discs carry
    // totala3.hpi, totala4.hpi and worlds.hpi, which an install without discs
    // keeps in the game directory instead; the game directory then serves as
    // the disc root: the archives past the *.HPI limit mount after everything
    // else, as the disc copies would. A scratch store runs the scan so main()
    // mounts the same archives in the same order. --archive bypasses this.
    // A mod folder layers over the folder: discovery runs over both, as
    // over one folder holding the files of both.
    oa::AssetStore probe(install.folders);
    for (const auto& outcome : probe.discover(discovery_plan_of(install.profile.get())))
        if (!outcome.mounted && !outcome.already_mounted)
            std::cerr << "open-annihilation: skipping archive "
                      << path_to_utf8(outcome.path.filename()) << ": " << outcome.error << '\n';
    take_mounted(root, probe, install);
}

// Takes `archive` as the only archive of the installation in `root`: the
// demo's checked archive, whatever else its folder holds.
void take_archive(const fs::path& root, const fs::path& archive, GameInstall& install) {
    install.folders = {root};
    oa::AssetStore probe(root);
    std::string error;
    if (!probe.try_mount(archive, &error))
        std::cerr << "open-annihilation: skipping archive " << path_to_utf8(archive.filename())
                  << ": " << error << '\n';
    take_mounted(root, probe, install);
}

} // namespace

GameInstall inspect_game_install(
    const fs::path& root,
    const fs::path& data_folder,
    const DemoRelease& release,
    const ModChoice& mod
) {
    GameInstall install;
    std::error_code error;
    install.folder = fs::is_directory(root, error);
    if (!install.folder)
        return install;
    if (!mod.folder.empty()) {
        if (!fs::is_directory(mod.folder, error)) {
            install.profile_errors.push_back(
                path_to_utf8(mod.folder) + ": the mod folder does not exist"
            );
            return install;
        }
        install.folders.push_back(mod.folder);
    }
    install.folders.push_back(root);
    // The profile is resolved before any archive is mounted; one that cannot
    // be used stops here and never falls back to the base game.
    auto profile = resolve_folder_profile(install.folders, mod);
    install.profile = std::move(profile.profile);
    install.profile_errors = std::move(profile.errors);
    install.profile_warnings = std::move(profile.warnings);
    if (!install.profile_errors.empty())
        return install;
    discover_archives(root, install);
    if (!install.archives.empty() || install.profile)
        return install;
    // A folder with no archives may hold the demo's installer; its unpacked
    // archive, checked, is the one archive mounted from the folder it was
    // unpacked to.
    install.demo = set_up_demo(root, data_folder, release);
    if (install.demo.outcome == DemoOutcome::ready)
        take_archive(install.demo.folder, install.demo.archive, install);
    return install;
}

bool usable(const GameInstall& install) {
    return install.problem.empty() && install.profile_errors.empty() && install.folder &&
           !install.archives.empty() && install.missing.empty();
}

std::optional<GameDirectory>
resolve_game_directory(const GameDirectoryRequest& request, const GameDirectoryHost& host) {
    if (!request.argument.empty()) {
        if (request.archives_named)
            return GameDirectory{
                request.argument,
                {},
                GameDirectorySource::argument,
                request.argument,
                {},
                {},
                {},
                {}
            };
        auto install = inspect_folder(host, request.argument);
        if (install.problem.empty() && !install.folder)
            throw std::runtime_error(
                "game directory does not exist: " + path_to_utf8(request.argument) +
                " (name it with --game-dir PATH)"
            );
        if (!install.problem.empty() || !install.profile_errors.empty() || install.archives.empty())
            throw std::runtime_error(
                "the folder --game-dir names cannot be played: " + path_to_utf8(request.argument) +
                " (" + install_problem(install) + ")"
            );
        return resolved(request.argument, std::move(install), GameDirectorySource::argument);
    }
    if (request.unattended) {
        if (request.choose)
            throw std::runtime_error(
                "--choose-game-dir opens a dialog, which CI and the dummy or offscreen video "
                "driver never show"
            );
        if (!request.stored || request.stored->empty())
            throw std::runtime_error(
                "no Total Annihilation folder is known and nobody can answer the folder dialog "
                "in this run; name it with --game-dir PATH"
            );
        const auto stored = path_from_utf8(*request.stored);
        auto install = inspect_folder(host, stored);
        if (!usable(install))
            throw std::runtime_error(
                "the Total Annihilation folder chosen earlier can no longer be used: " +
                path_to_utf8(stored) + " (" + install_problem(install) +
                "); name one with --game-dir PATH"
            );
        return resolved(stored, std::move(install), GameDirectorySource::stored);
    }
    fs::path start;
    if (request.stored && !request.stored->empty()) {
        start = path_from_utf8(*request.stored);
        if (!request.choose) {
            auto install = inspect_folder(host, start);
            if (usable(install))
                return resolved(start, std::move(install), GameDirectorySource::stored);
            host.tell_user(
                host.context,
                Notice::information,
                "The Total Annihilation folder chosen earlier can no longer be used:\n\n" +
                    path_to_utf8(start) + "\n\n" + install_problem(install) +
                    "\n\nChoose the folder again."
            );
        }
    } else if (!request.choose) {
        host.tell_user(
            host.context,
            Notice::information,
            "Open Annihilation needs your Total Annihilation installation.\n\n" +
                std::string(kWhatToChoose) +
                "\nIt is remembered for later starts; --choose-game-dir changes it."
        );
    }
    // Windows reports a choice with no folder on disk behind it (This PC, a
    // library) as a failure, so a failure opens the dialog once more; where
    // there is no dialog the second attempt fails at once.
    bool failed_before = false;
    for (;;) {
        fs::path chosen;
        std::string error;
        const auto pick = host.pick_folder(host.context, start, &chosen, &error);
        if (pick == FolderPick::unavailable && !failed_before) {
            failed_before = true;
            continue;
        }
        failed_before = false;
        switch (pick) {
        case FolderPick::chosen: {
            auto install = inspect_folder(host, chosen);
            if (usable(install))
                return resolved(chosen, std::move(install), GameDirectorySource::chosen);
            host.tell_user(
                host.context,
                Notice::warning,
                "This folder does not hold a Total Annihilation installation:\n\n" +
                    path_to_utf8(chosen) + "\n\n" + install_problem(install) + "\n\n" +
                    std::string(kWhatToChoose)
            );
            start = chosen;
            break;
        }
        case FolderPick::cancelled:
            host.tell_user(
                host.context,
                Notice::information,
                "Open Annihilation cannot start without your Total Annihilation folder.\n"
                "Start it again to choose the folder, or name it on the command line:\n\n"
                "open-annihilation --game-dir PATH"
            );
            return std::nullopt;
        case FolderPick::unavailable:
            host.tell_user(
                host.context,
                Notice::warning,
                "Open Annihilation could not get a folder from the system's folder dialog:\n" +
                    error +
                    "\n\nName your Total Annihilation folder on the command line:\n\n"
                    "open-annihilation --game-dir PATH"
            );
            return std::nullopt;
        }
    }
}

bool unattended_environment(std::string_view ci, std::string_view video_drivers) {
    if (!ci.empty())
        return true;
    const auto first = video_drivers.substr(0, video_drivers.find(','));
    return equal_ignoring_case(first, "dummy") || equal_ignoring_case(first, "offscreen");
}

std::string dialog_location(const fs::path& start) {
    if (start.empty())
        return {};
    std::error_code error;
    auto folder = fs::absolute(start, error);
    if (error)
        return {};
    while (!fs::is_directory(folder, error)) {
        const auto parent = folder.parent_path();
        if (parent.empty() || parent == folder)
            return {};
        folder = parent;
    }
    auto text = path_to_utf8(folder.make_preferred());
    if (text.back() != static_cast<char>(fs::path::preferred_separator))
        text += static_cast<char>(fs::path::preferred_separator);
    return text;
}

fs::path preference_file(const std::optional<fs::path>& explicit_file) {
    return explicit_file ? *explicit_file : oa::platform::preferences::default_file();
}

std::optional<std::string> stored_game_directory(const oa::platform::preferences::Values& values) {
    const auto found = values.find(std::string(kGameDirectoryPreference));
    if (found == values.end())
        return std::nullopt;
    return found->second;
}

void remember_game_directory(oa::platform::preferences::Values& values, const fs::path& folder) {
    values[std::string(kGameDirectoryPreference)] =
        path_to_utf8(fs::absolute(folder).lexically_normal());
}

fs::path chosen_mod_directory(
    const fs::path& mod_dir, bool base_game, const oa::platform::preferences::Values& values
) {
    if (!mod_dir.empty())
        return mod_dir;
    if (base_game)
        return {};
    const auto found = values.find(std::string(mod_directory_preference));
    if (found == values.end() || found->second.empty())
        return {};
    return path_from_utf8(found->second);
}

void remember_mod_directory(oa::platform::preferences::Values& values, const fs::path& folder) {
    if (folder.empty()) {
        values.erase(std::string(mod_directory_preference));
        return;
    }
    values[std::string(mod_directory_preference)] =
        path_to_utf8(fs::absolute(folder).lexically_normal());
}

} // namespace oa::app
