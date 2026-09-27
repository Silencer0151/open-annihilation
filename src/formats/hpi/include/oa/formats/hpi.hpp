// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <cstdint>
#include <array>
#include <cstddef>
#include <filesystem>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace oa {

// Packed little-endian images of the HPI records. A uint8 follows uint32
// fields with no pad on disk, so these records stay packed.
namespace formats::hpi {
inline constexpr uint32_t HeaderMarker = 0x49504148U; // "HAPI"
inline constexpr uint32_t ChunkMarker = 0x48535153U;  // "SQSH"
inline constexpr uint32_t VersionV1 = 0x00010000U;    // Total Annihilation
inline constexpr uint32_t VersionV2 = 0x00020000U;    // TA: Kingdoms
inline constexpr uint16_t HeaderSize = 20;
inline constexpr uint16_t DirectoryEntrySize = 9;
inline constexpr uint16_t FileEntrySize = 9;
inline constexpr uint16_t ChunkHeaderSize = 9;
inline constexpr uint16_t SQSHHeaderSize = 19;
inline constexpr uint16_t CompressionNone = 0;
inline constexpr uint16_t CompressionLZ77 = 1;
inline constexpr uint16_t CompressionZLib = 2;
inline constexpr uint16_t EntryTypeFile = 0;
inline constexpr uint16_t EntryTypeDirectory = 1;

#pragma pack(push, 1)

struct Header {
    uint32_t marker;
    uint32_t version;
    uint32_t directory_size;
    uint32_t decrypt_key;
    uint32_t offset;
};

struct ChunkHeader {
    uint32_t magic;
    uint8_t version;
    uint8_t compression_type;
    uint8_t encoded;
    uint32_t compressed_size;
    uint32_t decompressed_size;
    uint32_t checksum;
};

struct DirectoryNode {
    uint32_t count;
    uint32_t list_offset;
};

struct DirectoryEntry {
    uint32_t name_offset;
    uint32_t data_offset;
    uint8_t type;
};

struct FileEntry {
    uint32_t offset;
    uint32_t size;
    uint8_t compression;
};

#pragma pack(pop)

static_assert(sizeof(Header) == HeaderSize);
static_assert(offsetof(Header, version) == 4);
static_assert(offsetof(Header, directory_size) == 8);
static_assert(offsetof(Header, decrypt_key) == 12);
static_assert(offsetof(Header, offset) == 16);
static_assert(sizeof(ChunkHeader) == SQSHHeaderSize);
static_assert(offsetof(ChunkHeader, version) == 4);
static_assert(offsetof(ChunkHeader, compression_type) == 5);
static_assert(offsetof(ChunkHeader, encoded) == 6);
static_assert(offsetof(ChunkHeader, compressed_size) == 7);
static_assert(offsetof(ChunkHeader, decompressed_size) == 11);
static_assert(offsetof(ChunkHeader, checksum) == 15);
static_assert(sizeof(DirectoryNode) == 8);
static_assert(offsetof(DirectoryNode, list_offset) == 4);
static_assert(sizeof(DirectoryEntry) == DirectoryEntrySize);
static_assert(offsetof(DirectoryEntry, data_offset) == 4);
static_assert(offsetof(DirectoryEntry, type) == 8);
static_assert(sizeof(FileEntry) == FileEntrySize);
static_assert(offsetof(FileEntry, size) == 4);
static_assert(offsetof(FileEntry, compression) == 8);
static_assert(ChunkHeaderSize != SQSHHeaderSize);
static_assert(VersionV1 != VersionV2);

// Directory-entry flag bit marking a subdirectory; other stored bits are ignored.
inline constexpr uint8_t EntryFlagDirectory = 0x01;
// The archive's 36-byte copyright trailer, the text every archive must end
// with, with the year as four zero digits and no terminating NUL; the four
// year characters are not compared.
inline constexpr char TrailerChars[] = {0x43, 0x6f, 0x70, 0x79, 0x72, 0x69, 0x67, 0x68, 0x74,
                                        0x20, 0x30, 0x30, 0x30, 0x30, 0x20, 0x43, 0x61, 0x76,
                                        0x65, 0x64, 0x6f, 0x67, 0x20, 0x45, 0x6e, 0x74, 0x65,
                                        0x72, 0x74, 0x61, 0x69, 0x6e, 0x6d, 0x65, 0x6e, 0x74};
inline constexpr std::string_view Trailer{TrailerChars, sizeof TrailerChars};
static_assert(Trailer.size() == 36);
inline constexpr std::size_t TrailerYearOffset = 10;
inline constexpr std::size_t TrailerYearBytes = 4;
// Decoded bytes per chunk of a compressed entry, and the per-read block cache.
inline constexpr uint32_t BlockBytes = 0x10000;
// Plain *.HPI archives mounted from the game directory before the scan stops.
inline constexpr int PlainArchiveMountLimit = 10;

// Squash codec status; squash_status_name gives each value's SQUASHERR_* name.
enum class SquashStatus : uint8_t {
    ok = 0,
    bad_header = 1,
    bad_checksum = 2,
    bad_unpack_size = 3,
    bad_unpack_type = 4,
    bad_pack_type = 5,
    bad_params = 6,
};
} // namespace formats::hpi

