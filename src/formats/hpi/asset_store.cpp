// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include "oa/base/threads.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <memory>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>

namespace oa {
namespace {

constexpr std::size_t kMaxPathLength = 4096;
constexpr uint32_t kSeekToEnd = 0xFFFFFFFFU;
// Search spec rewritten to "*" when a find names it as its basename.
constexpr std::string_view kAllFilesPattern = "*.*";
constexpr std::string_view kCurrentDirectory = ".";
constexpr std::string_view kParentDirectory = "..";
constexpr std::string_view kDiscoveryGp3 = ".GP3";
constexpr std::string_view kDiscoveryGp3Prefix = "rev";
// Mount-time mark on an archive file that a loose file of the same path hides.
constexpr uint8_t kShadowedByLoose = 0x02;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

/// Returns a path's UTF-8 spelling: a name outside the system's code page
/// has no narrow spelling on Windows.
///
/// @param path the path
/// @return its UTF-8 spelling
std::string utf8_text(const std::filesystem::path& path) {
    const auto text = path.u8string();
    return {text.begin(), text.end()};
}

/// Throws the error of a read from a mounted archive, naming the entry and
/// the archive.
///
/// @param error the archive's error
/// @param archive_path host path of the archive read
/// @param node the entry read
[[noreturn]] void fail_entry_read(
    const base::bytes::DecodeError& error,
    const std::filesystem::path& archive_path,
    const ArchiveNode& node
) {
    // A chunk that fails to decode carries its SQUASHERR_* name.
    const std::string what = error.detail != 0
                                 ? "HPI decompression error " + std::string(error.message)
                                 : std::string(error.message);
    fail(
        what + " (" + node.name + ", length " + std::to_string(node.size) + ", at byte " +
        std::to_string(error.offset) + " of " + utf8_text(archive_path.filename()) + ")"
    );
}

char ascii_lower(char value) noexcept {
    return value >= 'A' && value <= 'Z' ? static_cast<char>(value + ('a' - 'A')) : value;
}

char ascii_upper(char value) noexcept {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A')) : value;
}

// NTFS directory order: names compared after ASCII upper-casing.
bool ntfs_less(const std::string& a, const std::string& b) noexcept {
    return std::lexicographical_compare(a.begin(), a.end(), b.begin(), b.end(), [](char x, char y) {
        return static_cast<unsigned char>(ascii_upper(x)) <
               static_cast<unsigned char>(ascii_upper(y));
    });
}

// Directory-listing match: "*.*" is "*", otherwise the shared wildcard rule.
bool match_host_pattern(std::string_view name, std::string_view pattern) noexcept {
    if (pattern == kAllFilesPattern)
        pattern = "*";
    return match_wildcard(name, pattern);
}

std::string normalized_path(std::string_view path) {
    std::string result;
    result.reserve(path.size());
    bool previous_slash = true;
    for (const char raw : path) {
        const char ch = raw == '\\' ? '/' : raw;
        if (ch == '/') {
            if (!previous_slash)
                result.push_back('/');
            previous_slash = true;
            continue;
        }
        result.push_back(ascii_lower(ch));
        previous_slash = false;
    }
    if (!result.empty() && result.back() == '/')
        result.pop_back();
    return result;
}

std::size_t last_separator(std::string_view path) noexcept {
    const auto at = path.find_last_of("\\/");
    return at == std::string_view::npos ? 0 : at + 1;
}

std::string full_path_key(const std::filesystem::path& path) {
    // In UTF-8: a folder named outside the system's code page has no narrow
    // spelling on Windows.
    const auto spelled = std::filesystem::weakly_canonical(path).generic_u8string();
    std::string text(spelled.begin(), spelled.end());
    std::transform(text.begin(), text.end(), text.begin(), ascii_upper);
    return text;
}

/// Returns a host path with every link followed, or the path as given when
/// it cannot be resolved.
///
/// @param path the path
/// @return the resolved path
std::filesystem::path resolved(const std::filesystem::path& path) {
    std::error_code error;
    auto canonical = std::filesystem::weakly_canonical(path, error);
    return error ? path : canonical;
}

/// Tells whether a host path, every link followed, is a folder or lies
/// inside it, the folder's own links followed too.
///
/// The folder is matched by identity, not by how its path is spelled, so
/// the case of its letters and the system's spelling of long paths do not
/// matter.
///
/// @param root the folder
/// @param path a path found below it
/// @return true when the resolved path is the folder or lies inside it
bool stays_inside(const std::filesystem::path& root, const std::filesystem::path& path) {
    std::error_code error;
    const auto canonical = std::filesystem::weakly_canonical(path, error);
    if (error)
        return false;
    for (auto at = canonical;; at = at.parent_path()) {
        if (std::filesystem::equivalent(at, root, error))
            return true;
        if (!at.has_relative_path())
            return false;
    }
}

std::vector<uint8_t> read_loose(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (!formats::hpi::entry_size_allowed(size))
        fail("loose asset exceeds entry size limit");
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        fail("cannot open loose asset '" + utf8_text(path) + "'");
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()))
        fail("truncated loose asset '" + utf8_text(path) + "'");
    return bytes;
}

/// An archive file kept open for reads of its entries.
class ArchiveFile final : public ArchiveSource {
  public:

    /// Takes over an open file.
    ///
    /// @param stream the file, opened for binary reads
    /// @param size its size in bytes
    ArchiveFile(std::ifstream stream, uint64_t size) : stream_(std::move(stream)), size_(size) {}

