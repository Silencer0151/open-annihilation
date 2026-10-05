// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The changes in a scratch Mods folder: packages unpacked in small steps
// and installed; three replaces that keep one .backup, the oldest version
// deleted a step at a time; a reinstall that keeps the .backup and drops
// added files; two roll backs that return to the start; a rename refused at
// each step of each change, which changes nothing before the new files are
// in place and leaves a visible folder after; every crash point of every
// change settled by recovery to the state before or after it; the folders
// recovery cannot settle made visible; renames tried again while another
// program holds the files; a rename never replacing; links in a discard
// folder removed, never followed; a .backup that is a link to a folder
// outside Mods, dropped by a replace and refused by a roll back, and a
// discard folder that is itself such a link, each leaving that folder
// whole; the Windows reparse tags taken as links, a file sync's cloud tags
// not; files made anew only; a target that changed under a plan; the lock
// another copy holds; the room and path checks; and the files and folders
// a step makes held to its budget.

#include "oa/app/mod_install.hpp"
#include "oa/platform/files.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"
#include "oa/test/raw_zip.hpp"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iterator>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::mod_install;
namespace raw = oa::test::raw_zip;
using install::Change;
using install::Refusal;

/// The target folder of the tests.
constexpr std::string_view target = "example-mod";

/// A profile of the test mod at a version and revision.
///
/// @param revision its packaging revision
/// @param version its version
/// @return its text
std::string profile_text(int revision, std::string_view version = "1.0") {
    return "oamod: 1\n"
           "id: example-mod\n"
           "name: Example Mod\n"
           "version: \"" +
           std::string(version) +
           "\"\n"
           "description: A made-up mod the tests install.\n"
           "requires: {base: ta-3.1c, catalogue: 1}\n"
           "author: {name: unknown}\n"
           "packaging: {revision: " +
           std::to_string(revision) + ", date: 2026-10-04, packager: Open Annihilation}\n";
}

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-mod-install-change-")) {}

    ~Scratch() {
        std::error_code ignored;
        fs::remove_all(path_, ignored);
    }

    Scratch(const Scratch&) = delete;
    Scratch& operator=(const Scratch&) = delete;

    [[nodiscard]] const fs::path& path() const { return path_; }

  private:

    fs::path path_;
};

/// Writes a text file, making its folders.
///
/// @param file the file
/// @param text its text
void write(const fs::path& file, std::string_view text) {
    fs::create_directories(file.parent_path());
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out << text;
}

/// Reads a text file.
///
/// @param file the file
/// @return its text; empty when it cannot be read
std::string read(const fs::path& file) {
    std::ifstream in(file, std::ios::binary);
    return {std::istreambuf_iterator<char>{in}, std::istreambuf_iterator<char>{}};
}

/// Makes a mod folder by hand: its profile at a revision and one file.
///
/// @param folder the folder
/// @param revision the revision
void make_mod(const fs::path& folder, int revision) {
    write(folder / "oamod.yaml", profile_text(revision));
    write(folder / "units" / "unit.fbi", "revision " + std::to_string(revision));
}

/// Returns the revision a folder holds; -1 when it holds no mod.
///
/// @param folder the folder
/// @return the revision
int64_t revision_in(const fs::path& folder) {
    const auto held = install::read_installed_mod(folder);
    return held.kind == install::FolderKind::mod ? held.revision : -1;
}

/// Returns the names of the folders an install keeps in Mods while it
/// works, and of the visible ones recovery made.
///
/// @param mods the Mods folder
/// @return the names, sorted
std::vector<std::string> reserved_in(const fs::path& mods) {
    std::vector<std::string> names;
    std::error_code error;
    for (fs::directory_iterator entry{mods, error}, end; !error && entry != end;
         entry.increment(error)) {
        const std::string name = entry->path().filename().string();
        if ((name.starts_with(".oamod-") && name != ".oamod-lock") ||
            name.find("-left-over") != std::string::npos)
            names.push_back(name);
    }
    std::sort(names.begin(), names.end());
    return names;
}