/// Derives the per-archive XOR key from header byte 12.
///
/// @param header_key the header's key byte
/// @return 0 for 0, otherwise ~((byte >> 6) | (byte << 2)) truncated to 8
///         bits; header 0xFF also yields 0, which disables decryption
[[nodiscard]] uint8_t hpi_archive_key(uint8_t header_key) noexcept;

/// Inflates a whole zlib stream into a buffer, reporting the length only on a clean end.
///
/// @param[out] output destination buffer
/// @param[in,out] length expected length on entry, clamped to output.size();
///        updated only when the stream ends cleanly, so a corrupt or overlong
///        stream leaves the caller's value in place
/// @param input compressed bytes
/// @return the zlib status
int uncompress_legacy(
    std::span<uint8_t> output, uint32_t* length, std::span<const uint8_t> input
) noexcept;

/// Validates and decodes one SQSH chunk of an archive entry.
///
/// Checks the marker, a type below four and the 32-bit byte-sum checksum,
/// descrambles the payload when flagged, then decodes LZ77 or zlib. Stored
/// (0) and type-3 chunks never decode.
///
/// @param[out] output decoded bytes, at most one 64 KiB block
/// @param[in,out] block the whole chunk, header first; its payload is
///        descrambled in place
/// @return ok, or the SQUASHERR_* condition; the header's unpacked size must fit
///         output, and a packed size past the chunk is a checksum failure
[[nodiscard]] formats::hpi::SquashStatus
unsquash_archive_block(std::span<uint8_t> output, std::span<uint8_t> block) noexcept;

/// Returns the SQUASHERR_* diagnostic name of a squash status.
///
/// @param status squash status
/// @return the name, or null for a value outside the table
[[nodiscard]] const char* squash_status_name(formats::hpi::SquashStatus status) noexcept;

/// Matches a name against a '*'/'?' wildcard pattern, ignoring ASCII case.
///
/// @param name candidate name; a NUL ends it
/// @param pattern wildcard pattern
/// @return true when the whole name matches
/// @quirk At most 100 pattern positions are tracked at once; further '*'
///        branches are dropped.
[[nodiscard]] bool match_wildcard(std::string_view name, std::string_view pattern) noexcept;

struct ArchiveEntry {
    std::string path;
    uint32_t size{};
};

// One record of an archive directory, as kept after the directory block has
// been decrypted and its offsets resolved.
struct ArchiveNode {
    std::string name;
    uint8_t flags{};
    // Files: absolute data offset, decoded size, nonzero when chunked.
    uint32_t data_offset{};
    uint32_t size{};
    uint8_t compression{};
    // Directories: children occupy nodes[first_child, first_child + child_count).
    uint32_t first_child{};
    uint32_t child_count{};

