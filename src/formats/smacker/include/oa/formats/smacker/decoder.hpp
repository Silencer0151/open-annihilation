// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// Decoding of SMK2 frames: a frame payload split into its palette, audio and
// video chunks, the palette updates, the packed audio and the 8-bit video.
// Everything works on byte spans; nothing here opens a file. A movie is
// decoded one frame at a time in file order, keeping one frame of pixels.

#include "oa/formats/smacker.hpp"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <vector>

namespace oa::formats::smacker {

/// Colours in a movie palette.
inline constexpr std::size_t kPaletteColours = 256;
/// Bytes of a palette: red, green and blue of each colour, 0 to 255.
inline constexpr std::size_t kPaletteBytes = kPaletteColours * 3;
/// A movie palette, starting all black.
using Palette = std::array<uint8_t, kPaletteBytes>;

/// Frame-type bit: the frame starts with a palette chunk.
inline constexpr uint8_t kFramePaletteFlag = 0x01U;
/// Frame-type bit of audio track 0; track n uses this bit shifted left by n.
inline constexpr uint8_t kFrameFirstAudioFlag = 0x02U;
/// The palette chunk's first byte gives its length in units of this many bytes.
inline constexpr std::size_t kPaletteChunkUnitBytes = 4;
/// Bytes of the length word that leads each audio chunk; the length counts it.
inline constexpr std::size_t kAudioChunkLengthBytes = 4;
/// Bytes of the decoded-size word that leads a packed audio chunk.
inline constexpr std::size_t kPackedAudioSizeBytes = 4;
/// Largest decoded size a packed audio chunk may give, in bytes.
inline constexpr uint32_t kMaxPackedAudioBytes = 1U << 24;

// Sample-format bits of an audio track: the high byte of its packed rate word.
/// The track is Huffman-packed with per-sample deltas.
inline constexpr uint8_t kAudioPackedFlag = 0x80U;
/// Samples are 16-bit signed; without it they are 8-bit unsigned.
inline constexpr uint8_t kAudioSixteenBitFlag = 0x20U;
/// Samples are interleaved left and right; without it the track is mono.
inline constexpr uint8_t kAudioStereoFlag = 0x10U;
/// The track uses a transform codec, which 3.1c's movies do not use.
inline constexpr uint8_t kAudioTransformFlag = 0x08U;
/// The track uses a second transform codec, which 3.1c's movies do not use.
inline constexpr uint8_t kAudioTransformDctFlag = 0x04U;

/// The chunks of one frame payload, as views into it.
struct FrameChunks {
    /// The palette chunk, length byte included; empty when the frame has none.
    std::span<const uint8_t> palette;
    /// Each track's audio chunk after its length word; empty for a track
    /// without one in this frame.
    std::array<std::span<const uint8_t>, kSmackerAudioTrackCount> audio{};
    /// The video chunk: everything after the palette and audio chunks.
    std::span<const uint8_t> video;
};

/// How an audio track is coded.
enum class AudioCoding : uint8_t {
    none,   ///< no track, or one this decoder does not play
    pcm,    ///< plain samples
    packed, ///< Huffman-coded sample deltas
};

/// The samples one audio track holds.
struct AudioFormat {
    AudioCoding coding{};
    uint32_t sample_rate{}; ///< hertz
    uint8_t channels{};     ///< 1 or 2
    uint8_t bits{};         ///< 8 or 16
};

/// Splits a frame payload into its chunks.
///
/// The palette chunk comes first when the frame type has kFramePaletteFlag,
/// then an audio chunk for each track whose bit is set, in track order, and
/// the video chunk takes the rest.
///
/// @param payload the frame's payload, as the frame-size table gives it
/// @param frame_type the frame's entry in the frame-type table
/// @param header the movie's header, for each track's format
/// @param[out] chunks the chunks, viewing `payload`
/// @param[out] error why the payload was refused
/// @return true when every chunk lies within the payload
[[nodiscard]] bool split_frame(
    std::span<const uint8_t> payload,
    uint8_t frame_type,
    const Header& header,
    FrameChunks& chunks,
    std::string& error
);

/// Applies a palette chunk to the movie palette.
///
/// The chunk updates the colours in order. A byte with bit 7 set keeps the
/// next (low seven bits + 1) colours. A byte with bit 6 set, followed by a
/// start colour, copies the next (low six bits + 1) colours from the
/// palette as it was before this chunk. Any other byte is a new colour: it
/// and the next two bytes give red, green and blue in six bits each, widened
/// to eight bits by repeating their top two bits below them.
///
/// @param chunk the palette chunk, length byte included
/// @param[in,out] palette the movie palette, updated in place
/// @param[out] error why the chunk was refused
/// @return true when the chunk was applied; the palette may be partly
///         updated when it is refused
[[nodiscard]] bool
update_palette(std::span<const uint8_t> chunk, Palette& palette, std::string& error);

/// Returns how a header's audio track is coded.
///
/// A track with a zero sample rate, or one coded with either transform
/// codec, has AudioCoding::none.
///
/// @param track the header's description of the track
/// @return its coding, rate, channels and sample size
[[nodiscard]] AudioFormat audio_format(const AudioTrack& track) noexcept;

/// Decodes one audio chunk to interleaved signed 16-bit samples.
///
/// A packed chunk starts with its decoded size in bytes, then a bit that is
/// clear when the chunk holds no samples, the stereo and 16-bit bits (which
/// must match the track), one Huffman tree per byte of a sample of each
/// channel, the first sample of each channel and then each later sample as
/// a coded delta from the channel's previous sample, wrapping around.
/// 8-bit samples are unsigned and widened: (sample - 128) * 256.
///
/// @param chunk the audio chunk after its length word
/// @param format the track's format, from audio_format()
/// @param[out] samples the decoded samples, replaced; empty for a chunk
///             with no samples
/// @param[out] error why the chunk was refused
/// @return true when the chunk was decoded
[[nodiscard]] bool decode_audio(
    std::span<const uint8_t> chunk,
    const AudioFormat& format,
    std::vector<int16_t>& samples,
    std::string& error
);

/// A Huffman table whose leaves are values, kept as a flattened tree.
///
/// Each entry is a leaf value, or kNodeFlag with the number of entries in the
/// node's first subtree; the first subtree follows the node and the second
/// follows the first. A clear bit takes the first subtree.
struct CodeTree {
    /// Marks an entry that is a node rather than a leaf.
    static constexpr uint32_t kNodeFlag = 0x80000000U;
    /// Bits a lookup of `first_steps` consumes at most.
    static constexpr unsigned kLookupBits = 8;
    /// Bits of a `first_steps` value that count the bits consumed.
    static constexpr unsigned kLookupCountBits = 4;
    std::vector<uint32_t> entries;
    /// The three entries that hold the most recent values; see VideoDecoder.
    std::array<uint32_t, 3> recent{};
    /// For each value of the next kLookupBits bits: the entry reached from
    /// the root, shifted left by kLookupCountBits, plus the bits it took.
    std::array<uint32_t, 1U << kLookupBits> first_steps{};
};

/// The video decoder of one SMK2 movie: four Huffman tables and the frame.
///
/// The frame is width by height palette indices, starting at index 0, and
/// each decoded chunk updates it in 4x4 blocks in row order. Each block run
/// starts with a type code: its low two bits give the kind, the next six a
/// run length (1 to 59, then 128, 256, 512, 1024 and 2048 blocks) and the
/// high byte a fill colour. Mono blocks take a two-colour code and a 16-bit
/// mask (bit n picks the high colour for pixel n, row by row); full blocks
/// take eight codes of two pixels each, per row the right pair first; skip
/// blocks keep the previous frame; fill blocks take the fill colour.
///
/// The 16-bit tables keep three "recent value" leaves: each frame starts
/// with them at zero, and every decoded value different from the most
/// recent one shifts it into them, so those leaves repeat recent values.
class VideoDecoder {
  public:

