// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Builds SMK2 streams bit by bit for the decoder's and the player's tests,
// and one small whole movie whose every picture and sample the tests state.
//
// The movie is 8x8 pixels (four 4x4 blocks), three frames at a frame-rate
// field of -3333, with one packed 8-bit stereo track at 22050 Hz:
// - frame 0: a palette giving every colour, a chunk of three samples per
//   channel, and the four block kinds (fill 0x30, mono, full, then a skip);
// - frame 1: an audio chunk holding no samples, and one fill run of 128
//   blocks in colour 0x09, which stops at the frame's four;
// - frame 2: a palette swapping colours 0 and 1, then a full block whose
//   recent values start again from 0, a skip of two and a mono block.

#include "oa/formats/smacker.hpp"
#include "oa/formats/smacker/decoder.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <utility>
#include <vector>

namespace oa::formats::smacker::test_movie {

/// Writes bits least significant first, as the decoder reads them.
class BitWriter {
  public:

    /// Appends one bit.
    ///
    /// @param bit 0 or 1
    void put(uint32_t bit) {
        if (count_ % 8 == 0)
            bytes_.push_back(0);
        if (bit != 0U)
            bytes_.back() = static_cast<uint8_t>(bytes_.back() | 1U << (count_ % 8));
        ++count_;
    }

    /// Appends the low bits of a value, lowest first.
    ///
    /// @param value the value
    /// @param count bits to append
    void put_bits(uint32_t value, unsigned count) {
        for (unsigned index = 0; index < count; ++index)
            put((value >> index) & 1U);
    }

    /// Returns the bytes written, the last one padded with zero bits.
    [[nodiscard]] const std::vector<uint8_t>& bytes() const noexcept { return bytes_; }

  private:

