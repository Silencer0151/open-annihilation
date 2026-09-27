// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Recognising the installer of the Total Annihilation demo (1997) and
// unpacking the game data archive it carries.
#include "oa/app/demo_installer.hpp"
#include "oa/app/game_directory.hpp"
#include "oa/formats/pe.hpp"
#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <fstream>
#include <optional>
#include <span>
#include <system_error>
#include <utility>

namespace oa::app {
namespace {

namespace sha256 = oa::base::sha256;

/// Bytes read at a time while hashing a file.
constexpr size_t read_piece_size = size_t{1} << 16;
/// The extension of a Windows program's name, in lower case.
constexpr std::string_view program_extension = ".exe";
/// Bytes in a megabyte, for the sizes told to the player.
constexpr uint64_t megabyte = 1'000'000;
/// Numbers this process's temporary files.
std::atomic<unsigned long> temporary_sequence{};
/// Follows the archive's name in the name of the temporary file it is
/// unpacked to.
constexpr std::string_view temporary_marker = ".unpacking-";
/// How long a temporary file must have gone unwritten to be taken for one an
/// unpacking that stopped part way left behind, and not one being written.
constexpr auto abandoned_temporary_age = std::chrono::minutes(1);

/// The files in a folder that may be the installer.
struct InstallerCandidates {
    /// Files of the installer's size, in name order.
    std::vector<fs::path> same_size;
    /// Programs (.exe) of any other size.
    std::vector<fs::path> programs;
};

/// What find_installer() found.
struct InstallerSearch {
    fs::path installer;
    /// The installer's bytes, when it was found.
    std::vector<uint8_t> bytes;
    std::vector<fs::path> rejected;
};

/// Tests whether a file's name ends in .exe, in any case.
[[nodiscard]] bool is_program_name(const fs::path& path) {
    const auto extension = path.extension().u8string();
    if (extension.size() != program_extension.size())
        return false;
    for (size_t index = 0; index < extension.size(); ++index)
        if (std::tolower(static_cast<unsigned char>(extension[index])) != program_extension[index])
            return false;
    return true;
}

/// Hashes a file of a known size, reading it in pieces.
///
/// @return its SHA-256; nullopt when it cannot be read or its size differs
[[nodiscard]] std::optional<sha256::Digest>
file_digest(const fs::path& path, uint64_t expected_size) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    std::vector<uint8_t> piece(read_piece_size);
    sha256::Hasher hasher;
    uint64_t total = 0;
    for (;;) {
        input.read(
            reinterpret_cast<char*>(piece.data()), static_cast<std::streamsize>(piece.size())
        );
        const auto count = static_cast<size_t>(input.gcount());
        if (count == 0)
            break;
        total += count;
        if (total > expected_size)
            return std::nullopt;
        sha256::update(hasher, std::span<const uint8_t>(piece.data(), count));
    }
    if (input.bad() || total != expected_size)
        return std::nullopt;
    return sha256::finish(hasher);
}

/// Reads a whole file of a known size.
///
/// @return its bytes; nullopt when it cannot be read or its size differs
[[nodiscard]] std::optional<std::vector<uint8_t>> read_file(const fs::path& path, uint64_t size) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        return std::nullopt;
    std::vector<uint8_t> bytes(size);
    input.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(size));
    if (static_cast<uint64_t>(input.gcount()) != size ||
        input.peek() != std::ifstream::traits_type::eof())
        return std::nullopt;
    return bytes;
}

/// Lists the files directly in a folder that may be the installer.
[[nodiscard]] InstallerCandidates
list_candidates(const fs::path& folder, const DemoRelease& release) {
    InstallerCandidates candidates;
    std::error_code error;
    for (fs::directory_iterator entry(folder, error), end; !error && entry != end;
         entry.increment(error)) {
        std::error_code entry_error;
        if (!entry->is_regular_file(entry_error))
            continue;
        const auto size = entry->file_size(entry_error);
        if (entry_error)
            continue;
        if (size == release.installer_size)
            candidates.same_size.push_back(entry->path());
        else if (is_program_name(entry->path()))
            candidates.programs.push_back(entry->path());
    }
    std::sort(candidates.same_size.begin(), candidates.same_size.end());
    std::sort(candidates.programs.begin(), candidates.programs.end());
    return candidates;
}

