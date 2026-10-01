// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/smacker.hpp"

#include <algorithm>
#include <bit>
#include <cstdint>
#include <fstream>
#include <limits>
#include <type_traits>
#include <utility>

namespace oa::formats::smacker {
namespace {

static_assert(std::endian::native == std::endian::little, "SMK2 records are little-endian");
static_assert(std::is_trivially_copyable_v<on_disk::FileHeader>);
static_assert(std::is_trivially_copyable_v<on_disk::FrameSize>);

bool read_exact(std::ifstream& file, void* destination, std::size_t bytes) {
    return bytes == 0 ||
           static_cast<bool>(
               file.read(static_cast<char*>(destination), static_cast<std::streamsize>(bytes))
           );
}

OpenResult failure(const char* message) {
    return OpenResult{std::nullopt, message};
}

} // namespace

double Header::frame_rate_hz() const noexcept {
    if (frame_rate < 0) {
        const auto magnitude = static_cast<uint32_t>(-(frame_rate + 1)) + 1U;
        return magnitude == 0 ? 0.0 : static_cast<double>(kNegativeRateScale) / magnitude;
    }
    if (frame_rate > 0)
        return static_cast<double>(kPositiveRateScale) / frame_rate;
    return 0.0;
}

bool Header::has_audio() const noexcept {
    return std::any_of(audio.begin(), audio.end(), [](const AudioTrack& track) {
        return track.present();
    });
}

SmackerReader::SmackerReader(SmackerReader&&) noexcept = default;
SmackerReader& SmackerReader::operator=(SmackerReader&&) noexcept = default;
SmackerReader::~SmackerReader() = default;

OpenResult SmackerReader::open(const std::filesystem::path& path, const Limits& limits) {
    std::error_code status;
    const auto file_size = std::filesystem::file_size(path, status);
    if (status)
        return failure("cannot stat Smacker resource");
    if (file_size < kSmackerFixedHeaderBytes || file_size > limits.max_file_bytes)
        return failure("Smacker resource size is outside configured bounds");
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return failure("cannot open Smacker resource");

    on_disk::FileHeader disk{};
    if (!read_exact(file, &disk, sizeof(disk)))
        return failure("Smacker fixed header is truncated");
    SmackerReader result;
    result.path_ = path;
    result.limits_ = limits;
    auto& header = result.header_;
    header.signature = disk.signature;
    if (header.signature != kSmk2)
        return failure("unsupported Smacker signature (target requires SMK2)");
    header.width = disk.width;
    header.height = disk.height;
    header.frame_count = disk.frames;
    header.frame_rate = disk.frame_rate;
    header.flags = disk.flags;
    if (header.width == 0 || header.height == 0 || header.width > limits.max_width ||
        header.height > limits.max_height || header.frame_count == 0 ||
        header.frame_count > limits.max_frames)
        return failure("Smacker geometry or frame count is outside bounds");
    for (std::size_t i = 0; i < header.audio.size(); ++i) {
        header.audio[i].compressed_size = disk.audio_size[i];
        header.audio[i].packed_rate_flags = disk.audio_rate[i];
    }
    header.trees_size = disk.trees_size;
    header.mmap_size = disk.mmap_size;
    header.mclr_size = disk.mclr_size;
    header.full_size = disk.full_size;
    header.type_size = disk.type_size;
    if (header.trees_size > limits.max_tree_bytes)
        return failure("Smacker Huffman tree data exceeds bound");

    result.table_frame_count_ =
        header.frame_count + ((header.flags & kRingFrameFlag) != 0U ? 1U : 0U);
    if (result.table_frame_count_ < header.frame_count ||
        static_cast<uint64_t>(result.table_frame_count_) >
            static_cast<uint64_t>(limits.max_frames) + 1U)
        return failure("Smacker frame table count exceeds bounds");
    const uint64_t frame_table_bytes =
        static_cast<uint64_t>(result.table_frame_count_) * kFrameSizeEntryBytes;
    const uint64_t type_table_offset = kSmackerFixedHeaderBytes + frame_table_bytes;
    const uint64_t tree_offset =
        type_table_offset + static_cast<uint64_t>(result.table_frame_count_) * kFrameTypeEntryBytes;
    const uint64_t data_offset = tree_offset + header.trees_size;
    if (data_offset > file_size)
        return failure("Smacker tables exceed file");
    file.seekg(static_cast<std::streamoff>(kSmackerFixedHeaderBytes), std::ios::beg);
    result.frame_sizes_.resize(result.table_frame_count_);
    if (!read_exact(
            file,
            result.frame_sizes_.data(),
            result.frame_sizes_.size() * sizeof(on_disk::FrameSize)
        ))
        return failure("Smacker frame-size table is truncated");
    result.frame_types_.resize(result.table_frame_count_);
    if (!read_exact(file, result.frame_types_.data(), result.frame_types_.size()))
        return failure("Smacker frame-type table is truncated");
    file.seekg(static_cast<std::streamoff>(tree_offset), std::ios::beg);
    uint64_t compressed_bytes = 0;
    result.frame_offsets_.resize(result.table_frame_count_);
    for (uint32_t index = 0; index < result.table_frame_count_; ++index) {
        const auto size = result.frame_sizes_[index].payload_bytes();
        if (size > limits.max_frame_bytes ||
            compressed_bytes > std::numeric_limits<uint64_t>::max() - size)
            return failure("Smacker frame payload exceeds bounds");
        result.frame_offsets_[index] = data_offset + compressed_bytes;
        compressed_bytes += size;
    }
    if (data_offset + compressed_bytes > file_size)
        return failure("Smacker frame payloads exceed file");
    result.frame_data_offset_ = data_offset;
    result.frame_data_bytes_ = compressed_bytes;
    return OpenResult{std::move(result), {}};
}

std::optional<Frame> SmackerReader::frame(uint32_t index) const noexcept {
    if (index >= header_.frame_count)
        return std::nullopt;
    return Frame{
        index, frame_offsets_[index], frame_sizes_[index].payload_bytes(), frame_types_[index]
    };
}

bool SmackerReader::read_frame(
    uint32_t index, std::vector<uint8_t>& output, std::string& error
) const {
    const auto selected = frame(index);
    if (!selected) {
        error = "Smacker frame index is outside the table";
        return false;
    }
    output.resize(selected->compressed_size);
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        error = "cannot reopen Smacker resource";
        return false;
    }
    file.seekg(static_cast<std::streamoff>(selected->file_offset), std::ios::beg);
    if (!read_exact(file, output.data(), output.size())) {
        output.clear();
        error = "Smacker frame payload is truncated";
        return false;
    }
    return true;
}

bool SmackerReader::read_huffman_trees(std::vector<uint8_t>& output, std::string& error) const {
    output.resize(header_.trees_size);
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        error = "cannot reopen Smacker resource";
        return false;
    }
    const auto offset = kSmackerFixedHeaderBytes +
                        static_cast<uint64_t>(table_frame_count_) * kFrameTableEntryBytes;
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!read_exact(file, output.data(), output.size())) {
        output.clear();
        error = "Smacker Huffman trees are truncated";
        return false;
    }
    return true;
}

} // namespace oa::formats::smacker