    std::vector<uint8_t> bytes_;
    std::size_t count_{};
};

/// A Huffman tree: a leaf with a value, or a node whose first child a clear
/// bit selects.
struct Tree {
    std::vector<Tree> children;
    uint32_t value{};
};

/// Returns a leaf.
///
/// @param value the leaf's value
/// @return the tree
inline Tree leaf(uint32_t value) {
    return Tree{{}, value};
}

/// Returns a node.
///
/// @param first the subtree a clear bit selects
/// @param second the subtree a set bit selects
/// @return the tree
inline Tree node(Tree first, Tree second) {
    Tree tree;
    tree.children.push_back(std::move(first));
    tree.children.push_back(std::move(second));
    return tree;
}

/// Finds the bits that reach a value's leaf.
///
/// @param tree the tree
/// @param value the leaf's value
/// @param[in,out] path the bits so far; the path when found
/// @return true when the tree holds the value
inline bool find_path(const Tree& tree, uint32_t value, std::vector<uint32_t>& path) {
    if (tree.children.empty())
        return tree.value == value;
    for (uint32_t bit = 0; bit < 2; ++bit) {
        path.push_back(bit);
        if (find_path(tree.children[bit], value, path))
            return true;
        path.pop_back();
    }
    return false;
}

/// Writes the code of a value.
///
/// @param[in,out] bits the stream
/// @param tree the tree that codes it
/// @param value a value the tree holds
inline void put_code(BitWriter& bits, const Tree& tree, uint32_t value) {
    std::vector<uint32_t> path;
    (void)find_path(tree, value, path);
    for (const auto bit : path)
        bits.put(bit);
}

/// Writes a byte tree: a set bit per node, a clear bit and eight bits per leaf.
///
/// @param[in,out] bits the stream
/// @param tree the tree
inline void put_byte_tree(BitWriter& bits, const Tree& tree) {
    if (tree.children.empty()) {
        bits.put(0);
        bits.put_bits(tree.value, 8);
        return;
    }
    bits.put(1);
    put_byte_tree(bits, tree.children[0]);
    put_byte_tree(bits, tree.children[1]);
}

/// The trees of one 16-bit table.
struct Table {
    Tree values;
    Tree low;
    Tree high;
    std::array<uint32_t, 3> escapes{};
};

/// Writes a 16-bit table's leaves, each coded by the byte trees.
///
/// @param[in,out] bits the stream
/// @param table the table
/// @param tree the subtree to write
inline void put_values(BitWriter& bits, const Table& table, const Tree& tree) {
    if (tree.children.empty()) {
        bits.put(0);
        put_code(bits, table.low, tree.value & 0xFFU);
        put_code(bits, table.high, tree.value >> 8);
        return;
    }
    bits.put(1);
    put_values(bits, table, tree.children[0]);
    put_values(bits, table, tree.children[1]);
}

/// Writes a present 16-bit table, with set bits where the format ignores one.
///
/// @param[in,out] bits the stream
/// @param table the table
inline void put_table(BitWriter& bits, const Table& table) {
    bits.put(1);
    for (const auto* byte_tree : {&table.low, &table.high}) {
        bits.put(1);
        put_byte_tree(bits, *byte_tree);
        bits.put(1);
    }
    for (const auto escape : table.escapes)
        bits.put_bits(escape, 16);
    put_values(bits, table, table.values);
    bits.put(1);
}

// The movie's geometry, rate and audio track.
inline constexpr uint32_t kWidth = 8;
inline constexpr uint32_t kHeight = 8;
inline constexpr uint32_t kFrames = 3;
inline constexpr int32_t kFrameRate = -3333;
inline constexpr uint32_t kTrackRate = 0xD0005622U; // packed, present, stereo, 8-bit, 22050 Hz
inline constexpr uint32_t kTableBytes = 64;

// Block types: kind in the low two bits, run index above, fill colour in the
// high byte.
inline constexpr uint32_t kTypeFill = 0x3003;     // one block of colour 0x30
inline constexpr uint32_t kTypeMono = 0x0000;     // one mono block
inline constexpr uint32_t kTypeFull = 0x0001;     // one full block
inline constexpr uint32_t kTypeSkipTwo = 0x0006;  // skip two blocks
inline constexpr uint32_t kTypeFillMany = 0x09F3; // 128 blocks of colour 0x09

// Full-block values; the escapes are leaves that repeat recent values.
inline constexpr uint32_t kFullFirst = 0x2211;
inline constexpr uint32_t kFullSecond = 0x2244;
inline constexpr std::array<uint32_t, 3> kFullEscapes = {0xBBAA, 0xBBCC, 0xBBDD};
inline constexpr uint32_t kMonoColours = 0x0507; // high 0x05, low 0x07
inline constexpr uint32_t kMonoMask = 0x8421;    // the diagonal

/// Returns the block-type table.
inline Table type_table() {
    return Table{
        node(
            node(leaf(kTypeFill), leaf(kTypeMono)),
            node(leaf(kTypeFull), node(leaf(kTypeSkipTwo), leaf(kTypeFillMany)))
        ),
        node(node(leaf(0x03), leaf(0x00)), node(leaf(0x01), node(leaf(0x06), leaf(0xF3)))),
        node(leaf(0x30), node(leaf(0x00), leaf(0x09))),
        {0xFFFF, 0xFFFE, 0xFFFD},
    };
}

/// Returns the full-block table, whose three escape leaves repeat values.
inline Table full_table() {
    return Table{
        node(
            node(leaf(kFullFirst), leaf(kFullSecond)),
            node(leaf(kFullEscapes[0]), node(leaf(kFullEscapes[1]), leaf(kFullEscapes[2])))
        ),
        node(node(leaf(0x11), leaf(0x44)), node(leaf(0xAA), node(leaf(0xCC), leaf(0xDD)))),
        node(leaf(0x22), leaf(0xBB)),
        kFullEscapes,
    };
}

/// Returns a table of one value, coded in no bits.
///
/// @param value the value
inline Table single_value_table(uint32_t value) {
    return Table{leaf(value), leaf(value & 0xFFU), leaf(value >> 8), {0xFFFF, 0xFFFE, 0xFFFD}};
}

/// Returns the movie's Huffman tree block: mono masks, mono colours, full
/// pixels and block types.
inline std::vector<uint8_t> tree_block() {
    BitWriter bits;
    put_table(bits, single_value_table(kMonoMask));
    put_table(bits, single_value_table(kMonoColours));
    put_table(bits, full_table());
    put_table(bits, type_table());
    return bits.bytes();
}

/// The full-block values of frame 0, as written: escapes by their value.
inline constexpr std::array<uint32_t, 8> kFrameZeroFull = {
    kFullFirst,
    kFullSecond,
    kFullEscapes[0],
    kFullEscapes[1],
    kFullEscapes[2],
    kFullEscapes[1],
    kFullFirst,
    kFullEscapes[2],
};
/// The full-block values of frame 2, as written.
inline constexpr std::array<uint32_t, 8> kFrameTwoFull = {
    kFullEscapes[0],
    kFullSecond,
    kFullEscapes[1],
    kFullEscapes[0],
    kFullEscapes[2],
    kFullFirst,
    kFullEscapes[2],
    kFullEscapes[1],
};

/// Returns the video chunk of a frame.
///
/// @param frame 0, 1 or 2
inline std::vector<uint8_t> video_chunk(uint32_t frame) {
    const auto types = type_table();
    const auto full = full_table();
    BitWriter bits;
    if (frame == 0) {
        put_code(bits, types.values, kTypeFill);
        put_code(bits, types.values, kTypeMono);
        put_code(bits, types.values, kTypeFull);
        for (const auto value : kFrameZeroFull)
            put_code(bits, full.values, value);
        put_code(bits, types.values, kTypeSkipTwo);
    } else if (frame == 1) {
        put_code(bits, types.values, kTypeFillMany);
    } else {
        put_code(bits, types.values, kTypeFull);
        for (const auto value : kFrameTwoFull)
            put_code(bits, full.values, value);
        put_code(bits, types.values, kTypeSkipTwo);
        put_code(bits, types.values, kTypeMono);
    }
    return bits.bytes();
}

/// Returns the six-bit components frame 0's palette gives colour `colour`.
///
/// @param colour 0 to 255
inline std::array<uint8_t, 3> first_palette_components(uint32_t colour) {
    return {
        static_cast<uint8_t>(colour & 0x3FU),
        static_cast<uint8_t>((colour >> 2) & 0x3FU),
        static_cast<uint8_t>(0x3FU - (colour & 0x3FU)),
    };
}

/// Returns frame 0's palette chunk: 256 new colours.
inline std::vector<uint8_t> first_palette_chunk() {
    std::vector<uint8_t> chunk{0};
    for (uint32_t colour = 0; colour < kPaletteColours; ++colour) {
        const auto components = first_palette_components(colour);
        chunk.insert(chunk.end(), components.begin(), components.end());
    }
    while (chunk.size() % kPaletteChunkUnitBytes != 0)
        chunk.push_back(0);
    chunk[0] = static_cast<uint8_t>(chunk.size() / kPaletteChunkUnitBytes);
    return chunk;
}

/// Returns frame 2's palette chunk: colours 0 and 1 swapped, the rest kept.
inline std::vector<uint8_t> second_palette_chunk() {
    return {2, 0x40, 1, 0x40, 0, 0xFF, 0xFD, 0};
}

/// Returns frame 0's audio chunk after its length word: three samples per
/// channel, the left channel stepping +1 then -1 and the right +2 each.
inline std::vector<uint8_t> first_audio_chunk() {
    BitWriter bits;
    bits.put(1); // samples follow
    bits.put(1); // stereo
    bits.put(0); // 8-bit
    bits.put(0);
    put_byte_tree(bits, node(leaf(0x01), leaf(0xFF)));
    bits.put(0);
    bits.put(0);
    put_byte_tree(bits, leaf(0x02));
    bits.put(0);
    bits.put_bits(0x80, 8); // right first
    bits.put_bits(0x10, 8); // then left
    bits.put(0);            // left +1
    bits.put(1);            // left -1
    std::vector<uint8_t> chunk{6, 0, 0, 0};
    chunk.insert(chunk.end(), bits.bytes().begin(), bits.bytes().end());
    return chunk;
}

/// The samples frame 0's audio chunk decodes to: 8-bit values 0x10, 0x80,
/// 0x11, 0x82, 0x10, 0x84, each (value - 128) * 256.
inline constexpr std::array<int16_t, 6> kFirstAudioSamples = {
    -28672,
    0,
    -28416,
    512,
    -28672,
    1024,
};

/// Returns frame 1's audio chunk, which holds no samples.
inline std::vector<uint8_t> empty_audio_chunk() {
    return {0, 0, 0, 0, 0};
}

/// Appends a little-endian 32-bit value.
///
/// @param[in,out] bytes the buffer
/// @param value the value
inline void append32(std::vector<uint8_t>& bytes, uint32_t value) {
    for (unsigned shift = 0; shift < 32; shift += 8)
        bytes.push_back(static_cast<uint8_t>(value >> shift));
}

/// Returns an audio chunk with its length word.
///
/// @param chunk the chunk after the length word
inline std::vector<uint8_t> with_length(const std::vector<uint8_t>& chunk) {
    std::vector<uint8_t> bytes;
    append32(bytes, static_cast<uint32_t>(chunk.size() + kAudioChunkLengthBytes));
    bytes.insert(bytes.end(), chunk.begin(), chunk.end());
    return bytes;
}

/// Returns the frame types of the movie.
inline std::array<uint8_t, kFrames> frame_types() {
    return {
        kFramePaletteFlag | kFrameFirstAudioFlag,
        kFrameFirstAudioFlag,
        kFramePaletteFlag,
    };
}

/// Returns a frame's payload.
///
/// @param frame 0, 1 or 2
inline std::vector<uint8_t> frame_payload(uint32_t frame) {
    std::vector<uint8_t> payload;
    const auto add = [&](const std::vector<uint8_t>& part) {
        payload.insert(payload.end(), part.begin(), part.end());
    };
    if (frame == 0) {
        add(first_palette_chunk());
        add(with_length(first_audio_chunk()));
    } else if (frame == 1) {
        add(with_length(empty_audio_chunk()));
    } else {
        add(second_palette_chunk());
    }
    add(video_chunk(frame));
    // Payloads are padded to whole 32-bit words, as Smacker files are.
    while (payload.size() % 4 != 0)
        payload.push_back(0);
    return payload;
}

/// Returns the whole movie file.
inline std::vector<uint8_t> movie_file() {
    const auto trees = tree_block();
    std::vector<uint8_t> file;
    append32(file, kSmk2);
    append32(file, kWidth);
    append32(file, kHeight);
    append32(file, kFrames);
    append32(file, static_cast<uint32_t>(kFrameRate));
    append32(file, 0); // flags
    // The track's buffer size, which decoding does not read.
    const auto audio_buffer = static_cast<uint32_t>(first_audio_chunk().size());
    for (std::size_t track = 0; track < kSmackerAudioTrackCount; ++track)
        append32(file, track == 0 ? audio_buffer : 0);
    append32(file, static_cast<uint32_t>(trees.size()));
    for (int table = 0; table < 4; ++table)
        append32(file, kTableBytes);
    for (std::size_t track = 0; track < kSmackerAudioTrackCount; ++track)
        append32(file, track == 0 ? kTrackRate : 0);
    append32(file, 0); // reserved
    for (uint32_t frame = 0; frame < kFrames; ++frame)
        append32(file, static_cast<uint32_t>(frame_payload(frame).size()));
    for (const auto type : frame_types())
        file.push_back(type);
    file.insert(file.end(), trees.begin(), trees.end());
    for (uint32_t frame = 0; frame < kFrames; ++frame) {
        const auto payload = frame_payload(frame);
        file.insert(file.end(), payload.begin(), payload.end());
    }
    return file;
}

/// Returns the palette index of each pixel of a frame, row by row.
///
/// @param frame 0, 1 or 2
inline std::array<uint8_t, kWidth * kHeight> expected_indices(uint32_t frame) {
    std::array<uint8_t, kWidth * kHeight> pixels{};
    const auto set_block = [&](uint32_t block, const std::array<std::array<uint8_t, 4>, 4>& rows) {
        const auto x0 = (block % 2) * 4;
        const auto y0 = (block / 2) * 4;
        for (uint32_t y = 0; y < 4; ++y)
            for (uint32_t x = 0; x < 4; ++x)
                pixels[(y0 + y) * kWidth + x0 + x] = rows[y][x];
    };
    using Rows = std::array<std::array<uint8_t, 4>, 4>;
    const Rows mono = {{{5, 7, 7, 7}, {7, 5, 7, 7}, {7, 7, 5, 7}, {7, 7, 7, 5}}};
    const Rows fill_nine = {{{9, 9, 9, 9}, {9, 9, 9, 9}, {9, 9, 9, 9}, {9, 9, 9, 9}}};
    if (frame == 0) {
        set_block(
            0,
            {{{0x30, 0x30, 0x30, 0x30},
              {0x30, 0x30, 0x30, 0x30},
              {0x30, 0x30, 0x30, 0x30},
              {0x30, 0x30, 0x30, 0x30}}}
        );
        set_block(1, mono);
        // Values 2211, 2244, 2244, 2211, 2211, 2244, 2211, 2211; per row
        // the right pair, then the left.
        set_block(
            2,
            {{{0x44, 0x22, 0x11, 0x22},
              {0x11, 0x22, 0x44, 0x22},
              {0x44, 0x22, 0x11, 0x22},
              {0x11, 0x22, 0x11, 0x22}}}
        );
        return pixels;
    }
    for (uint32_t block = 0; block < 4; ++block)
        set_block(block, fill_nine);
    if (frame == 2) {
        // Values 0000, 2244, 0000, 0000, 0000, 2211, 2244, 2211.
        set_block(
            0,
            {{{0x44, 0x22, 0x00, 0x00},
              {0x00, 0x00, 0x00, 0x00},
              {0x11, 0x22, 0x00, 0x00},
              {0x11, 0x22, 0x44, 0x22}}}
        );
        set_block(3, mono);
    }
    return pixels;
}

} // namespace oa::formats::smacker::test_movie
