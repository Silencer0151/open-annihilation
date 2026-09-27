// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/hpi.hpp"

#include "oa/formats/sqsh.hpp"

#include <zlib.h>

#include <algorithm>
#include <stdexcept>

namespace oa {
namespace {

constexpr uint8_t kSquashWriterVersion = 2;
constexpr std::size_t kLz77Slack = 64;

void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    for (unsigned i = 0; i < 4; ++i)
        bytes[at + i] = static_cast<uint8_t>(value >> (8U * i));
}

void append32(std::vector<uint8_t>& bytes, uint32_t value) {
    bytes.resize(bytes.size() + 4);
    put32(bytes, bytes.size() - 4, value);
}

bool same_name(std::string_view a, std::string_view b) {
    return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
               const auto fold = [](char c) {
                   return c >= 'a' && c <= 'z' ? static_cast<char>(c - ('a' - 'A')) : c;
               };
               return fold(x) == fold(y);
           });
}

struct TreeNode {
    std::string name;
    bool directory = true;
    std::size_t file = 0;
    std::vector<TreeNode> children;
};

TreeNode build_tree(std::span<const HpiWriteFile> files) {
    TreeNode root;
    for (std::size_t index = 0; index < files.size(); ++index) {
        const std::string& path = files[index].path;
        TreeNode* directory = &root;
        std::size_t begin = 0;
        for (;;) {
            const auto end = path.find_first_of("/\\", begin);
            const std::string part =
                path.substr(begin, end == std::string::npos ? end : end - begin);
            if (part.empty())
                throw std::invalid_argument("HPI path has an empty segment: " + path);
            if (end == std::string::npos) {
                directory->children.push_back(TreeNode{part, false, index, {}});
                break;
            }
            auto found = std::find_if(
                directory->children.begin(), directory->children.end(), [&](const TreeNode& child) {
                    return child.directory && same_name(child.name, part);
                }
            );
            if (found == directory->children.end()) {
                directory->children.push_back(TreeNode{part, true, 0, {}});
                found = directory->children.end() - 1;
            }
            directory = &*found;
            begin = end + 1;
        }
    }
    return root;
}

std::vector<uint8_t> squash_chunk(std::span<const uint8_t> input, uint8_t method, bool scramble) {
    std::vector<uint8_t> payload;
    if (method == formats::hpi::CompressionLZ77) {
        payload = formats::sqsh::encode_lz77(input, input.size() * 2 + kLz77Slack);
    } else if (method == formats::hpi::CompressionZLib) {
        uLongf size = compressBound(static_cast<uLong>(input.size()));
        payload.resize(size);
        if (compress2(
                payload.data(),
                &size,
                input.data(),
                static_cast<uLong>(input.size()),
                Z_DEFAULT_COMPRESSION
            ) != Z_OK)
            throw std::runtime_error("zlib compression failed");
        payload.resize(size);
    } else {
        throw std::invalid_argument("unsupported HPI chunk compression");
    }
    if (scramble)
        formats::sqsh::encrypt_chunk(payload);
    std::vector<uint8_t> chunk(formats::hpi::SQSHHeaderSize);
    put32(chunk, 0, formats::hpi::ChunkMarker);
    chunk[4] = kSquashWriterVersion;
    chunk[5] = method;
    chunk[6] = scramble ? 1 : 0;
    put32(chunk, 7, static_cast<uint32_t>(payload.size()));
    put32(chunk, 11, static_cast<uint32_t>(input.size()));
    put32(chunk, 15, formats::sqsh::chunk_checksum(payload));
    chunk.insert(chunk.end(), payload.begin(), payload.end());
    return chunk;
}

std::vector<uint8_t> encode_entry(const HpiWriteFile& file, bool scramble) {
    if (file.compression == formats::hpi::CompressionNone)
        return file.bytes;
    std::vector<std::vector<uint8_t>> chunks;
    for (std::size_t at = 0; at < file.bytes.size(); at += formats::hpi::BlockBytes) {
        const std::size_t size =
            std::min<std::size_t>(formats::hpi::BlockBytes, file.bytes.size() - at);
        chunks.push_back(
            squash_chunk(std::span(file.bytes).subspan(at, size), file.compression, scramble)
        );
    }
    std::vector<uint8_t> stream;
    for (const auto& chunk : chunks)
        append32(stream, static_cast<uint32_t>(chunk.size()));
    for (const auto& chunk : chunks)
        stream.insert(stream.end(), chunk.begin(), chunk.end());
    return stream;
}

