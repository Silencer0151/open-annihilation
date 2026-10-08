// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A fixture mod names two archives from the installation. A file resolves
// from the mod's archive, then from those archives in the order written,
// then from the base archive. The same order holds for a mod folder layered
// over the game folder and for a copied install of the same files. A missing
// named archive is reported as a profile error and the folder is unusable.

#include "oa/app/game_directory.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace {

namespace fs = std::filesystem;
using namespace oa::app;

constexpr std::string_view profile_text =
    "oamod: 1\n"
    "id: layer-mod\n"
    "name: Layer Mod\n"
    "version: \"1\"\n"
    "author: {name: unknown}\n"
    "packaging: {revision: 1, date: 2026-10-04, packager: t}\n"
    "layout:\n"
    "  installation-archives: [zeta.ccx, addon.ccx]\n";

/// Writes bytes to a file, creating its folder.
///
/// @param path the file
/// @param bytes its contents
void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    fs::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
}

/// The bytes of a text.
///
/// @param text the text
/// @return its bytes
std::vector<uint8_t> text_bytes(std::string_view text) {
    return {text.begin(), text.end()};
}

/// An archive of the named files, each holding its text.
///
/// @param files path and text of each entry
/// @return the archive bytes
std::vector<uint8_t> archive(std::vector<std::pair<std::string, std::string>> files) {
    std::vector<oa::HpiWriteFile> written;
    for (auto& file : files)
        written.push_back(oa::HpiWriteFile{std::move(file.first), text_bytes(file.second)});
    return oa::write_hpi(written);
}

/// Writes the installation: a revision archive, two named archives and the
/// base archive. `addon.ccx` sorts before `mod.ccx` and `zeta.ccx`, so a scan
/// that left the named archives in the ccx group would mount it first.
///
/// @param folder the installation folder
void write_installation(const fs::path& folder) {
    write_bytes(folder / "rev31.gp3", archive({{"rev.txt", "rev"}}));
    write_bytes(folder / "zeta.ccx", archive({{"zeta-only.txt", "zeta"}, {"order.txt", "zeta"}}));
    write_bytes(
        folder / "addon.ccx",
        archive(
            {{"shared.txt", "addon"},
             {"addon-only.txt", "addon"},
             {"covered.txt", "addon"},
             {"order.txt", "addon"}}
        )
    );
    write_bytes(
        folder / "base.hpi",
        archive(
            {{"shared.txt", "base"},
             {"covered.txt", "base"},
             {"base-only.txt", "base"},
             {"order.txt", "base"},
             {"guis/mainmenu.gui", "gui"},
             {"palettes/palette.pal", "pal"},
             {"gamedata/sidedata.tdf", "side"},
             {"gamedata/sound.tdf", "sound"}}
        )
    );
}

/// Writes the mod folder: its profile and its own archive, which holds the
/// file the other archives also hold.
///
/// @param folder the mod folder
void write_mod(const fs::path& folder) {
    write_bytes(folder / "mod.ccx", archive({{"shared.txt", "mod"}, {"mod-only.txt", "mod"}}));
    write_bytes(folder / "oamod.yaml", text_bytes(profile_text));
}

/// Copies one file over another.
///
/// @param from the file
/// @param to its copy
void copy_one(const fs::path& from, const fs::path& to) {
    fs::create_directories(to.parent_path());
    fs::copy_file(from, to, fs::copy_options::overwrite_existing);
}

/// Reads one resource through the installation's archives, in mount order.
///
/// @param install the installation
/// @param name the resource
/// @return its text
std::string read_text(const GameInstall& install, std::string_view name) {
    oa::AssetStore store(install.folders);
    for (const auto& mounted : install.archives)
        store.mount(mounted);
    const auto data = store.read(name);
    return {data.bytes.begin(), data.bytes.end()};
}

/// The mounted archives' file names, in mount order.
///
/// @param install the installation
/// @return the names
std::vector<std::string> archive_names(const GameInstall& install) {
    std::vector<std::string> names;
    for (const auto& mounted : install.archives)
        names.push_back(mounted.filename().string());
    return names;
}

/// Tells whether a line list holds a needle.
///
/// @param lines the lines
/// @param needle the text
/// @return true when one line contains it
bool contains(const std::vector<std::string>& lines, std::string_view needle) {
    for (const auto& line : lines)
        if (line.find(needle) != std::string::npos)
            return true;
    return false;
}