/// Looks through a folder's candidates for the installer.
///
/// Files of the installer's size are read and hashed in name order until
/// one matches. Those that do not, and programs of any other size, are
/// rejected; files after the match are not read.
[[nodiscard]] InstallerSearch
find_installer(const InstallerCandidates& candidates, const DemoRelease& release) {
    InstallerSearch search;
    search.rejected = candidates.programs;
    for (const auto& candidate : candidates.same_size) {
        auto bytes = read_file(candidate, release.installer_size);
        if (bytes && sha256::digest_of(*bytes) == release.installer_sha256) {
            search.installer = candidate;
            search.bytes = std::move(*bytes);
            break;
        }
        search.rejected.push_back(candidate);
    }
    std::sort(search.rejected.begin(), search.rejected.end());
    return search;
}

/// Removes the temporary files that unpackings which stopped part way, such
/// as one whose process was ended, left in the archive's folder.
///
/// A temporary file is the archive's name, temporary_marker and a number;
/// one written within abandoned_temporary_age may belong to an unpacking
/// still under way and is left alone.
void remove_abandoned_temporaries(const fs::path& folder, const DemoRelease& release) {
    const auto prefix = std::string(release.archive_name) + std::string(temporary_marker);
    const auto now = fs::file_time_type::clock::now();
    std::vector<fs::path> abandoned;
    std::error_code error;
    for (fs::directory_iterator entry(folder, error), end; !error && entry != end;
         entry.increment(error)) {
        std::error_code entry_error;
        if (!path_to_utf8(entry->path().filename()).starts_with(prefix) ||
            !entry->is_regular_file(entry_error))
            continue;
        const auto written = entry->last_write_time(entry_error);
        if (!entry_error && now - written >= abandoned_temporary_age)
            abandoned.push_back(entry->path());
    }
    for (const auto& path : abandoned) {
        std::error_code ignored;
        fs::remove(path, ignored);
    }
}

/// Writes a size in megabytes, rounded to the nearest.
[[nodiscard]] std::string megabytes(uint64_t bytes) {
    return std::to_string((bytes + megabyte / 2) / megabyte) + " MB";
}

/// Tests whether the disk holding `folder` has room for `size` bytes.
///
/// @param[out] available the bytes free, when known
/// @return false only when the disk is known to lack the room
[[nodiscard]] bool has_room(const fs::path& folder, uint64_t size, uint64_t& available) {
    std::error_code error;
    const auto space = fs::space(folder, error);
    if (error)
        return true;
    available = space.available;
    return space.available >= size;
}