    /// Returns whether the record is a subdirectory.
    [[nodiscard]] bool directory() const noexcept {
        return (flags & formats::hpi::EntryFlagDirectory) != 0;
    }
};

class HpiArchive {
  public:

    /// Opens and validates an archive and decrypts its directory block.
    ///
    /// The archive must be HPI version 1 with the copyright trailer; the
    /// directory is resolved into nodes() and the file stays open for reads.
    /// Throws std::runtime_error for an unreadable or malformed archive.
    ///
    /// @param path host path of the archive
    explicit HpiArchive(const std::filesystem::path& path);
    /// Closes the archive file.
    ~HpiArchive();

    /// Takes over another archive's open file and directory.
    ///
    /// @param other archive left empty
    HpiArchive(HpiArchive&& other) noexcept;
    /// Takes over another archive's open file and directory, closing this one's.
    ///
    /// @param other archive left empty
    /// @return this archive
    HpiArchive& operator=(HpiArchive&& other) noexcept;
    HpiArchive(const HpiArchive&) = delete;
    HpiArchive& operator=(const HpiArchive&) = delete;

    /// Lists every file record in stored depth-first order.
    ///
    /// Records a lookup can never reach (earlier duplicates, children of a
    /// shadowed directory) are included.
    ///
    /// @return '/'-joined paths and decoded sizes
    [[nodiscard]] std::vector<ArchiveEntry> entries() const;
    /// Reads the whole decoded content of a file.
    ///
    /// Throws std::invalid_argument when lookup() finds no file.
    ///
    /// @param path '\\'- or '/'-separated path inside the archive
    /// @return the decoded bytes
    [[nodiscard]] std::vector<uint8_t> read(std::string_view path) const;

    /// Returns the host path the archive was opened from.
    ///
    /// @return the path passed to the constructor
    [[nodiscard]] const std::filesystem::path& path() const noexcept;
    /// Returns every directory and file record.
    ///
    /// @return the records; node 0 is a synthetic root directory holding the root records
    [[nodiscard]] std::span<const ArchiveNode> nodes() const noexcept;
    /// Resolves a path through the archive tree.
    ///
    /// @param path '\\'- or '/'-separated path
    /// @return the node index, or nullopt
    /// @quirk Each segment takes the last case-insensitive match in its
    ///        directory; an intermediate match that is a file ends the lookup
    ///        unsuccessfully.
    [[nodiscard]] std::optional<uint32_t> lookup(std::string_view path) const noexcept;
    /// Resolves the directory named by all but the last segment of a path.
    ///
    /// @param path '\\'- or '/'-separated path; its last segment is not looked up
    /// @return the directory's node index, or nullopt
    [[nodiscard]] std::optional<uint32_t> lookup_directory(std::string_view path) const noexcept;
    /// Reads the whole decoded content of a file node.
    ///
    /// Throws std::invalid_argument for a directory or bad index and
    /// std::runtime_error for a truncated entry.
    ///
    /// @param index file node index
    /// @return the decoded bytes
    [[nodiscard]] std::vector<uint8_t> read_node(uint32_t index) const;
    /// Copies a range of a file node's decoded bytes.
    ///
    /// Compressed entries are located through the decrypted chunk-size table
    /// and decoded one 64 KiB block at a time. A chunk that fails to decode
    /// throws std::runtime_error, as 3.1c treats it as fatal.
    ///
    /// @param index file node index
    /// @param position first decoded byte to copy
    /// @param[out] output destination; the range is clamped to the entry
    /// @return the count copied, or -1 when a chunk cannot be read
    [[nodiscard]] int64_t
    read_node_range(uint32_t index, uint32_t position, std::span<uint8_t> output) const;

  private:

    struct Impl;
    std::unique_ptr<Impl> impl_;
};

// Serialises an HPI version-1 archive: header, encrypted directory, entry data
// and the copyright trailer the reader requires.
struct HpiWriteFile {
    std::string path; // '/'-separated; directories are created as needed
    std::vector<uint8_t> bytes;
    uint8_t compression = formats::hpi::CompressionLZ77;
};