    /// Returns the file's size when it was opened.
    ///
    /// @return the size in bytes
    [[nodiscard]] uint64_t size() const noexcept override { return size_; }

    /// Reads file bytes from an offset.
    ///
    /// @param offset file offset of the first byte
    /// @param[out] output receives up to output.size() bytes
    /// @return the count read; short past the end of the file
    std::size_t read_at(uint64_t offset, std::span<uint8_t> output) override {
        stream_.clear();
        stream_.seekg(static_cast<std::streamoff>(offset));
        if (!stream_)
            return 0;
        stream_.read(
            reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())
        );
        return static_cast<std::size_t>(stream_.gcount());
    }

  private:

    std::ifstream stream_;
    uint64_t size_{};
};

/// Opens an archive file for reads, saying why it cannot be opened.
///
/// @param path host path of the archive
/// @param[out] reason receives why the file cannot be opened
/// @return the open file, or null
std::unique_ptr<ArchiveFile>
open_archive_file(const std::filesystem::path& path, std::string& reason) {
    std::error_code status;
    const auto size = std::filesystem::file_size(path, status);
    if (status) {
        reason = "cannot stat HPI archive '" + utf8_text(path) + "': " + status.message();
        return nullptr;
    }
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        reason = "cannot open HPI archive '" + utf8_text(path) + "'";
        return nullptr;
    }
    return std::make_unique<ArchiveFile>(std::move(stream), size);
}

struct LooseItem {
    std::string name;
    std::filesystem::path path;
    bool directory = false;
    uint32_t size = 0;
};

/// Lists a directory in NTFS order, led by the "." and ".." entries Windows
/// lists in every directory but a drive root.
std::vector<LooseItem> loose_listing(const std::filesystem::path& directory) {
    std::vector<LooseItem> items;
    std::error_code error;
    if (!std::filesystem::is_directory(directory, error))
        return items;
    for (const auto& item : std::filesystem::directory_iterator(directory, error)) {
        LooseItem entry{
            utf8_text(item.path().filename()), item.path(), item.is_directory(error), 0
        };
        if (!entry.directory && item.is_regular_file(error)) {
            const auto size = item.file_size(error);
            entry.size = error ? 0 : static_cast<uint32_t>(std::min<uintmax_t>(size, 0xFFFFFFFFU));
        }
        items.push_back(std::move(entry));
    }
    std::sort(items.begin(), items.end(), [](const LooseItem& a, const LooseItem& b) {
        return ntfs_less(a.name, b.name);
    });
    items.insert(
        items.begin(), LooseItem{std::string(kParentDirectory), directory / "..", true, 0}
    );
    items.insert(items.begin(), LooseItem{std::string(kCurrentDirectory), directory, true, 0});
    return items;
}

/// Tells whether a name at the top of a layered folder is hidden from the
/// store: the folder a mod keeps its earlier version in (backup_folder_name).
///
/// @param folded_name the name, ASCII lower case
/// @return true for the hidden folder
bool hidden_at_top(std::string_view folded_name) noexcept {
    return folded_name == backup_folder_name;
}

/// Returns the first '/'-separated part of a folded key.
///
/// @param key the key
/// @return its first part; the key itself without a separator
std::string_view first_part(std::string_view key) noexcept {
    return key.substr(0, key.find('/'));
}

/// Tests whether a listing entry is the "." or ".." entry.
bool dot_entry(const LooseItem& item) noexcept {
    return item.name == kCurrentDirectory || item.name == kParentDirectory;
}

/// Lists folders layered in order as one folder, in NTFS order, led by "."
/// and "..": an entry the folders share by name, compared without case, takes
/// the earliest folder's file and the latest folder's spelling, as copying
/// the folders over each other from the last to the first would leave it.
/// One folder is listed as loose_listing() lists it.
///
/// @param folders the folders, highest precedence first; a missing one adds nothing
/// @param roots the folders are the store's layered folders, whose hidden
///        folder (hidden_at_top) is left out
/// @return the entries; empty when no folder exists
std::vector<LooseItem>
merged_listing(std::span<const std::filesystem::path> folders, bool roots = false) {
    if (folders.size() == 1) {
        auto items = loose_listing(folders.front());
        if (roots)
            std::erase_if(items, [](const LooseItem& item) {
                return hidden_at_top(normalized_path(item.name));
            });
        return items;
    }
    std::unordered_map<std::string, LooseItem> merged;
    bool present = false;
    for (auto folder = folders.rbegin(); folder != folders.rend(); ++folder) {
        auto items = loose_listing(*folder);
        if (items.empty())
            continue;
        present = true;
        for (auto& item : items) {
            if (dot_entry(item) || (roots && hidden_at_top(normalized_path(item.name))))
                continue;
            auto [slot, inserted] = merged.try_emplace(normalized_path(item.name), item);
            if (!inserted) {
                slot->second.path = std::move(item.path);
                slot->second.directory = item.directory;
                slot->second.size = item.size;
            }
        }
    }
    std::vector<LooseItem> items;
    if (!present)
        return items;
    items.reserve(merged.size() + 2);
    for (auto& [key, item] : merged)
        items.push_back(std::move(item));
    std::sort(items.begin(), items.end(), [](const LooseItem& a, const LooseItem& b) {
        return ntfs_less(a.name, b.name);
    });
    const auto& first = folders.front();
    items.insert(items.begin(), LooseItem{std::string(kParentDirectory), first / "..", true, 0});
    items.insert(items.begin(), LooseItem{std::string(kCurrentDirectory), first, true, 0});
    return items;
}

