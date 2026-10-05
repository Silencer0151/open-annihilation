// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// A mod package read and checked before anything is written, and what a
// folder in Mods holds.

#include "files.hpp"
#include "package_file.hpp"

#include "oa/app/mod_install.hpp"
#include "oa/app/mod_profile_loader.hpp"
#include "oa/formats/oamod.hpp"
#include "oa/app/user_folder.hpp"
#include "oa/platform/files.hpp"

#include <algorithm>
#include <cstdio>
#include <exception>
#include <fstream>
#include <iterator>
#include <map>
#include <string>
#include <string_view>
#include <system_error>
#include <utility>
#include <vector>

namespace oa::app::mod_install {

namespace fs = std::filesystem;
namespace zip = oa::formats::zip;
namespace oamod = oa::formats::oamod;
namespace mod_profile = oa::data::mod_profile;

namespace detail {

PackageFile::~PackageFile() {
    if (stream != nullptr)
        std::fclose(stream);
}

bool PackageFile::open(const fs::path& file) {
    stream = oa::platform::open_file(file, "rb");
    if (stream == nullptr)
        return false;
    files = oa::platform::stdio_files();
    auto* handle = reinterpret_cast<oa::platform::FileHandle*>(stream);
    if (files.seek(nullptr, handle, 0, oa::platform::SeekOrigin::end) != 0)
        return false;
    const int64_t end = files.tell(nullptr, handle);
    if (end < 0)
        return false;
    bytes = static_cast<uint64_t>(end);
    return true;
}

zip::SourceHooks PackageFile::source() {
    return {
        this, [](void* context, uint64_t offset, std::span<uint8_t> out) {
            auto& file = *static_cast<PackageFile*>(context);
            auto* handle = reinterpret_cast<oa::platform::FileHandle*>(file.stream);
            if (file.stream == nullptr || offset > static_cast<uint64_t>(INT64_MAX) ||
                file.files.seek(
                    nullptr, handle, static_cast<int64_t>(offset), oa::platform::SeekOrigin::begin
                ) != 0)
                return false;
            std::size_t done = 0;
            while (done < out.size()) {
                const std::size_t read =
                    std::fread(out.data() + done, 1, out.size() - done, file.stream);
                if (read == 0)
                    return false;
                done += read;
            }
            return true;
        }
    };
}

} // namespace detail

namespace {

/// The profile's own name in a mod folder (mod_profile_name), lowered.
constexpr std::string_view profile_name = "oamod.yaml";
/// The folder macOS writes its file attributes in, when it zips a folder.
constexpr std::string_view mac_attributes_folder = "__macosx";
/// The file the Finder keeps a folder's look in.
constexpr std::string_view finder_file = ".ds_store";
/// What the name of a file of AppleDouble attributes starts with.
constexpr std::string_view apple_double_prefix = "._";

/// Returns a problem.
///
/// @param refusal why
/// @param subject what it names
/// @param detail English, for the log
/// @return the problem
Problem problem_of(Refusal refusal, std::string subject = {}, std::string detail = {}) {
    Problem problem{};
    problem.refusal = refusal;
    problem.subject = std::move(subject);
    problem.detail = std::move(detail);
    return problem;
}

/// Returns a result that refuses.
///
/// @param problem why
/// @return the result
PackageResult refused(Problem problem) {
    PackageResult result{};
    result.problem = std::move(problem);
    return result;
}

/// Returns the last '/'-separated part of a name, a folder's trailing '/' left out.
///
/// @param name the name
/// @return its last part
std::string_view last_part(std::string_view name) {
    if (!name.empty() && name.back() == '/')
        name.remove_suffix(1);
    const std::size_t separator = name.rfind('/');
    return separator == std::string_view::npos ? name : name.substr(separator + 1);
}

/// Returns the first '/'-separated part of a name.
///
/// @param name the name
/// @return its first part
std::string_view first_part(std::string_view name) {
    return name.substr(0, name.find('/'));
}

/// Tells whether an entry is one a system adds when it zips a folder, which
/// no mod holds: macOS's __MACOSX folder, the Finder's .DS_Store files and
/// AppleDouble files.
///
/// @param name the entry's name
/// @return true when it is ignored
bool ignored_entry(std::string_view name) {
    const std::string first = detail::folded(first_part(name));
    const std::string last = detail::folded(last_part(name));
    return first == mac_attributes_folder || last == finder_file ||
           last.starts_with(apple_double_prefix);
}

/// Maps a zip status to the refusal it gives.
///
/// @param error the zip error
/// @return the problem
Problem problem_of_zip(const zip::ZipError& error) {
    const std::string detail = zip::zip_status_message(error.status);
    switch (error.status) {
    case zip::ZipStatus::no_end_record:
        return problem_of(Refusal::not_zip, {}, detail);
    case zip::ZipStatus::encrypted:
        return problem_of(Refusal::encrypted, error.entry, detail);
    case zip::ZipStatus::unsupported_method:
        return problem_of(Refusal::method, error.entry, detail);
    case zip::ZipStatus::unsafe_name:
    case zip::ZipStatus::duplicate_name:
    case zip::ZipStatus::name_too_long:
    case zip::ZipStatus::bad_name_encoding:
        return problem_of(Refusal::unsafe_name, error.entry, detail);
    case zip::ZipStatus::read_failed:
        return problem_of(Refusal::unreadable, {}, detail);
    default:
        return problem_of(Refusal::damaged, error.entry, detail);
    }
}

/// Adds every diagnostic of a resolution, formatted, to a list.
///
/// @param diagnostics the diagnostics
/// @param[out] lines receives one line each
void add_lines(
    const std::vector<mod_profile::Diagnostic>& diagnostics, std::vector<std::string>& lines
) {
    for (const auto& diagnostic : diagnostics)
        lines.push_back(mod_profile::format_diagnostic(diagnostic));
}

/// Reads a scalar's text: a string's value, or any other plain scalar as written.
///
/// @param mapping the mapping
/// @param key its key
/// @return the text; empty when there is none
std::string scalar_text(const oamod::Node& mapping, std::string_view key) {
    const oamod::Node* node = oamod::find_entry(mapping, key);
    if (node == nullptr || node->kind == oamod::NodeKind::mapping ||
        node->kind == oamod::NodeKind::sequence || node->kind == oamod::NodeKind::null_value)
        return {};
    return node->text;
}

/// Returns a byte with its ASCII letter lowered.
///
/// @param byte the byte
/// @return the byte
unsigned char lowered(char byte) noexcept {
    const auto value = static_cast<unsigned char>(byte);
    return value >= 'A' && value <= 'Z' ? static_cast<unsigned char>(value - 'A' + 'a') : value;
}

/// The names a package unpacks, kept as a tree of their parts compared
/// without case, to find two that differ only in case and a file and a
/// folder of one name, and to list the folders it makes. Each part is kept
/// once, under its folder, so the tree grows with the parts the package
/// names, never with the length of their paths.
class NameTree {
  public:

