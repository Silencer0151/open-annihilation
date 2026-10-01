// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The Smacker decoder on streams built bit by bit: the small movie of
// smacker_test_movie.hpp frame by frame, then palettes, audio and video
// tables one at a time, and the refusal of malformed input.
#include "oa/formats/smacker.hpp"
#include "oa/formats/smacker/decoder.hpp"
#include "smacker_test_movie.hpp"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <source_location>
#include <span>
#include <string>
#include <vector>

namespace {

namespace smacker = oa::formats::smacker;
namespace movie = oa::formats::smacker::test_movie;

int failures = 0;

/// Reports a failed check with its line and counts it.
///
/// @param condition the check
/// @param what what was expected
/// @param where the check's place in the source
/// @return `condition`
bool check(
    bool condition, const char* what, std::source_location where = std::source_location::current()
) {
    if (!condition) {
        std::fprintf(stderr, "%s:%u: FAIL: %s\n", where.file_name(), where.line(), what);
        ++failures;
    }
    return condition;
}

/// Returns a header with the test movie's size, tables and track.
smacker::Header movie_header() {
    smacker::Header header;
    header.signature = smacker::kSmk2;
    header.width = movie::kWidth;
    header.height = movie::kHeight;
    header.frame_count = movie::kFrames;
    header.frame_rate = movie::kFrameRate;
    header.mmap_size = movie::kTableBytes;
    header.mclr_size = movie::kTableBytes;
    header.full_size = movie::kTableBytes;
    header.type_size = movie::kTableBytes;
    header.audio[0].packed_rate_flags = movie::kTrackRate;
    return header;
}

/// Returns one colour of a palette.
///
/// @param palette the palette
/// @param colour 0 to 255
std::array<uint8_t, 3> colour_of(const smacker::Palette& palette, std::size_t colour) {
    return {palette[colour * 3], palette[colour * 3 + 1], palette[colour * 3 + 2]};
}

/// Plays the test movie from a file through the reader and the decoders.
///
/// @param scratch directory for the file
void test_movie(const std::filesystem::path& scratch) {
    const auto path = scratch / "oa-smacker-decoder-test.smk";
    {
        const auto bytes = movie::movie_file();
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        output.write(
            reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
        );
    }
    const auto opened = smacker::SmackerReader::open(path);
    if (!check(static_cast<bool>(opened), "the movie opens"))
        return;
    const auto& reader = *opened.reader;
    const auto& header = reader.header();
    std::string error;
    std::vector<uint8_t> trees;
    check(reader.read_huffman_trees(trees, error), "the trees are read");
    auto video = smacker::VideoDecoder::create(header, trees, error);
    if (!check(video.has_value(), "the tables are read"))
        return;
    check(video->width() == movie::kWidth && video->height() == movie::kHeight, "frame size");
    for (const auto pixel : video->pixels())
        check(pixel == 0, "the frame starts at index 0");
    const auto format = smacker::audio_format(header.audio[0]);
    check(
        format.coding == smacker::AudioCoding::packed && format.sample_rate == 22050 &&
            format.channels == 2 && format.bits == 8,
        "the track is packed 8-bit stereo at 22050 Hz"
    );
    smacker::Palette palette{};
    std::vector<int16_t> samples;
    for (uint32_t index = 0; index < movie::kFrames; ++index) {
        std::vector<uint8_t> payload;
        check(reader.read_frame(index, payload, error), "the payload is read");
        smacker::FrameChunks chunks;
        if (!check(
                smacker::split_frame(payload, reader.frame(index)->type, header, chunks, error),
                "the frame splits"
            ))
            return;
        if (!chunks.palette.empty())
            check(smacker::update_palette(chunks.palette, palette, error), "the palette applies");
        if (!chunks.audio[0].empty()) {
            check(
                smacker::decode_audio(chunks.audio[0], format, samples, error), "the audio decodes"
            );
            if (index == 0)
                check(
                    samples ==
                        std::vector<int16_t>(
                            movie::kFirstAudioSamples.begin(), movie::kFirstAudioSamples.end()
                        ),
                    "frame 0's samples: deltas per channel, widened from 8 bits"
                );
            else
                check(samples.empty(), "frame 1's chunk holds no samples");
        }
        if (!check(video->decode(chunks.video, error), "the video decodes"))
            return;
        const auto expected = movie::expected_indices(index);
        check(
            std::equal(
                expected.begin(), expected.end(), video->pixels().begin(), video->pixels().end()
            ),
            index == 0   ? "frame 0: fill, mono, full with recent values, skip"
            : index == 1 ? "frame 1: a fill run clipped to the frame"
                         : "frame 2: recent values restart, skip keeps frame 1, mono"
        );
        if (index == 0) {
            check(
                colour_of(palette, 0) == std::array<uint8_t, 3>{0, 0, 255},
                "colour 0 is (0, 0, 63) widened"
            );
            check(
                colour_of(palette, 1) == std::array<uint8_t, 3>{4, 0, 251},
                "colour 1 is (1, 0, 62) widened"
            );
            check(
                colour_of(palette, 0x30) == std::array<uint8_t, 3>{195, 48, 60},
                "colour 0x30 is (48, 12, 15) widened"
            );
        }
    }
    check(
        colour_of(palette, 0) == std::array<uint8_t, 3>{4, 0, 251},
        "frame 2 copies the old colour 1 to 0"
    );
    check(colour_of(palette, 1) == std::array<uint8_t, 3>{0, 0, 255}, "and the old colour 0 to 1");
    check(colour_of(palette, 0x30) == std::array<uint8_t, 3>{195, 48, 60}, "and keeps the rest");
    std::filesystem::remove(path);
}

/// Checks palette chunks that keep colours, and the refused ones.
void test_palette() {
    std::string error;
    smacker::Palette palette{};
    palette[3] = 7;
    // One new colour (63, 31, 0), keep 255.
    const std::vector<uint8_t> new_then_keep = {2, 0x3F, 0x1F, 0x00, 0xFF, 0xFF, 0, 0};
    check(smacker::update_palette(new_then_keep, palette, error), "keeps after a new colour");
    check(colour_of(palette, 0) == std::array<uint8_t, 3>{255, 125, 0}, "six bits widen to eight");
    check(palette[3] == 7, "kept colours are unchanged");

    const std::vector<uint8_t> copy_past_end = {1, 0x41, 0xFF, 0};
    check(
        !smacker::update_palette(copy_past_end, palette, error), "a copy past colour 255 is refused"
    );
    const std::vector<uint8_t> truncated = {1, 0x80, 0x80, 0x80};
    check(
        !smacker::update_palette(truncated, palette, error),
        "a chunk short of 256 colours is refused"
    );
    const std::vector<uint8_t> truncated_colour = {1, 0x01, 0x02};
    check(
        !smacker::update_palette(truncated_colour, palette, error), "a cut-off colour is refused"
    );
}

/// Checks 16-bit packed audio, plain samples and refused chunks.
void test_audio() {
    std::string error;
    std::vector<int16_t> samples;
    // 16-bit mono: first sample 0x1234 stored high byte first, then +0x0010
    // and +0xFFF0 (wrapping back).
    {
        movie::BitWriter bits;
        bits.put(1);
        bits.put(0);
        bits.put(1);
        for (const auto& tree :
             {movie::node(movie::leaf(0x10), movie::leaf(0xF0)),
              movie::node(movie::leaf(0x00), movie::leaf(0xFF))}) {
            bits.put(0);
            movie::put_byte_tree(bits, tree);
            bits.put(0);
        }
        bits.put_bits(0x3412, 16);
        bits.put(0);
        bits.put(0);
        bits.put(1);
        bits.put(1);
        std::vector<uint8_t> chunk{6, 0, 0, 0};
        chunk.insert(chunk.end(), bits.bytes().begin(), bits.bytes().end());
        const smacker::AudioFormat mono16{smacker::AudioCoding::packed, 22050, 1, 16};
        check(smacker::decode_audio(chunk, mono16, samples, error), "16-bit mono decodes");
        check(samples == std::vector<int16_t>{0x1234, 0x1244, 0x1234}, "16-bit deltas wrap around");

        const smacker::AudioFormat stereo16{smacker::AudioCoding::packed, 22050, 2, 16};
        check(
            !smacker::decode_audio(chunk, stereo16, samples, error),
            "a mono chunk on a stereo track is refused"
        );
        const smacker::AudioFormat mono8{smacker::AudioCoding::packed, 22050, 1, 8};
        check(
            !smacker::decode_audio(chunk, mono8, samples, error),
            "a 16-bit chunk on an 8-bit track is refused"
        );
        auto longer = chunk;
        longer[0] = 0x40; // 32 samples promised, three coded
        check(
            !smacker::decode_audio(longer, mono16, samples, error),
            "a chunk short of its samples is refused"
        );
        auto odd = chunk;
        odd[0] = 5;
        check(
            !smacker::decode_audio(odd, mono16, samples, error),
            "a size of half a sample is refused"
        );
        auto huge = chunk;
        huge[3] = 0x02;
        check(
            !smacker::decode_audio(huge, mono16, samples, error), "a size over 16 MiB is refused"
        );
    }
    // 16-bit stereo with one-leaf trees: right first, left +1, right +0x8000.
    {
        movie::BitWriter bits;
        bits.put(1);
        bits.put(1);
        bits.put(1);
        for (const uint32_t value : {0x01U, 0x00U, 0x00U, 0x80U}) {
            bits.put(1);
            movie::put_byte_tree(bits, movie::leaf(value));
            bits.put(1);
        }
        bits.put_bits(0xFF7F, 16); // right 0x7FFF
        bits.put_bits(0xFFFF, 16); // left 0xFFFF
        std::vector<uint8_t> chunk{8, 0, 0, 0};
        chunk.insert(chunk.end(), bits.bytes().begin(), bits.bytes().end());
        const smacker::AudioFormat stereo16{smacker::AudioCoding::packed, 22050, 2, 16};
        check(smacker::decode_audio(chunk, stereo16, samples, error), "16-bit stereo decodes");
        check(
            samples == std::vector<int16_t>{-1, 32767, 0, -1},
            "left then right, each its own deltas"
        );
    }
    const smacker::AudioFormat stereo8{smacker::AudioCoding::packed, 22050, 2, 8};
    check(
        !smacker::decode_audio(std::vector<uint8_t>{6, 0, 0, 0}, stereo8, samples, error),
        "a chunk of only its size is refused"
    );
    check(
        smacker::decode_audio(movie::empty_audio_chunk(), stereo8, samples, error) &&
            samples.empty(),
        "a chunk whose first bit is clear holds no samples"
    );

    const smacker::AudioFormat plain16{smacker::AudioCoding::pcm, 22050, 2, 16};
    check(
        smacker::decode_audio(
            std::vector<uint8_t>{0x01, 0x80, 0xFF, 0x7F, 0x00}, plain16, samples, error
        ) && samples == std::vector<int16_t>{-32767, 32767},
        "plain 16-bit samples are little-endian; a partial sample frame is dropped"
    );
    const smacker::AudioFormat plain8{smacker::AudioCoding::pcm, 11025, 1, 8};
    check(
        smacker::decode_audio(std::vector<uint8_t>{0x00, 0x80, 0xFF}, plain8, samples, error) &&
            samples == std::vector<int16_t>{-32768, 0, 32512},
        "plain 8-bit samples are widened"
    );
    check(
        !smacker::decode_audio(std::vector<uint8_t>{0x01}, plain16, samples, error),
        "less than one sample frame is refused"
    );

    smacker::AudioTrack transform;
    transform.packed_rate_flags = 0x48005622U;
    check(
        smacker::audio_format(transform).coding == smacker::AudioCoding::none,
        "transform-coded tracks are not played"
    );
    check(
        smacker::audio_format(smacker::AudioTrack{}).coding == smacker::AudioCoding::none,
        "a track without a rate is none"
    );
    const smacker::AudioFormat none{};
    check(
        !smacker::decode_audio(movie::empty_audio_chunk(), none, samples, error),
        "an unplayable track is refused"
    );
}

/// Checks the chunk boundaries of frame payloads.
void test_split() {
    const auto header = movie_header();
    std::string error;
    smacker::FrameChunks chunks;
    const auto payload = movie::frame_payload(0);
    check(
        smacker::split_frame(payload, movie::frame_types()[0], header, chunks, error),
        "frame 0 splits"
    );
    check(
        chunks.palette.size() == movie::first_palette_chunk().size(),
        "the palette chunk is its length byte's size"
    );
    check(
        chunks.audio[0].size() == movie::first_audio_chunk().size(),
        "the audio chunk follows its length word"
    );
    check(chunks.audio[1].empty(), "other tracks have no chunk");
    check(
        chunks.video.data() == chunks.audio[0].data() + chunks.audio[0].size(),
        "the video chunk takes the rest"
    );

    const std::vector<uint8_t> zero_palette = {0, 1, 2, 3};
    check(
        !smacker::split_frame(zero_palette, smacker::kFramePaletteFlag, header, chunks, error),
        "an empty palette chunk is refused"
    );
    const std::vector<uint8_t> long_palette = {2, 1, 2, 3};
    check(
        !smacker::split_frame(long_palette, smacker::kFramePaletteFlag, header, chunks, error),
        "a palette chunk past the payload is refused"
    );
    const std::vector<uint8_t> short_audio = {7, 0, 0, 0, 1, 2, 3, 4};
    check(
        !smacker::split_frame(short_audio, smacker::kFrameFirstAudioFlag, header, chunks, error),
        "a packed chunk without its size is refused"
    );
    const std::vector<uint8_t> long_audio = {9, 0, 0, 0, 1, 2, 3, 4};
    check(
        !smacker::split_frame(long_audio, smacker::kFrameFirstAudioFlag, header, chunks, error),
        "an audio chunk past the payload is refused"
    );
    const std::vector<uint8_t> no_length = {1, 2};
    check(
        !smacker::split_frame(no_length, smacker::kFrameFirstAudioFlag, header, chunks, error),
        "a cut-off length word is refused"
    );
    check(
        !smacker::split_frame({}, smacker::kFramePaletteFlag, header, chunks, error),
        "a missing palette chunk is refused"
    );
}

/// Checks the tables the header leaves out and refused tables and chunks.
void test_video_refusals() {
    std::string error;
    auto header = movie_header();
    const auto trees = movie::tree_block();

    // Mono masks left out: every mono pixel takes the low colour.
    {
        movie::BitWriter bits;
        bits.put(0);
        movie::put_table(bits, movie::single_value_table(movie::kMonoColours));
        movie::put_table(bits, movie::full_table());
        movie::put_table(bits, movie::type_table());
        auto video = smacker::VideoDecoder::create(header, bits.bytes(), error);
        if (check(video.has_value(), "a table may be left out")) {
            movie::BitWriter frame;
            for (int block = 0; block < 2; ++block) {
                movie::put_code(frame, movie::type_table().values, movie::kTypeMono);
                movie::put_code(frame, movie::type_table().values, movie::kTypeMono);
            }
            check(video->decode(frame.bytes(), error), "mono blocks decode");
            for (const auto pixel : video->pixels())
                check(pixel == 7, "a left-out mask table gives mask 0");
        }
    }
    {
        movie::BitWriter bits;
        for (int table = 0; table < 4; ++table)
            bits.put(0);
        check(
            !smacker::VideoDecoder::create(header, bits.bytes(), error),
            "four left-out tables are refused"
        );
    }
    check(
        !smacker::VideoDecoder::create(header, std::span(trees).first(trees.size() / 2), error),
        "cut-off trees are refused"
    );
    auto small = header;
    small.full_size = 8; // two entries; the table needs nine
    check(
        !smacker::VideoDecoder::create(small, trees, error),
        "a table larger than its size is refused"
    );
    auto huge = header;
    huge.type_size = 0xFFFFFFFFU;
    check(
        !smacker::VideoDecoder::create(huge, trees, error), "a table size over the bound is refused"
    );
    auto smk4 = header;
    smk4.signature = smacker::kSmk4;
    check(!smacker::VideoDecoder::create(smk4, trees, error), "SMK4 is refused");
    {
        movie::BitWriter bits;
        bits.put(1);
        bits.put(1);
        for (int depth = 0; depth < 28; ++depth)
            bits.put(1);
        for (int byte = 0; byte < 16; ++byte)
            bits.put_bits(0, 8);
        check(
            !smacker::VideoDecoder::create(header, bits.bytes(), error),
            "a byte tree deeper than 27 is refused"
        );
    }
    auto video = smacker::VideoDecoder::create(header, trees, error);
    if (check(video.has_value(), "the movie's tables are read")) {
        check(!video->decode({}, error), "an empty video chunk is refused");
        const auto chunk = movie::video_chunk(0);
        check(!video->decode(std::span(chunk).first(2), error), "a cut-off video chunk is refused");
    }
}

} // namespace

int main(int argc, char** argv) {
    const auto scratch =
        argc > 1 ? std::filesystem::path(argv[1]) : std::filesystem::temp_directory_path();
    std::error_code created;
    std::filesystem::create_directories(scratch, created);
    test_movie(scratch);
    test_palette();
    test_audio();
    test_split();
    test_video_refusals();
    if (failures != 0)
        return 1;
    std::printf("Smacker decoder tests passed\n");
    return 0;
}
