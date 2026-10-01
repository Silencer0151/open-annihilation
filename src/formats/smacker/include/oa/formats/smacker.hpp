// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <optional>
#include <string>
#include <vector>

namespace oa::formats::smacker {

inline constexpr uint32_t kSmk2 = 0x324B4D53U; // bytes "SMK2"
inline constexpr uint32_t kSmk4 = 0x344B4D53U; // bytes "SMK4"
inline constexpr std::size_t kSmackerFixedHeaderBytes = 104;
inline constexpr std::size_t kSmackerAudioTrackCount = 7;
inline constexpr uint32_t kNegativeRateScale = 100000;
inline constexpr uint32_t kPositiveRateScale = 1000;
inline constexpr uint32_t kRingFrameFlag = 1U;
// Height modes 2 and 4 double the presented movie height; the remaining flag
// meanings are not assigned here.
inline constexpr uint32_t kHeightModeMask = 0x6U;
inline constexpr uint32_t kDoubleHeightModeTwo = 0x2U;
inline constexpr uint32_t kDoubleHeightModeFour = 0x4U;
// A track is present only when this flag of its packed rate word is set. The
// other high-byte bits describe the encoded sample format and remain
// intentionally unresolved here.
inline constexpr uint32_t kAudioTrackPresentFlag = 0x40000000U;
// A frame's payload size is its whole 32-bit table entry. Other SMK readers
// treat the low bits as flags; 3.1c movies are read without that rule.
inline constexpr uint32_t kFrameSizeBytesMask = 0xFFFFFFFFU;
// Packed audio-rate word: sample rate in the low 24 bits, codec flags above.
inline constexpr uint32_t kAudioSampleRateMask = 0x00FFFFFFU;
inline constexpr unsigned kAudioFormatFlagsShift = 24U;
// Frame-size table entry, then one frame-type byte. Together they stride the
// index that precedes the Huffman trees.
inline constexpr std::size_t kFrameSizeEntryBytes = 4;
inline constexpr std::size_t kFrameTypeEntryBytes = 1;
inline constexpr std::size_t kFrameTableEntryBytes = kFrameSizeEntryBytes + kFrameTypeEntryBytes;

// Little-endian SMK2 records. The trailing 32-bit word is unused; frame sizes
// begin at byte 104. Track flags live in the high byte of each AudioRate word,
// not in a second table of seven words.
namespace on_disk {

struct FileHeader {
    uint32_t signature = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frames = 0;
    int32_t frame_rate = 0;
    uint32_t flags = 0;
    std::array<uint32_t, kSmackerAudioTrackCount> audio_size{};
    uint32_t trees_size = 0;
    uint32_t mmap_size = 0;
    uint32_t mclr_size = 0;
    uint32_t full_size = 0;
    uint32_t type_size = 0;
    std::array<uint32_t, kSmackerAudioTrackCount> audio_rate{};
    uint32_t reserved = 0; // unused by the format; kept as read and not used
};

struct FrameSize {
    uint32_t size = 0;

    /// Returns the frame payload size: the whole table entry.
    [[nodiscard]] uint32_t payload_bytes() const noexcept { return size & kFrameSizeBytesMask; }
};

} // namespace on_disk

static_assert(sizeof(on_disk::FileHeader) == kSmackerFixedHeaderBytes);
static_assert(offsetof(on_disk::FileHeader, signature) == 0);
static_assert(offsetof(on_disk::FileHeader, width) == 4);
static_assert(offsetof(on_disk::FileHeader, height) == 8);
static_assert(offsetof(on_disk::FileHeader, frames) == 12);
static_assert(offsetof(on_disk::FileHeader, frame_rate) == 16);
static_assert(offsetof(on_disk::FileHeader, flags) == 20);
static_assert(offsetof(on_disk::FileHeader, audio_size) == 24);
static_assert(offsetof(on_disk::FileHeader, trees_size) == 52);
static_assert(offsetof(on_disk::FileHeader, mmap_size) == 56);
static_assert(offsetof(on_disk::FileHeader, mclr_size) == 60);
static_assert(offsetof(on_disk::FileHeader, full_size) == 64);
static_assert(offsetof(on_disk::FileHeader, type_size) == 68);
static_assert(offsetof(on_disk::FileHeader, audio_rate) == 72);
static_assert(offsetof(on_disk::FileHeader, reserved) == 100);
static_assert(
    sizeof(on_disk::FileHeader::audio_size) == kSmackerAudioTrackCount * sizeof(uint32_t)
);
static_assert(
    sizeof(on_disk::FileHeader::audio_rate) == kSmackerAudioTrackCount * sizeof(uint32_t)
);
static_assert(sizeof(on_disk::FrameSize) == kFrameSizeEntryBytes);
static_assert(kFrameTableEntryBytes == 5);

struct Limits {
    uintmax_t max_file_bytes = 512U * 1024U * 1024U;
    uint32_t max_width = 4096;
    uint32_t max_height = 4096;
    uint32_t max_frames = 100000;
    uint32_t max_tree_bytes = 64U * 1024U * 1024U;
    uint32_t max_frame_bytes = 16U * 1024U * 1024U;
};

struct AudioTrack {
    uint32_t compressed_size = 0;
    // Smacker stores sample rate in the low 24 bits and codec/channel flags in
    // the high byte. The individual flag meanings remain format-level data;
    // callers must not infer a PCM layout from them without a decoder.
    uint32_t packed_rate_flags = 0;

