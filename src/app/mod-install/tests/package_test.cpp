// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A mod package read and checked before anything is written: a package the
// Finder made, its top folder stripped and the files macOS adds ignored; a
// package with its profile at its top; a .backup folder of its own left
// out; the profile resolved a second time with its packaged INI file; the
// folders it makes; and each refusal once. The portable name rule and the
// folder-safe version.

#include "oa/app/mod_install.hpp"
#include "oa/test/check.hpp"
#include "oa/test/scratch_directory.hpp"
#include "oa/test/raw_zip.hpp"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

namespace fs = std::filesystem;
namespace install = oa::app::mod_install;
namespace raw = oa::test::raw_zip;
using install::Refusal;

/// A profile that resolves without game data, of an id and a revision.
///
/// @param id the profile's id
/// @param revision its packaging revision
/// @param more lines after it
/// @return the profile's text
std::string profile_text(std::string_view id, int revision = 1, std::string_view more = {}) {
    return "oamod: 1\n"
           "id: " +
           std::string(id) +
           "\n"
           "name: Example Mod\n"
           "version: \"1.0\"\n"
           "description: A made-up mod the tests install.\n"
           "requires: {base: ta-3.1c, catalogue: 1}\n"
           "author: {name: unknown}\n"
           "packaging: {revision: " +
           std::to_string(revision) + ", date: 2026-10-04, packager: Open Annihilation}\n" +
           std::string(more);
}

/// A scratch folder, deleted when the test ends.
class Scratch {
  public:

    Scratch() : path_(oa::test::make_scratch_directory("oa-mod-install-package-")) {}

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

/// Writes bytes to a file.
///
/// @param file the file
/// @param bytes the bytes
void write(const fs::path& file, const std::vector<uint8_t>& bytes) {
    std::ofstream out(file, std::ios::binary | std::ios::trunc);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
}

/// Writes a package of files and opens it.
///
/// @param scratch where the package goes
/// @param files its files
/// @param options how its profile is resolved
/// @return what open_package gave
install::PackageResult open_files(
    const Scratch& scratch,
    const std::vector<raw::RawFile>& files,
    const install::PackageOptions& options = {}
) {
    static int number = 0;
    const fs::path file = scratch.path() / ("package-" + std::to_string(number++) + ".oamod");
    write(file, raw::build_raw_archive(files).bytes);
    return install::open_package(file, options);
}

/// Opens a package of files and returns the refusal.
///
/// @param scratch where the package goes
/// @param files its files
/// @return the refusal; none when it opened
Refusal refusal_of(const Scratch& scratch, const std::vector<raw::RawFile>& files) {
    const auto result = open_files(scratch, files);
    OA_CHECK(result.package.has_value() == (result.problem.refusal == Refusal::none));
    return result.problem.refusal;
}

/// Returns the relative names a package unpacks.
///
/// @param package the package
/// @return the names
std::vector<std::string> names_of(const install::Package& package) {
    std::vector<std::string> names;
    for (const auto& file : package.files)
        names.push_back(file.name);
    return names;
}

void test_finder_package(const Scratch& scratch) {
    // The Finder's Compress: one top folder, macOS's own files beside it.
    const auto result = open_files(
        scratch,
        {raw::stored_text("Example Mod/", ""),
         raw::deflated_file(
             "Example Mod/oamod.yaml", raw::stored_text("", profile_text("example-mod")).data
         ),
         raw::stored_text("Example Mod/units/ARMCOM.FBI", "[UNITINFO]"),
         raw::stored_text("Example Mod/.DS_Store", "finder"),
         raw::stored_text("__MACOSX/Example Mod/._oamod.yaml", "attributes"),
         raw::stored_text("Example Mod/units/._ARMCOM.FBI", "attributes")}
    );
    OA_CHECK(result.package.has_value());
    if (!result.package)
        return;
    const auto& package = *result.package;
    OA_CHECK(package.root == "Example Mod/");
    OA_CHECK(package.profile && package.profile->id == "example-mod");
    OA_CHECK(package.profile && package.profile->packaging.revision == 1);
    const std::vector<std::string> expected{"oamod.yaml", "units/ARMCOM.FBI"};
    OA_CHECK(names_of(package) == expected);
    OA_CHECK(package.folders == std::vector<std::string>{"units"});
    OA_CHECK(package.unpacked_bytes == profile_text("example-mod").size() + 10);
    OA_CHECK(!package.backup_left_out);
    const auto incoming = install::incoming_of(*package.profile);
    OA_CHECK(incoming.id == "example-mod" && incoming.name == "Example Mod");
    OA_CHECK(incoming.version == "1.0" && incoming.revision == 1);
}

void test_root_profile(const Scratch& scratch) {
    // The profile at the top, with a .backup of the package's own left out,
    // and a packaged INI file the second resolution reads.
    const auto result = open_files(
        scratch,
        {raw::stored_text(
             "OAMOD.YAML",
             profile_text(
                 "example-mod",
                 2,
                 "identity: {settings-file: Example.ini}\n"
                 "limits:\n"
                 "  effects: true\n"
                 "settings:\n"
                 "  limits.effects.queue: {ini: Preferences/SfxLimit}\n"
             )
         ),
         raw::stored_text("example.INI", "[Preferences]\nSfxLimit = 1000;\n"),
         raw::stored_text(".backup/oamod.yaml", profile_text("example-mod")),
         raw::stored_text(".Backup/units/x.fbi", "kept")}
    );
    OA_CHECK(result.package.has_value());
    if (!result.package)
        return;
    OA_CHECK(result.package->root.empty());
    OA_CHECK(result.package->backup_left_out);
    const std::vector<std::string> expected{"OAMOD.YAML", "example.INI"};
    OA_CHECK(names_of(*result.package) == expected);
    OA_CHECK(result.package->profile->limits.effects.queue == 1000);
}

void test_refusals(const Scratch& scratch) {
    const auto profile = [](std::string_view id = "example-mod") {
        return raw::stored_text("oamod.yaml", profile_text(id));
    };
    // Not a file, and not a zip archive.
    OA_CHECK(
        install::open_package(scratch.path() / "missing.oamod").problem.refusal ==
        Refusal::unreadable
    );
    const fs::path text = scratch.path() / "text.oamod";
    write(text, std::vector<uint8_t>(500, 'x'));
    OA_CHECK(install::open_package(text).problem.refusal == Refusal::not_zip);
    // No profile at the top, or in one folder at the top.
    OA_CHECK(
        refusal_of(
            scratch,
            {raw::stored_text("a/oamod.yaml", profile_text("example-mod")),
             raw::stored_text("b/x.txt", "x")}
        ) == Refusal::no_profile
    );
    OA_CHECK(refusal_of(scratch, {raw::stored_text("x.txt", "x")}) == Refusal::no_profile);
    // A profile larger than the reader takes.
    OA_CHECK(
        refusal_of(scratch, {raw::stored_text("oamod.yaml", std::string(300 * 1024, '#'))}) ==
        Refusal::profile_too_large
    );
    // A profile that does not resolve keeps its diagnostics.
    const auto broken =
        open_files(scratch, {raw::stored_text("oamod.yaml", "oamod: 1\nname: Broken\n")});
    OA_CHECK(broken.problem.refusal == Refusal::profile_errors);
    OA_CHECK(!broken.problem.lines.empty());
    if (!broken.problem.lines.empty())
        OA_CHECK(broken.problem.lines.front().find("oamod.yaml") != std::string::npos);
    // Names that cannot be unpacked safely.
    for (const std::string_view name :
         {"../x.txt", "/x.txt", "C:x.txt", "CON.txt", "units/name.", "a|b"})
        OA_CHECK(
            refusal_of(scratch, {profile(), raw::stored_text(std::string(name), "x")}) ==
            Refusal::unsafe_name
        );
    // Two names that differ only in case, and a file and a folder of one name.
    OA_CHECK(
        refusal_of(
            scratch, {profile(), raw::stored_text("a", "x"), raw::stored_text("A/b", "y")}
        ) == Refusal::case_clash
    );
    OA_CHECK(
        refusal_of(
            scratch,
            {profile(), raw::stored_text("Units/a.fbi", "x"), raw::stored_text("units/b.fbi", "y")}
        ) == Refusal::case_clash
    );
    // A link.
    raw::RawFile link = raw::stored_text("units", "/etc");
    link.host = 3;
    link.external_attributes = 0120777U << 16U;
    OA_CHECK(refusal_of(scratch, {profile(), link}) == Refusal::link);
    // An encrypted entry, and one packed another way.
    raw::RawFile secret = raw::stored_text("secret.txt", "s");
    secret.flags = 1;
    OA_CHECK(refusal_of(scratch, {profile(), secret}) == Refusal::encrypted);
    raw::RawFile packed = raw::stored_text("packed.bin", "p");
    packed.method = 12;
    OA_CHECK(refusal_of(scratch, {profile(), packed}) == Refusal::method);
    // Larger than a mod may take, by the sizes the entries declare.
    raw::RawFile half = raw::deflated_file("half-a.bin", std::vector<uint8_t>(1, 0));
    half.bytes = uint64_t{5} << 29;
    half.zip64 = true;
    half.data.assign(3 << 20, 0);
    raw::RawFile other_half = half;
    other_half.name = "half-b.bin";
    const auto large = open_files(scratch, {profile(), half, other_half});
    OA_CHECK(large.problem.refusal == Refusal::too_large);
    OA_CHECK(large.problem.size_bytes == (uint64_t{5} << 30) + profile_text("example-mod").size());
    // Far more unpacked than packed.
    raw::RawFile bomb = raw::deflated_file("bomb.bin", std::vector<uint8_t>(1, 0));
    bomb.bytes = uint64_t{100} << 20;
    bomb.data.assign(120 * 1024, 0);
    OA_CHECK(refusal_of(scratch, {profile(), bomb}) == Refusal::bomb);
    // More folders than a mod may take: paths of 254 one-letter folders,
    // each under a folder of its own.
    std::vector<raw::RawFile> deep{profile()};
    std::string chain;
    for (int part = 0; part < 253; ++part)
        chain += "a/";
    for (std::size_t top = 0; top * 254 <= install::max_package_folders; ++top)
        deep.push_back(raw::stored_text(std::to_string(top) + "/" + chain + "x", ""));
    OA_CHECK(refusal_of(scratch, deep) == Refusal::too_many_folders);
    // An id that names a device on Windows.
    OA_CHECK(refusal_of(scratch, {profile("con")}) == Refusal::reserved_id);
    // Damaged data is refused once it is read: the profile's CRC-32.
    raw::RawFile damaged = profile();
    damaged.crc32 ^= 1;
    OA_CHECK(refusal_of(scratch, {damaged}) == Refusal::damaged);
}

void test_names() {
    OA_CHECK(install::portable_name_problem("units/armcom.fbi").empty());
    OA_CHECK(install::portable_name_problem("units/").empty());
    OA_CHECK(install::portable_name_problem("Caf\xc3\xa9 Mod/readme.txt").empty());
    OA_CHECK(!install::portable_name_problem("units/aux.fbi").empty());
    OA_CHECK(!install::portable_name_problem("COM1").empty());
    OA_CHECK(!install::portable_name_problem("lpt\xc2\xb9.txt").empty());
    OA_CHECK(!install::portable_name_problem("conout$.log").empty());
    OA_CHECK(install::portable_name_problem("console.txt").empty());
    OA_CHECK(!install::portable_name_problem("trailing ").empty());
    OA_CHECK(!install::portable_name_problem("what?.txt").empty());
    OA_CHECK(!install::portable_name_problem("tab\there").empty());
    OA_CHECK(!install::portable_name_problem("bad\xff").empty());
    OA_CHECK(!install::portable_name_problem(std::string(256, 'n')).empty());
    OA_CHECK(install::version_folder_part("10.2") == "10.2");
    OA_CHECK(install::version_folder_part("2.1 Beta") == "2.1-beta");
    OA_CHECK(install::version_folder_part("  v3 / final!! ") == "v3-final");
    OA_CHECK(install::version_folder_part("...") == "version");
    OA_CHECK(install::version_folder_part(std::string(40, '9')) == std::string(32, '9'));
    OA_CHECK(install::names_mod_package("Example-1.0.OAMOD"));
    OA_CHECK(!install::names_mod_package("example.zip"));
    OA_CHECK(!install::names_mod_package(".oamod"));
}

} // namespace

int main() {
    const Scratch scratch;
    test_finder_package(scratch);
    test_root_profile(scratch);
    test_refusals(scratch);
    test_names();
    return oa::test::check_exit_status();
}
