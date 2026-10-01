// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include "oa/base/threads.hpp"

#include <algorithm>
#include <atomic>
#include <cstdint>
#include <fstream>
#include <limits>
#include <set>
#include <stdexcept>
#include <unordered_map>

namespace oa {
namespace {

constexpr uint64_t kMaxEntrySize = 1ULL << 30;
constexpr std::size_t kMaxPathLength = 4096;
constexpr uint32_t kSeekToEnd = 0xFFFFFFFFU;
// Search spec rewritten to "*" when a find names it as its basename.
constexpr std::string_view kAllFilesPattern = "*.*";
constexpr std::string_view kCurrentDirectory = ".";
constexpr std::string_view kParentDirectory = "..";
constexpr std::string_view kDiscoveryGp3 = ".GP3";
constexpr std::string_view kDiscoveryGp3Prefix = "rev";
constexpr std::string_view kDiscoveryCcx = "*.CCX";
constexpr std::string_view kDiscoveryUfo = "*.UFO";
constexpr std::string_view kDiscoveryHpi = "*.HPI";
constexpr std::string_view kDiscoveryRemovable = "*.hpi";
// Mount-time mark on an archive file that a loose file of the same path hides.
constexpr uint8_t kShadowedByLoose = 0x02;

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
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
    std::string text = std::filesystem::weakly_canonical(path).generic_string();
    std::transform(text.begin(), text.end(), text.begin(), ascii_upper);
    return text;
}

std::vector<uint8_t> read_loose(const std::filesystem::path& path) {
    const auto size = std::filesystem::file_size(path);
    if (size > kMaxEntrySize)
        fail("loose asset exceeds entry size limit");
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        fail("cannot open loose asset '" + path.string() + "'");
    std::vector<uint8_t> bytes(static_cast<std::size_t>(size));
    stream.read(reinterpret_cast<char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
    if (stream.gcount() != static_cast<std::streamsize>(bytes.size()))
        fail("truncated loose asset '" + path.string() + "'");
    return bytes;
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
        LooseItem entry{item.path().filename().string(), item.path(), item.is_directory(error), 0};
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

} // namespace

struct ResourceFile {
    std::ifstream loose;
    uint32_t loose_size = 0;
    const HpiArchive* archive = nullptr;
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
            fresh.children.try_emplace(normalized_path(item.path().filename().string()));
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
    : loose_root_(std::filesystem::absolute(std::move(loose_root))),
      loose_index_(std::make_unique<LooseIndex>(loose_index_limit)) {
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
    const auto absolute = std::filesystem::weakly_canonical(archive);
    mounts_.push_back(Mount{absolute, HpiArchive(absolute), {}});
    mount_paths_.push_back(absolute);
}

bool AssetStore::try_mount(const std::filesystem::path& archive, std::string* error) {
    if (is_mounted(archive)) {
        if (error != nullptr)
            *error = "already mounted";
        return false;
    }
    try {
        const auto absolute = std::filesystem::weakly_canonical(archive);
        mounts_.push_back(Mount{absolute, HpiArchive(absolute), {}});
        mount_paths_.push_back(absolute);
        return true;
    } catch (const std::exception& failure) {
        if (error != nullptr)
            *error = failure.what();
        return false;
    }
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
    drop_vanished_mounts();
    std::vector<DiscoveredArchive> report;
    const auto scan =
        [&](const std::filesystem::path& directory, std::string_view pattern, int limit) {
            int remaining = limit;
            for (const auto& item : loose_listing(directory)) {
                if (!match_host_pattern(item.name, pattern))
                    continue;
                DiscoveredArchive outcome{item.path, false, {}, is_mounted(item.path)};
                if (!outcome.already_mounted)
                    outcome.mounted = try_mount(item.path, &outcome.error);
                report.push_back(std::move(outcome));
                if (report.back().mounted && --remaining == 0)
                    break;
            }
        };
    const std::string revision =
        std::string(kDiscoveryGp3Prefix) + std::string(version) + std::string(kDiscoveryGp3);
    scan(loose_root_, revision, -1);
    scan(loose_root_, kDiscoveryCcx, -1);
    scan(loose_root_, kDiscoveryUfo, -1);
    scan(loose_root_, kDiscoveryHpi, formats::hpi::PlainArchiveMountLimit);
    for (const auto& root : removable_roots)
        scan(root, kDiscoveryRemovable, -1);
    mark_loose_shadows();
    return report;
}

void AssetStore::mark_loose_shadows() {
    if (loose_index_)
        loose_index_->reset();
    for (auto& mounted : mounts_)
        mounted.marks.assign(mounted.archive.nodes().size(), 0);
    if (!mounts_.empty())
        mark_loose_directory("", loose_root_);
}

void AssetStore::mark_loose_directory(
    const std::string& prefix, const std::filesystem::path& directory
) {
    for (const auto& item : loose_listing(directory)) {
        if (item.directory) {
            if (item.name != kCurrentDirectory && item.name != kParentDirectory)
                mark_loose_directory(prefix + item.name + "\\", item.path);
            continue;
        }
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
    if (resource.empty() || resource.front() == '/' || resource.front() == '\\' ||
        resource.find(':') != std::string_view::npos ||
        resource.find('\0') != std::string_view::npos || resource.size() > kMaxPathLength)
        fail("invalid asset resource path");
    const auto key = normalized_path(resource);
    if (!loose_index_ || !loose_index_->enabled.load())
        return loose_path_listed(key);
    const base::threads::LockGuard guard(loose_index_->lock);
    auto loose = loose_root_;
    std::string folder;
    for (std::size_t begin = 0; begin < key.size();) {
        const auto end = key.find('/', begin);
        const auto part = key.substr(begin, end == std::string::npos ? end : end - begin);
        if (part == "." || part == "..")
            fail("asset path contains traversal");
        const auto* listing = loose_index_->listing(folder, loose);
        if (listing == nullptr)
            return loose_index_->enabled.load() ? std::nullopt : loose_path_listed(key);
        const auto child = listing->children.find(part);
        if (child == listing->children.end())
            return std::nullopt;
        if (child->second.ambiguous)
            fail("ambiguous loose asset case: " + key);
        if (end == std::string::npos)
            return child->second.regular ? std::optional(child->second.path) : std::nullopt;
        if (!child->second.directory)
            return std::nullopt;
        loose = child->second.path;
        folder = key.substr(0, end);
        begin = end + 1;
    }
    return std::nullopt;
}

std::optional<std::filesystem::path> AssetStore::loose_path_listed(const std::string& key) const {
    auto loose = loose_root_;
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
            if (normalized_path(item.path().filename().string()) == part) {
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
    if (!std::filesystem::is_regular_file(loose, error))
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
        return {mounted.archive.read_node(found->node), mounted.path, true};
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
    if (static_cast<int32_t>(size) > 0 && seek(file, 0) != -1) {
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
    std::vector<uint8_t> bytes(size);
    const auto slice = static_cast<size_t>(static_cast<int32_t>(size) / kPasses);
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
    std::vector<FoundEntry> found;
    const std::size_t split = last_separator(pattern);
    const std::string_view directory = pattern.substr(0, split);
    std::string_view spec = pattern.substr(split);
    if (spec == kAllFilesPattern)
        spec = "*";
    if (scope.first_mount < 0) {
        std::optional<std::filesystem::path> loose = loose_root_;
        if (!directory.empty()) {
            const std::string trimmed(directory.substr(0, directory.size() - 1));
            loose.reset();
            if (!trimmed.empty() && trimmed.find(':') == std::string::npos) {
                auto candidate = loose_root_;
                bool present = true;
                const auto key = normalized_path(trimmed);
                for (std::size_t begin = 0; present && begin < key.size();) {
                    const auto end = key.find('/', begin);
                    const auto part =
                        key.substr(begin, end == std::string::npos ? end : end - begin);
                    present = false;
                    for (const auto& item : loose_listing(candidate))
                        if (item.directory && item.name != kCurrentDirectory &&
                            item.name != kParentDirectory && normalized_path(item.name) == part) {
                            candidate = item.path;
                            present = true;
                            break;
                        }
                    if (end == std::string::npos)
                        break;
                    begin = end + 1;
                }
                if (present)
                    loose = candidate;
            }
        }
        if (loose)
            for (const auto& item : loose_listing(*loose))
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
        if (count > 0)
            file->position += static_cast<uint32_t>(count);
        return static_cast<int32_t>(count);
    }
    std::size_t copied = 0;
    while (copied < wanted) {
        const uint32_t base = file->position & ~(formats::hpi::BlockBytes - 1U);
        if (file->block.empty()) {
            file->block.assign(std::min<uint32_t>(formats::hpi::BlockBytes, file->size - base), 0);
            if (file->archive->read_node_range(file->node, base, file->block) < 0) {
                file->block.clear();
                return -1;
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
    auto prefix = normalized_path(directory);
    auto loose = loose_root_;
    bool present = true;
    for (std::size_t begin = 0; begin < prefix.size();) {
        const auto end = prefix.find('/', begin);
        const auto part = prefix.substr(begin, end == std::string::npos ? end : end - begin);
        if (part.empty() || part == "." || part == "..")
            fail("asset listing contains traversal");
        if (present) {
            std::optional<std::filesystem::path> match;
            if (std::filesystem::is_directory(loose))
                for (const auto& item : std::filesystem::directory_iterator(loose))
                    if (normalized_path(item.path().filename().string()) == part) {
                        if (match)
                            fail("ambiguous loose asset directory: " + prefix);
                        match = item.path();
                    }
            if (match)
                loose = *match;
            else
                present = false;
        }
        if (end == std::string::npos)
            break;
        begin = end + 1;
    }
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
    if (present && std::filesystem::is_directory(loose)) {
        const auto append = [&](const auto& item) {
            if (!item.is_regular_file())
                return;
            const auto key =
                prefix + normalized_path(item.path().lexically_relative(loose).generic_string());
            if (matches(key)) {
                if (!keys.insert(key).second)
                    fail("ambiguous loose asset case: " + key);
                result.push_back(key);
            }
        };
        if (recursive)
            for (const auto& item : std::filesystem::recursive_directory_iterator(loose))
                append(item);
        else
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
