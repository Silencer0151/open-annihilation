// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include "oa/formats/sqsh.hpp"

#include <zlib.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <exception>
#include <fstream>
#include <limits>
#include <mutex>
#include <stdexcept>
#include <unordered_set>

namespace oa {
namespace {

constexpr uint64_t kMaxDirectorySize = 256ULL << 20;
constexpr uint32_t kMaxStoredChunkSize = 1U << 20;
constexpr std::size_t kMaxEntries = 1'000'000;
constexpr std::size_t kMaxDirectoryDepth = 128;
constexpr std::size_t kWildcardStates = 100;
constexpr uint8_t kSquashTypeLimit = 4;
constexpr uint8_t kSquashLz77 = 1;
constexpr uint8_t kSquashZlib = 2;

namespace squash_field {
constexpr std::size_t type = 0x5, scrambled = 0x6, packed_size = 0x7, unpacked_size = 0xb,
                      checksum = 0xf;
} // namespace squash_field

constexpr const char* kSquashNames[] = {
    "SQUASHERR_OK",
    "SQUASHERR_BADHEADER",
    "SQUASHERR_BADCHECKSUM",
    "SQUASHERR_BADUNPACKSIZE",
    "SQUASHERR_BADUNPACKTYPE",
    "SQUASHERR_BADPACKTYPE",
    "SQUASHERR_BADPARAMS",
};

[[noreturn]] void fail(const std::string& message) {
    throw std::runtime_error(message);
}

uint32_t le32(const uint8_t* bytes) noexcept {
    return static_cast<uint32_t>(bytes[0]) | (static_cast<uint32_t>(bytes[1]) << 8U) |
           (static_cast<uint32_t>(bytes[2]) << 16U) | (static_cast<uint32_t>(bytes[3]) << 24U);
}

char ascii_upper(char value) noexcept {
    return value >= 'a' && value <= 'z' ? static_cast<char>(value - ('a' - 'A')) : value;
}

bool equal_nocase(std::string_view a, std::string_view b) noexcept {
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (ascii_upper(a[i]) != ascii_upper(b[i]))
            return false;
    return true;
}

// Archive data transform `out = position ^ ~in ^ key`; a zero key is a no-op.
void decrypt_at(std::span<uint8_t> bytes, uint8_t key, uint64_t position) noexcept {
    if (key == 0)
        return;
    for (std::size_t i = 0; i < bytes.size(); ++i)
        bytes[i] = static_cast<uint8_t>(
            static_cast<uint8_t>(position + i) ^ static_cast<uint8_t>(~bytes[i]) ^ key
        );
}

uint64_t file_size_of(const std::filesystem::path& path) {
    std::error_code error;
    const auto size = std::filesystem::file_size(path, error);
    if (error)
        fail("cannot stat HPI archive '" + path.string() + "': " + error.message());
    return size;
}

// Reads up to `length` bytes at `offset`; a read past the end is short.
std::size_t read_at(std::ifstream& stream, uint64_t offset, std::span<uint8_t> output) {
    stream.clear();
    stream.seekg(static_cast<std::streamoff>(offset));
    if (!stream)
        return 0;
    stream.read(
        reinterpret_cast<char*>(output.data()), static_cast<std::streamsize>(output.size())
    );
    return static_cast<std::size_t>(stream.gcount());
}

std::ifstream open_stream(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream)
        fail("cannot open HPI archive '" + path.string() + "'");
    return stream;
}

} // namespace

uint8_t hpi_archive_key(uint8_t header_key) noexcept {
    if (header_key == 0)
        return 0;
    const auto promoted = static_cast<uint32_t>(header_key);
    return static_cast<uint8_t>(~((promoted >> 6U) | (promoted << 2U)));
}

int uncompress_legacy(
    std::span<uint8_t> output, uint32_t* length, std::span<const uint8_t> input
) noexcept {
    z_stream stream{};
    stream.next_in = const_cast<Bytef*>(input.data());
    stream.avail_in = static_cast<uInt>(input.size());
    stream.next_out = output.data();
    stream.avail_out = static_cast<uInt>(std::min<std::size_t>(*length, output.size()));
    int status = inflateInit(&stream);
    if (status != Z_OK)
        return status;
    status = inflate(&stream, Z_FINISH);
    if (status != Z_STREAM_END) {
        inflateEnd(&stream);
        return status;
    }
    *length = static_cast<uint32_t>(stream.total_out);
    return inflateEnd(&stream);
}