std::size_t directory_bytes(const TreeNode& directory) {
    std::size_t size = sizeof(formats::hpi::DirectoryNode) +
                       directory.children.size() * formats::hpi::DirectoryEntrySize;
    for (const auto& child : directory.children)
        size += child.name.size() + 1 +
                (child.directory ? directory_bytes(child) : formats::hpi::FileEntrySize);
    return size;
}

struct Emitter {
    std::vector<uint8_t>& out;
    const std::vector<uint32_t>& data_offsets;
    std::span<const HpiWriteFile> files;

    void directory(const TreeNode& node) {
        const std::size_t at = out.size();
        const std::size_t list = at + sizeof(formats::hpi::DirectoryNode);
        out.resize(list + node.children.size() * formats::hpi::DirectoryEntrySize);
        put32(out, at, static_cast<uint32_t>(node.children.size()));
        put32(out, at + 4, static_cast<uint32_t>(list));
        for (std::size_t i = 0; i < node.children.size(); ++i) {
            const TreeNode& child = node.children[i];
            const std::size_t entry = list + i * formats::hpi::DirectoryEntrySize;
            put32(out, entry, static_cast<uint32_t>(out.size()));
            out.insert(out.end(), child.name.begin(), child.name.end());
            out.push_back(0);
            put32(out, entry + 4, static_cast<uint32_t>(out.size()));
            out[entry + 8] = child.directory ? formats::hpi::EntryFlagDirectory : 0;
            if (child.directory) {
                directory(child);
                continue;
            }
            const std::size_t record = out.size();
            out.resize(record + formats::hpi::FileEntrySize);
            put32(out, record, data_offsets[child.file]);
            put32(out, record + 4, static_cast<uint32_t>(files[child.file].bytes.size()));
            out[record + 8] = files[child.file].compression;
        }
    }
};

} // namespace

std::vector<uint8_t>
write_hpi(std::span<const HpiWriteFile> files, const HpiWriteOptions& options) {
    if (options.trailer_year.size() != formats::hpi::TrailerYearBytes)
        throw std::invalid_argument("HPI trailer year must have four characters");
    const TreeNode root = build_tree(files);
    const std::size_t directory_end = formats::hpi::HeaderSize + directory_bytes(root);

    std::vector<std::vector<uint8_t>> encoded;
    std::vector<uint32_t> data_offsets;
    std::size_t cursor = directory_end;
    for (const auto& file : files) {
        encoded.push_back(encode_entry(file, options.scramble_chunks));
        data_offsets.push_back(static_cast<uint32_t>(cursor));
        cursor += encoded.back().size();
    }

    std::vector<uint8_t> out(formats::hpi::HeaderSize);
    put32(out, 0, formats::hpi::HeaderMarker);
    put32(out, 4, formats::hpi::VersionV1);
    put32(out, 8, static_cast<uint32_t>(directory_end));
    put32(out, 12, options.header_key);
    put32(out, 16, formats::hpi::HeaderSize);
    Emitter{out, data_offsets, files}.directory(root);
    for (const auto& data : encoded)
        out.insert(out.end(), data.begin(), data.end());

    // Stored = ~(plain ^ position ^ key), the inverse of the reader transform.
    const uint8_t key = hpi_archive_key(options.header_key);
    if (key != 0)
        for (std::size_t i = formats::hpi::HeaderSize; i < out.size(); ++i)
            out[i] = static_cast<uint8_t>(~(out[i] ^ static_cast<uint8_t>(i) ^ key));

    std::string trailer(formats::hpi::Trailer);
    trailer.replace(
        formats::hpi::TrailerYearOffset, formats::hpi::TrailerYearBytes, options.trailer_year
    );
    out.insert(out.end(), trailer.begin(), trailer.end());
    return out;
}

} // namespace oa
