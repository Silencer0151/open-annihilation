// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/smacker/decoder.hpp"
#include "oa/base/bytes.hpp"

#include <algorithm>
#include <cstring>
#include <utility>

namespace oa::formats::smacker {
namespace {

using base::bytes::load_le32;

/// Values a byte tree may hold; each leaf holds one byte.
constexpr uint32_t kMaxByteTreeLeaves = 256;
/// Deepest leaf of a byte tree; a deeper one is refused.
constexpr unsigned kMaxByteTreeDepth = 27;
/// Deepest leaf of a 16-bit table; a deeper one is refused.
constexpr unsigned kMaxCodeTreeDepth = 500;
/// Most entries a 16-bit table may hold, recent-value slots included. The
/// header's table sizes are bytes at four per entry; 3.1c's largest needs
/// about 60000 entries.
constexpr uint64_t kMaxCodeTreeEntries = 1U << 20;
/// Entries of a 16-bit table the header leaves out: one leaf of 0, whose
/// recent-value slots are a second entry.
constexpr uint32_t kAbsentTableRecentSlot = 1;
/// Bytes per entry in the header's table sizes.
constexpr uint64_t kTableSizeBytesPerEntry = 4;
/// Bits of a 16-bit table's escape values.
constexpr unsigned kEscapeBits = 16;
/// Pixels along each side of a block.
constexpr uint32_t kBlockSide = 4;
/// Bits of a block type code: the kind in the low two, the run index in the
/// next six, a fill colour in the high byte.
constexpr uint32_t kBlockKindMask = 0x3U;
constexpr unsigned kBlockRunShift = 2;
constexpr uint32_t kBlockRunMask = 0x3FU;
constexpr unsigned kBlockFillColourShift = 8;
/// Bits of a sample byte, and the offset of an unsigned 8-bit sample.
constexpr unsigned kByteBits = 8;
constexpr int32_t kUnsignedSampleZero = 128;
/// Bits of the six-bit palette components.
constexpr uint8_t kPaletteComponentMask = 0x3FU;
/// Palette-chunk command bits.
constexpr uint8_t kPaletteKeepFlag = 0x80U;
constexpr uint8_t kPaletteKeepCountMask = 0x7FU;
constexpr uint8_t kPaletteCopyFlag = 0x40U;
constexpr uint8_t kPaletteCopyCountMask = 0x3FU;

/// The block kinds of a type code's low two bits.
enum class BlockKind : uint32_t {
    mono = 0, ///< two colours and a mask
    full = 1, ///< every pixel coded
    skip = 2, ///< kept from the previous frame
    fill = 3, ///< one colour
};

/// Block counts of the 64 run indices.
constexpr std::array<uint32_t, 64> kBlockRuns = {
    1,  2,  3,  4,  5,  6,  7,  8,  9,  10, 11, 12, 13, 14, 15, 16,  17,  18,  19,   20,   21, 22,
    23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38,  39,  40,  41,   42,   43, 44,
    45, 46, 47, 48, 49, 50, 51, 52, 53, 54, 55, 56, 57, 58, 59, 128, 256, 512, 1024, 2048,
};

/// Reads bits least significant first from a byte span.
///
/// Reading past the end gives zero bits; left() then turns negative, and the
/// callers check it where a stream must not run short.
class BitReader {
  public:

    /// Starts at the first bit of `bytes`.
    ///
    /// @param bytes the bit stream
    explicit BitReader(std::span<const uint8_t> bytes) noexcept
        : bytes_(bytes), total_(static_cast<int64_t>(bytes.size()) * kByteBits) {}

    /// Returns the bits not yet read; negative once reads ran past the end.
    [[nodiscard]] int64_t left() const noexcept { return total_ - position_; }

    /// Reads one bit.
    ///
    /// @return the bit, 0 past the end
    uint32_t bit() noexcept {
        const auto at = position_++;
        if (at >= total_)
            return 0;
        return (bytes_[static_cast<std::size_t>(at >> 3)] >> (at & 7)) & 1U;
    }