struct HpiWriteOptions {
    uint8_t header_key = 0;
    bool scramble_chunks = true;
    std::string_view trailer_year = "1997";
};

/// Serialises files as an HPI version-1 archive.
///
/// Throws std::invalid_argument when the trailer year is not four characters.
///
/// @param files entries in the order their data is written
/// @param options header key, chunk scrambling and trailer year
/// @return the archive bytes
[[nodiscard]] std::vector<uint8_t>
write_hpi(std::span<const HpiWriteFile> files, const HpiWriteOptions& options = {});

struct AssetData {
    std::vector<uint8_t> bytes;
    std::filesystem::path source;
    bool archived = false;
};

// One match of a find over loose files, then archives.
struct FoundEntry {
    std::string name;
    bool directory = false;
    uint32_t size = 0;
    // -1 for a loose file, otherwise the mount index that produced it.
    int mount = -1;
};

// Where a find enumeration starts: loose files first (then every mount when
// `continue_into_mounts`), or one mount onward.
struct FindScope {
    int first_mount = -1;
    bool continue_into_mounts = true;
};

// Mount outcome for one archive candidate during discovery.
struct DiscoveredArchive {
    std::filesystem::path path;
    bool mounted = false;
    std::string error;
    // Rejected because its full path was already in the table; such a
    // candidate never counts toward the *.HPI limit.
    bool already_mounted = false;
};

// Open-file handle over a loose file or an archive entry. Its reads and seeks
// keep 3.1c's position rules, and it caches one decoded block.
struct ResourceFile;

// The game checks loose files before scanning mounted archives in mount
// order. Callers supply that order explicitly (or use discover()): platform
// directory enumeration order is not a portable replacement for the Windows
// order the game lists files in.
class AssetStore {
  public:

    /// Creates a store over a game directory with no archives mounted.
    ///
    /// @param loose_root game directory; loose files are resolved below it
    explicit AssetStore(std::filesystem::path loose_root);
    /// Unmounts every archive.
    ~AssetStore();
    /// Takes over another store's root and mounts.
    ///
    /// @param other store left empty
    AssetStore(AssetStore&& other) noexcept;
    /// Takes over another store's root and mounts, dropping this one's.
    ///
    /// @param other store left empty
    /// @return this store
    AssetStore& operator=(AssetStore&& other) noexcept;
    AssetStore(const AssetStore&) = delete;
    AssetStore& operator=(const AssetStore&) = delete;

    /// Mounts one archive after the existing mounts.
    ///
    /// A path already mounted (case-insensitive full path) is ignored. Throws
    /// when the archive cannot be opened or is invalid.
    ///
    /// @param archive host path of the archive
    void mount(const std::filesystem::path& archive);
    /// Mounts one archive, reporting failure instead of throwing.
    ///
    /// @param archive host path of the archive
    /// @param[out] error receives the reason when not mounted, if not null
    /// @return true when mounted; false for an invalid or already-mounted path
    bool try_mount(const std::filesystem::path& archive, std::string* error = nullptr);
    /// Scans the game directory and removable roots for archives, as the game does.
    ///
    /// Mounts that no longer open are dropped first. Then rev<version>.GP3,
    /// *.CCX, *.UFO and at most ten *.HPI in the game directory, then *.hpi on
    /// each removable root, each group in Windows NTFS name order. The mount
    /// order is the lookup precedence after loose files, so a path several
    /// archives provide resolves from the earliest mount. Candidates that fail
    /// to open are reported and do not count toward the *.HPI limit; a
    /// directory matching a pattern is such a candidate. Loose shadows are
    /// recomputed afterwards.
    ///
    /// @param version game revision in the GP3 name, e.g. "31" for rev31.gp3
    /// @param removable_roots roots of removable drives searched last
    /// @return one outcome per candidate, in scan order
    std::vector<DiscoveredArchive>
    discover(std::string_view version, std::span<const std::filesystem::path> removable_roots = {});
    /// Recomputes which archive files a loose file of the same path hides from find().
    ///
    /// discover() does this after mounting.
    void mark_loose_shadows();
    /// Returns the mounted archive paths.
    ///
    /// @return canonical paths in lookup order
    [[nodiscard]] std::span<const std::filesystem::path> mount_paths() const noexcept;
    /// Returns one mounted archive.
    ///
    /// Throws std::out_of_range past the mounts.
    ///
    /// @param index mount index, in lookup order
    /// @return the archive
    [[nodiscard]] const HpiArchive& mounted(std::size_t index) const;

