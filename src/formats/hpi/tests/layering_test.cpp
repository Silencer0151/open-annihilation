// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A mod folder layered over a base folder against a copied install of the
// same files: discovery, every listing and every read must agree, and a
// discovery plan replaces the revision archive and a group's pattern; a
// mod's kept version (.backup) is passed over by every lookup.

#include "oa/formats/hpi.hpp"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
using Bytes = std::vector<uint8_t>;

int failures = 0;

void check(bool condition, const std::string& message) {
    if (!condition) {
        std::cerr << "FAIL: " << message << '\n';
        ++failures;
    }
}

Bytes text(std::string_view value) {
    return Bytes(value.begin(), value.end());
}

std::string lower(std::string value) {
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return value;
}

class TempDir {
  public:

    TempDir() {
        static std::atomic<int> counter{0};
        const auto stamp = std::chrono::steady_clock::now().time_since_epoch().count();
        path_ = fs::temp_directory_path() /
                ("oa-layering-test-" + std::to_string(stamp) + "-" + std::to_string(counter++));
        fs::create_directories(path_);
    }

    ~TempDir() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    TempDir(const TempDir&) = delete;
    TempDir& operator=(const TempDir&) = delete;

    const fs::path& path() const { return path_; }

  private:

    fs::path path_;
};

void write_file(const fs::path& target, const Bytes& bytes) {
    fs::create_directories(target.parent_path());
    std::ofstream stream(target, std::ios::binary | std::ios::trunc);
    stream.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
}

/// An archive holding `id.txt` with its name, so a mount can be told apart,
/// and the files given.
Bytes archive(std::string_view id, std::vector<oa::HpiWriteFile> files = {}) {
    files.push_back({"id.txt", text(id), 0});
    return oa::write_hpi(files);
}

/// Copies a folder over another as Windows does: a name that already exists,
/// compared without case, is overwritten and keeps its spelling; a folder of
/// an existing name is merged into it.
void copy_over(const fs::path& from, const fs::path& to) {
    fs::create_directories(to);
    for (const auto& item : fs::directory_iterator(from)) {
        const auto name = item.path().filename().string();
        fs::path target = to / name;
        for (const auto& existing : fs::directory_iterator(to))
            if (lower(existing.path().filename().string()) == lower(name))
                target = existing.path();
        if (item.is_directory()) {
            copy_over(item.path(), target);
            continue;
        }
        if (fs::exists(target))
            fs::remove(target);
        fs::copy_file(item.path(), target);
    }
}

/// A base folder, a mod folder over it, and a copied install of both.
struct Layout {
    TempDir dir;
    fs::path base = dir.path() / "base";
    fs::path mod = dir.path() / "mod";
    fs::path copied = dir.path() / "copied";

    Layout() {
        write_file(base / "rev31.gp3", archive("rev", {{"gamedata/sidedata.tdf", text("rev"), 0}}));
        write_file(base / "A.CCX", archive("a.ccx"));
        write_file(base / "b.ufo", archive("b.ufo"));
        // Nine plain archives in the base folder and two more in the mod
        // folder, one of whose names the base folder holds in another case:
        // ten of the eleven names mount, and the eleventh only as a disc
        // archive.
        for (int index = 0; index < 9; ++index) {
            const auto name = "pack0" + std::to_string(index) + ".hpi";
            write_file(
                base / name,
                archive(
                    name,
                    {{"weapons/b.tdf", text("archived b"), 0},
                     {"weapons/d.tdf", text("archived d " + name), 0}}
                )
            );
        }
        write_file(mod / "Pack05.HPI", archive("mod pack05"));
        write_file(mod / "pack09.hpi", archive("pack09"));
        write_file(mod / "pack10.hpi", archive("pack10"));
        write_file(mod / "c.ufo", archive("c.ufo"));

        write_file(base / "Weapons" / "a.tdf", text("base a"));
        write_file(base / "Weapons" / "e.tdf", text("base e"));
        write_file(base / "gamedata" / "x.tdf", text("base x"));
        write_file(base / "units" / "u.fbi", text("base u"));
        write_file(base / "units" / "deep" / "v.fbi", text("base v"));
        // The mod spells the folder and a file in another case, adds a file,
        // and hides a base file and the archives' copy with empty files.
        write_file(mod / "weapons" / "A.TDF", text("mod a"));
        write_file(mod / "weapons" / "c.tdf", text("mod c"));
        write_file(mod / "weapons" / "b.tdf", {});
        write_file(mod / "weapons" / "e.tdf", {});
        write_file(mod / "unitsE" / "m.fbi", text("mod m"));
        write_file(mod / "units" / "deep" / "w.fbi", text("mod w"));

        copy_over(base, copied);
        copy_over(mod, copied);
    }
};