formats::hpi::SquashStatus
unsquash_archive_block(std::span<uint8_t> output, std::span<uint8_t> block) noexcept {
    using formats::hpi::SquashStatus;
    if (block.size() < formats::hpi::SQSHHeaderSize ||
        le32(block.data()) != formats::hpi::ChunkMarker)
        return SquashStatus::bad_header;
    const uint8_t type = block[squash_field::type];
    if (type >= kSquashTypeLimit)
        return SquashStatus::bad_unpack_type;
    const uint32_t packed = le32(block.data() + squash_field::packed_size);
    const uint32_t unpacked = le32(block.data() + squash_field::unpacked_size);
    // A packed size past the chunk is reported as a checksum failure.
    if (packed > block.size() - formats::hpi::SQSHHeaderSize)
        return SquashStatus::bad_checksum;
    const auto payload = block.subspan(formats::hpi::SQSHHeaderSize, packed);
    if (formats::sqsh::chunk_checksum(payload) != le32(block.data() + squash_field::checksum))
        return SquashStatus::bad_checksum;
    if (block[squash_field::scrambled] != 0)
        formats::sqsh::decrypt_chunk(payload);
    if (unpacked > output.size())
        return SquashStatus::bad_params;
    uint32_t produced = 0;
    if (type == kSquashLz77) {
        try {
            const auto decoded = formats::sqsh::decode_lz77(payload, output.size());
            std::copy(decoded.begin(), decoded.end(), output.begin());
            produced = static_cast<uint32_t>(decoded.size());
        } catch (const std::exception&) {
            return SquashStatus::bad_unpack_size;
        }
    } else if (type == kSquashZlib) {
        produced = unpacked;
        (void)uncompress_legacy(output, &produced, payload);
    } else {
        return SquashStatus::bad_unpack_size;
    }
    return produced == unpacked ? SquashStatus::ok : SquashStatus::bad_unpack_size;
}

const char* squash_status_name(formats::hpi::SquashStatus status) noexcept {
    const auto index = static_cast<std::size_t>(status);
    return index < std::size(kSquashNames) ? kSquashNames[index] : nullptr;
}

bool match_wildcard(std::string_view name, std::string_view pattern) noexcept {
    const auto pattern_at = [&](int index) {
        return index >= 0 && static_cast<std::size_t>(index) < pattern.size()
                   ? pattern[static_cast<std::size_t>(index)]
                   : '\0';
    };
    std::array<int, kWildcardStates> states{};
    int count = 1;
    for (const char raw : name) {
        const char folded = ascii_upper(raw);
        if (folded == '\0')
            break;
        int seen = 0;
        int read = 0;
        int write = count;
        // States appended for a '*' are visited within the same character.
        while (seen < count) {
            const int at = states[static_cast<std::size_t>(read)];
            const char wanted = ascii_upper(pattern_at(at));
            if (wanted == '?' || wanted == folded) {
                states[static_cast<std::size_t>(read)] = at + 1;
            } else if (wanted == '*') {
                if (count < static_cast<int>(kWildcardStates)) {
                    ++count;
                    states[static_cast<std::size_t>(write++)] = at + 1;
                }
            } else {
                --count;
                --write;
                if (count == 0)
                    return false;
                --seen;
                states[static_cast<std::size_t>(read)] = states[static_cast<std::size_t>(write)];
                --read;
            }
            ++seen;
            ++read;
        }
    }
    for (int i = 0; i < count; ++i) {
        const int at = states[static_cast<std::size_t>(i)];
        if (pattern_at(at) == '\0' || (pattern_at(at) == '*' && pattern_at(at + 1) == '\0'))
            return true;
    }
    return false;
}

struct HpiArchive::Impl {
    std::filesystem::path path;
    uint64_t archive_size{};
    uint8_t key{};
    std::vector<ArchiveNode> nodes;
    // Kept open while the archive is mounted; reads are serialised.
    mutable std::ifstream stream;
    mutable std::mutex stream_lock;