/// Unpacks the installer's archive resource into setup.archive, checking it on the way.
///
/// @param bytes the recognised installer
/// @param release the release it is
/// @param[in,out] setup names the folder and the archive; takes the outcome
void unpack(std::span<const uint8_t> bytes, const DemoRelease& release, DemoSetup& setup) {
    const auto fail = [&setup](DemoOutcome outcome, std::string problem) {
        setup.outcome = outcome;
        setup.problem = std::move(problem);
    };
    const auto no_room = [&](uint64_t available) {
        fail(
            DemoOutcome::disk_full,
            "unpacking its game data needs about " + megabytes(release.archive_size) + " free in " +
                path_to_utf8(setup.folder) + ", and about " + megabytes(available) + " is free"
        );
    };
    const auto lookup = formats::pe::find_resource(
        bytes,
        {release.archive_resource_type,
         release.archive_resource_id,
         release.archive_resource_language}
    );
    if (lookup.error != formats::pe::Error::none)
        return fail(
            DemoOutcome::unpack_failed,
            "its game data could not be found in it: " +
                std::string(formats::pe::describe(lookup.error)) + " (at byte " +
                std::to_string(lookup.error_offset) + ")"
        );
    if (lookup.range.size != release.archive_size)
        return fail(
            DemoOutcome::unpack_failed,
            "its game data is " + std::to_string(lookup.range.size) + " bytes, where " +
                std::to_string(release.archive_size) + " were expected"
        );
    const auto archive = bytes.subspan(lookup.range.offset, lookup.range.size);

    std::error_code error;
    fs::create_directories(setup.folder, error);
    if (error)
        return fail(
            DemoOutcome::unpack_failed,
            "the folder " + path_to_utf8(setup.folder) + " could not be made: " + error.message()
        );
    uint64_t available = 0;
    if (!has_room(setup.folder, archive.size(), available))
        return no_room(available);

    const auto temporary =
        setup.folder /
        (std::string(release.archive_name) + std::string(temporary_marker) +
         std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-" +
         std::to_string(temporary_sequence++));
    const auto discard = [&temporary] {
        std::error_code ignored;
        fs::remove(temporary, ignored);
    };
    bool written = false;
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (output) {
            output.write(
                reinterpret_cast<const char*>(archive.data()),
                static_cast<std::streamsize>(archive.size())
            );
            output.flush();
            written = static_cast<bool>(output);
            output.close();
            written = written && !output.fail();
        }
    }
    if (!written) {
        discard();
        if (!has_room(setup.folder, archive.size(), available))
            return no_room(available);
        return fail(
            DemoOutcome::unpack_failed,
            "its game data could not be written to " + path_to_utf8(setup.folder)
        );
    }
    const auto digest = file_digest(temporary, release.archive_size);
    if (!digest || *digest != release.archive_sha256) {
        discard();
        return fail(
            DemoOutcome::unpack_failed,
            "the unpacked game data did not pass its SHA-256 check, so it was not kept"
        );
    }
    // An archive left there failed its check; the checked one replaces it.
    fs::remove(setup.archive, error);
    error.clear();
    fs::rename(temporary, setup.archive, error);
    if (error) {
        discard();
        return fail(
            DemoOutcome::unpack_failed,
            "the unpacked game data could not be put in place as " + path_to_utf8(setup.archive) +
                ": " + error.message()
        );
    }
    setup.outcome = DemoOutcome::ready;
    setup.unpacked = true;
}

} // namespace

DemoSetup
set_up_demo(const fs::path& folder, const fs::path& data_folder, const DemoRelease& release) {
    DemoSetup setup;
    const auto candidates = list_candidates(folder, release);
    if (candidates.same_size.empty()) {
        setup.rejected = candidates.programs;
        setup.outcome =
            setup.rejected.empty() ? DemoOutcome::no_installer : DemoOutcome::unrecognised;
        return setup;
    }
    const auto unpacked_folder =
        data_folder.empty() ? fs::path{} : data_folder / fs::path(std::string(release.folder_name));
    const auto archive = unpacked_folder / fs::path(std::string(release.archive_name));
    if (!data_folder.empty()) {
        remove_abandoned_temporaries(unpacked_folder, release);
        // An archive unpacked before that passes its check needs nothing
        // from the installer: the first file of its size is taken for it
        // unread.
        if (archive_matches(archive, release)) {
            setup.outcome = DemoOutcome::ready;
            setup.installer = candidates.same_size.front();
            setup.rejected = candidates.programs;
            setup.folder = unpacked_folder;
            setup.archive = archive;
            return setup;
        }
    }
    auto search = find_installer(candidates, release);
    setup.rejected = std::move(search.rejected);
    if (search.installer.empty()) {
        setup.outcome = DemoOutcome::unrecognised;
        return setup;
    }
    setup.installer = std::move(search.installer);
    if (data_folder.empty()) {
        setup.outcome = DemoOutcome::unpack_failed;
        setup.problem = "no per-user data folder is known to unpack it to";
        return setup;
    }
    setup.folder = unpacked_folder;
    setup.archive = archive;
    unpack(search.bytes, release, setup);
    return setup;
}

bool archive_matches(const fs::path& archive, const DemoRelease& release) {
    std::error_code error;
    if (!fs::is_regular_file(archive, error) ||
        fs::file_size(archive, error) != release.archive_size || error)
        return false;
    const auto digest = file_digest(archive, release.archive_size);
    return digest && *digest == release.archive_sha256;
}

std::string describe_ready(const DemoSetup& setup) {
    return "mounted the Total Annihilation demo (1997) from " + path_to_utf8(setup.archive) +
           (setup.unpacked ? ", unpacked now from " : ", unpacked earlier and checked, from ") +
           path_to_utf8(setup.installer);
}

} // namespace oa::app