    /// Returns the next eight bits without reading them; the first is the lowest.
    ///
    /// @return the bits; call only while left() is at least 8
    [[nodiscard]] uint32_t peek_byte() const noexcept {
        const auto at = static_cast<std::size_t>(position_ >> 3);
        uint32_t window = bytes_[at];
        if (at + 1 < bytes_.size())
            window |= static_cast<uint32_t>(bytes_[at + 1]) << kByteBits;
        return (window >> (position_ & 7)) & 0xFFU;
    }

    /// Moves past bits already looked at.
    ///
    /// @param count bits to skip
    void skip(unsigned count) noexcept { position_ += count; }

    /// Reads up to 32 bits; the first bit read is the lowest.
    ///
    /// @param count bits to read
    /// @return the value
    uint32_t bits(unsigned count) noexcept {
        uint32_t value = 0;
        for (unsigned index = 0; index < count; ++index)
            value |= bit() << index;
        return value;
    }

  private:

    std::span<const uint8_t> bytes_;
    int64_t total_{};
    int64_t position_{};
};

/// Reads one leaf of a byte tree or 16-bit table, unchecked.
///
/// @param bits the stream
/// @param entries a flattened tree, see CodeTree
/// @return the leaf's index
std::size_t walk(BitReader& bits, const std::vector<uint32_t>& entries) noexcept {
    std::size_t at = 0;
    while ((entries[at] & CodeTree::kNodeFlag) != 0U) {
        if (bits.bit() != 0U)
            at += entries[at] & ~CodeTree::kNodeFlag;
        ++at;
    }
    return at;
}

/// Reads one subtree of a byte tree.
///
/// A set bit is a node with two subtrees; a clear bit is a leaf followed by
/// its eight-bit value.
///
/// @param bits the stream
/// @param[in,out] tree the entries so far
/// @param[in,out] leaves the leaves so far
/// @param depth the subtree's depth
/// @return false when the tree is too deep, too large or cut short
bool read_byte_subtree(
    BitReader& bits, std::vector<uint32_t>& tree, uint32_t& leaves, unsigned depth
) {
    if (depth > kMaxByteTreeDepth)
        return false;
    if (bits.bit() == 0U) {
        if (leaves >= kMaxByteTreeLeaves || bits.left() < static_cast<int64_t>(kByteBits))
            return false;
        tree.push_back(bits.bits(kByteBits));
        ++leaves;
        return true;
    }
    const auto node = tree.size();
    tree.push_back(CodeTree::kNodeFlag);
    if (!read_byte_subtree(bits, tree, leaves, depth + 1))
        return false;
    tree[node] = CodeTree::kNodeFlag | static_cast<uint32_t>(tree.size() - node - 1);
    return read_byte_subtree(bits, tree, leaves, depth + 1);
}

/// Reads a byte tree.
///
/// @param bits the stream
/// @param[out] tree the tree, replaced
/// @return false when the tree is malformed
bool read_byte_tree(BitReader& bits, std::vector<uint32_t>& tree) {
    tree.clear();
    uint32_t leaves = 0;
    return read_byte_subtree(bits, tree, leaves, 0);
}

/// What reading a 16-bit table needs: its byte trees, escapes and bounds.
struct TableReading {
    std::array<std::vector<uint32_t>, 2> bytes; ///< low byte, high byte
    std::array<uint32_t, 3> escapes{};
    std::array<int64_t, 3> escape_leaves{-1, -1, -1};
    uint64_t max_entries{};
};

/// Reads one subtree of a 16-bit table.
///
/// A set bit is a node; a clear bit a leaf whose value is coded by the low
/// and the high byte trees. A leaf equal to one of the escape values holds
/// a recent value instead, and starts at 0.
///
/// @param bits the stream
/// @param[in,out] reading the byte trees, escapes and bounds
/// @param[in,out] table the entries so far
/// @param depth the subtree's depth
/// @return the subtree's entry count, or 0 when it is malformed
uint32_t read_code_subtree(
    BitReader& bits, TableReading& reading, std::vector<uint32_t>& table, unsigned depth
) {
    if (depth > kMaxCodeTreeDepth || table.size() >= reading.max_entries || bits.left() <= 0)
        return 0;
    if (bits.bit() == 0U) {
        const auto low = reading.bytes[0][walk(bits, reading.bytes[0])];
        const auto high = reading.bytes[1][walk(bits, reading.bytes[1])];
        auto value = low | (high << kByteBits);
        for (std::size_t slot = 0; slot < reading.escapes.size(); ++slot) {
            if (value == reading.escapes[slot]) {
                reading.escape_leaves[slot] = static_cast<int64_t>(table.size());
                value = 0;
                break;
            }
        }
        table.push_back(value);
        return 1;
    }
    const auto node = table.size();
    table.push_back(CodeTree::kNodeFlag);
    const auto first = read_code_subtree(bits, reading, table, depth + 1);
    if (first == 0)
        return 0;
    table[node] = CodeTree::kNodeFlag | first;
    const auto second = read_code_subtree(bits, reading, table, depth + 1);
    if (second == 0)
        return 0;
    return first + 1 + second;
}

/// Fills a table's lookup of the entries its first eight bits reach.
///
/// @param[in,out] tree the table, whose entries are read
void build_first_steps(CodeTree& tree) noexcept {
    const auto& entries = tree.entries;
    for (uint32_t prefix = 0; prefix < tree.first_steps.size(); ++prefix) {
        std::size_t at = 0;
        uint32_t used = 0;
        while (used < CodeTree::kLookupBits && (entries[at] & CodeTree::kNodeFlag) != 0U) {
            if (((prefix >> used) & 1U) != 0U)
                at += entries[at] & ~CodeTree::kNodeFlag;
            ++at;
            ++used;
        }
        tree.first_steps[prefix] = static_cast<uint32_t>(at) << CodeTree::kLookupCountBits | used;
    }
}

/// Reads one of the four 16-bit tables from the header's tree block.
///
/// A clear first bit leaves the table out: it is then one leaf of 0.
/// Otherwise come the low and high byte trees (each a clear bit for a
/// tree of one 0 leaf, or a set bit, the tree and one more bit), three
/// 16-bit escape values, the table and one more bit. An escape value no
/// leaf holds gets a slot after the table.
///
/// @param bits the stream
/// @param size_bytes the header's size of the table
/// @param[out] tree the table
/// @param[out] present false when the header leaves the table out
/// @param[out] error why the table was refused
/// @return true when the table was read
bool read_code_table(
    BitReader& bits, uint32_t size_bytes, CodeTree& tree, bool& present, std::string& error
) {
    tree.entries.clear();
    present = bits.bit() != 0U;
    if (!present) {
        tree.entries = {0, 0};
        tree.recent = {kAbsentTableRecentSlot, kAbsentTableRecentSlot, kAbsentTableRecentSlot};
        build_first_steps(tree);
        return true;
    }
    TableReading reading;
    for (auto& byte_tree : reading.bytes) {
        if (bits.bit() == 0U) {
            byte_tree = {0};
            continue;
        }
        if (!read_byte_tree(bits, byte_tree)) {
            error = "Smacker byte tree is malformed";
            return false;
        }
        (void)bits.bit();
    }
    for (auto& escape : reading.escapes)
        escape = bits.bits(kEscapeBits);
    const auto declared_entries =
        (static_cast<uint64_t>(size_bytes) + kTableSizeBytesPerEntry - 1) / kTableSizeBytesPerEntry;
    if (declared_entries + reading.escapes.size() > kMaxCodeTreeEntries) {
        error = "Smacker Huffman table exceeds bound";
        return false;
    }
    reading.max_entries = declared_entries;
    tree.entries.reserve(static_cast<std::size_t>(declared_entries + reading.escapes.size()));
    if (read_code_subtree(bits, reading, tree.entries, 0) == 0) {
        error = "Smacker Huffman table is malformed";
        return false;
    }
    (void)bits.bit();
    for (std::size_t slot = 0; slot < reading.escapes.size(); ++slot) {
        if (reading.escape_leaves[slot] < 0) {
            reading.escape_leaves[slot] = static_cast<int64_t>(tree.entries.size());
            tree.entries.push_back(0);
        }
        tree.recent[slot] = static_cast<uint32_t>(reading.escape_leaves[slot]);
    }
    build_first_steps(tree);
    return true;
}

/// Reads one value of a 16-bit table and updates its recent values.
///
/// @param bits the stream
/// @param[in,out] tree the table
/// @param[out] value the value
/// @return false when the stream ends inside the code
bool read_code(BitReader& bits, CodeTree& tree, uint32_t& value) noexcept {
    auto& entries = tree.entries;
    std::size_t at = 0;
    if (bits.left() >= static_cast<int64_t>(CodeTree::kLookupBits)) {
        const auto step = tree.first_steps[bits.peek_byte()];
        bits.skip(step & ((1U << CodeTree::kLookupCountBits) - 1U));
        at = step >> CodeTree::kLookupCountBits;
    }
    while ((entries[at] & CodeTree::kNodeFlag) != 0U) {
        if (bits.left() < 1)
            return false;
        if (bits.bit() != 0U)
            at += entries[at] & ~CodeTree::kNodeFlag;
        ++at;
    }
    value = entries[at];
    if (value != entries[tree.recent[0]]) {
        entries[tree.recent[2]] = entries[tree.recent[1]];
        entries[tree.recent[1]] = entries[tree.recent[0]];
        entries[tree.recent[0]] = value;
    }
    return true;
}

/// Sets a table's recent values to zero, as each frame starts.
///
/// @param[in,out] tree the table
void reset_recent(CodeTree& tree) noexcept {
    for (const auto slot : tree.recent)
        tree.entries[slot] = 0;
}

/// Widens a six-bit palette component to eight bits.
///
/// @param component the component; bits above the sixth are ignored
/// @return the component with its top two bits repeated below it
uint8_t widen_component(uint8_t component) noexcept {
    const auto six = static_cast<uint8_t>(component & kPaletteComponentMask);
    return static_cast<uint8_t>(six << 2 | six >> 4);
}

/// Widens an unsigned 8-bit sample to a signed 16-bit one.
///
/// @param sample the sample, 128 for silence
/// @return the sample centred on 0 and scaled by 256
int16_t widen_sample(uint32_t sample) noexcept {
    return static_cast<int16_t>((static_cast<int32_t>(sample & 0xFFU) - kUnsignedSampleZero) * 256);
}

/// Returns the bytes before a track's samples inside its audio chunk.
///
/// @param format the track's format
/// @return 4 for a packed track's decoded-size word, otherwise 0
std::size_t audio_chunk_prefix(const AudioFormat& format) noexcept {
    return format.coding == AudioCoding::packed ? kPackedAudioSizeBytes : 0;
}

/// Decodes a chunk of plain samples.
///
/// A trailing partial sample frame is dropped.
///
/// @param chunk the samples
/// @param format the track's format
/// @param[out] samples the decoded samples
/// @param[out] error why the chunk was refused
/// @return false when the chunk holds no whole sample frame
bool decode_pcm(
    std::span<const uint8_t> chunk,
    const AudioFormat& format,
    std::vector<int16_t>& samples,
    std::string& error
) {
    const std::size_t sample_bytes = format.bits / kByteBits;
    const std::size_t frame_bytes = sample_bytes * format.channels;
    const auto frames = chunk.size() / frame_bytes;
    if (frames == 0) {
        error = "Smacker audio chunk holds no whole sample";
        return false;
    }
    samples.resize(frames * format.channels);
    for (std::size_t index = 0; index < samples.size(); ++index) {
        if (sample_bytes == 1) {
            samples[index] = widen_sample(chunk[index]);
        } else {
            samples[index] = static_cast<int16_t>(
                static_cast<uint16_t>(chunk[index * 2] | chunk[index * 2 + 1] << kByteBits)
            );
        }
    }
    return true;
}

} // namespace

bool split_frame(
    std::span<const uint8_t> payload,
    uint8_t frame_type,
    const Header& header,
    FrameChunks& chunks,
    std::string& error
) {
    chunks = {};
    std::size_t at = 0;
    if ((frame_type & kFramePaletteFlag) != 0U) {
        if (payload.empty()) {
            error = "Smacker palette chunk is missing";
            return false;
        }
        const auto size = static_cast<std::size_t>(payload[0]) * kPaletteChunkUnitBytes;
        if (size == 0 || size > payload.size()) {
            error = "Smacker palette chunk exceeds its frame";
            return false;
        }
        chunks.palette = payload.first(size);
        at = size;
    }
    for (std::size_t track = 0; track < kSmackerAudioTrackCount; ++track) {
        if ((frame_type & (kFrameFirstAudioFlag << track)) == 0U)
            continue;
        if (payload.size() - at < kAudioChunkLengthBytes) {
            error = "Smacker audio chunk length is truncated";
            return false;
        }
        const auto size = load_le32(payload.subspan(at).data());
        const auto prefix = audio_chunk_prefix(audio_format(header.audio[track]));
        if (size < kAudioChunkLengthBytes + prefix || size > payload.size() - at) {
            error = "Smacker audio chunk exceeds its frame";
            return false;
        }
        chunks.audio[track] =
            payload.subspan(at + kAudioChunkLengthBytes, size - kAudioChunkLengthBytes);
        at += size;
    }
    chunks.video = payload.subspan(at);
    return true;
}

bool update_palette(std::span<const uint8_t> chunk, Palette& palette, std::string& error) {
    const Palette previous = palette;
    std::size_t at = 1; // after the length byte
    std::size_t colour = 0;
    const auto next = [&](uint8_t& byte) {
        if (at >= chunk.size())
            return false;
        byte = chunk[at++];
        return true;
    };
    while (colour < kPaletteColours) {
        uint8_t command = 0;
        if (!next(command)) {
            error = "Smacker palette chunk is truncated";
            return false;
        }
        if ((command & kPaletteKeepFlag) != 0U) {
            colour += static_cast<std::size_t>(command & kPaletteKeepCountMask) + 1;
        } else if ((command & kPaletteCopyFlag) != 0U) {
            uint8_t source = 0;
            if (!next(source)) {
                error = "Smacker palette chunk is truncated";
                return false;
            }
            const auto count = static_cast<std::size_t>(command & kPaletteCopyCountMask) + 1;
            if (source + count > kPaletteColours) {
                error = "Smacker palette copy runs past the palette";
                return false;
            }
            for (std::size_t index = 0; index < count && colour < kPaletteColours; ++index)
                std::memcpy(&palette[colour++ * 3], &previous[(source + index) * 3], 3);
        } else {
            uint8_t green = 0;
            uint8_t blue = 0;
            if (!next(green) || !next(blue)) {
                error = "Smacker palette chunk is truncated";
                return false;
            }
            palette[colour * 3] = widen_component(command);
            palette[colour * 3 + 1] = widen_component(green);
            palette[colour * 3 + 2] = widen_component(blue);
            ++colour;
        }
    }
    return true;
}

AudioFormat audio_format(const AudioTrack& track) noexcept {
    AudioFormat format;
    format.sample_rate = track.sample_rate();
    const auto flags = track.format_flags();
    format.channels = (flags & kAudioStereoFlag) != 0U ? 2 : 1;
    format.bits = (flags & kAudioSixteenBitFlag) != 0U ? 16 : 8;
    if (format.sample_rate == 0 || (flags & (kAudioTransformFlag | kAudioTransformDctFlag)) != 0U)
        format.coding = AudioCoding::none;
    else
        format.coding = (flags & kAudioPackedFlag) != 0U ? AudioCoding::packed : AudioCoding::pcm;
    return format;
}

bool decode_audio(
    std::span<const uint8_t> chunk,
    const AudioFormat& format,
    std::vector<int16_t>& samples,
    std::string& error
) {
    samples.clear();
    if (format.coding == AudioCoding::none || (format.channels != 1 && format.channels != 2) ||
        (format.bits != 8 && format.bits != 16)) {
        error = "Smacker audio track is not playable";
        return false;
    }
    if (format.coding == AudioCoding::pcm)
        return decode_pcm(chunk, format, samples, error);
    if (chunk.size() <= kPackedAudioSizeBytes) {
        error = "Smacker audio chunk is too short";
        return false;
    }
    const auto decoded_bytes = load_le32(chunk.data());
    if (decoded_bytes > kMaxPackedAudioBytes) {
        error = "Smacker audio chunk exceeds bound";
        return false;
    }
    BitReader bits(chunk.subspan(kPackedAudioSizeBytes));
    if (bits.bit() == 0U)
        return true; // the chunk holds no samples
    const auto stereo = bits.bit();
    const auto sixteen = bits.bit();
    const uint32_t channels = stereo + 1;
    const uint32_t sample_bytes = sixteen + 1;
    if (channels != format.channels || sample_bytes * kByteBits != format.bits) {
        error = "Smacker audio chunk differs from its track's format";
        return false;
    }
    const auto frame_bytes = channels * sample_bytes;
    if (decoded_bytes < frame_bytes || decoded_bytes % frame_bytes != 0) {
        error = "Smacker audio chunk holds no whole number of samples";
        return false;
    }
    // One tree per byte of a sample of each channel: low then high byte of
    // the left channel, then of the right.
    std::array<std::vector<uint32_t>, 4> trees;
    const auto tree_count = std::size_t{1} << (sixteen + stereo);
    for (std::size_t tree = 0; tree < tree_count; ++tree) {
        (void)bits.bit();
        if (!read_byte_tree(bits, trees[tree])) {
            error = "Smacker audio tree is malformed";
            return false;
        }
        (void)bits.bit();
    }
    if (bits.left() < static_cast<int64_t>(frame_bytes * kByteBits)) {
        error = "Smacker audio chunk is truncated";
        return false;
    }
    const auto count = decoded_bytes / sample_bytes;
    samples.resize(count);
    std::array<uint32_t, 2> previous{};
    if (sixteen != 0U) {
        // The first sample of each channel is stored high byte first, the
        // right channel's first.
        for (auto channel = static_cast<int32_t>(stereo); channel >= 0; --channel) {
            const auto stored = bits.bits(16);
            previous[static_cast<std::size_t>(channel)] =
                (stored & 0xFFU) << kByteBits | stored >> kByteBits;
        }
    } else {
        for (auto channel = static_cast<int32_t>(stereo); channel >= 0; --channel)
            previous[static_cast<std::size_t>(channel)] = bits.bits(kByteBits);
    }
    for (std::size_t index = 0; index < count; ++index) {
        const auto channel = index & stereo;
        if (index >= channels) {
            if (bits.left() < 0) {
                error = "Smacker audio chunk is truncated";
                samples.clear();
                return false;
            }
            if (sixteen != 0U) {
                const auto low = trees[channel * 2][walk(bits, trees[channel * 2])];
                const auto high = trees[channel * 2 + 1][walk(bits, trees[channel * 2 + 1])];
                previous[channel] += low | high << kByteBits;
            } else {
                previous[channel] += trees[channel][walk(bits, trees[channel])];
            }
        }
        samples[index] = sixteen != 0U
                             ? static_cast<int16_t>(static_cast<uint16_t>(previous[channel]))
                             : widen_sample(previous[channel]);
    }
    return true;
}

std::optional<VideoDecoder>
VideoDecoder::create(const Header& header, std::span<const uint8_t> trees, std::string& error) {
    if (header.signature != kSmk2) {
        error = "Smacker video decoding needs SMK2";
        return std::nullopt;
    }
    if (header.width == 0 || header.height == 0) {
        error = "Smacker frame is empty";
        return std::nullopt;
    }
    // The frame's pixels are counted at 64 bits, so no size wraps to a
    // smaller buffer than the frame.
    const uint64_t frame_pixels = uint64_t{header.width} * header.height;
    if (static_cast<std::size_t>(frame_pixels) != frame_pixels) {
        error = "Smacker frame is larger than memory can hold";
        return std::nullopt;
    }
    VideoDecoder decoder;
    BitReader bits(trees);
    const std::array<std::pair<CodeTree*, uint32_t>, 4> tables = {{
        {&decoder.mono_masks_, header.mmap_size},
        {&decoder.mono_colours_, header.mclr_size},
        {&decoder.full_pixels_, header.full_size},
        {&decoder.block_types_, header.type_size},
    }};
    int present = 0;
    for (const auto& [table, size] : tables) {
        bool read = false;
        if (!read_code_table(bits, size, *table, read, error))
            return std::nullopt;
        present += read ? 1 : 0;
    }
    if (present == 0 || bits.left() < 0) {
        error = "Smacker Huffman tables are missing or truncated";
        return std::nullopt;
    }
    decoder.width_ = header.width;
    decoder.height_ = header.height;
    decoder.pixels_.assign(static_cast<std::size_t>(frame_pixels), 0);
    return decoder;
}

bool VideoDecoder::decode(std::span<const uint8_t> chunk, std::string& error) {
    if (chunk.empty()) {
        error = "Smacker frame has no video chunk";
        return false;
    }
    reset_recent(mono_masks_);
    reset_recent(mono_colours_);
    reset_recent(full_pixels_);
    reset_recent(block_types_);
    BitReader bits(chunk);
    const auto blocks_wide = width_ / kBlockSide;
    const auto blocks = blocks_wide * (height_ / kBlockSide);
    const auto stride = static_cast<std::size_t>(width_);
    const auto truncated = [&] {
        error = "Smacker video chunk is truncated";
        return false;
    };
    uint32_t block = 0;
    while (block < blocks) {
        uint32_t type = 0;
        if (!read_code(bits, block_types_, type))
            return truncated();
        const auto run = kBlockRuns[(type >> kBlockRunShift) & kBlockRunMask];
        const auto kind = static_cast<BlockKind>(type & kBlockKindMask);
        const auto end = std::min(blocks, block + run);
        if (kind == BlockKind::skip) {
            block = end;
            continue;
        }
        for (; block < end; ++block) {
            // Each row of the block is found from its own y and x, so no
            // pointer is formed past the frame's last row.
            const auto top = static_cast<std::size_t>(block / blocks_wide) * kBlockSide;
            const auto left_column = static_cast<std::size_t>(block % blocks_wide) * kBlockSide;
            const auto row_at = [&](uint32_t row) {
                return pixels_.data() + (top + row) * stride + left_column;
            };
            if (kind == BlockKind::mono) {
                uint32_t colours = 0;
                uint32_t mask = 0;
                if (!read_code(bits, mono_colours_, colours) || !read_code(bits, mono_masks_, mask))
                    return truncated();
                const auto high = static_cast<uint8_t>(colours >> kByteBits);
                const auto low = static_cast<uint8_t>(colours);
                for (uint32_t row = 0; row < kBlockSide; ++row) {
                    uint8_t* const out = row_at(row);
                    for (uint32_t column = 0; column < kBlockSide; ++column, mask >>= 1)
                        out[column] = (mask & 1U) != 0U ? high : low;
                }
            } else if (kind == BlockKind::full) {
                for (uint32_t row = 0; row < kBlockSide; ++row) {
                    uint32_t right = 0;
                    uint32_t left = 0;
                    if (!read_code(bits, full_pixels_, right) ||
                        !read_code(bits, full_pixels_, left))
                        return truncated();
                    uint8_t* const out = row_at(row);
                    out[0] = static_cast<uint8_t>(left);
                    out[1] = static_cast<uint8_t>(left >> kByteBits);
                    out[2] = static_cast<uint8_t>(right);
                    out[3] = static_cast<uint8_t>(right >> kByteBits);
                }
            } else {
                const auto colour = static_cast<uint8_t>(type >> kBlockFillColourShift);
                for (uint32_t row = 0; row < kBlockSide; ++row)
                    std::memset(row_at(row), colour, kBlockSide);
            }
        }
    }
    return true;
}

} // namespace oa::formats::smacker