    /// Reads a whole resource: a loose file, else the first mount holding it as a file.
    ///
    /// Throws std::runtime_error for an invalid or traversing path, an
    /// ambiguous loose case collision, or a resource nothing provides.
    ///
    /// @param resource '\\'- or '/'-separated path, matched ignoring ASCII case
    /// @return the bytes, the providing path and whether it came from an archive
    [[nodiscard]] AssetData read(std::string_view resource) const;
    /// Reads a resource as though one archive were not mounted.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @param archive mount path skipped; loose files and every other mount are searched in order
    /// @return the bytes and their source
    [[nodiscard]] AssetData
    read_without(std::string_view resource, const std::filesystem::path& archive) const;
    /// Returns the archive read() would take a resource from.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @return the mount path, or nullopt when a loose file provides it or nothing does
    [[nodiscard]] std::optional<std::filesystem::path>
    providing_archive(std::string_view resource) const;
    /// Loads a whole resource by reading it through a handle, so it holds what a handle read gives.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @return the bytes, or nullopt when the resource is absent, empty or yields no bytes
    /// @quirk A short read keeps the full length; the unread tail is zero.
    [[nodiscard]] std::optional<std::vector<uint8_t>>
    load_file_contents(std::string_view resource) const;
    /// Loads a whole resource in ten equal reads plus the remainder, reporting progress.
    ///
    /// Throws std::runtime_error when the resource is absent.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @param progress called after each of the ten reads with 9, 18, ... 90; may be null
    /// @param user value passed to progress
    /// @return the bytes
    [[nodiscard]] std::vector<uint8_t> load_with_progress(
        std::string_view resource, void (*progress)(void* user, uint8_t percent), void* user
    ) const;
    /// Returns a resource's size without reading its content.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @return the size in bytes, or zero when absent
    [[nodiscard]] uint32_t file_size(std::string_view resource) const;
    /// Reads part of a resource.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @param position first byte to read
    /// @param[out] output receives up to output.size() bytes
    /// @return false when the resource is absent or nothing was read
    [[nodiscard]] bool
    read_chunk(std::string_view resource, uint32_t position, std::span<uint8_t> output) const;

    /// Enumerates a directory in the order the game lists it.
    ///
    /// Loose matches come first in NTFS name order, led by "." and "..", then
    /// each mount's records in stored order. Archive records shadowed by a
    /// loose file are skipped; duplicates across archives are all reported.
    ///
    /// @param pattern "dir\\*.ext"-style pattern; "*.*" matches everything
    /// @param scope where the enumeration starts and whether it continues into mounts
    /// @return every match with its source
    [[nodiscard]] std::vector<FoundEntry>
    find(std::string_view pattern, FindScope scope = {}) const;
    /// Collects files under a directory that match a pattern, recursively.
    ///
    /// Each subdirectory is rescanned only in the source that reported it.
    ///
    /// @param directory directory scanned; empty yields nothing
    /// @param pattern wildcard matched against file names
    /// @param scope where the top-level enumeration starts
    /// @return '\\'-joined paths in enumeration order
    [[nodiscard]] std::vector<std::string> scan_recursive(
        std::string_view directory, std::string_view pattern, FindScope scope = {}
    ) const;
    /// Counts the entries of a find other than "." and "..".
    ///
    /// @param pattern "dir\\*.ext"-style pattern
    /// @param directories_only true to count only directories
    /// @return the count
    [[nodiscard]] int count_entries(std::string_view pattern, bool directories_only) const;