/// Deletes discard folders to the end.
///
/// @param folders the folders
void discard_all(const std::vector<fs::path>& folders) {
    install::Discarder discarder;
    discarder.add(folders);
    for (int step = 0; step < 100'000 && discarder.step(); ++step) {
    }
    OA_CHECK(!discarder.busy());
}

/// Writes a package of the test mod at a revision, with a stored file, a
/// deflated file and a folder.
///
/// @param folder where it goes
/// @param revision its revision
/// @param version its version
/// @return the package
fs::path write_package(const fs::path& folder, int revision, std::string_view version = "1.0") {
    std::string big;
    for (int line = 0; line < 4000; ++line)
        big += "line " + std::to_string(line) + " of revision " + std::to_string(revision) + "\n";
    const fs::path file = folder / ("example-mod-" + std::string(version) + "-" +
                                    std::to_string(revision) + ".oamod");
    const std::string profile = profile_text(revision, version);
    const auto archive = raw::build_raw_archive({
        raw::stored_text("oamod.yaml", profile),
        raw::stored_text("units/", ""),
        raw::stored_text("units/unit.fbi", "revision " + std::to_string(revision)),
        raw::deflated_file("gamedata/big.tdf", raw::stored_text("", big).data),
    });
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(archive.bytes.data()),
        static_cast<std::streamsize>(archive.bytes.size())
    );
    return file;
}

/// What an install did.
struct Installed {
    install::InstallPlan plan{};
    install::ChangeResult result{};
    install::Problem problem{};
};

/// Installs a package the way the game does: reads it, plans, unpacks it
/// in small steps and commits the change an answer picks.
///
/// @param file the package
/// @param mods the Mods folder
/// @param answer the change: install for a plan that asks nothing, else
///        replace, reinstall, or install for Install alongside
/// @param hooks the unpacking's stand-ins
/// @return what it did
Installed install_package(
    const fs::path& file,
    const fs::path& mods,
    Change answer,
    const install::UnpackHooks& hooks = {}
) {
    Installed done{};
    const auto opened = install::open_package(file);
    OA_CHECK(opened.package.has_value());
    if (!opened.package) {
        done.problem = opened.problem;
        return done;
    }
    const auto incoming = install::incoming_of(*opened.package->profile);
    done.plan = install::plan_install(incoming, install::mods_folder_hooks(mods));
    const bool alongside =
        answer == Change::install && done.plan.kind != install::PlanKind::install;
    const std::string folder = alongside ? done.plan.alongside : done.plan.target;
    const install::InstalledMod expected =
        alongside ? install::InstalledMod{} : done.plan.installed;
    install::Unpacking unpacking;
    if (!unpacking.start(*opened.package, mods, folder, expected, done.problem, hooks)) {
        discard_all(unpacking.discards());
        return done;
    }
    auto step = oa::formats::zip::StreamStep::more;
    while (step == oa::formats::zip::StreamStep::more)
        step = unpacking.step(4096, done.problem);
    if (step != oa::formats::zip::StreamStep::done) {
        discard_all(unpacking.discards());
        return done;
    }
    OA_CHECK(unpacking.done_bytes() == unpacking.total_bytes());
    install::ChangeOptions options{};
    options.expected = expected;
    done.result = install::commit_change(mods, folder, answer, options);
    discard_all(done.result.discards);
    return done;
}