    /// Reads the movie's Huffman tables.
    ///
    /// @param header the movie's header: size and table sizes
    /// @param trees the Huffman tree block that follows the frame tables
    /// @param[out] error why the tables were refused
    /// @return the decoder with a frame of index 0, or nullopt on failure
    [[nodiscard]] static std::optional<VideoDecoder>
    create(const Header& header, std::span<const uint8_t> trees, std::string& error);

    /// Decodes one video chunk onto the frame.
    ///
    /// @param chunk the frame's video chunk
    /// @param[out] error why the chunk was refused; the frame may be partly
    ///             updated
    /// @return true when the whole frame was decoded
    [[nodiscard]] bool decode(std::span<const uint8_t> chunk, std::string& error);

    /// Returns the frame: width() * height() palette indices, row by row.
    [[nodiscard]] std::span<const uint8_t> pixels() const noexcept { return pixels_; }

    /// Returns the frame's width in pixels.
    [[nodiscard]] uint32_t width() const noexcept { return width_; }

    /// Returns the frame's height in pixels.
    [[nodiscard]] uint32_t height() const noexcept { return height_; }

  private:

    VideoDecoder() = default;
    CodeTree mono_masks_;
    CodeTree mono_colours_;
    CodeTree full_pixels_;
    CodeTree block_types_;
    std::vector<uint8_t> pixels_;
    uint32_t width_{};
    uint32_t height_{};
};

} // namespace oa::formats::smacker