    explicit Impl(const std::filesystem::path& archive_path) : path(archive_path) {
        archive_size = file_size_of(path);
        stream = open_stream(path);
        std::array<uint8_t, formats::hpi::HeaderSize> header{};
        if (read_at(stream, 0, header) != header.size() ||
            le32(header.data()) != formats::hpi::HeaderMarker)
            fail("invalid HPI marker");
        if (le32(header.data() + offsetof(formats::hpi::Header, version)) !=
            formats::hpi::VersionV1)
            fail("unsupported HPI version");
        check_trailer(stream);

        const uint32_t block_size =
            le32(header.data() + offsetof(formats::hpi::Header, directory_size));
        if (block_size < formats::hpi::HeaderSize || block_size > archive_size)
            fail("invalid HPI directory bounds");
        if (block_size > kMaxDirectorySize)
            fail("HPI directory exceeds the 256 MiB safety limit");
        std::vector<uint8_t> block(block_size);
        if (read_at(stream, 0, block) != block.size())
            fail("truncated HPI directory");
        key = hpi_archive_key(header[offsetof(formats::hpi::Header, decrypt_key)]);
        decrypt_at(
            std::span(block).subspan(formats::hpi::HeaderSize), key, formats::hpi::HeaderSize
        );

        const uint32_t root = le32(header.data() + offsetof(formats::hpi::Header, offset));
        if (root < formats::hpi::HeaderSize)
            fail("HPI root directory overlaps the header");
        nodes.push_back(ArchiveNode{"", formats::hpi::EntryFlagDirectory});
        std::unordered_set<uint32_t> visited;
        parse_directory(block, root, 0, visited, 0);
    }

    // The last 36 bytes must equal Trailer, apart from its four year characters.
    void check_trailer(std::ifstream& stream) const {
        if (archive_size < formats::hpi::Trailer.size())
            fail("HPI archive has no copyright trailer");
        std::array<uint8_t, formats::hpi::Trailer.size()> tail{};
        if (read_at(stream, archive_size - tail.size(), tail) != tail.size())
            fail("HPI archive has no copyright trailer");
        std::copy_n(
            formats::hpi::Trailer.begin() + formats::hpi::TrailerYearOffset,
            formats::hpi::TrailerYearBytes,
            tail.begin() + formats::hpi::TrailerYearOffset
        );
        if (!std::equal(tail.begin(), tail.end(), formats::hpi::Trailer.begin()))
            fail("HPI archive has no copyright trailer");
    }

    static std::string name_at(std::span<const uint8_t> block, uint32_t offset) {
        if (offset >= block.size())
            fail("HPI entry name lies outside the directory");
        const auto begin = block.begin() + offset;
        const auto end = std::find(begin, block.end(), uint8_t{0});
        if (end == block.end())
            fail("unterminated HPI entry name");
        return std::string(begin, end);
    }