void test_installs(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "installs" / "Mods";
    const fs::path packages = scratch.path() / "installs";
    fs::create_directories(packages);
    const fs::path folder = mods / std::string(target);
    // A new id installs with no question.
    auto done = install_package(write_package(packages, 1), mods, Change::install);
    OA_CHECK(done.plan.kind == install::PlanKind::install && done.result.changed);
    OA_CHECK(revision_in(folder) == 1 && !fs::exists(folder / ".backup"));
    OA_CHECK(
        read(folder / "gamedata" / "big.tdf").find("line 3999 of revision 1") != std::string::npos
    );
    OA_CHECK(reserved_in(mods).empty());
    // Three replaces keep one version back.
    done = install_package(write_package(packages, 2), mods, Change::replace);
    OA_CHECK(done.plan.kind == install::PlanKind::ask_update && done.result.backup_kept);
    OA_CHECK(revision_in(folder) == 2 && revision_in(folder / ".backup") == 1);
    done = install_package(write_package(packages, 3), mods, Change::replace);
    OA_CHECK(done.plan.backup && done.plan.backup->revision == 1);
    OA_CHECK(revision_in(folder) == 3 && revision_in(folder / ".backup") == 2);
    OA_CHECK(!fs::exists(folder / ".backup" / ".backup"));
    OA_CHECK(reserved_in(mods).empty());
    // A reinstall drops files added since and keeps the version kept.
    write(folder / "added.txt", "added");
    write(folder / "units" / "unit.fbi", "edited");
    done = install_package(write_package(packages, 3), mods, Change::reinstall);
    OA_CHECK(done.plan.kind == install::PlanKind::ask_reinstall && done.result.changed);
    OA_CHECK(
        !fs::exists(folder / "added.txt") && read(folder / "units" / "unit.fbi") == "revision 3"
    );
    OA_CHECK(revision_in(folder / ".backup") == 2 && done.result.backup_kept);
    OA_CHECK(reserved_in(mods).empty());
    // Two roll backs return to the start.
    auto rolled = install::commit_change(mods, target, Change::roll_back);
    OA_CHECK(rolled.changed && rolled.backup_kept);
    OA_CHECK(revision_in(folder) == 2 && revision_in(folder / ".backup") == 3);
    rolled = install::commit_change(mods, target, Change::roll_back);
    OA_CHECK(revision_in(folder) == 3 && revision_in(folder / ".backup") == 2);
    OA_CHECK(reserved_in(mods).empty());
    // Another version installs alongside, in a folder of its own.
    done = install_package(write_package(packages, 1, "2.0 Beta"), mods, Change::install);
    OA_CHECK(done.plan.kind == install::PlanKind::ask_version);
    OA_CHECK(done.plan.alongside == "example-mod-2.0-beta" && done.result.changed);
    OA_CHECK(revision_in(mods / "example-mod-2.0-beta") == 1 && revision_in(folder) == 3);
}

/// A rename hook that refuses one call, by its number, with an error no try
/// again mends.
struct RefuseOne {
    int refuse{};
    int calls{};
};

/// Sets up a target for a change, runs it with one rename refused, and
/// checks the folder afterwards.
///
/// @param scratch the scratch folder
/// @param change the change
/// @param refuse the rename refused, from 1
/// @return whether the change put the new files in place
bool refused_at(const Scratch& scratch, Change change, int refuse) {
    const fs::path mods =
        scratch.path() /
        ("refused-" + std::to_string(static_cast<int>(change)) + "-" + std::to_string(refuse)) /
        "Mods";
    const fs::path folder = mods / std::string(target);
    make_mod(folder, 2);
    if (change != Change::install)
        make_mod(folder / ".backup", 1);
    else
        fs::remove_all(folder);
    if (change != Change::roll_back)
        make_mod(mods / (".oamod-staging-" + std::string(target)), 3);
    RefuseOne hook{refuse, 0};
    install::ChangeOptions options{};
    options.hooks.context = &hook;
    options.hooks.rename =
        [](void* context, const fs::path& from, const fs::path& to, std::error_code& error) {
            auto& self = *static_cast<RefuseOne*>(context);
            if (++self.calls == self.refuse) {
                error = std::make_error_code(std::errc::io_error);
                return;
            }
            install::rename_exclusively(from, to, error);
        };
    const auto result = install::commit_change(mods, target, change, options);
    discard_all(result.discards);
    // The rename refused at or before the new files are in place changes
    // nothing; one after leaves the old version visible.
    if (!result.changed) {
        OA_CHECK(result.refusal == Refusal::not_placed);
        OA_CHECK(change == Change::install ? !fs::exists(folder) : revision_in(folder) == 2);
        if (change != Change::install)
            OA_CHECK(revision_in(folder / ".backup") == 1);
        OA_CHECK(reserved_in(mods).empty());
    } else {
        const int64_t now = change == Change::roll_back ? 1 : 3;
        OA_CHECK(revision_in(folder) == now);
        OA_CHECK(result.backup_kept || !result.left_over.empty() || change == Change::reinstall);
        if (!result.left_over.empty())
            OA_CHECK(result.left_over.filename().string().find("-left-over") != std::string::npos);
    }
    return result.changed;
}