/// Finds a folder below a root by a folded relative path, each part matched without case.
///
/// @param root the folder searched from
/// @param key '/'-separated path, ASCII lower case; empty for the root itself
/// @return the host folder, or nullopt when a part is missing or not a folder
std::optional<std::filesystem::path>
find_loose_directory(const std::filesystem::path& root, const std::string& key) {
    if (hidden_at_top(first_part(key)))
        return std::nullopt;
    auto candidate = root;
    for (std::size_t begin = 0; begin < key.size();) {
        const auto end = key.find('/', begin);
        const auto part = key.substr(begin, end == std::string::npos ? end : end - begin);
        bool present = false;
        for (const auto& item : loose_listing(candidate))
            if (item.directory && !dot_entry(item) && normalized_path(item.name) == part) {
                candidate = item.path;
                present = true;
                break;
            }
        if (!present)
            return std::nullopt;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    // A folder a link leads to outside the root is not the root's.
    if (!key.empty() && !stays_inside(root, candidate))
        return std::nullopt;
    return candidate;
}

} // namespace

struct ResourceFile {
    std::ifstream loose;
    uint32_t loose_size = 0;
    const HpiArchive* archive = nullptr;
    // Host path of the archive, named in the errors of its reads.
    const std::filesystem::path* archive_path = nullptr;
    uint32_t node = 0;
    uint32_t size = 0;
    uint32_t position = 0;
    // Decoded 64 KiB block of a chunked entry, dropped on crossing a block.
    std::vector<uint8_t> block;
};

// Folder listings of the loose tree, keyed by the folded resource path of the
// folder. Each folder is listed once, the first time a lookup passes through it.
struct AssetStore::LooseIndex {
    // The entry a folded name selects in one folder.
    struct Child {
        std::filesystem::path path; // host path of the entry
        bool regular = false;       // a regular file, following links, when listed
        bool directory = false;     // a folder, following links, when listed
        bool ambiguous = false;     // several entries fold to this name
    };

    // One folder's listing.
    struct Folder {
        std::size_t entry_count = 0;                     // entries the listing held
        std::unordered_map<std::string, Child> children; // keyed by folded name
    };

    explicit LooseIndex(std::size_t limit) : entry_limit(limit) {}

    /// Returns a folder's listing, listing the folder the first time.
    ///
    /// Disables the index, dropping every listing, when the listings would
    /// hold more than entry_limit entries.
    ///
    /// @param key folded resource path of the folder, empty for the game directory
    /// @param host host path of the folder
    /// @return the listing, or null when host is not a readable folder or the index was disabled
    const Folder* listing(const std::string& key, const std::filesystem::path& host);
    /// Drops every listing and enables the index again.
    void reset();

    base::threads::Mutex lock;
    const std::size_t entry_limit;
    std::size_t entry_count = 0; // entries held across every listing
    std::atomic<bool> enabled{true};
    std::unordered_map<std::string, Folder> folders;
};

const AssetStore::LooseIndex::Folder*
AssetStore::LooseIndex::listing(const std::string& key, const std::filesystem::path& host) {
    if (const auto kept = folders.find(key); kept != folders.end())
        return &kept->second;
    std::error_code error;
    if (!std::filesystem::is_directory(host, error))
        return nullptr;
    Folder fresh;
    for (const auto& item : std::filesystem::directory_iterator(host, error)) {
        ++fresh.entry_count;
        auto [slot, inserted] =
            fresh.children.try_emplace(normalized_path(utf8_text(item.path().filename())));
        if (!inserted) {
            slot->second.ambiguous = true;
            continue;
        }
        std::error_code status_error;
        slot->second.path = item.path();
        slot->second.regular = item.is_regular_file(status_error);
        slot->second.directory = item.is_directory(status_error);
    }
    if (error)
        return nullptr;
    if (entry_count + fresh.entry_count > entry_limit) {
        folders.clear();
        entry_count = 0;
        enabled.store(false);
        return nullptr;
    }
    entry_count += fresh.entry_count;
    return &(folders[key] = std::move(fresh));
}

void AssetStore::LooseIndex::reset() {
    const base::threads::LockGuard guard(lock);
    folders.clear();
    entry_count = 0;
    enabled.store(true);
}

AssetStore::AssetStore(std::filesystem::path loose_root, std::size_t loose_index_limit)
    : loose_roots_{std::filesystem::absolute(std::move(loose_root))},
      loose_index_(std::make_unique<LooseIndex>(loose_index_limit)) {
}

AssetStore::AssetStore(
    std::vector<std::filesystem::path> loose_roots, std::size_t loose_index_limit
)
    : loose_roots_(std::move(loose_roots)),
      loose_index_(std::make_unique<LooseIndex>(loose_index_limit)) {
    if (loose_roots_.empty())
        fail("an asset store needs a folder");
    for (auto& root : loose_roots_)
        root = std::filesystem::absolute(root);
}

std::span<const std::filesystem::path> AssetStore::loose_roots() const noexcept {
    return loose_roots_;
}

void AssetStore::observe_lookups(LookupObserver observer) noexcept {
    observer_ = observer;
}

void AssetStore::note_lookup(std::string_view name) const {
    if (observer_.looked_up != nullptr)
        observer_.looked_up(observer_.context, name);
}

std::optional<std::filesystem::path> AssetStore::loose_file(std::string_view resource) const {
    return loose_path(resource);
}

bool AssetStore::loose_index_enabled() const noexcept {
    return loose_index_ && loose_index_->enabled.load();
}