    /// Resolves one directory node and its subtree into nodes.
    ///
    /// Children are appended as one contiguous run, then subdirectories are
    /// resolved depth-first. Throws std::runtime_error for a node or name
    /// outside the directory block, a cycle or reused node, too many entries
    /// or nesting past the depth limit.
    ///
    /// @param block decrypted directory block, header included
    /// @param offset block offset of the directory node (count, list offset)
    /// @param owner index of the node that receives the children
    /// @param[in,out] visited directory offsets already resolved
    /// @param depth nesting depth of this directory
    /// @quirk A negative count is an empty directory, as in 3.1c.
    void parse_directory(
        std::span<const uint8_t> block,
        uint32_t offset,
        uint32_t owner,
        std::unordered_set<uint32_t>& visited,
        std::size_t depth
    ) {
        if (depth > kMaxDirectoryDepth)
            fail("HPI directory nesting exceeds the safety limit");
        // A node reached twice is a cycle or a shared node; both are rejected.
        if (!visited.insert(offset).second)
            fail("HPI directory contains a cycle or reused node");
        if (offset > block.size() || block.size() - offset < sizeof(formats::hpi::DirectoryNode))
            fail("HPI directory node lies outside the directory");
        const auto raw_count = static_cast<int32_t>(le32(block.data() + offset));
        const uint32_t list = le32(block.data() + offset + 4);
        // A negative count is an empty directory.
        const uint32_t count = raw_count > 0 ? static_cast<uint32_t>(raw_count) : 0;
        if (count > kMaxEntries - nodes.size())
            fail("HPI archive contains too many entries");
        if (list > block.size() ||
            (block.size() - list) / formats::hpi::DirectoryEntrySize < static_cast<uint64_t>(count))
            fail("HPI directory entry list lies outside the directory");

        const auto first = static_cast<uint32_t>(nodes.size());
        nodes[owner].first_child = first;
        nodes[owner].child_count = count;
        nodes.resize(nodes.size() + count);
        for (uint32_t i = 0; i < count; ++i) {
            const uint8_t* entry = block.data() + list + i * formats::hpi::DirectoryEntrySize;
            ArchiveNode& node = nodes[first + i];
            node.name = name_at(block, le32(entry));
            node.flags = entry[offsetof(formats::hpi::DirectoryEntry, type)];
            const uint32_t data = le32(entry + offsetof(formats::hpi::DirectoryEntry, data_offset));
            if (node.directory()) {
                node.data_offset = data;
                continue;
            }
            if (data > block.size() || block.size() - data < formats::hpi::FileEntrySize)
                fail("HPI file record lies outside the directory");
            node.data_offset = le32(block.data() + data);
            node.size = le32(block.data() + data + offsetof(formats::hpi::FileEntry, size));
            node.compression = block[data + offsetof(formats::hpi::FileEntry, compression)];
        }
        for (uint32_t i = 0; i < count; ++i)
            if (nodes[first + i].directory())
                parse_directory(block, nodes[first + i].data_offset, first + i, visited, depth + 1);
    }

    [[nodiscard]] std::optional<uint32_t>
    walk(std::string_view path, bool stop_before_last) const noexcept {
        uint32_t directory = 0;
        std::size_t begin = 0;
        for (;;) {
            std::size_t end = begin;
            while (end < path.size() && path[end] != '\\' && path[end] != '/')
                ++end;
            const bool last = end == path.size();
            if (last && stop_before_last)
                return directory;
            const std::string_view segment = path.substr(begin, end - begin);
            const ArchiveNode& parent = nodes[directory];
            std::optional<uint32_t> match;
            for (uint32_t i = parent.child_count; i-- > 0;) {
                if (equal_nocase(nodes[parent.first_child + i].name, segment)) {
                    match = parent.first_child + i;
                    break;
                }
            }
            if (!match)
                return std::nullopt;
            if (last)
                return match;
            if (!nodes[*match].directory())
                return std::nullopt;
            directory = *match;
            begin = end + 1;
        }
    }