    /// Lists the effective resource names in a directory, sorted.
    ///
    /// Same loose/mount precedence as read(); names are ASCII case-folded.
    /// This is not the order the game lists a directory in.
    ///
    /// @param directory '/'-separated directory, relative
    /// @param extension required suffix, or empty for any
    /// @return the names, each once
    [[nodiscard]] std::vector<std::string>
    list_effective(std::string_view directory, std::string_view extension) const;
    /// Lists the effective resource names in a directory in mount order.
    ///
    /// Loose files come in host directory order, then mounts in insertion
    /// order with archive entries in stored order; the first occurrence of a
    /// path wins. Host loose-file order cannot reproduce the order Windows
    /// lists loose files in.
    ///
    /// @param directory '/'-separated directory, relative
    /// @param extension required suffix, or empty for any
    /// @return the names, ASCII case-folded, each once
    [[nodiscard]] std::vector<std::string>
    list_effective_in_mount_order(std::string_view directory, std::string_view extension) const;
    /// Lists the effective resource names under a directory, recursively, in mount order.
    ///
    /// Feature loading relies on this order: the first document containing a
    /// requested section wins.
    ///
    /// @param directory '/'-separated directory, relative
    /// @param extension required suffix, or empty for any
    /// @return the names, ASCII case-folded, each once
    [[nodiscard]] std::vector<std::string>
    list_effective_recursive(std::string_view directory, std::string_view extension) const;

    /// Opens a loose file or else the first archive holding that path as a file, for reading.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @return a handle to release with close(), or nullptr when the resource is absent
    [[nodiscard]] ResourceFile* open(std::string_view resource) const;
    /// Releases a handle and its cached block.
    ///
    /// @param file handle from open(), or null
    static void close(ResourceFile* file) noexcept;
    /// Returns whether a handle reads from an archive rather than a loose file.
    ///
    /// @param file open handle
    /// @return true for an archive entry
    [[nodiscard]] static bool archived(const ResourceFile* file) noexcept;
    /// Moves a handle's read position.
    ///
    /// Crossing into another 64 KiB block drops an archive handle's cached block.
    ///
    /// @param[in,out] file open handle
    /// @param position new position, or 0xFFFFFFFF for the end
    /// @return the new position for a loose file, 0 for an archive entry, or -1 on failure
    static int32_t seek(ResourceFile* file, uint32_t position);
    /// Returns a handle's read position.
    ///
    /// @param file open handle
    /// @return the position in bytes
    [[nodiscard]] static int32_t tell(const ResourceFile* file);
    /// Returns the length of an open resource.
    ///
    /// @param file open handle
    /// @return the length in bytes
    [[nodiscard]] static uint32_t length(const ResourceFile* file);
    /// Reads from a handle's position, decoding at most one cached block at a time.
    ///
    /// @param[in,out] file open handle; its position advances by the count read
    /// @param[out] output receives up to output.size() bytes
    /// @return the count read, 0 at the end, or -1 when a chunk cannot be read
    static int32_t read(ResourceFile* file, std::span<uint8_t> output);

  private:

    /// Lists resource names under a directory with read()'s precedence.
    ///
    /// Throws std::runtime_error for an invalid or traversing path or a loose
    /// case collision.
    ///
    /// @param directory '/'-separated directory, relative
    /// @param extension required suffix, or empty for any
    /// @param recursive true to include subdirectories
    /// @return the names, loose files first, each once
    [[nodiscard]] std::vector<std::string>
    list_resources(std::string_view directory, std::string_view extension, bool recursive) const;
    /// Resolves a resource to a loose file, ignoring ASCII case.
    ///
    /// Throws std::runtime_error for an absolute, drive-qualified, traversing
    /// or overlong path, or an ambiguous case collision.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @return the host path, or nullopt when no loose file exists
    [[nodiscard]] std::optional<std::filesystem::path> loose_path(std::string_view resource) const;

    struct ArchivedNode {
        std::size_t mount{};
        uint32_t node{};
    };