    /// Adds a path and every folder above it.
    ///
    /// @param path the path, '/' between its parts, without a trailing '/'
    /// @param folder it is a folder
    /// @param[in,out] folders receives each folder added for the first
    ///        time, its parents before it
    /// @return false when a part clashes with one added before
    bool add(std::string_view path, bool folder, std::vector<std::string>& folders) {
        uint32_t parent = 0;
        std::size_t start = 0;
        while (true) {
            const std::size_t end = std::min(path.find('/', start), path.size());
            const bool last = end == path.size();
            const bool part_folder = !last || folder;
            const auto [slot, added] = names_.try_emplace(
                Key{parent, std::string(path.substr(start, end - start))}, Name{next_, part_folder}
            );
            if (added) {
                ++next_;
                if (part_folder)
                    folders.emplace_back(path.substr(0, end));
            } else if (
                slot->first.part != path.substr(start, end - start) ||
                slot->second.folder != part_folder
            ) {
                return false;
            }
            if (last)
                return true;
            parent = slot->second.number;
            start = end + 1;
        }
    }

  private:

    /// A part under its folder.
    struct Key {
        uint32_t parent{}; ///< its folder's number; 0 for the package's top
        std::string part{};
    };

    /// Orders parts by their folder, then without case.
    struct Order {
        bool operator()(const Key& left, const Key& right) const noexcept {
            if (left.parent != right.parent)
                return left.parent < right.parent;
            return std::lexicographical_compare(
                left.part.begin(),
                left.part.end(),
                right.part.begin(),
                right.part.end(),
                [](char a, char b) { return lowered(a) < lowered(b); }
            );
        }
    };

    /// What a part names.
    struct Name {
        uint32_t number{}; ///< its own number, as a folder of later parts
        bool folder{};
    };