    /// Copies file node `index`'s decoded bytes from `position` into `output`.
    [[nodiscard]] int64_t
    read_range(uint32_t index, uint32_t position, std::span<uint8_t> output) const {
        if (index >= nodes.size() || nodes[index].directory())
            throw std::invalid_argument("HPI node is not a file");
        const ArchiveNode& node = nodes[index];
        if (position >= node.size || output.empty())
            return 0;
        const std::size_t wanted = std::min<std::size_t>(output.size(), node.size - position);
        const std::lock_guard guard(stream_lock);
        if (node.compression == 0) {
            const uint64_t at = static_cast<uint64_t>(node.data_offset) + position;
            const std::size_t got = read_at(stream, at, output.first(wanted));
            decrypt_at(output.first(got), key, at);
            return static_cast<int64_t>(got);
        }

        const uint32_t chunks =
            node.size / formats::hpi::BlockBytes + (node.size % formats::hpi::BlockBytes != 0);
        std::vector<uint8_t> table(static_cast<std::size_t>(chunks) * 4U);
        if (read_at(stream, node.data_offset, table) != table.size())
            return -1;
        decrypt_at(table, key, node.data_offset);

        std::vector<uint8_t> decoded(formats::hpi::BlockBytes);
        std::size_t copied = 0;
        uint32_t cursor = position;
        uint32_t loaded = std::numeric_limits<uint32_t>::max();
        while (copied < wanted) {
            const uint32_t block_index = cursor / formats::hpi::BlockBytes;
            if (block_index != loaded) {
                uint32_t at = node.data_offset + static_cast<uint32_t>(table.size());
                for (uint32_t i = 0; i < block_index; ++i)
                    at += le32(table.data() + i * 4U);
                const uint32_t stored = le32(table.data() + block_index * 4U);
                if (stored > kMaxStoredChunkSize)
                    fail("stored HPI chunk exceeds the 1 MiB safety limit");
                std::vector<uint8_t> chunk(stored);
                if (read_at(stream, at, chunk) != chunk.size())
                    return -1;
                decrypt_at(chunk, key, at);
                // Bytes a short chunk leaves unwritten read as zero.
                std::fill(decoded.begin(), decoded.end(), uint8_t{0});
                const auto status = unsquash_archive_block(decoded, chunk);
                if (status != formats::hpi::SquashStatus::ok)
                    fail(
                        "HPI decompression error " + std::string(squash_status_name(status)) +
                        " in block " + std::to_string(block_index) + " of " +
                        std::to_string(chunks) + " (" + node.name + ", length " +
                        std::to_string(node.size) + ", " + path.filename().string() + ")"
                    );
                loaded = block_index;
            }
            const uint32_t offset = cursor % formats::hpi::BlockBytes;
            const std::size_t take =
                std::min<std::size_t>(formats::hpi::BlockBytes - offset, wanted - copied);
            std::copy_n(
                decoded.begin() + offset, take, output.begin() + static_cast<std::ptrdiff_t>(copied)
            );
            copied += take;
            cursor += static_cast<uint32_t>(take);
        }
        return static_cast<int64_t>(copied);
    }
};

HpiArchive::HpiArchive(const std::filesystem::path& path) : impl_(std::make_unique<Impl>(path)) {
}

HpiArchive::~HpiArchive() = default;
HpiArchive::HpiArchive(HpiArchive&&) noexcept = default;
HpiArchive& HpiArchive::operator=(HpiArchive&&) noexcept = default;

const std::filesystem::path& HpiArchive::path() const noexcept {
    return impl_->path;
}

std::span<const ArchiveNode> HpiArchive::nodes() const noexcept {
    return impl_->nodes;
}

std::vector<ArchiveEntry> HpiArchive::entries() const {
    std::vector<ArchiveEntry> result;
    const auto visit =
        [&](const auto& self, uint32_t directory, const std::string& prefix) -> void {
        const ArchiveNode& parent = impl_->nodes[directory];
        for (uint32_t i = 0; i < parent.child_count; ++i) {
            const auto index = parent.first_child + i;
            const ArchiveNode& node = impl_->nodes[index];
            const std::string full = prefix.empty() ? node.name : prefix + "/" + node.name;
            if (node.directory())
                self(self, index, full);
            else
                result.push_back({full, node.size});
        }
    };
    visit(visit, 0, "");
    return result;
}

std::optional<uint32_t> HpiArchive::lookup(std::string_view path) const noexcept {
    return impl_->walk(path, false);
}

std::optional<uint32_t> HpiArchive::lookup_directory(std::string_view path) const noexcept {
    return impl_->walk(path, true);
}

int64_t
HpiArchive::read_node_range(uint32_t index, uint32_t position, std::span<uint8_t> output) const {
    return impl_->read_range(index, position, output);
}

std::vector<uint8_t> HpiArchive::read_node(uint32_t index) const {
    if (index >= impl_->nodes.size() || impl_->nodes[index].directory())
        throw std::invalid_argument("HPI node is not a file");
    std::vector<uint8_t> bytes(impl_->nodes[index].size);
    if (impl_->read_range(index, 0, bytes) < 0)
        fail("truncated HPI entry: " + impl_->nodes[index].name);
    return bytes;
}

std::vector<uint8_t> HpiArchive::read(std::string_view path) const {
    const auto index = lookup(path);
    if (!index || impl_->nodes[*index].directory())
        throw std::invalid_argument("HPI entry not found: " + std::string(path));
    return read_node(*index);
}

} // namespace oa