AssetStore::~AssetStore() = default;
AssetStore::AssetStore(AssetStore&&) noexcept = default;
AssetStore& AssetStore::operator=(AssetStore&&) noexcept = default;

bool AssetStore::is_mounted(const std::filesystem::path& archive) const {
    const auto key = full_path_key(archive);
    for (const auto& mounted : mounts_)
        if (full_path_key(mounted.path) == key)
            return true;
    return false;
}

void AssetStore::mount(const std::filesystem::path& archive) {
    if (is_mounted(archive))
        return;
    std::string error;
    if (!try_mount(archive, &error))
        fail(error);
}

bool AssetStore::try_mount(const std::filesystem::path& archive, std::string* error) {
    if (is_mounted(archive)) {
        if (error != nullptr)
            *error = "already mounted";
        return false;
    }
    std::error_code status;
    const auto absolute = std::filesystem::weakly_canonical(archive, status);
    std::string reason;
    if (status)
        reason =
            "cannot resolve HPI archive path '" + utf8_text(archive) + "': " + status.message();
    auto file = status ? nullptr : open_archive_file(absolute, reason);
    if (!file) {
        if (error != nullptr)
            *error = std::move(reason);
        return false;
    }
    auto opened = HpiArchive::open(std::move(file));
    if (!opened.ok()) {
        if (error != nullptr)
            *error = opened.error.message;
        return false;
    }
    mounts_.push_back(Mount{absolute, std::move(*opened.value), {}});
    mount_paths_.push_back(absolute);
    return true;
}

void AssetStore::drop_vanished_mounts() {
    for (std::size_t i = 0; i < mounts_.size();) {
        std::ifstream probe(mounts_[i].path, std::ios::binary);
        if (probe) {
            ++i;
            continue;
        }
        mounts_.erase(mounts_.begin() + static_cast<std::ptrdiff_t>(i));
        mount_paths_.erase(mount_paths_.begin() + static_cast<std::ptrdiff_t>(i));
    }
}

std::vector<DiscoveredArchive> AssetStore::discover(
    std::string_view version, std::span<const std::filesystem::path> removable_roots
) {
    DiscoveryPlan plan;
    plan.revision_archive =
        std::string(kDiscoveryGp3Prefix) + std::string(version) + std::string(kDiscoveryGp3);
    return discover(plan, removable_roots);
}

bool same_archive_file_name(std::string_view left, std::string_view right) {
    return normalized_path(left) == normalized_path(right);
}

std::vector<DiscoveredArchive> AssetStore::discover(
    const DiscoveryPlan& plan, std::span<const std::filesystem::path> removable_roots
) {
    drop_vanished_mounts();
    std::vector<DiscoveredArchive> report;
    // A listed name is pulled out of every group and mounted later, in the
    // list's order. An empty list pulls nothing, so the groups scan as before.
    const bool pulling = !plan.installation_archives.empty();
    const auto named = [&](std::string_view name) {
        for (const auto& archive : plan.installation_archives)
            if (same_archive_file_name(name, archive))
                return true;
        return false;
    };
    // The first item of a listing that is the named archive; null when none is.
    const auto find_named = [](const std::vector<LooseItem>& items,
                               std::string_view archive) -> const LooseItem* {
        for (const auto& item : items)
            if (!dot_entry(item) && same_archive_file_name(item.name, archive))
                return &item;
        return nullptr;
    };
    const auto scan =
        [&](const std::vector<LooseItem>& items, std::string_view pattern, int limit) {
            int remaining = limit;
            for (const auto& item : items) {
                if (dot_entry(item) || !match_host_pattern(item.name, pattern))
                    continue;
                if (pulling && named(item.name))
                    continue;
                DiscoveredArchive outcome{item.path, false, {}, is_mounted(item.path)};
                if (!outcome.already_mounted)
                    outcome.mounted = try_mount(item.path, &outcome.error);
                report.push_back(std::move(outcome));
                if (report.back().mounted && --remaining == 0)
                    break;
            }
        };
    const auto folders = merged_listing(loose_roots_, true);
    // Each removable root is listed once: the installation archives are
    // sought there after the folders, and its disc scan below reads the same.
    std::vector<std::vector<LooseItem>> disc_listings;
    disc_listings.reserve(removable_roots.size());
    for (const auto& root : removable_roots)
        disc_listings.push_back(loose_listing(root));
    scan(folders, plan.revision_archive, -1);
    scan(folders, plan.ccx_pattern, -1);
    scan(folders, plan.ufo_pattern, -1);
    for (const auto& archive : plan.installation_archives) {
        const LooseItem* found = find_named(folders, archive);
        for (std::size_t index = 0; found == nullptr && index < disc_listings.size(); ++index)
            found = find_named(disc_listings[index], archive);
        if (found == nullptr) {
            // The game folder is the last root. The name is what the report
            // and the skip line show; the file is not there to open.
            std::filesystem::path path{std::u8string(archive.begin(), archive.end())};
            if (!loose_roots_.empty())
                path = loose_roots_.back() / path;
            report.push_back(
                DiscoveredArchive{
                    std::move(path),
                    false,
                    "installation archive '" + archive + "' is missing",
                    false
                }
            );
            continue;
        }
        DiscoveredArchive outcome{found->path, false, {}, is_mounted(found->path)};
        if (!outcome.already_mounted)
            outcome.mounted = try_mount(found->path, &outcome.error);
        report.push_back(std::move(outcome));
    }
    scan(folders, plan.hpi_pattern, plan.hpi_limit);
    for (const auto& listing : disc_listings)
        scan(listing, plan.disc_pattern, -1);
    if (plan.folders_as_disc)
        scan(folders, plan.disc_pattern, -1);
    mark_loose_shadows();
    return report;
}