/// The mount order and the file each archive wins.
///
/// @param install a usable installation of the fixture
void check_resolution(const GameInstall& install) {
    OA_CHECK(usable(install));
    OA_CHECK(install.profile_errors.empty());
    OA_CHECK(install.skipped.empty());
    OA_CHECK(
        archive_names(install) ==
        (std::vector<std::string>{"rev31.gp3", "mod.ccx", "zeta.ccx", "addon.ccx", "base.hpi"})
    );
    // The mod's archive is mounted before either named archive, so it wins
    // the file all three hold. zeta.ccx is written before addon.ccx, so it
    // wins the file those two hold, ahead of NTFS name order.
    OA_CHECK(read_text(install, "shared.txt") == "mod");
    OA_CHECK(read_text(install, "order.txt") == "zeta");
    OA_CHECK(read_text(install, "covered.txt") == "addon");
    OA_CHECK(read_text(install, "addon-only.txt") == "addon");
    OA_CHECK(read_text(install, "zeta-only.txt") == "zeta");
    OA_CHECK(read_text(install, "base-only.txt") == "base");
    OA_CHECK(read_text(install, "mod-only.txt") == "mod");
}

/// A mod folder and a copied install resolve the same way, and a missing
/// named archive is reported and stops the folder being played.
///
/// @param scratch the scratch directory
void test_layered_and_copied(const fs::path& scratch) {
    const auto base = scratch / "base";
    const auto mod = scratch / "mod";
    const auto copied = scratch / "copied";
    write_installation(base);
    write_mod(mod);
    write_installation(copied);
    copy_one(mod / "mod.ccx", copied / "mod.ccx");
    copy_one(mod / "oamod.yaml", copied / "oamod.yaml");

    check_resolution(inspect_game_install(base, {}, demo_1997, {mod, {}, false, nullptr}));
    check_resolution(inspect_game_install(copied));

    fs::remove(base / "addon.ccx");
    fs::remove(copied / "addon.ccx");
    const auto missing = inspect_game_install(base, {}, demo_1997, {mod, {}, false, nullptr});
    const auto missing_copy = inspect_game_install(copied);
    for (const GameInstall& install : {missing, missing_copy}) {
        OA_CHECK(!usable(install));
        OA_CHECK(contains(
            install.profile_errors,
            "layout.installation-archives: installation archive 'addon.ccx' is missing"
        ));
        OA_CHECK(
            describe_install_problem(install).find("installation archive 'addon.ccx' is missing") !=
            std::string::npos
        );
        bool skipped = false;
        for (const auto& skip : install.skipped)
            skipped = skipped || skip.error == "installation archive 'addon.ccx' is missing";
        OA_CHECK(skipped);
        const auto names = archive_names(install);
        OA_CHECK(std::find(names.begin(), names.end(), "addon.ccx") == names.end());
        OA_CHECK(std::find(names.begin(), names.end(), "zeta.ccx") != names.end());
        OA_CHECK(read_text(install, "shared.txt") == "mod");
        OA_CHECK(read_text(install, "zeta-only.txt") == "zeta");
    }
}

/// A name is matched without case: `Addon.CCX` is the profile's `addon.ccx`.
///
/// @param scratch the scratch directory
void test_name_without_case(const fs::path& scratch) {
    const auto base = scratch / "case-base";
    const auto mod = scratch / "case-mod";
    write_bytes(base / "rev31.gp3", archive({{"rev.txt", "rev"}}));
    write_bytes(
        base / "Addon.CCX", archive({{"addon-only.txt", "addon"}, {"shared.txt", "addon"}})
    );
    write_bytes(
        base / "base.hpi",
        archive(
            {{"shared.txt", "base"},
             {"base-only.txt", "base"},
             {"guis/mainmenu.gui", "gui"},
             {"palettes/palette.pal", "pal"},
             {"gamedata/sidedata.tdf", "side"},
             {"gamedata/sound.tdf", "sound"}}
        )
    );
    const std::string_view one = "oamod: 1\n"
                                 "id: layer-mod\n"
                                 "name: Layer Mod\n"
                                 "version: \"1\"\n"
                                 "author: {name: unknown}\n"
                                 "packaging: {revision: 1, date: 2026-10-04, packager: t}\n"
                                 "layout:\n"
                                 "  installation-archives: [addon.ccx]\n";
    write_bytes(mod / "mod.ccx", archive({{"shared.txt", "mod"}}));
    write_bytes(mod / "oamod.yaml", text_bytes(one));
    const auto install = inspect_game_install(base, {}, demo_1997, {mod, {}, false, nullptr});
    OA_CHECK(usable(install));
    OA_CHECK(install.profile_errors.empty());
    OA_CHECK(read_text(install, "shared.txt") == "mod");
    OA_CHECK(read_text(install, "addon-only.txt") == "addon");
    OA_CHECK(read_text(install, "base-only.txt") == "base");
    bool found = false;
    for (const auto& mounted : install.archives)
        found = found || mounted.filename() == "Addon.CCX";
    OA_CHECK(found);
}

} // namespace

int main() {
    const fs::path scratch = oa::test::make_scratch_directory("oa-app-installation-archives");
    test_layered_and_copied(scratch);
    test_name_without_case(scratch);
    std::error_code ignored;
    fs::remove_all(scratch, ignored);
    return oa::test::check_exit_status();
}