/// What a store reports, in a form that does not depend on its folders.
struct Report {
    std::vector<std::string> discovered;
    std::vector<std::string> mounts;
    std::map<std::string, std::string> found;
    std::map<std::string, std::vector<std::string>> listed;
    std::map<std::string, std::string> reads;
};

Report report_of(oa::AssetStore& store, const oa::DiscoveryPlan& plan) {
    Report report;
    for (const auto& outcome : store.discover(plan))
        report.discovered.push_back(
            lower(outcome.path.filename().string()) + (outcome.mounted ? " mounted" : "") +
            (outcome.already_mounted ? " again" : "")
        );
    for (std::size_t index = 0; index < store.mount_paths().size(); ++index) {
        const auto id = store.mounted(index).read("id.txt");
        report.mounts.push_back(
            lower(store.mount_paths()[index].filename().string()) + "=" +
            std::string(id.value->begin(), id.value->end())
        );
    }
    for (const char* pattern :
         {"*", "weapons\\*", "WEAPONS\\*.tdf", "units\\*", "units\\deep\\*", "unitse\\*.fbi"}) {
        std::string line;
        for (const auto& entry : store.find(pattern))
            line += entry.name + (entry.directory ? "/" : "") + ":" + std::to_string(entry.size) +
                    "@" + std::to_string(entry.mount) + " ";
        report.found[pattern] = line;
    }
    for (const char* directory : {"weapons", "units", "unitse", "gamedata", ""}) {
        auto in_order = store.list_effective_in_mount_order(directory, "");
        std::sort(in_order.begin(), in_order.end());
        auto recursive = store.list_effective_recursive(directory, "");
        std::sort(recursive.begin(), recursive.end());
        report.listed[std::string("sorted ") + directory] = store.list_effective(directory, "");
        report.listed[std::string("in order ") + directory] = in_order;
        report.listed[std::string("recursive ") + directory] = recursive;
        for (const auto& path : recursive) {
            const auto data = store.read(path);
            report.reads[path] = std::string(data.bytes.begin(), data.bytes.end()) +
                                 (data.archived ? " (archived)" : "");
        }
    }
    return report;
}

void layered_folders_match_copied_install() {
    Layout layout;
    oa::DiscoveryPlan plan;
    plan.folders_as_disc = true;
    oa::AssetStore layered(std::vector<fs::path>{layout.mod, layout.base});
    oa::AssetStore copied(layout.copied);
    const auto from_layers = report_of(layered, plan);
    const auto from_copy = report_of(copied, plan);
    check(from_layers.discovered == from_copy.discovered, "discovery reports agree");
    check(from_layers.mounts == from_copy.mounts, "the same archives mount in the same order");
    for (const auto& [pattern, line] : from_copy.found)
        check(
            from_layers.found.at(pattern) == line,
            "find(" + pattern + ") agrees: " + from_layers.found.at(pattern) + " vs " + line
        );
    for (const auto& [listing, names] : from_copy.listed)
        check(from_layers.listed.at(listing) == names, listing + " agrees");
    check(from_layers.reads == from_copy.reads, "every read agrees");

    // The combined ten-archive limit: pack00..pack09 mount in the group, and
    // pack10 mounts last from the folders serving as the disc.
    std::vector<std::string> expected{"rev31.gp3=rev", "a.ccx=a.ccx", "b.ufo=b.ufo", "c.ufo=c.ufo"};
    for (int index = 0; index < 10; ++index) {
        const auto name = "pack0" + std::to_string(index) + ".hpi";
        // pack05 is the mod's copy; pack09 is the mod's own.
        const auto id = index == 5 ? std::string("mod pack05") : index == 9 ? "pack09" : name;
        expected.push_back(name + "=" + id);
    }
    expected.emplace_back("pack10.hpi=pack10");
    check(from_layers.mounts == expected, "the hpi limit counts both folders' archives");

    check(layered.read("Weapons/a.tdf").bytes == text("mod a"), "the mod's file wins");
    check(layered.read("weapons/e.tdf").bytes.empty(), "an empty file wins its path");
    check(
        !layered.read("weapons/b.tdf").archived && layered.read("weapons/b.tdf").bytes.empty(),
        "an empty loose file hides the archives' copy"
    );
    check(!layered.load_file_contents("weapons/b.tdf"), "an empty file loads no contents");
    check(layered.file_size("weapons/b.tdf") == 0, "an empty file has size 0");
    check(
        layered.loose_file("WEAPONS/A.tdf") &&
            layered.loose_file("WEAPONS/A.tdf")->parent_path().parent_path() ==
                fs::absolute(layout.mod),
        "a loose file resolves from the first folder holding it"
    );
    check(
        layered.loose_file("units/u.fbi") &&
            layered.loose_file("units/u.fbi")->parent_path().parent_path() ==
                fs::absolute(layout.base),
        "a loose file only the base holds resolves from the base"
    );
}