void AssetStore::mark_loose_shadows() {
    if (loose_index_)
        loose_index_->reset();
    for (auto& mounted : mounts_)
        mounted.marks.assign(mounted.archive.nodes().size(), 0);
    if (!mounts_.empty())
        for (const auto& root : loose_roots_) {
            walk_root_ = root;
            walked_folders_.clear();
            mark_loose_directory("", root);
        }
    walked_folders_.clear();
}

void AssetStore::mark_loose_directory(
    const std::string& prefix, const std::filesystem::path& directory
) {
    // A folder reached again through a link, or one a link leads to outside
    // the root, is not walked: a link to a folder above it cannot recurse.
    if (!walked_folders_.insert(resolved(directory)).second ||
        (!prefix.empty() && !stays_inside(walk_root_, directory)))
        return;
    for (const auto& item : loose_listing(directory)) {
        if (item.directory) {
            // A layered folder's hidden folder shadows nothing.
            const bool hidden = prefix.empty() && hidden_at_top(normalized_path(item.name));
            if (item.name != kCurrentDirectory && item.name != kParentDirectory && !hidden)
                mark_loose_directory(prefix + item.name + "\\", item.path);
            continue;
        }
        std::error_code link_error;
        if (std::filesystem::is_symlink(std::filesystem::symlink_status(item.path, link_error)) &&
            !stays_inside(walk_root_, item.path))
            continue;
        const std::string resource = prefix + item.name;
        for (auto& mounted : mounts_) {
            const auto node = mounted.archive.lookup(resource);
            if (node && !mounted.archive.nodes()[*node].directory())
                mounted.marks[*node] |= kShadowedByLoose;
        }
    }
}

std::span<const std::filesystem::path> AssetStore::mount_paths() const noexcept {
    return mount_paths_;
}

const HpiArchive& AssetStore::mounted(std::size_t index) const {
    return mounts_.at(index).archive;
}

std::optional<std::filesystem::path> AssetStore::loose_path(std::string_view resource) const {
    note_lookup(resource);
    if (resource.empty() || resource.front() == '/' || resource.front() == '\\' ||
        resource.find(':') != std::string_view::npos ||
        resource.find('\0') != std::string_view::npos || resource.size() > kMaxPathLength)
        fail("invalid asset resource path");
    const auto key = normalized_path(resource);
    if (hidden_at_top(first_part(key)))
        return std::nullopt;
    for (std::size_t root = 0; root < loose_roots_.size(); ++root)
        if (auto found = loose_path_in(root, key))
            return found;
    return std::nullopt;
}