    /// Returns the sample rate in hertz: the low 24 bits of the packed word.
    [[nodiscard]] uint32_t sample_rate() const noexcept {
        return packed_rate_flags & kAudioSampleRateMask;
    }

    /// Returns the high byte of the packed word, whose bits describe the sample format.
    [[nodiscard]] uint8_t format_flags() const noexcept {
        return static_cast<uint8_t>(packed_rate_flags >> kAudioFormatFlagsShift);
    }

    /// Returns whether the track-present flag is set.
    [[nodiscard]] bool present() const noexcept {
        return (packed_rate_flags & kAudioTrackPresentFlag) != 0U;
    }
};

struct Header {
    uint32_t signature = 0;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t frame_count = 0;
    int32_t frame_rate = 0;
    uint32_t flags = 0;
    std::array<AudioTrack, kSmackerAudioTrackCount> audio{};
    uint32_t trees_size = 0;
    uint32_t mmap_size = 0;
    uint32_t mclr_size = 0;
    uint32_t full_size = 0;
    uint32_t type_size = 0;

    /// Returns the frame rate in frames per second.
    ///
    /// A negative value n means 100000 / -n (so -3333 is about 30 fps); a
    /// positive value is milliseconds per frame.
    ///
    /// @return the rate, or 0 for a zero value
    [[nodiscard]] double frame_rate_hz() const noexcept;
    /// Returns whether any audio track is present.
    ///
    /// @return true when a track has the present flag
    [[nodiscard]] bool has_audio() const noexcept;

    /// Returns whether the height mode doubles the displayed movie height.
    [[nodiscard]] bool uses_doubled_height() const noexcept {
        const auto mode = flags & kHeightModeMask;
        return mode == kDoubleHeightModeTwo || mode == kDoubleHeightModeFour;
    }
};

struct Frame {
    uint32_t index = 0;
    uint64_t file_offset = 0;
    uint32_t compressed_size = 0;
    uint8_t type = 0;
};

struct OpenResult;

class SmackerReader {
  public:

    /// Takes over another reader's header and tables.
    ///
    /// @param other reader left empty
    SmackerReader(SmackerReader&& other) noexcept;
    /// Takes over another reader's header and tables.
    ///
    /// @param other reader left empty
    /// @return this reader
    SmackerReader& operator=(SmackerReader&& other) noexcept;
    SmackerReader(const SmackerReader&) = delete;
    SmackerReader& operator=(const SmackerReader&) = delete;
    /// Releases the tables.
    ~SmackerReader();

    /// Opens an SMK2 file and reads its header and frame tables.
    ///
    /// Only SMK2 is accepted; 3.1c plays no other Smacker version.
    /// Every table and payload is checked against the file and the limits.
    ///
    /// @param path host path of the file
    /// @param limits size, geometry and frame bounds
    /// @return the reader, or why the file was refused
    [[nodiscard]] static OpenResult
    open(const std::filesystem::path& path, const Limits& limits = {});

    /// Returns the decoded file header.
    [[nodiscard]] const Header& header() const noexcept { return header_; }

    /// Returns the host path the reader was opened from.
    [[nodiscard]] const std::filesystem::path& path() const noexcept { return path_; }

    /// Returns the file offset of the first frame payload.
    [[nodiscard]] uint64_t frame_data_offset() const noexcept { return frame_data_offset_; }

    /// Returns the total bytes of all frame payloads.
    [[nodiscard]] uint64_t frame_data_bytes() const noexcept { return frame_data_bytes_; }

    /// Describes one frame's payload.
    ///
    /// @param index frame index
    /// @return offset, size and type, or nullopt past the frame count
    [[nodiscard]] std::optional<Frame> frame(uint32_t index) const noexcept;
    /// Reads one frame's compressed payload.
    ///
    /// @param index frame index
    /// @param[out] output receives the payload bytes
    /// @param[out] error receives the reason on failure
    /// @return true when the payload was read
    [[nodiscard]] bool
    read_frame(uint32_t index, std::vector<uint8_t>& output, std::string& error) const;
    /// Reads the Huffman tree block that precedes the frame payloads.
    ///
    /// @param[out] output receives trees_size bytes
    /// @param[out] error receives the reason on failure
    /// @return true when the trees were read
    [[nodiscard]] bool read_huffman_trees(std::vector<uint8_t>& output, std::string& error) const;

  private:

    SmackerReader() = default;
    Header header_;
    Limits limits_;
    std::filesystem::path path_;
    std::vector<on_disk::FrameSize> frame_sizes_;
    std::vector<uint8_t> frame_types_;
    std::vector<uint64_t> frame_offsets_; ///< file offset of each frame's payload
    uint64_t frame_data_offset_ = 0;
    uint64_t frame_data_bytes_ = 0;
    uint32_t table_frame_count_ = 0;
};

struct OpenResult {
    std::optional<SmackerReader> reader;
    std::string error;

    /// Returns whether the file was opened.
    explicit operator bool() const noexcept { return reader.has_value(); }
};

} // namespace oa::formats::smacker