void test_refused_renames(const Scratch& scratch) {
    // The renames each change makes, and the first that puts the new files
    // in place.
    struct Steps {
        Change change{};
        int renames{};
        int committed_at{};
    };

    for (const Steps steps :
         {Steps{Change::install, 1, 1},
          Steps{Change::replace, 4, 2},
          Steps{Change::reinstall, 4, 2},
          Steps{Change::roll_back, 4, 3}}) {
        for (int refuse = 1; refuse <= steps.renames; ++refuse) {
            const bool changed = refused_at(scratch, steps.change, refuse);
            OA_CHECK(changed == (refuse > steps.committed_at));
        }
    }
}

/// Builds a crash state by hand, recovers, and returns the Mods folder.
///
/// @param scratch the scratch folder
/// @param name the state's name
/// @param build makes the folders the stop left
/// @return the Mods folder, settled and its discard folders deleted
fs::path settled(
    const Scratch& scratch, std::string_view name, const std::function<void(const fs::path&)>& build
) {
    const fs::path mods = scratch.path() / ("crash-" + std::string(name)) / "Mods";
    fs::create_directories(mods);
    build(mods);
    const auto recovery = install::recover_changes(mods);
    OA_CHECK(!recovery.skipped);
    discard_all(recovery.discards);
    return mods;
}