    std::map<Key, Name, Order> names_{};
    uint32_t next_{1};
};

} // namespace

PackageResult open_package(const fs::path& file, const PackageOptions& options) {
    try {
        detail::PackageFile archive;
        if (!archive.open(file))
            return refused(problem_of(Refusal::unreadable, {}, "the file cannot be opened"));
        Package package{};
        package.file = file;
        package.file_name = detail::utf8_of(file.filename());
        package.archive_bytes = archive.bytes;
        zip::ZipError error{};
        if (!zip::read_stream_directory(
                archive.source(), archive.bytes, {}, package.directory, error
            ))
            return refused(problem_of_zip(error));
        const auto& entries = package.directory.entries;
        std::vector<std::size_t> kept;
        for (std::size_t index = 0; index < entries.size(); ++index)
            if (!ignored_entry(entries[index].name))
                kept.push_back(index);
        // The profile lies at the top, or in the one folder the top holds.
        std::optional<std::size_t> profile;
        for (const std::size_t index : kept)
            if (!entries[index].directory && detail::folded(entries[index].name) == profile_name) {
                profile = index;
                break;
            }
        if (!profile && !kept.empty()) {
            const std::string_view top = first_part(entries[kept.front()].name);
            const bool one_folder = std::all_of(kept.begin(), kept.end(), [&](std::size_t index) {
                const std::string& name = entries[index].name;
                return name.size() > top.size() && name.starts_with(top) && name[top.size()] == '/';
            });
            if (one_folder) {
                package.root = std::string(top) + "/";
                for (const std::size_t index : kept)
                    if (!entries[index].directory &&
                        detail::folded(entries[index].name) ==
                            detail::folded(package.root) + std::string(profile_name)) {
                        profile = index;
                        break;
                    }
            }
        }
        if (!profile)
            return refused(problem_of(Refusal::no_profile));
        package.profile_entry = *profile;
        NameTree names{};
        bool backup_left_out = false;
        for (const std::size_t index : kept) {
            const zip::StreamEntry& entry = entries[index];
            const std::string relative = entry.name.substr(package.root.size());
            if (relative.empty())
                continue;
            // A .backup of the package's own is ours to keep, never unpacked.
            if (detail::folded(first_part(relative)) == backup_folder_name) {
                backup_left_out = true;
                continue;
            }
            if (entry.symbolic_link || entry.special_file)
                return refused(problem_of(Refusal::link, relative));
            if (const std::string why = portable_name_problem(relative); !why.empty())
                return refused(problem_of(Refusal::unsafe_name, relative, why));
            // Every folder the path passes through, and the path itself.
            const bool folder = entry.directory;
            const std::string_view path =
                std::string_view(relative).substr(0, relative.size() - (folder ? 1 : 0));
            if (!names.add(path, folder, package.folders))
                return refused(problem_of(Refusal::case_clash, relative));
            if (package.folders.size() > max_package_folders)
                return refused(problem_of(Refusal::too_many_folders));
            package.files.push_back({index, relative});
            if (!folder)
                package.unpacked_bytes += entry.bytes;
        }
        package.backup_left_out = backup_left_out;
        if (package.unpacked_bytes > max_install_bytes) {
            Problem problem = problem_of(Refusal::too_large);
            problem.size_bytes = package.unpacked_bytes;
            problem.limit_bytes = max_install_bytes;
            return refused(std::move(problem));
        }
        if (package.unpacked_bytes > bomb_floor &&
            package.unpacked_bytes / bomb_ratio > package.archive_bytes) {
            Problem problem = problem_of(Refusal::bomb);
            problem.size_bytes = package.unpacked_bytes;
            problem.limit_bytes = package.archive_bytes;
            return refused(std::move(problem));
        }
        // The profile, read whole: one byte past the reader's limit is
        // enough to refuse it.
        std::vector<uint8_t> text;
        if (entries[*profile].bytes > oamod::max_input_bytes)
            return refused(problem_of(Refusal::profile_too_large));
        if (!zip::read_stream_entry(
                archive.source(),
                archive.bytes,
                entries[*profile],
                oamod::max_input_bytes,
                text,
                error
            ))
            return refused(problem_of(
                Refusal::damaged, entries[*profile].name, zip::zip_status_message(error.status)
            ));
        // Resolved as a load of the mod resolves it: once for its settings
        // file and registry root, then with the settings they hold.
        const std::string source = package.file_name + "/" + std::string(mod_profile_name);
        mod_profile::ResolveOptions resolve{};
        resolve.accept_unimplemented_hacks = options.accept_unimplemented_hacks;
        const auto first = mod_profile::resolve_profile(text, source, resolve);
        if (!first.resolution) {
            Problem problem = problem_of(Refusal::profile_errors);
            add_lines(first.errors, problem.lines);
            return refused(std::move(problem));
        }
        const auto& first_profile = first.resolution->profile;
        // The INI file the profile names, from the package's top, else the
        // game folder's.
        std::optional<std::string> ini;
        bool packaged_ini = false;
        const std::string ini_name =
            detail::folded(package.root + first_profile.identity.settings_file);
        for (const PackagedFile& packaged : package.files) {
            const zip::StreamEntry& entry = entries[packaged.entry];
            if (entry.directory || detail::folded(entry.name) != ini_name)
                continue;
            packaged_ini = true;
            std::vector<uint8_t> bytes;
            if (entry.bytes <= mod_ini_most_bytes &&
                zip::read_stream_entry(
                    archive.source(), archive.bytes, entry, mod_ini_most_bytes, bytes, error
                ))
                ini = std::string(bytes.begin(), bytes.end());
            break;
        }
        if (packaged_ini || options.game_folder.empty())
            resolve.settings = mod_settings_from(
                first_profile,
                ini ? std::optional<std::string_view>(*ini) : std::nullopt,
                options.preferences
            );
        else
            resolve.settings =
                mod_settings_of(first_profile, {options.game_folder}, options.preferences);
        const auto second = mod_profile::resolve_profile(text, source, resolve);
        if (!second.resolution) {
            Problem problem = problem_of(Refusal::profile_errors);
            add_lines(second.errors, problem.lines);
            return refused(std::move(problem));
        }
        add_lines(second.warnings, package.warnings);
        package.profile =
            std::make_shared<const mod_profile::ModProfile>(second.resolution->profile);
        // Its folder must be one every system can make.
        if (windows_device_name(package.profile->id))
            return refused(problem_of(Refusal::reserved_id, package.profile->id));
        PackageResult result{};
        result.package = std::move(package);
        return result;
    } catch (const std::exception& failure) {
        return refused(problem_of(Refusal::unreadable, {}, failure.what()));
    }
}

bool same_mod(const InstalledMod& left, const InstalledMod& right) noexcept {
    return left.kind == right.kind && left.id == right.id && left.version == right.version &&
           left.revision == right.revision;
}

InstalledMod read_installed_mod(const fs::path& folder) {
    InstalledMod installed{};
    try {
        std::error_code error;
        const fs::file_status status = fs::symlink_status(folder, error);
        if (error || !fs::exists(status))
            return installed;
        installed.kind = FolderKind::other;
        if (is_link_or_junction(folder) || !fs::is_directory(status))
            return installed;
        std::string problem;
        const auto profile = find_mod_profile(folder, problem);
        if (!profile)
            return installed;
        std::ifstream in(*profile, std::ios::binary);
        if (!in)
            return installed;
        std::vector<uint8_t> bytes;
        for (std::istreambuf_iterator<char> at{in}, end;
             at != end && bytes.size() <= oamod::max_input_bytes;
             ++at)
            bytes.push_back(static_cast<uint8_t>(*at));
        oamod::Node root;
        oamod::ReadError read{};
        if (!oamod::read_document(bytes, root, read))
            return installed;
        installed.id = scalar_text(root, "id");
        if (installed.id.empty())
            return installed;
        installed.name = scalar_text(root, "name");
        installed.version = scalar_text(root, "version");
        if (const oamod::Node* packaging = oamod::find_entry(root, "packaging"))
            if (const oamod::Node* revision = oamod::find_entry(*packaging, "revision");
                revision != nullptr && revision->kind == oamod::NodeKind::number) {
                int64_t value = 0;
                if (oamod::integer_value(revision->number, value))
                    installed.revision = value;
            }
        installed.kind = FolderKind::mod;
        return installed;
    } catch (const std::exception&) {
        return installed;
    }
}

std::optional<InstalledMod> read_backup(const fs::path& folder) {
    try {
        const auto kept = entry_without_case(folder, backup_folder_name);
        if (!kept)
            return std::nullopt;
        InstalledMod backup = read_installed_mod(*kept);
        if (backup.kind == FolderKind::missing)
            return std::nullopt;
        const InstalledMod own = read_installed_mod(folder);
        if (backup.kind == FolderKind::mod && (own.kind != FolderKind::mod || own.id != backup.id))
            backup.kind = FolderKind::other;
        return backup;
    } catch (const std::exception&) {
        return std::nullopt;
    }
}

Incoming incoming_of(const mod_profile::ModProfile& profile) {
    return {profile.id, profile.name, profile.version, profile.packaging.revision};
}

} // namespace oa::app::mod_install