void plan_replaces_revision_archive_and_patterns() {
    TempDir dir;
    const auto root = dir.path() / "game";
    write_file(root / "rev31.gp3", archive("rev31"));
    write_file(root / "MODREV.GP3", archive("modrev"));
    write_file(root / "a.ufo", archive("a.ufo"));
    write_file(root / "b.swx", archive("b.swx"));
    write_file(root / "c.hpi", archive("c.hpi"));
    oa::DiscoveryPlan plan;
    plan.revision_archive = "modrev.gp3";
    plan.ufo_pattern = "*.SWX";
    oa::AssetStore store(root);
    (void)store.discover(plan);
    std::vector<std::string> mounted;
    for (const auto& path : store.mount_paths())
        mounted.push_back(lower(path.filename().string()));
    const std::vector<std::string> expected{"modrev.gp3", "b.swx", "c.hpi"};
    check(mounted == expected, "the plan's revision archive and ufo pattern replace the base's");

    oa::AssetStore base(root);
    (void)base.discover(oa::DiscoveryPlan{});
    mounted.clear();
    for (const auto& path : base.mount_paths())
        mounted.push_back(lower(path.filename().string()));
    const std::vector<std::string> base_order{"rev31.gp3", "a.ufo", "c.hpi"};
    check(mounted == base_order, "the default plan is the base game's");
}

void backup_folder_is_hidden() {
    TempDir dir;
    const auto base = dir.path() / "base";
    const auto mod = dir.path() / "mod";
    write_file(base / "rev31.gp3", archive("rev"));
    write_file(base / "units" / "u.fbi", text("base u"));
    // The version a mod's update replaced, kept for one step back, in any case.
    write_file(mod / ".Backup" / "units" / "x.fbi", text("kept x"));
    write_file(mod / ".Backup" / "x.txt", text("kept"));
    write_file(mod / ".Backup" / "pack08.hpi", archive("kept pack08"));
    write_file(mod / "units" / "m.fbi", text("mod m"));
    oa::AssetStore store(std::vector<fs::path>{mod, base});
    oa::DiscoveryPlan plan;
    plan.folders_as_disc = true;
    for (const auto& found : store.discover(plan))
        check(
            found.path.parent_path().filename() != ".Backup",
            "no archive is discovered in the kept version"
        );
    for (const auto& path : store.mount_paths())
        check(lower(path.parent_path().filename().string()) != ".backup", "nothing mounts from it");
    check(!store.loose_file(".backup/x.txt"), "no lookup reaches its files");
    check(!store.loose_file(".BACKUP/units/x.fbi"), "no lookup reaches them in any case");
    for (const auto& entry : store.find("*"))
        check(lower(entry.name) != ".backup", "the top's listing leaves it out");
    check(store.find(".backup\\*").empty(), "a search inside it finds nothing");
    for (const auto& name : store.list_effective_recursive("", ".fbi"))
        check(!name.starts_with(".backup"), "a listing from the top does not walk it: " + name);
    check(store.list_effective(".backup/units", ".fbi").empty(), "a listing of it is empty");
    check(
        store.read("units/u.fbi").bytes == text("base u"), "the game folder's files read as before"
    );
    check(store.read("units/m.fbi").bytes == text("mod m"), "the mod's own files read as before");
    const auto units = store.list_effective("units", ".fbi");
    check(units.size() == 2, "the units folders list as before");
}

void lookups_are_observed() {
    TempDir dir;
    write_file(dir.path() / "game" / "gamedata" / "x.tdf", text("x"));
    oa::AssetStore store(dir.path() / "game");
    std::vector<std::string> seen;
    store.observe_lookups({&seen, [](void* context, std::string_view name) {
                               static_cast<std::vector<std::string>*>(context)->emplace_back(name);
                           }});
    (void)store.file_size("gamedata/x.tdf");
    (void)store.find("guis\\*.gui");
    (void)store.list_effective("units", ".fbi");
    const std::vector<std::string> expected{"gamedata/x.tdf", "guis\\*.gui", "units"};
    check(seen == expected, "reads, finds and listings are observed");
    store.observe_lookups({});
    (void)store.file_size("gamedata/x.tdf");
    check(seen.size() == expected.size(), "a default observer watches nothing");
}

} // namespace

int main() {
    try {
        layered_folders_match_copied_install();
        plan_replaces_revision_archive_and_patterns();
        backup_folder_is_hidden();
        lookups_are_observed();
    } catch (const std::exception& error) {
        std::cerr << "unexpected exception: " << error.what() << '\n';
        return 1;
    }
    if (failures != 0) {
        std::cerr << failures << " layering check(s) failed\n";
        return 1;
    }
    std::cout << "hpi layering tests passed\n";
    return 0;
}