void test_recovery(const Scratch& scratch) {
    const auto folder = [](const fs::path& mods) { return mods / std::string(target); };
    const auto role = [](const fs::path& mods, std::string_view prefix) {
        return mods / (std::string(prefix) + std::string(target));
    };
    // A replace stopped after each of its renames: before, after, after.
    auto mods = settled(scratch, "replace-1", [&](const fs::path& at) {
        make_mod(role(at, ".oamod-old-"), 2);
        make_mod(role(at, ".oamod-old-") / ".backup", 1);
        make_mod(role(at, ".oamod-staging-"), 3);
    });
    OA_CHECK(revision_in(folder(mods)) == 2 && revision_in(folder(mods) / ".backup") == 1);
    OA_CHECK(reserved_in(mods).empty());
    mods = settled(scratch, "replace-2", [&](const fs::path& at) {
        make_mod(folder(at), 3);
        make_mod(role(at, ".oamod-old-"), 2);
        make_mod(role(at, ".oamod-old-") / ".backup", 1);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(!fs::exists(folder(mods) / ".backup" / ".backup") && reserved_in(mods).empty());
    mods = settled(scratch, "replace-3", [&](const fs::path& at) {
        make_mod(folder(at), 3);
        make_mod(role(at, ".oamod-old-"), 2);
        make_mod(at / (".oamod-discard-" + std::string(target) + "-1"), 1);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(reserved_in(mods).empty());
    // A reinstall stopped after each of its renames.
    mods = settled(scratch, "reinstall-1", [&](const fs::path& at) {
        make_mod(role(at, ".oamod-replaced-"), 3);
        make_mod(role(at, ".oamod-replaced-") / ".backup", 2);
        make_mod(role(at, ".oamod-staging-"), 3);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(reserved_in(mods).empty());
    mods = settled(scratch, "reinstall-2", [&](const fs::path& at) {
        make_mod(folder(at), 3);
        make_mod(role(at, ".oamod-replaced-"), 3);
        make_mod(role(at, ".oamod-replaced-") / ".backup", 2);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(reserved_in(mods).empty());
    mods = settled(scratch, "reinstall-3", [&](const fs::path& at) {
        make_mod(folder(at), 3);
        make_mod(folder(at) / ".backup", 2);
        make_mod(role(at, ".oamod-replaced-"), 3);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(reserved_in(mods).empty());
    // A roll back stopped after each of its renames: before, before, after.
    mods = settled(scratch, "roll-back-1", [&](const fs::path& at) {
        make_mod(folder(at), 3);
        make_mod(role(at, ".oamod-restore-"), 2);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    mods = settled(scratch, "roll-back-2", [&](const fs::path& at) {
        make_mod(role(at, ".oamod-old-"), 3);
        make_mod(role(at, ".oamod-restore-"), 2);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(reserved_in(mods).empty());
    mods = settled(scratch, "roll-back-3", [&](const fs::path& at) {
        make_mod(folder(at), 2);
        make_mod(role(at, ".oamod-old-"), 3);
    });
    OA_CHECK(revision_in(folder(mods)) == 2 && revision_in(folder(mods) / ".backup") == 3);
    OA_CHECK(reserved_in(mods).empty());
    // What recovery cannot settle becomes a visible folder, never deleted.
    mods = settled(scratch, "leftovers", [&](const fs::path& at) {
        make_mod(folder(at), 3);
        make_mod(folder(at) / ".backup", 2);
        make_mod(role(at, ".oamod-old-"), 1);
        make_mod(role(at, ".oamod-restore-"), 0);
    });
    OA_CHECK(revision_in(folder(mods)) == 3 && revision_in(folder(mods) / ".backup") == 2);
    OA_CHECK(revision_in(mods / "example-mod-left-over") == 1);
    OA_CHECK(revision_in(mods / "example-mod-left-over-2") == 0);
    OA_CHECK(
        (reserved_in(mods) ==
         std::vector<std::string>{"example-mod-left-over", "example-mod-left-over-2"})
    );
}

void test_files(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "files" / "Mods";
    fs::create_directories(mods);
    // A rename never replaces what is there.
    make_mod(mods / "a", 1);
    make_mod(mods / "b", 2);
    std::error_code error;
    install::rename_exclusively(mods / "a", mods / "b", error);
    OA_CHECK(error);
    OA_CHECK(revision_in(mods / "a") == 1 && revision_in(mods / "b") == 2);
    fs::create_directories(mods / "empty");
    install::rename_exclusively(mods / "a", mods / "empty", error);
    OA_CHECK(error && revision_in(mods / "a") == 1);
    // Files are made anew only.
    install::NewFile file;
    OA_CHECK(!file.create(mods / "a" / "oamod.yaml", error));
    OA_CHECK(error == std::errc::file_exists);
    OA_CHECK(file.create(mods / "a" / "new.txt", error));
    const std::string bytes = "made";
    OA_CHECK(file.write(
        std::span<const uint8_t>(reinterpret_cast<const uint8_t*>(bytes.data()), bytes.size())
    ));
    OA_CHECK(file.finish() && read(mods / "a" / "new.txt") == "made");
    // A discard folder's link is removed, never followed.
    const fs::path outside = scratch.path() / "files" / "outside";
    write(outside / "kept.txt", "kept");
    const fs::path discard = mods / ".oamod-discard-a-1";
    make_mod(discard, 1);
    fs::create_directories(discard / "deep" / "deeper");
    write(discard / "deep" / "deeper" / "file.txt", "x");
    bool linked = true;
    try {
        fs::create_directory_symlink(outside, discard / "link");
    } catch (const fs::filesystem_error&) {
        linked = false;
        std::fprintf(stderr, "skipped: this system made no folder link\n");
    }
    discard_all({discard});
    OA_CHECK(!fs::exists(discard));
    if (linked)
        OA_CHECK(read(outside / "kept.txt") == "kept");
}

/// Makes a folder link, or says why the test skips it. Some systems report
/// a link made and make none; that skips it too.
///
/// @param to what it links to
/// @param made_link the link
/// @return true when it was made
bool make_folder_link(const fs::path& to, const fs::path& made_link) {
    std::error_code error;
    fs::create_directory_symlink(to, made_link, error);
    if (!error && !install::is_link_or_junction(made_link))
        error = std::make_error_code(std::errc::no_such_file_or_directory);
    if (error)
        std::fprintf(
            stderr, "skipped: this system made no folder link: %s\n", error.message().c_str()
        );
    return !error;
}

void test_linked_backup(const Scratch& scratch) {
    const fs::path root = scratch.path() / "linked";
    const fs::path mods = root / "Mods";
    const fs::path outside = root / "outside";
    write(outside / "kept.txt", "kept");
    write(outside / "sub" / "deeper.txt", "kept");
    const auto outside_whole = [&] {
        return read(outside / "kept.txt") == "kept" &&
               read(outside / "sub" / "deeper.txt") == "kept";
    };
    // A replace drops the old version's .backup, a link: the link goes.
    const fs::path folder = mods / std::string(target);
    make_mod(folder, 1);
    if (!make_folder_link(outside, folder / ".backup"))
        return;
    const auto done = install_package(write_package(root, 2), mods, Change::replace);
    OA_CHECK(done.plan.kind == install::PlanKind::ask_update && done.result.changed);
    OA_CHECK(revision_in(folder) == 2 && revision_in(folder / ".backup") == 1);
    OA_CHECK(!install::is_link_or_junction(folder / ".backup"));
    OA_CHECK(outside_whole() && reserved_in(mods).empty());
    // A roll back never puts a link in the target's place.
    const fs::path other = mods / "other-mod";
    make_mod(other, 1);
    if (!make_folder_link(outside, other / ".backup"))
        return;
    const auto rolled = install::commit_change(mods, "other-mod", Change::roll_back);
    OA_CHECK(!rolled.changed && rolled.refusal == Refusal::changed);
    discard_all(rolled.discards);
    OA_CHECK(revision_in(other) == 1 && install::is_link_or_junction(other / ".backup"));
    OA_CHECK(outside_whole());
    // A discard folder that is itself a link, as recovery finds it.
    const fs::path discard = mods / ".oamod-discard-other-mod-1";
    if (!make_folder_link(outside, discard))
        return;
    const auto recovery = install::recover_changes(mods);
    OA_CHECK(recovery.discards.size() == 1);
    discard_all(recovery.discards);
    OA_CHECK(!fs::exists(fs::symlink_status(discard)) && outside_whole());
}

// Which Windows reparse tags are links, as the values Windows gives: a name
// surrogate names another place; a file sync's cloud folder is itself.
void test_reparse_tags() {
    constexpr uint32_t junction = 0xA0000003U;      // IO_REPARSE_TAG_MOUNT_POINT
    constexpr uint32_t symbolic_link = 0xA000000CU; // IO_REPARSE_TAG_SYMLINK
    constexpr uint32_t cloud = 0x9000001AU;         // IO_REPARSE_TAG_CLOUD
    constexpr uint32_t cloud_flagged = 0x9000F01AU; // IO_REPARSE_TAG_CLOUD_F
    OA_CHECK(install::reparse_tag_is_link(junction));
    OA_CHECK(install::reparse_tag_is_link(symbolic_link));
    OA_CHECK(!install::reparse_tag_is_link(cloud));
    OA_CHECK(!install::reparse_tag_is_link(cloud_flagged));
}

void test_retries(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "retries" / "Mods";
    make_mod(mods / (".oamod-staging-" + std::string(target)), 1);

    // Twice refused while another program holds the files, then renamed.
    struct Busy {
        int busy{};
        int calls{};
        uint32_t waited{};
    } busy{2, 0, 0};

    install::ChangeOptions options{};
    options.first_retry_ms = 0;
    options.hooks.context = &busy;
    options.hooks.rename =
        [](void* context, const fs::path& from, const fs::path& to, std::error_code& error) {
            auto& self = *static_cast<Busy*>(context);
            if (self.calls++ < self.busy) {
#ifdef _WIN32
                error = std::error_code(32, std::system_category());
#else
                error = std::make_error_code(std::errc::device_or_resource_busy);
#endif
                return;
            }
            install::rename_exclusively(from, to, error);
        };
    auto result = install::commit_change(mods, target, Change::install, options);
    OA_CHECK(result.changed && busy.calls == 3);
    // Held for good: the waits add up to the most, then it gives up.
    make_mod(mods / (".oamod-staging-other"), 1);
    busy = {1'000'000, 0, 0};
    options.first_retry_ms = 2;
    options.most_wait_ms = 50;
    options.hooks.wait = [](void* context, uint32_t milliseconds) {
        static_cast<Busy*>(context)->waited += milliseconds;
    };
    result = install::commit_change(mods, "other", Change::install, options);
    OA_CHECK(!result.changed && result.refusal == Refusal::not_placed);
    // The move into place and the move of the staging folder aside each
    // wait the most.
    OA_CHECK(busy.waited == 100 && busy.calls > 6);
    discard_all(result.discards);
}

void test_unpacking_checks(const Scratch& scratch) {
    const fs::path packages = scratch.path() / "checks";
    const fs::path mods = packages / "Mods";
    fs::create_directories(packages);
    const fs::path file = write_package(packages, 1);
    // Too little room on the disk.
    install::UnpackHooks hooks{};
    hooks.available = [](void*, const fs::path&) { return uint64_t{1000}; };
    auto done = install_package(file, mods, Change::install, hooks);
    OA_CHECK(done.problem.refusal == Refusal::no_space && done.problem.limit_bytes == 1000);
    // A file the file system folds to one already made.
    hooks = {};
    hooks.before_file = [](void*, const fs::path& path) {
        if (path.filename() == "unit.fbi")
            write(path, "made first");
    };
    done = install_package(file, mods, Change::install, hooks);
    OA_CHECK(
        done.problem.refusal == Refusal::case_clash && done.problem.subject == "units/unit.fbi"
    );
    OA_CHECK(!fs::exists(mods / std::string(target)) && reserved_in(mods).empty());
    // The target changed under the plan: deleted between the plan and the change.
    done = install_package(file, mods, Change::install);
    OA_CHECK(done.result.changed);
    const auto opened = install::open_package(write_package(packages, 2));
    OA_CHECK(opened.package.has_value());
    if (opened.package) {
        const auto plan = install::plan_install(
            install::incoming_of(*opened.package->profile), install::mods_folder_hooks(mods)
        );
        install::Unpacking unpacking;
        install::Problem problem{};
        OA_CHECK(unpacking.start(*opened.package, mods, plan.target, plan.installed, problem));
        while (unpacking.step(1 << 20, problem) == oa::formats::zip::StreamStep::more) {
        }
        fs::remove_all(mods / std::string(target));
        install::ChangeOptions options{};
        options.expected = plan.installed;
        const auto result = install::commit_change(mods, plan.target, Change::replace, options);
        OA_CHECK(!result.changed && result.refusal == Refusal::changed);
        discard_all(result.discards);
        OA_CHECK(!fs::exists(mods / std::string(target)) && reserved_in(mods).empty());
    }
    // A path longer than the system opens, with room kept spare: a Mods
    // folder deep enough that a long name in staging passes the limit.
    const std::size_t longest = oa::platform::longest_path();
    if (longest > 8192) {
        std::fprintf(stderr, "skipped: this system opens paths of %zu characters\n", longest);
        return;
    }
    const std::string long_name = std::string(200, 'a') + "/" + std::string(199, 'b');
    fs::path deep = packages / "deep";
    while (deep.native().size() + 32 + long_name.size() + 16 <= longest)
        deep /= std::string(200, 'p');
    fs::create_directories(deep / "Mods");
    const fs::path long_package = packages / "long.oamod";
    const auto archive = raw::build_raw_archive(
        {raw::stored_text("oamod.yaml", profile_text(1)), raw::stored_text(long_name, "x")}
    );
    std::ofstream(long_package, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(archive.bytes.data()),
            static_cast<std::streamsize>(archive.bytes.size())
        );
    done = install_package(long_package, deep / "Mods", Change::install);
    OA_CHECK(done.problem.refusal == Refusal::path_too_long && !done.problem.detail.empty());
}

void test_step_budget(const Scratch& scratch) {
    // A package of many empty files in many folders: each step makes no more
    // than its budget pays for, however few bytes the files hold.
    const fs::path root = scratch.path() / "budget";
    const fs::path mods = root / "Mods";
    std::vector<raw::RawFile> files{raw::stored_text("oamod.yaml", profile_text(1))};
    for (int folder = 0; folder < 20; ++folder)
        for (int file = 0; file < 10; ++file)
            files.push_back(
                raw::stored_text(
                    "units/" + std::to_string(folder) + "/" + std::to_string(file) + ".fbi", ""
                )
            );
    const auto archive = raw::build_raw_archive(files);
    fs::create_directories(root);
    std::ofstream(root / "many.oamod", std::ios::binary)
        .write(
            reinterpret_cast<const char*>(archive.bytes.data()),
            static_cast<std::streamsize>(archive.bytes.size())
        );
    const auto opened = install::open_package(root / "many.oamod");
    OA_CHECK(opened.package.has_value());
    if (!opened.package)
        return;
    OA_CHECK(opened.package->folders.size() == 21 && opened.package->folders.front() == "units");
    install::Unpacking unpacking;
    install::Problem problem{};
    int made = 0;
    install::UnpackHooks hooks{};
    hooks.context = &made;
    hooks.before_file = [](void* context, const fs::path&) { ++*static_cast<int*>(context); };
    OA_CHECK(unpacking.start(*opened.package, mods, target, {}, problem, hooks));
    const uint64_t budget = 8 * install::unpack_entry_cost;
    int steps = 0;
    auto step = oa::formats::zip::StreamStep::more;
    while (step == oa::formats::zip::StreamStep::more) {
        made = 0;
        step = unpacking.step(budget, problem);
        OA_CHECK(static_cast<uint64_t>(made) <= budget / install::unpack_entry_cost);
        ++steps;
    }
    OA_CHECK(step == oa::formats::zip::StreamStep::done);
    // The folders, the files and the folders' syncs, eight a step at most.
    OA_CHECK(steps >= (21 + 201 + 22) / 8);
    unpacking.cancel();
    discard_all(unpacking.discards());
}

void test_lock(const Scratch& scratch) {
    const fs::path mods = scratch.path() / "lock" / "Mods";
    fs::create_directories(mods);
    make_mod(mods / (".oamod-staging-" + std::string(target)), 1);
    {
        // Another copy of the game holds the folder.
        const auto other = install::FileLock::take(mods / ".oamod-lock");
        OA_CHECK(other != nullptr);
        OA_CHECK(install::FileLock::take(mods / ".oamod-lock") == nullptr);
        OA_CHECK(install::hold_mods_folder(mods) == nullptr);
        const auto recovery = install::recover_changes(mods);
        OA_CHECK(recovery.skipped && recovery.discards.empty());
        OA_CHECK(fs::exists(mods / (".oamod-staging-" + std::string(target))));
        const auto result = install::commit_change(mods, target, Change::install);
        OA_CHECK(!result.changed && result.refusal == Refusal::busy);
    }
    // Holds this process takes share one lock.
    const auto first = install::hold_mods_folder(mods);
    const auto second = install::hold_mods_folder(mods);
    OA_CHECK(first != nullptr && first == second);
    const auto recovery = install::recover_changes(mods);
    OA_CHECK(!recovery.skipped && recovery.discards.size() == 1);
    discard_all(recovery.discards);
    OA_CHECK(reserved_in(mods).empty());
}

} // namespace

int main() {
    const Scratch scratch;
    test_installs(scratch);
    test_refused_renames(scratch);
    test_recovery(scratch);
    test_files(scratch);
    test_linked_backup(scratch);
    test_reparse_tags();
    test_retries(scratch);
    test_unpacking_checks(scratch);
    test_step_budget(scratch);
    test_lock(scratch);
    return oa::test::check_exit_status();
}
