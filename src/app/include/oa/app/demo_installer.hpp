// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The installer of the Total Annihilation demo (1997) as a game folder: the
// installer is recognised by its size and SHA-256, whatever its name, and the
// game data archive it carries is unpacked into the per-user data folder and
// checked, where the engine mounts it as the installation. Once that archive
// is there and passes its check, the installer is recognised by its size
// alone. The installer is only read; nothing in it runs.
#pragma once

#include "oa/base/sha256.hpp"
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace oa::app {

namespace fs = std::filesystem;

/// What identifies a release of the Total Annihilation demo (1997): its installer and the archive in it.
///
/// demo_1997 is the release the engine recognises; tests substitute their
/// own.
struct DemoRelease {
    /// The installer's size, in bytes.
    uint64_t installer_size{};
    base::sha256::Digest installer_sha256{};
    /// The installer's resource that holds the game data archive: its type
    /// name, id and language.
    std::string_view archive_resource_type;
    uint32_t archive_resource_id{};
    uint32_t archive_resource_language{};
    /// The archive's size, in bytes.
    uint64_t archive_size{};
    base::sha256::Digest archive_sha256{};
    /// The folder under the per-user data folder that holds the unpacked archive.
    std::string_view folder_name;
    /// The unpacked archive's file name.
    std::string_view archive_name;
};

/// The release of the Total Annihilation demo (1997) this engine recognises.
///
/// The installer is a program file whose resource of type ADD, id 130 and
/// language 1033 (English, United States) holds the game data archive, which
/// its own setup installs as TADemo.hpi.
inline constexpr DemoRelease demo_1997{
    21'540'864,
    base::sha256::parse_hex("5e41cf05226c274b4ac9e4398f74f6b321506bd7a4317ee1744ff7aceba34c49")
        .value(),
    "ADD",
    130,
    1033,
    20'474'804,
    base::sha256::parse_hex("fd53a2637ecf8fb5ca6d2c02a34b4ef783a4441f8be070137276afc4d5627e1e")
        .value(),
    "demo-1997",
    "TADemo.hpi",
};

/// What set_up_demo() found in a folder, and what came of it.
enum class DemoOutcome : uint8_t {
    not_searched,  ///< the folder was not searched for the installer
    no_installer,  ///< no file in the folder can be taken for the installer
    unrecognised,  ///< files that look meant for it are not the release recognised
    ready,         ///< the archive is unpacked and checked
    unpack_failed, ///< the installer was recognised, but its archive could not be unpacked
    disk_full,     ///< the installer was recognised, but the disk has no room for its archive
};

/// What set_up_demo() found in a folder, and what came of it.
struct DemoSetup {
    DemoOutcome outcome{};
    /// The recognised installer.
    fs::path installer;
    /// Files that look meant for the installer but are not the release
    /// recognised: Windows programs (.exe) and files of the installer's size.
    std::vector<fs::path> rejected;
    /// The folder that holds, or was to hold, the unpacked archive.
    fs::path folder;
    /// The unpacked archive.
    fs::path archive;
    /// True when this call unpacked the archive; false when it reused one
    /// unpacked before.
    bool unpacked = false;
    /// Why the archive could not be unpacked, in words.
    std::string problem;
};

/// Recognises the installer of the Total Annihilation demo (1997) in a folder and prepares its archive.
///
/// The archive goes to `data_folder`/release.folder_name/release.archive_name.
/// An archive already there with the archive's size and SHA-256 is reused,
/// and the first regular file directly in `folder`, in name order, whose size
/// is the installer's is then taken for the installer without being read:
/// only unpacking needs its bytes. Otherwise every such file is hashed, and
/// the first whose SHA-256 is the installer's is the installer, whatever its
/// name; its archive resource is written to a temporary file beside the
/// archive, whose size and SHA-256 are checked before it is renamed into
/// place, and a failed attempt leaves no temporary file. Temporary files an
/// unpacking that stopped part way left there, unwritten for a minute or
/// more, are removed first. Nothing in `folder` changes.
///
/// @param folder the game folder the player named or chose
/// @param data_folder the per-user data folder; empty when none is known,
///     which fails the unpacking
/// @param release the release to recognise
/// @return what was found and done
[[nodiscard]] DemoSetup set_up_demo(
    const fs::path& folder, const fs::path& data_folder, const DemoRelease& release = demo_1997
);

/// Tests whether a file is a release's archive, by its size and SHA-256.
///
/// @param archive the file
/// @param release the release whose archive it should be
/// @return true when the file can be read and matches
[[nodiscard]] bool archive_matches(const fs::path& archive, const DemoRelease& release = demo_1997);

/// Describes a prepared archive in one line, for the player.
///
/// @param setup a set_up_demo() result whose outcome is ready
/// @return which archive is mounted, where it came from and whether it was
///     unpacked now or checked
[[nodiscard]] std::string describe_ready(const DemoSetup& setup);

} // namespace oa::app
