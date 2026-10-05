// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Recognising the installer of the Total Annihilation demo (1997) and
// unpacking its archive, over a synthetic installer: a PE image built here
// whose resource carries a small archive, with the release's sizes and hashes
// taken from those bytes in place of the real ones. It covers a renamed
// installer, reuse and repair of the unpacked archive, temporary files left
// by unpackings that stopped, the installer taken by its size beside a
// checked archive, files that are not the installer, failed unpacking, and
// the game folder resolution and messages around it.
//
// With --data it unpacks the installer the OA_DEMO_INSTALLER environment
// variable names into a temporary data folder and checks it against the
// release the engine recognises; without it the cases skip. With --verify
// FILE it checks that FILE is that release's archive.
#include "oa/app/demo_installer.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/base/sha256.hpp"
#include "oa/formats/hpi.hpp"
#include "oa/test/game_data.hpp"
#include "oa/test/pe_image.hpp"
#include "oa/platform/system.hpp"
#include <algorithm>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <map>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {

using namespace oa::app;
namespace sha256 = oa::base::sha256;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: check failed: %s\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

bool contains(std::string_view text, std::string_view part) {
    return text.find(part) != std::string_view::npos;
}

std::vector<uint8_t> read_bytes(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void write_bytes(const fs::path& path, const std::vector<uint8_t>& bytes) {
    std::ofstream(path, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
}

// The names of the files directly in a folder.
std::set<std::string> names_in(const fs::path& folder) {
    std::set<std::string> names;
    std::error_code error;
    for (fs::directory_iterator entry(folder, error), end; !error && entry != end;
         entry.increment(error))
        names.insert(path_to_utf8(entry->path().filename()));
    return names;
}

// A synthetic release: an archive holding the resources the frontend opens
// first, in a PE image's ADD/130/1033 resource beside a readme resource.
struct Synthetic {
    std::vector<uint8_t> archive;
    std::vector<uint8_t> installer;
    DemoRelease release;
};

Synthetic synthetic_release() {
    Synthetic synthetic;
    const std::vector<oa::HpiWriteFile> files{
        {"guis/mainmenu.gui", {'G', 'U', 'I'}},
        {"palettes/palette.pal", std::vector<uint8_t>(1024, 7)},
        {"gamedata/sidedata.tdf", {'[', 'S', ']'}},
        {"gamedata/sound.tdf", {'[', 'S', ']'}},
    };
    synthetic.archive = oa::write_hpi(files);
    synthetic.installer = oa::test::pe_image::build({
                                                        {"ADD", 130, 1033, synthetic.archive},
                                                        {"ADD", 135, 1033, {'r', 'e', 'a', 'd'}},
                                                    })
                              .bytes;
    synthetic.release = DemoRelease{
        synthetic.installer.size(),
        sha256::digest_of(synthetic.installer),
        "ADD",
        130,
        1033,
        synthetic.archive.size(),
        sha256::digest_of(synthetic.archive),
        "demo-test",
        "TADemo.hpi",
    };
    return synthetic;
}

void test_unpacks_and_reuses(const fs::path& temporary, const Synthetic& synthetic) {
    const auto folder = temporary / "unpack" / "Downloads";
    const auto data = temporary / "unpack" / "data";
    fs::create_directories(folder);
    // Renamed, and not a program name: recognised by size and hash alone.
    write_bytes(folder / "setup copy.bin", synthetic.installer);
    write_bytes(folder / "readme.txt", {'h', 'i'});
    const auto chosen_before = names_in(folder);

    const auto first = set_up_demo(folder, data, synthetic.release);
    const auto archive = data / "demo-test" / "TADemo.hpi";
    CHECK(first.outcome == DemoOutcome::ready && first.unpacked);
    CHECK(first.installer == folder / "setup copy.bin");
    CHECK(first.folder == data / "demo-test" && first.archive == archive);
    CHECK(read_bytes(archive) == synthetic.archive);
    CHECK(names_in(data / "demo-test") == std::set<std::string>{"TADemo.hpi"});
    CHECK(names_in(folder) == chosen_before);
    CHECK(read_bytes(folder / "setup copy.bin") == synthetic.installer);
    CHECK(archive_matches(archive, synthetic.release));
    CHECK(!archive_matches(folder / "setup copy.bin", synthetic.release));
    CHECK(contains(describe_ready(first), "unpacked now from"));

    const auto second = set_up_demo(folder, data, synthetic.release);
    CHECK(second.outcome == DemoOutcome::ready && !second.unpacked && second.archive == archive);
    CHECK(contains(describe_ready(second), "unpacked earlier and checked"));

    // A damaged, a shortened and a missing archive are each unpacked again.
    auto damaged = synthetic.archive;
    damaged[damaged.size() / 2] ^= 1;
    write_bytes(archive, damaged);
    CHECK(!archive_matches(archive, synthetic.release));
    const auto repaired = set_up_demo(folder, data, synthetic.release);
    CHECK(repaired.outcome == DemoOutcome::ready && repaired.unpacked);
    CHECK(read_bytes(archive) == synthetic.archive);
    write_bytes(archive, {synthetic.archive.begin(), synthetic.archive.end() - 1});
    CHECK(set_up_demo(folder, data, synthetic.release).unpacked);
    CHECK(read_bytes(archive) == synthetic.archive);
    fs::remove(archive);
    CHECK(set_up_demo(folder, data, synthetic.release).unpacked);
    CHECK(read_bytes(archive) == synthetic.archive);
    CHECK(names_in(data / "demo-test") == std::set<std::string>{"TADemo.hpi"});
}

// A data folder whose demo folder is a link to a folder elsewhere: the
// unpacking fails, and nothing is written, removed or reused through it; the
// same for an archive name that is a link.
void test_links_are_not_followed(const fs::path& temporary, const Synthetic& synthetic) {
    const auto folder = temporary / "links" / "Downloads";
    const auto data = temporary / "links" / "data";
    const auto elsewhere = temporary / "links" / "elsewhere";
    fs::create_directories(folder);
    fs::create_directories(data);
    fs::create_directories(elsewhere);
    write_bytes(folder / "setup.exe", synthetic.installer);
    write_bytes(elsewhere / "TADemo.hpi", synthetic.archive);
    std::error_code error;
    fs::create_directory_symlink(elsewhere, data / "demo-test", error);
    if (error) {
        std::printf("skipped the link case: %s\n", error.message().c_str());
        return;
    }
    const auto through = set_up_demo(folder, data, synthetic.release);
    CHECK(through.outcome == DemoOutcome::unpack_failed && !through.unpacked);
    CHECK(contains(through.problem, "link"));
    CHECK(names_in(elsewhere) == std::set<std::string>{"TADemo.hpi"});

    fs::remove(data / "demo-test");
    fs::create_directories(data / "demo-test");
    fs::create_symlink(elsewhere / "TADemo.hpi", data / "demo-test" / "TADemo.hpi", error);
    if (error) {
        std::printf("skipped the archive link case: %s\n", error.message().c_str());
        return;
    }
    const auto named = set_up_demo(folder, data, synthetic.release);
    CHECK(named.outcome == DemoOutcome::unpack_failed && !named.unpacked);
    CHECK(read_bytes(elsewhere / "TADemo.hpi") == synthetic.archive);
}

// Temporary files left by unpackings that stopped part way are removed once
// they have gone unwritten for a minute, whether the archive is reused or
// unpacked again; a fresher one may belong to an unpacking under way and
// stays, and so does every other file.
void test_abandoned_temporaries(const fs::path& temporary, const Synthetic& synthetic) {
    const auto folder = temporary / "abandoned" / "Downloads";
    const auto data = temporary / "abandoned" / "data";
    const auto unpacked = data / "demo-test";
    fs::create_directories(folder);
    fs::create_directories(unpacked);
    write_bytes(folder / "Total Annihilation.exe", synthetic.installer);
    const auto abandon = [&unpacked](const std::string& name, bool stale) {
        write_bytes(unpacked / name, {'p', 'a', 'r', 't'});
        if (stale)
            fs::last_write_time(
                unpacked / name, fs::file_time_type::clock::now() - std::chrono::hours(1)
            );
    };
    abandon("TADemo.hpi.unpacking-1-0", true);
    abandon("TADemo.hpi.unpacking-2-1", false);
    write_bytes(unpacked / "notes.txt", {'n'});
    const auto first = set_up_demo(folder, data, synthetic.release);
    CHECK(first.outcome == DemoOutcome::ready && first.unpacked);
    CHECK(
        names_in(unpacked) ==
        (std::set<std::string>{"TADemo.hpi", "TADemo.hpi.unpacking-2-1", "notes.txt"})
    );

    abandon("TADemo.hpi.unpacking-3-2", true);
    const auto second = set_up_demo(folder, data, synthetic.release);
    CHECK(second.outcome == DemoOutcome::ready && !second.unpacked);
    CHECK(
        names_in(unpacked) ==
        (std::set<std::string>{"TADemo.hpi", "TADemo.hpi.unpacking-2-1", "notes.txt"})
    );
}

// Once the archive is unpacked and passes its check, a file of the
// installer's size is taken for it unread; unpacking again needs the
// installer itself.
void test_installer_by_size(const fs::path& temporary, const Synthetic& synthetic) {
    const auto folder = temporary / "by-size" / "Downloads";
    const auto data = temporary / "by-size" / "data";
    const auto archive = data / "demo-test" / "TADemo.hpi";
    fs::create_directories(folder);
    write_bytes(folder / "Total Annihilation.exe", synthetic.installer);
    CHECK(set_up_demo(folder, data, synthetic.release).unpacked);

    auto altered = synthetic.installer;
    altered[altered.size() / 2] ^= 1;
    write_bytes(folder / "Total Annihilation.exe", altered);
    const auto reused = set_up_demo(folder, data, synthetic.release);
    CHECK(reused.outcome == DemoOutcome::ready && !reused.unpacked);
    CHECK(reused.installer == folder / "Total Annihilation.exe" && reused.archive == archive);

    auto damaged = synthetic.archive;
    damaged[damaged.size() / 2] ^= 1;
    write_bytes(archive, damaged);
    const auto refused = set_up_demo(folder, data, synthetic.release);
    CHECK(refused.outcome == DemoOutcome::unrecognised && refused.installer.empty());
    CHECK(
        refused.rejected == std::vector<fs::path>{folder / "Total Annihilation.exe"} &&
        refused.archive.empty()
    );
    CHECK(read_bytes(archive) == damaged);

    write_bytes(folder / "Total Annihilation.exe", synthetic.installer);
    const auto repaired = set_up_demo(folder, data, synthetic.release);
    CHECK(repaired.outcome == DemoOutcome::ready && repaired.unpacked);
    CHECK(read_bytes(archive) == synthetic.archive);
}

void test_other_files(const fs::path& temporary, const Synthetic& synthetic) {
    const auto data = temporary / "other" / "data";
    {
        const auto folder = temporary / "other" / "nothing";
        fs::create_directories(folder);
        write_bytes(folder / "readme.txt", {'h', 'i'});
        const auto setup = set_up_demo(folder, data, synthetic.release);
        CHECK(setup.outcome == DemoOutcome::no_installer && setup.rejected.empty());
    }
    {
        // Another program, and a file of the installer's size with other
        // bytes: neither is the release.
        const auto folder = temporary / "other" / "programs";
        fs::create_directories(folder);
        write_bytes(folder / "Other Setup.EXE", oa::test::pe_image::build({}).bytes);
        auto altered = synthetic.installer;
        altered.back() ^= 1;
        write_bytes(folder / "installer.bin", altered);
        const auto setup = set_up_demo(folder, data, synthetic.release);
        CHECK(setup.outcome == DemoOutcome::unrecognised);
        CHECK(
            setup.rejected ==
            (std::vector<fs::path>{folder / "Other Setup.EXE", folder / "installer.bin"})
        );
        CHECK(setup.installer.empty() && setup.archive.empty());
    }
    CHECK(!fs::exists(data));
    {
        const auto folder = temporary / "other" / "installer";
        fs::create_directories(folder);
        write_bytes(folder / "Total.exe", synthetic.installer);
        // No data folder known.
        const auto nowhere = set_up_demo(folder, {}, synthetic.release);
        CHECK(
            nowhere.outcome == DemoOutcome::unpack_failed &&
            contains(nowhere.problem, "data folder")
        );
        // The data folder is a file.
        const auto blocked = temporary / "other" / "blocked";
        write_bytes(blocked, {'x'});
        const auto unmade = set_up_demo(folder, blocked, synthetic.release);
        CHECK(
            unmade.outcome == DemoOutcome::unpack_failed &&
            contains(unmade.problem, "could not be made")
        );
    }
}

void test_failed_unpacking(const fs::path& temporary, const Synthetic& synthetic) {
    const auto folder = temporary / "failed" / "installer";
    const auto data = temporary / "failed" / "data";
    fs::create_directories(folder);
    write_bytes(folder / "Total.exe", synthetic.installer);
    {
        auto release = synthetic.release;
        release.archive_resource_id = 131;
        const auto setup = set_up_demo(folder, data, release);
        CHECK(setup.outcome == DemoOutcome::unpack_failed);
        CHECK(contains(setup.problem, "the resource id is missing"));
    }
    {
        auto release = synthetic.release;
        release.archive_size += 1;
        const auto setup = set_up_demo(folder, data, release);
        CHECK(
            setup.outcome == DemoOutcome::unpack_failed && contains(setup.problem, "bytes, where")
        );
    }
    {
        // The archive fails its hash: nothing is kept, not even the
        // temporary file.
        auto release = synthetic.release;
        release.archive_sha256[0] ^= 1;
        const auto setup = set_up_demo(folder, data, release);
        CHECK(setup.outcome == DemoOutcome::unpack_failed && contains(setup.problem, "SHA-256"));
        CHECK(names_in(data / "demo-test").empty());
    }
    {
        // A damaged archive left there fails the check and stays put.
        write_bytes(data / "demo-test" / "TADemo.hpi", {'x'});
        auto release = synthetic.release;
        release.archive_sha256[0] ^= 1;
        (void)set_up_demo(folder, data, release);
        CHECK(names_in(data / "demo-test") == std::set<std::string>{"TADemo.hpi"});
        CHECK(read_bytes(data / "demo-test" / "TADemo.hpi") == std::vector<uint8_t>{'x'});
    }
}

void test_inspection(const fs::path& temporary, const Synthetic& synthetic) {
    const auto data = temporary / "inspect" / "data";
    const auto folder = temporary / "inspect" / "installer";
    fs::create_directories(folder);
    write_bytes(folder / "Total Annihilation.exe", synthetic.installer);
    const auto install = inspect_game_install(folder, data, synthetic.release);
    CHECK(usable(install));
    CHECK(install.installation == data / "demo-test");
    CHECK(
        install.archives.size() == 1 && install.archives[0].filename() == "TADemo.hpi" &&
        fs::equivalent(install.archives[0], data / "demo-test" / "TADemo.hpi")
    );
    CHECK(install.demo.outcome == DemoOutcome::ready);

    // Other archives in the unpacked archive's folder, even ones that would
    // be looked in first, are not mounted: only the checked archive is.
    const std::vector<oa::HpiWriteFile> override_files{{"guis/mainmenu.gui", {'X'}}};
    const auto other_archive = oa::write_hpi(override_files);
    for (const auto* name : {"rev31.gp3", "extra.ccx", "extra.ufo", "extra.hpi"})
        write_bytes(data / "demo-test" / name, other_archive);
    const auto crowded = inspect_game_install(folder, data, synthetic.release);
    CHECK(usable(crowded) && crowded.demo.outcome == DemoOutcome::ready);
    CHECK(
        crowded.archives.size() == 1 &&
        fs::equivalent(crowded.archives[0], data / "demo-test" / "TADemo.hpi")
    );
    for (const auto* name : {"rev31.gp3", "extra.ccx", "extra.ufo", "extra.hpi"})
        fs::remove(data / "demo-test" / name);

    // A folder that holds the archive itself is an ordinary installation.
    const auto installed = temporary / "inspect" / "installed";
    fs::create_directories(installed);
    write_bytes(installed / "TADemo.hpi", synthetic.archive);
    const auto ordinary = inspect_game_install(installed, data, synthetic.release);
    CHECK(usable(ordinary) && ordinary.installation == installed);
    CHECK(ordinary.demo.outcome == DemoOutcome::not_searched);

    const auto empty = temporary / "inspect" / "empty";
    fs::create_directories(empty);
    const auto none = inspect_game_install(empty, data, synthetic.release);
    CHECK(!usable(none) && none.demo.outcome == DemoOutcome::no_installer);
}

// Resolves through the real inspection over the temporary data folder, with
// scripted dialogs.
struct Host {
    fs::path data{};
    const DemoRelease* release{};
    std::vector<fs::path> picks{};
    std::vector<std::string> notices{};
    std::map<fs::path, GameInstall> scripted{};
    std::size_t asked = 0;

    GameDirectoryHost host() { return {this, pick_folder, tell_user, inspect}; }

    static FolderPick pick_folder(void* context, const fs::path&, fs::path* chosen, std::string*) {
        auto& self = *static_cast<Host*>(context);
        if (self.asked >= self.picks.size())
            return FolderPick::cancelled;
        *chosen = self.picks[self.asked++];
        return FolderPick::chosen;
    }

    static void tell_user(void* context, Notice, std::string_view text) {
        static_cast<Host*>(context)->notices.emplace_back(text);
    }

    static GameInstall inspect(void* context, const fs::path& folder) {
        auto& self = *static_cast<Host*>(context);
        if (const auto found = self.scripted.find(folder); found != self.scripted.end())
            return found->second;
        return inspect_game_install(folder, self.data, *self.release);
    }
};

void test_resolution(const fs::path& temporary, const Synthetic& synthetic) {
    const auto data = temporary / "resolve" / "data";
    const auto folder = temporary / "resolve" / "installer";
    const auto other = temporary / "resolve" / "other";
    fs::create_directories(folder);
    fs::create_directories(other);
    write_bytes(folder / "Total Annihilation.exe", synthetic.installer);
    write_bytes(other / "Setup.exe", oa::test::pe_image::build({}).bytes);
    {
        Host host{data, &synthetic.release};
        const auto h = host.host();
        const auto result = resolve_game_directory({folder, std::nullopt, false, true}, h);
        CHECK(result && result->path == folder && result->source == GameDirectorySource::argument);
        CHECK(result && result->installation == data / "demo-test");
        CHECK(result && result->archives.size() == 1 && result->demo.outcome == DemoOutcome::ready);
    }
    {
        Host host{data, &synthetic.release};
        const auto h = host.host();
        std::string refusal;
        try {
            (void)resolve_game_directory({other, std::nullopt, false, true}, h);
        } catch (const std::runtime_error& error) {
            refusal = error.what();
        }
        CHECK(contains(refusal, "--game-dir") && contains(refusal, "Setup.exe is not the release"));
    }
    {
        // The dialog: the installer's folder is taken and remembered, the
        // archive's folder mounted; a folder of other programs is refused.
        Host host{data, &synthetic.release};
        host.picks = {other, folder};
        const auto h = host.host();
        const auto result = resolve_game_directory({}, h);
        CHECK(result && result->path == folder && result->source == GameDirectorySource::chosen);
        CHECK(result && result->installation == data / "demo-test");
        CHECK(host.notices.size() == 2);
        CHECK(host.notices.size() == 2 && contains(host.notices[0], "demo (1997)"));
        CHECK(
            host.notices.size() == 2 &&
            contains(
                host.notices[1],
                "Setup.exe is not the release of the Total Annihilation demo (1997) that Open "
                "Annihilation recognises"
            ) &&
            contains(host.notices[1], "or the folder that holds the installer")
        );
    }
    {
        // A stored installer folder is used again without asking.
        Host host{data, &synthetic.release};
        const auto h = host.host();
        const auto result = resolve_game_directory({{}, path_to_utf8(folder), false, false}, h);
        CHECK(result && result->source == GameDirectorySource::stored && result->path == folder);
        CHECK(result && result->installation == data / "demo-test" && !result->demo.unpacked);
        CHECK(host.asked == 0 && host.notices.empty());
    }
    {
        // What the player is told when unpacking fails or the disk is full.
        const fs::path failing = "/games/failing";
        const fs::path full = "/games/full";
        GameInstall failed;
        failed.folder = true;
        failed.demo.outcome = DemoOutcome::unpack_failed;
        failed.demo.installer = failing / "Total Annihilation.exe";
        failed.demo.problem = "the folder could not be made";
        GameInstall no_room = failed;
        no_room.demo.outcome = DemoOutcome::disk_full;
        no_room.demo.problem = "unpacking its game data needs about 20 MB free";
        Host host{data, &synthetic.release};
        host.scripted = {{failing, failed}, {full, no_room}};
        host.picks = {failing, full};
        const auto h = host.host();
        CHECK(!resolve_game_directory({}, h));
        CHECK(host.notices.size() == 4);
        CHECK(
            host.notices.size() == 4 &&
            contains(
                host.notices[1], "Total Annihilation.exe, but its game data could not be unpacked"
            ) &&
            contains(host.notices[1], "the folder could not be made")
        );
        CHECK(
            host.notices.size() == 4 && contains(host.notices[2], "but the disk is full") &&
            contains(host.notices[2], "about 20 MB free")
        );
    }
}

void test_release() {
    CHECK(demo_1997.installer_size == 21'540'864 && demo_1997.archive_size == 20'474'804);
    const auto installer = sha256::to_hex(demo_1997.installer_sha256);
    CHECK(
        std::string(installer.begin(), installer.end()) ==
        "5e41cf05226c274b4ac9e4398f74f6b321506bd7a4317ee1744ff7aceba34c49"
    );
    const auto archive = sha256::to_hex(demo_1997.archive_sha256);
    CHECK(
        std::string(archive.begin(), archive.end()) ==
        "fd53a2637ecf8fb5ca6d2c02a34b4ef783a4441f8be070137276afc4d5627e1e"
    );
    CHECK(demo_1997.folder_name == "demo-1997" && demo_1997.archive_name == "TADemo.hpi");
}

fs::path fresh_temporary(std::string_view name) {
    return fs::temp_directory_path() /
           (std::string(name) + "-" +
            std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
}

// The installer OA_DEMO_INSTALLER names, unpacked into a temporary data folder.
void test_installed_release() {
    const auto named = oa::platform::environment_value("OA_DEMO_INSTALLER");
    if (!named || named->empty())
        oa::test::skip_test(
            "the Total Annihilation demo (1997) installer checks",
            "OA_DEMO_INSTALLER is not set; set it to the demo's installer"
        );
    const auto installer = path_from_utf8(named->c_str());
    std::error_code error;
    if (!fs::is_regular_file(installer, error)) {
        std::fprintf(stderr, "FAILED: OA_DEMO_INSTALLER names no file: %s\n", named->c_str());
        ++failures;
        return;
    }
    const auto data = fresh_temporary("oa-demo-installer-data");
    const auto folder = installer.parent_path();
    const auto first = set_up_demo(folder, data);
    CHECK(first.outcome == DemoOutcome::ready && first.unpacked);
    CHECK(fs::equivalent(first.installer, installer));
    const auto archive = data / "demo-1997" / "TADemo.hpi";
    CHECK(first.archive == archive && fs::file_size(archive, error) == demo_1997.archive_size);
    CHECK(archive_matches(archive));
    CHECK(sha256::digest_of(read_bytes(archive)) == demo_1997.archive_sha256);
    CHECK(names_in(data / "demo-1997") == std::set<std::string>{"TADemo.hpi"});
    const auto second = set_up_demo(folder, data);
    CHECK(second.outcome == DemoOutcome::ready && !second.unpacked);
    const auto install = inspect_game_install(folder, data);
    CHECK(usable(install) && install.installation == data / "demo-1997");
    std::printf("%s\n", describe_ready(first).c_str());
    fs::remove_all(data, error);
}

} // namespace

int main(int argc, char** argv) {
    try {
        if (argc == 3 && std::string_view(argv[1]) == "--verify") {
            const auto archive = path_from_utf8(argv[2]);
            CHECK(archive_matches(archive));
            if (failures == 0)
                std::printf("%s is the Total Annihilation demo (1997) archive\n", argv[2]);
        } else if (oa::test::game_data_requested(argc, argv)) {
            test_installed_release();
        } else {
            test_release();
            const auto temporary = fresh_temporary("oa-demo-installer-test");
            const auto synthetic = synthetic_release();
            test_unpacks_and_reuses(temporary, synthetic);
            test_links_are_not_followed(temporary, synthetic);
            test_abandoned_temporaries(temporary, synthetic);
            test_installer_by_size(temporary, synthetic);
            test_other_files(temporary, synthetic);
            test_failed_unpacking(temporary, synthetic);
            test_inspection(temporary, synthetic);
            test_resolution(temporary, synthetic);
            std::error_code error;
            fs::remove_all(temporary, error);
        }
    } catch (const std::exception& error) {
        std::fprintf(stderr, "FAILED: %s\n", error.what());
        ++failures;
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    return 0;
}