    /// Finds the first mount holding a resource as a file.
    ///
    /// A directory of that name sends the search on to the next mount.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @param skipped mount path to pass over, or null
    /// @return the mount and node, or nullopt
    [[nodiscard]] std::optional<ArchivedNode>
    archived_node(std::string_view resource, const std::filesystem::path* skipped) const;
    /// Reads a resource from a loose file or the mounts, optionally skipping one mount.
    ///
    /// @param resource '\\'- or '/'-separated path
    /// @param skipped mount path to pass over, or null
    /// @return the bytes and their source; throws when nothing provides it
    [[nodiscard]] AssetData
    read_skipping(std::string_view resource, const std::filesystem::path* skipped) const;
    /// Tests whether an archive path is already mounted.
    ///
    /// @param archive host path compared by case-insensitive canonical full path
    /// @return true when mounted
    [[nodiscard]] bool is_mounted(const std::filesystem::path& archive) const;
    /// Drops mounts whose archive file can no longer be opened.
    void drop_vanished_mounts();
    /// Walks the loose tree, marking each archive file a loose file hides.
    ///
    /// @param prefix resource path of directory, with a trailing '\\'
    /// @param directory host directory walked
    void mark_loose_directory(const std::string& prefix, const std::filesystem::path& directory);
    /// Continues an enumeration through the archive records of each mount.
    ///
    /// @param[in,out] found matches appended in mount and stored order
    /// @param directory directory part of the pattern, with its trailing separator
    /// @param spec wildcard for the names
    /// @param scope first mount searched and whether later mounts follow
    void find_in_mounts(
        std::vector<FoundEntry>& found,
        std::string_view directory,
        std::string_view spec,
        FindScope scope
    ) const;

    struct Mount {
        std::filesystem::path path;
        HpiArchive archive;
        // Per-node mount-time marks (loose shadowing), indexed like nodes().
        std::vector<uint8_t> marks;
    };

    std::filesystem::path loose_root_;
    std::vector<Mount> mounts_;
    std::vector<std::filesystem::path> mount_paths_;
};

/// Writes a loose file, replacing any previous content.
///
/// @param path host path of the file
/// @param bytes content written
/// @return the count written, 0 when the write fails, or -1 when the file cannot be opened
int32_t write_loose_file(const std::filesystem::path& path, std::span<const uint8_t> bytes);
/// Creates each directory along a '\\'- or '/'-separated path, ignoring failures.
///
/// @param path host directory path; the last component is created too
void create_directory_path(const std::filesystem::path& path);

inline constexpr std::size_t palette_color_count = 256;
inline constexpr std::size_t palette_entry_bytes = 4;
using PaletteBytes = std::array<uint8_t, palette_color_count * palette_entry_bytes>;
using PaletteMap = std::array<uint8_t, palette_color_count>;

struct Image {
    uint32_t width{};
    uint32_t height{};
    std::vector<uint8_t> rgb;
    // Indexed source palette, expanded RGB+zero fourth byte for the game's GUI
    // color remapping. Absent for three-plane true-color images.
    std::optional<PaletteBytes> palette{};
    // 8-bit source indices for paletted PCX (empty for true-color images).
    std::vector<uint8_t> indices{};
};

/// Maps each source colour to the nearest destination colour.
///
/// Nearest means the smallest summed absolute channel difference; the first
/// index wins ties and byte 3 of each entry is ignored.
///
/// @param source palette whose entries are mapped
/// @param destination palette searched
/// @return the destination index for each source index
[[nodiscard]] PaletteMap remap_palette(const PaletteBytes& source, const PaletteBytes& destination);

/// Decodes an 8-bit indexed or 24-bit three-plane PCX image.
///
/// Throws std::runtime_error for an unsupported layout, a missing 256-colour
/// palette, invalid RLE or an image over the 64-megapixel limit.
///
/// @param data the whole file
/// @return RGB pixels, plus indices and the palette for an indexed image
[[nodiscard]] Image decode_pcx(std::span<const uint8_t> data);

} // namespace oa