std::optional<std::filesystem::path>
AssetStore::loose_path_in(std::size_t root, const std::string& key) const {
    if (!loose_index_ || !loose_index_->enabled.load())
        return loose_path_listed_in(root, key);
    const base::threads::LockGuard guard(loose_index_->lock);
    auto loose = loose_roots_[root];
    // Each folder's listings are kept under its own index, so that layered
    // folders never answer for each other.
    const std::string tag = std::to_string(root) + '|';
    std::string folder = tag;
    for (std::size_t begin = 0; begin < key.size();) {
        const auto end = key.find('/', begin);
        const auto part = key.substr(begin, end == std::string::npos ? end : end - begin);
        if (part == "." || part == "..")
            fail("asset path contains traversal");
        const auto* listing = loose_index_->listing(folder, loose);
        if (listing == nullptr)
            return loose_index_->enabled.load() ? std::nullopt : loose_path_listed_in(root, key);
        const auto child = listing->children.find(part);
        if (child == listing->children.end())
            return std::nullopt;
        if (child->second.ambiguous)
            fail("ambiguous loose asset case: " + key);
        if (end == std::string::npos)
            return child->second.regular && stays_inside(loose_roots_[root], child->second.path)
                       ? std::optional(child->second.path)
                       : std::nullopt;
        if (!child->second.directory)
            return std::nullopt;
        loose = child->second.path;
        folder = tag + key.substr(0, end);
        begin = end + 1;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path>
AssetStore::loose_path_listed_in(std::size_t root, const std::string& key) const {
    auto loose = loose_roots_[root];
    for (std::size_t begin = 0; begin < key.size();) {
        const auto end = key.find('/', begin);
        const auto part = key.substr(begin, end == std::string::npos ? end : end - begin);
        if (part == "." || part == "..")
            fail("asset path contains traversal");
        std::error_code error;
        if (!std::filesystem::is_directory(loose, error))
            return std::nullopt;
        std::optional<std::filesystem::path> match;
        // Windows ASCII case-insensitivity on case-sensitive hosts. Ambiguous
        // case collisions are rejected instead of choosing host order.
        for (const auto& item : std::filesystem::directory_iterator(loose, error)) {
            if (normalized_path(utf8_text(item.path().filename())) == part) {
                if (match)
                    fail("ambiguous loose asset case: " + key);
                match = item.path();
            }
        }
        if (!match)
            return std::nullopt;
        loose = *match;
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    std::error_code error;
    if (!std::filesystem::is_regular_file(loose, error) || !stays_inside(loose_roots_[root], loose))
        return std::nullopt;
    return loose;
}

std::optional<AssetStore::ArchivedNode>
AssetStore::archived_node(std::string_view resource, const std::filesystem::path* skipped) const {
    for (std::size_t index = 0; index < mounts_.size(); ++index) {
        const auto& mounted = mounts_[index];
        if (skipped != nullptr && mounted.path == *skipped)
            continue;
        const auto node = mounted.archive.lookup(resource);
        if (node && !mounted.archive.nodes()[*node].directory())
            return ArchivedNode{index, *node};
    }
    return std::nullopt;
}

AssetData
AssetStore::read_skipping(std::string_view resource, const std::filesystem::path* skipped) const {
    if (const auto loose = loose_path(resource))
        return {read_loose(*loose), *loose, false};
    if (const auto found = archived_node(resource, skipped)) {
        const auto& mounted = mounts_[found->mount];
        auto bytes = mounted.archive.read_node(found->node);
        if (!bytes.ok())
            fail_entry_read(bytes.error, mounted.path, mounted.archive.nodes()[found->node]);
        return {std::move(*bytes.value), mounted.path, true};
    }
    fail("asset not found: " + normalized_path(resource));
}

AssetData AssetStore::read(std::string_view resource) const {
    return read_skipping(resource, nullptr);
}

AssetData
AssetStore::read_without(std::string_view resource, const std::filesystem::path& archive) const {
    return read_skipping(resource, &archive);
}

std::optional<std::filesystem::path>
AssetStore::providing_archive(std::string_view resource) const {
    if (loose_path(resource))
        return std::nullopt;
    if (const auto found = archived_node(resource, nullptr))
        return mounts_[found->mount].path;
    return std::nullopt;
}

std::optional<std::vector<uint8_t>>
AssetStore::load_file_contents(std::string_view resource) const {
    ResourceFile* file = open(resource);
    if (file == nullptr)
        return std::nullopt;
    std::optional<std::vector<uint8_t>> result;
    const uint32_t size = length(file);
    if (static_cast<int32_t>(size) > 0 && formats::hpi::entry_size_allowed(size) &&
        seek(file, 0) != -1) {
        std::vector<uint8_t> bytes(size);
        if (read(file, bytes) > 0)
            result = std::move(bytes);
    }
    close(file);
    return result;
}

std::vector<uint8_t> AssetStore::load_with_progress(
    std::string_view resource, void (*progress)(void* user, uint8_t percent), void* user
) const {
    constexpr int kPasses = 10;
    constexpr uint8_t kPercentPerPass = 9;
    ResourceFile* file = open(resource);
    if (file == nullptr)
        fail("cannot open " + std::string(resource));
    const uint32_t size = length(file);
    if (!formats::hpi::entry_size_allowed(size)) {
        close(file);
        fail(std::string(resource) + " exceeds the entry size limit");
    }
    std::vector<uint8_t> bytes(size);
    const std::size_t slice = size / static_cast<uint32_t>(kPasses);
    size_t loaded = 0;
    for (int pass = 1; pass <= kPasses; ++pass) {
        const int32_t count = read(file, std::span(bytes).subspan(loaded, slice));
        if (count > 0)
            loaded += static_cast<size_t>(count);
        if (progress != nullptr)
            progress(user, static_cast<uint8_t>(pass * kPercentPerPass));
    }
    if (loaded < size)
        (void)read(file, std::span(bytes).subspan(loaded));
    close(file);
    return bytes;
}

uint32_t AssetStore::file_size(std::string_view resource) const {
    ResourceFile* file = open(resource);
    if (file == nullptr)
        return 0;
    const uint32_t size = length(file);
    close(file);
    return size;
}

bool AssetStore::read_chunk(
    std::string_view resource, uint32_t position, std::span<uint8_t> output
) const {
    ResourceFile* file = open(resource);
    if (file == nullptr)
        return false;
    const bool ok = seek(file, position) != -1 && read(file, output) >= 1;
    close(file);
    return ok;
}

std::vector<FoundEntry> AssetStore::find(std::string_view pattern, FindScope scope) const {
    note_lookup(pattern);
    std::vector<FoundEntry> found;
    const std::size_t split = last_separator(pattern);
    const std::string_view directory = pattern.substr(0, split);
    std::string_view spec = pattern.substr(split);
    if (spec == kAllFilesPattern)
        spec = "*";
    if (scope.first_mount < 0) {
        std::vector<std::filesystem::path> folders;
        const bool roots = directory.empty();
        if (roots) {
            folders = loose_roots_;
        } else {
            const std::string trimmed(directory.substr(0, directory.size() - 1));
            if (!trimmed.empty() && trimmed.find(':') == std::string::npos) {
                const auto key = normalized_path(trimmed);
                for (const auto& root : loose_roots_)
                    if (auto folder = find_loose_directory(root, key))
                        folders.push_back(std::move(*folder));
            }
        }
        if (!folders.empty())
            for (const auto& item : merged_listing(folders, roots))
                if (match_host_pattern(item.name, spec))
                    found.push_back(
                        {item.name, item.directory, item.directory ? 0 : item.size, -1}
                    );
        if (!scope.continue_into_mounts)
            return found;
        scope.first_mount = 0;
    }
    find_in_mounts(found, directory, spec, scope);
    return found;
}

void AssetStore::find_in_mounts(
    std::vector<FoundEntry>& found,
    std::string_view directory,
    std::string_view spec,
    FindScope scope
) const {
    for (auto mount = static_cast<std::size_t>(scope.first_mount); mount < mounts_.size();
         ++mount) {
        const auto& archive = mounts_[mount].archive;
        const auto& marks = mounts_[mount].marks;
        if (const auto node = archive.lookup_directory(directory)) {
            const ArchiveNode& parent = archive.nodes()[*node];
            for (uint32_t i = 0; i < parent.child_count; ++i) {
                const auto index = parent.first_child + i;
                const ArchiveNode& child = archive.nodes()[index];
                const bool hidden = index < marks.size() && (marks[index] & kShadowedByLoose) != 0;
                if (!match_wildcard(child.name, spec) || hidden)
                    continue;
                found.push_back(
                    {child.name,
                     child.directory(),
                     child.directory() ? 0 : child.size,
                     static_cast<int>(mount)}
                );
            }
        }
        if (!scope.continue_into_mounts)
            return;
    }
}

std::vector<std::string> AssetStore::scan_recursive(
    std::string_view directory, std::string_view pattern, FindScope scope
) const {
    std::vector<std::string> result;
    const auto visit = [&](const auto& self, const std::string& path, FindScope from) -> void {
        for (const auto& entry : find(path + "\\*", from)) {
            if (entry.name == kCurrentDirectory || entry.name == kParentDirectory)
                continue;
            const std::string child = path + "\\" + entry.name;
            if (!entry.directory) {
                if (match_wildcard(entry.name, pattern))
                    result.push_back(child);
                continue;
            }
            self(self, child, FindScope{entry.mount, false});
        }
    };
    // An empty directory names the drive root in the game; nothing here.
    if (!directory.empty())
        visit(visit, std::string(directory), scope);
    return result;
}

int AssetStore::count_entries(std::string_view pattern, bool directories_only) const {
    int count = 0;
    for (const auto& entry : find(pattern))
        if (entry.name != kCurrentDirectory && entry.name != kParentDirectory &&
            (!directories_only || entry.directory))
            ++count;
    return count;
}

ResourceFile* AssetStore::open(std::string_view resource) const {
    if (const auto loose = loose_path(resource)) {
        auto* file = new ResourceFile;
        file->loose.open(*loose, std::ios::binary);
        if (file->loose) {
            std::error_code error;
            const auto size = std::filesystem::file_size(*loose, error);
            file->loose_size =
                error ? 0 : static_cast<uint32_t>(std::min<uintmax_t>(size, 0xFFFFFFFFU));
            return file;
        }
        delete file;
    }
    for (const auto& mounted : mounts_) {
        const auto node = mounted.archive.lookup(resource);
        if (!node || mounted.archive.nodes()[*node].directory())
            continue;
        auto* file = new ResourceFile;
        file->archive = &mounted.archive;
        file->archive_path = &mounted.path;
        file->node = *node;
        file->size = mounted.archive.nodes()[*node].size;
        return file;
    }
    return nullptr;
}

void AssetStore::close(ResourceFile* file) noexcept {
    delete file;
}

bool AssetStore::archived(const ResourceFile* file) noexcept {
    return file->archive != nullptr;
}

int32_t AssetStore::seek(ResourceFile* file, uint32_t position) {
    if (file->archive == nullptr) {
        file->loose.clear();
        if (position == kSeekToEnd)
            file->loose.seekg(0, std::ios::end);
        else
            file->loose.seekg(static_cast<std::streamoff>(position));
        if (!file->loose)
            return -1;
        return static_cast<int32_t>(file->loose.tellg());
    }
    const uint32_t previous = file->position;
    if (position == kSeekToEnd)
        position = file->size;
    file->position = position;
    if (((previous ^ position) & ~(formats::hpi::BlockBytes - 1U)) != 0)
        file->block.clear();
    return 0;
}

int32_t AssetStore::tell(const ResourceFile* file) {
    if (file->archive == nullptr)
        return static_cast<int32_t>(const_cast<ResourceFile*>(file)->loose.tellg());
    return static_cast<int32_t>(file->position);
}

uint32_t AssetStore::length(const ResourceFile* file) {
    return file->archive == nullptr ? file->loose_size : file->size;
}

int32_t AssetStore::read(ResourceFile* file, std::span<uint8_t> output) {
    if (file->archive == nullptr) {
        file->loose.read(
            reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())
        );
        const auto count = file->loose.gcount();
        file->loose.clear();
        return static_cast<int32_t>(count);
    }
    if (file->position >= file->size)
        return 0;
    const std::size_t wanted = std::min<std::size_t>(output.size(), file->size - file->position);
    const ArchiveNode& node = file->archive->nodes()[file->node];
    if (node.compression == 0) {
        const auto count =
            file->archive->read_node_range(file->node, file->position, output.first(wanted));
        if (!count.ok())
            fail_entry_read(count.error, *file->archive_path, node);
        file->position += *count.value;
        return static_cast<int32_t>(*count.value);
    }
    std::size_t copied = 0;
    while (copied < wanted) {
        const uint32_t base = file->position & ~(formats::hpi::BlockBytes - 1U);
        if (file->block.empty()) {
            file->block.assign(std::min<uint32_t>(formats::hpi::BlockBytes, file->size - base), 0);
            const auto read = file->archive->read_node_range(file->node, base, file->block);
            if (!read.ok()) {
                file->block.clear();
                // A chunk that cannot be read ends the read; one that fails
                // to decode is fatal, as in 3.1c.
                if (read.error.code == base::bytes::DecodeCode::truncated)
                    return -1;
                fail_entry_read(read.error, *file->archive_path, node);
            }
        }
        const uint32_t offset = file->position - base;
        const std::size_t take =
            std::min<std::size_t>(file->block.size() - offset, wanted - copied);
        std::copy_n(
            file->block.begin() + offset, take, output.begin() + static_cast<std::ptrdiff_t>(copied)
        );
        copied += take;
        (void)seek(file, file->position + static_cast<uint32_t>(take));
    }
    return static_cast<int32_t>(copied);
}

base::bytes::Decoded<HpiArchive> open_hpi_file(const std::filesystem::path& path) {
    std::string reason;
    auto file = open_archive_file(path, reason);
    if (!file)
        return base::bytes::DecodeError{
            base::bytes::DecodeCode::not_found, 0, "cannot open HPI archive"
        };
    return HpiArchive::open(std::move(file));
}

int32_t write_loose_file(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream)
        return -1;
    stream.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    return stream ? static_cast<int32_t>(bytes.size()) : 0;
}

void create_directory_path(const std::filesystem::path& path) {
    const std::string text = path.string();
    std::error_code ignored;
    for (std::size_t i = 0; i < text.size(); ++i)
        if (text[i] == '\\' || text[i] == '/')
            std::filesystem::create_directory(text.substr(0, i), ignored);
    std::filesystem::create_directory(text, ignored);
}

std::vector<std::string>
AssetStore::list_effective(std::string_view directory, std::string_view extension) const {
    auto result = list_effective_in_mount_order(directory, extension);
    std::sort(result.begin(), result.end());
    return result;
}

std::vector<std::string> AssetStore::list_effective_in_mount_order(
    std::string_view directory, std::string_view extension
) const {
    return list_resources(directory, extension, false);
}

std::vector<std::string>
AssetStore::list_effective_recursive(std::string_view directory, std::string_view extension) const {
    // Feature loading retains this enumeration order; the first document
    // containing a requested section wins.
    return list_resources(directory, extension, true);
}

std::vector<std::string> AssetStore::list_resources(
    std::string_view directory, std::string_view extension, bool recursive
) const {
    if (directory.size() > kMaxPathLength ||
        directory.find_first_of("\\:\0", 0, 3) != std::string_view::npos ||
        (!directory.empty() && directory.front() == '/') ||
        extension.find_first_of("/\\:\0", 0, 4) != std::string_view::npos)
        fail("invalid asset listing path");
    note_lookup(directory);
    auto prefix = normalized_path(directory);
    for (std::size_t begin = 0; begin < prefix.size();) {
        const auto end = prefix.find('/', begin);
        const auto part = prefix.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..")
            fail("asset listing contains traversal");
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
    // The folder below each layered root that the listing names.
    const auto folder_in = [&](const std::filesystem::path& root) {
        std::optional<std::filesystem::path> loose = root;
        if (hidden_at_top(first_part(prefix)))
            return std::optional<std::filesystem::path>{};
        for (std::size_t begin = 0; loose && begin < prefix.size();) {
            const auto end = prefix.find('/', begin);
            const auto part = prefix.substr(begin, end == std::string::npos ? end : end - begin);
            std::optional<std::filesystem::path> match;
            if (std::filesystem::is_directory(*loose))
                for (const auto& item : std::filesystem::directory_iterator(*loose))
                    if (normalized_path(utf8_text(item.path().filename())) == part) {
                        if (match)
                            fail("ambiguous loose asset directory: " + prefix);
                        match = item.path();
                    }
            loose = match;
            if (end == std::string::npos)
                break;
            begin = end + 1;
        }
        return loose;
    };
    if (!prefix.empty())
        prefix += '/';
    const auto suffix = normalized_path(extension);
    const auto matches = [&](std::string_view key) {
        return key.starts_with(prefix) &&
               (recursive || key.find('/', prefix.size()) == std::string_view::npos) &&
               (suffix.empty() || key.ends_with(suffix));
    };
    std::set<std::string> keys;
    std::vector<std::string> result;
    for (const auto& root : loose_roots_) {
        const auto found = folder_in(root);
        if (!found || !std::filesystem::is_directory(*found) ||
            (!prefix.empty() && !stays_inside(root, *found)))
            continue;
        const auto& loose = *found;
        // Names that differ only in case are ambiguous within one folder;
        // across layered folders they are one file.
        std::set<std::string> folder_keys;
        const auto append = [&](const auto& item) {
            std::error_code link_error;
            if (!item.is_regular_file() ||
                (item.is_symlink(link_error) && !stays_inside(root, item.path())))
                return;
            // Keys spell names in UTF-8, as the lookups compare them.
            const auto relative = item.path().lexically_relative(loose).generic_u8string();
            const auto key =
                prefix + normalized_path(std::string(relative.begin(), relative.end()));
            if (matches(key)) {
                if (!folder_keys.insert(key).second)
                    fail("ambiguous loose asset case: " + key);
                if (keys.insert(key).second)
                    result.push_back(key);
            }
        };
        if (recursive) {
            // From a layered folder's top, its hidden folder is not walked.
            const bool at_top = prefix.empty();
            for (auto item = std::filesystem::recursive_directory_iterator(loose);
                 item != std::filesystem::recursive_directory_iterator();
                 ++item) {
                if (at_top && item.depth() == 0 && item->is_directory() &&
                    hidden_at_top(normalized_path(utf8_text(item->path().filename())))) {
                    item.disable_recursion_pending();
                    continue;
                }
                append(*item);
            }
        } else
            for (const auto& item : std::filesystem::directory_iterator(loose))
                append(item);
    }
    // Names only: read() resolves the winning content in the same mount order.
    for (const auto& mounted : mounts_)
        for (const auto& entry : mounted.archive.entries()) {
            const auto key = normalized_path(entry.path);
            if (matches(key) && keys.insert(key).second)
                result.push_back(key);
        }
    return result;
}

} // namespace oa
