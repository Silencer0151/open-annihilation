// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The music decoder: the resampler, the stereo mix, every encoding against
// the samples it was made from, damaged files, and with --data every music
// file of the installed game.

#include "oa/audio/music_decoder.hpp"
#include "oa/audio/resampler.hpp"
#include "oa/test/game_data.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <numbers>
#include <string>
#include <vector>

using namespace oa::audio;

namespace {

[[noreturn]] void fail(const char* file, int line, const char* expression) {
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    std::exit(1);
}

#define CHECK(expression)                                                                          \
    do {                                                                                           \
        if (!(expression))                                                                         \
            fail(__FILE__, __LINE__, #expression);                                                 \
    } while (false)

constexpr double tone_amplitude = 0.5 * 32767.0 / 32768.0;
constexpr float half_power = static_cast<float>(std::numbers::sqrt2 / 2.0);

const std::filesystem::path fixtures = OA_AUDIO_TEST_DATA;

std::filesystem::path scratch_directory() {
    const auto directory = std::filesystem::temp_directory_path() / "oa-music-decoder-test";
    std::filesystem::create_directories(directory);
    return directory;
}

void put_le(std::vector<uint8_t>& bytes, uint32_t value, int count) {
    for (int i = 0; i < count; ++i)
        bytes.push_back(static_cast<uint8_t>(value >> (8 * i)));
}

// Writes a RIFF WAVE file of raw sample bytes.
void write_wave(
    const std::filesystem::path& path,
    uint16_t tag,
    uint32_t rate,
    uint16_t channels,
    uint16_t bits,
    const std::vector<uint8_t>& samples
) {
    std::vector<uint8_t> file{'R', 'I', 'F', 'F'};
    put_le(file, static_cast<uint32_t>(36 + 10 + samples.size()), 4);
    file.insert(file.end(), {'W', 'A', 'V', 'E'});
    // An unknown chunk of odd size, padded, comes first.
    file.insert(file.end(), {'n', 'o', 't', 'e'});
    put_le(file, 1, 4);
    file.insert(file.end(), {'x', 0});
    file.insert(file.end(), {'f', 'm', 't', ' '});
    put_le(file, 16, 4);
    put_le(file, tag, 2);
    put_le(file, channels, 2);
    put_le(file, rate, 4);
    put_le(file, rate * channels * bits / 8U, 4);
    put_le(file, channels * bits / 8U, 2);
    put_le(file, bits, 2);
    file.insert(file.end(), {'d', 'a', 't', 'a'});
    put_le(file, static_cast<uint32_t>(samples.size()), 4);
    file.insert(file.end(), samples.begin(), samples.end());
    std::ofstream(path, std::ios::binary)
        .write(
            reinterpret_cast<const char*>(file.data()), static_cast<std::streamsize>(file.size())
        );
}

// Decodes a whole file.
std::vector<float> decode_all(const std::filesystem::path& path, MusicDecoder& decoder) {
    std::string error;
    if (!decoder.open(path, error)) {
        std::fprintf(stderr, "%s\n", error.c_str());
        fail(__FILE__, __LINE__, "decoder.open(path, error)");
    }
    std::vector<float> samples;
    while (decoder.decode(samples)) {
    }
    return samples;
}

double tone(double frequency, double frame, double rate) {
    return tone_amplitude * std::sin(2.0 * std::numbers::pi * frequency * frame / rate);
}

// Signal-to-error ratio of one decoded side against its expected samples,
// in decibels, leaving out `edge` frames at each end.
template <typename Expected>
double side_snr(const std::vector<float>& stereo, int side, std::size_t edge, Expected expected) {
    double signal = 0.0;
    double error = 0.0;
    const std::size_t frames = stereo.size() / 2;
    for (std::size_t frame = edge; frame + edge < frames; ++frame) {
        const double want = expected(static_cast<double>(frame));
        const double got = stereo[2 * frame + static_cast<std::size_t>(side)];
        signal += want * want;
        error += (got - want) * (got - want);
    }
    return error == 0.0 ? 999.0 : 10.0 * std::log10(signal / error);
}

void resampler_limits() {
    Resampler resampler;
    CHECK(!resampler.configure(resampler_min_rate - 1, 44100, 2));
    CHECK(!resampler.configure(44100, resampler_max_rate + 1, 2));
    CHECK(!resampler.configure(44100, 48000, 0));
    CHECK(!resampler.configure(44100, 48000, resampler_max_channels + 1));
    CHECK(resampler.configure(44100, 44100, 2) && resampler.passthrough());
    const std::vector<float> input{0.25F, -0.5F, 0.125F, 1.0F};
    std::vector<float> output;
    resampler.process(input, output);
    resampler.finish(output);
    CHECK(output == input);
}

void resampler_lengths_and_chunks() {
    // ceil(frames * out / in) frames, whatever the split of the input.
    const uint32_t rates[][2] = {
        {22050, 44100},
        {48000, 44100},
        {8000, 44100},
        {96000, 44100},
        {44101, 44100},
        {32000, 48000}
    };
    for (const auto& rate : rates) {
        for (const uint32_t frames : {1U, 5U, 999U, 4096U}) {
            std::vector<float> input(frames * 2);
            for (std::size_t i = 0; i < input.size(); ++i)
                input[i] = static_cast<float>(std::sin(0.01 * static_cast<double>(i)));
            Resampler whole;
            CHECK(whole.configure(rate[0], rate[1], 2));
            std::vector<float> once;
            whole.process(input, once);
            whole.finish(once);
            const uint64_t owed = (uint64_t{frames} * rate[1] + rate[0] - 1) / rate[0];
            CHECK(once.size() == owed * 2);
            Resampler pieces;
            CHECK(pieces.configure(rate[0], rate[1], 2));
            std::vector<float> split;
            std::size_t at = 0;
            for (std::size_t step = 1; at < frames; step = step * 3 + 1) {
                const std::size_t take = std::min<std::size_t>(step, frames - at);
                pieces.process(std::span<const float>(input).subspan(at * 2, take * 2), split);
                at += take;
            }
            pieces.finish(split);
            CHECK(split == once);
        }
    }
}

void resampler_quality() {
    // A constant stays constant to the ends, and a tone well inside the band
    // comes out within the error of 16-bit samples.
    Resampler resampler;
    CHECK(resampler.configure(22050, 44100, 1));
    std::vector<float> constant(500, 0.5F);
    std::vector<float> output;
    resampler.process(constant, output);
    resampler.finish(output);
    CHECK(output.size() == 1000);
    for (const float sample : output)
        CHECK(std::fabs(sample - 0.5F) < 1e-4F);

    for (const uint32_t rate : {11025U, 22050U, 32000U, 48000U, 96000U}) {
        std::vector<float> input(rate / 2);
        for (std::size_t i = 0; i < input.size(); ++i)
            input[i] = static_cast<float>(tone(1000.0, static_cast<double>(i), rate));
        CHECK(resampler.configure(rate, 44100, 1));
        output.clear();
        resampler.process(input, output);
        resampler.finish(output);
        double signal = 0.0;
        double error = 0.0;
        for (std::size_t frame = 64; frame + 64 < output.size(); ++frame) {
            const double want = tone(1000.0, static_cast<double>(frame), 44100.0);
            signal += want * want;
            error += (output[frame] - want) * (output[frame] - want);
        }
        CHECK(10.0 * std::log10(signal / error) > 85.0);
    }
}

void wave_formats() {
    const auto directory = scratch_directory();

    // Each depth scales by 2^-(bits-1); 8-bit is unsigned around 128.
    struct Case {
        uint16_t tag;
        uint16_t bits;
        std::vector<uint8_t> bytes; // one stereo frame
        float left;
        float right;
    };

    float quarter = 0.25F;
    uint32_t quarter_bits = 0;
    std::memcpy(&quarter_bits, &quarter, sizeof(quarter_bits));
    std::vector<uint8_t> float_frame;
    put_le(float_frame, quarter_bits, 4);
    put_le(float_frame, 0x80000000U | quarter_bits, 4);
    const Case cases[] = {
        {1, 8, {0xc0, 0x00}, 0.5F, -1.0F},
        {1, 16, {0x00, 0x40, 0x00, 0x80}, 0.5F, -1.0F},
        {1, 24, {0x00, 0x00, 0x40, 0xff, 0xff, 0xff}, 0.5F, -1.0F / 8388608.0F},
        {1, 32, {0x00, 0x00, 0x00, 0xc0, 0x01, 0x00, 0x00, 0x00}, -0.5F, 1.0F / 2147483648.0F},
        {3, 32, float_frame, 0.25F, -0.25F},
    };
    for (const auto& c : cases) {
        const auto path = directory / "format.wav";
        write_wave(path, c.tag, 44100, 2, c.bits, c.bytes);
        MusicDecoder decoder;
        const auto samples = decode_all(path, decoder);
        CHECK(decoder.codec() == MusicCodec::wave);
        CHECK(samples.size() == 2 && samples[0] == c.left && samples[1] == c.right);
    }
}

void wave_channel_mix() {
    const auto directory = scratch_directory();

    // Channel k of an n-channel file holds 0.5 for one frame each: the mix
    // of frame k is the share channel k adds to each side.
    struct Shares {
        float left;
        float right;
    };

    const float r = half_power;
    const std::vector<std::vector<Shares>> expected = {
        {{r, r}},
        {{1, 0}, {0, 1}},
        {{1, 0}, {0, 1}, {0, 0}},
        {{1, 0}, {0, 1}, {r, r}, {0.5F, 0.5F}},
        {{1, 0}, {0, 1}, {r, r}, {r, 0}, {0, r}},
        {{1, 0}, {0, 1}, {r, r}, {0, 0}, {r, 0}, {0, r}},
        {{1, 0}, {0, 1}, {r, r}, {0, 0}, {0.5F, 0.5F}, {r, 0}, {0, r}},
        {{1, 0}, {0, 1}, {r, r}, {0, 0}, {r, 0}, {0, r}, {r, 0}, {0, r}},
    };
    for (std::size_t index = 0; index < expected.size(); ++index) {
        const auto channels = static_cast<uint16_t>(index + 1);
        std::vector<uint8_t> bytes;
        for (uint16_t frame = 0; frame < channels; ++frame)
            for (uint16_t channel = 0; channel < channels; ++channel)
                put_le(bytes, channel == frame ? 0x4000U : 0U, 2);
        const auto path = directory / "channels.wav";
        write_wave(path, 1, 44100, channels, 16, bytes);
        MusicDecoder decoder;
        const auto samples = decode_all(path, decoder);
        CHECK(decoder.source_channels() == channels);
        CHECK(samples.size() == std::size_t{channels} * 2);
        for (std::size_t frame = 0; frame < channels; ++frame) {
            CHECK(samples[2 * frame] == 0.5F * expected[index][frame].left);
            CHECK(samples[2 * frame + 1] == 0.5F * expected[index][frame].right);
        }
    }
}

void lossless_fixture() {
    // 24-bit FLAC: the samples of the noise it was made from, exactly.
    MusicDecoder decoder;
    const auto samples = decode_all(fixtures / "noise-44100-2-24.flac", decoder);
    CHECK(decoder.codec() == MusicCodec::flac && decoder.source_rate() == 44100);
    CHECK(samples.size() == 2000 * 2);
    uint32_t state = 1;
    for (const float sample : samples) {
        state = state * 1103515245U + 12345U;
        const auto top = static_cast<int32_t>(state & 0xffffff00U);
        CHECK(sample == static_cast<float>(top) / 2147483648.0F);
    }
}

void lossy_fixtures() {
    // MPEG audio, gapless: as many frames as the tone it was made from.
    MusicDecoder decoder;
    auto samples = decode_all(fixtures / "sine-44100-2.mp3", decoder);
    CHECK(decoder.codec() == MusicCodec::mp3);
    CHECK(samples.size() == 11025 * 2);
    CHECK(side_snr(samples, 0, 0, [](double f) { return tone(440, f, 44100); }) > 60.0);
    CHECK(side_snr(samples, 1, 0, [](double f) { return tone(880, f, 44100); }) > 60.0);

    // Mono Ogg Vorbis at 22050 Hz: both sides at 1/sqrt(2), at 44100 Hz.
    samples = decode_all(fixtures / "sine-22050-1.ogg", decoder);
    CHECK(decoder.codec() == MusicCodec::vorbis && decoder.source_rate() == 22050);
    CHECK(samples.size() == 11026 * 2);
    for (const int side : {0, 1})
        CHECK(side_snr(samples, side, 32, [](double f) {
                  return half_power * tone(440, f, 44100);
              }) > 35.0);

    // 5.1 Ogg Vorbis, in its own channel order: left adds front left,
    // centre and back left; the low-frequency channel is left out.
    samples = decode_all(fixtures / "sine-44100-6.ogg", decoder);
    CHECK(decoder.source_channels() == 6 && samples.size() == 11025 * 2);
    CHECK(side_snr(samples, 0, 32, [](double f) {
              return tone(440, f, 44100) +
                     half_power * (tone(880, f, 44100) + tone(1320, f, 44100));
          }) > 30.0);
    CHECK(side_snr(samples, 1, 32, [](double f) {
              return tone(660, f, 44100) +
                     half_power * (tone(880, f, 44100) + tone(1760, f, 44100));
          }) > 30.0);

    // FLAC at 48000 Hz, converted.
    samples = decode_all(fixtures / "sine-48000-2.flac", decoder);
    CHECK(samples.size() == 4410 * 2);
    CHECK(side_snr(samples, 0, 32, [](double f) { return tone(440, f, 44100); }) > 80.0);
    CHECK(side_snr(samples, 1, 32, [](double f) { return tone(880, f, 44100); }) > 80.0);
}

void damaged_files() {
    const auto directory = scratch_directory();
    std::string error;
    MusicDecoder decoder;
    CHECK(!decoder.open(directory / "missing.ogg", error) && !error.empty());
    CHECK(decoder.codec() == MusicCodec::none);
    std::vector<float> samples;
    CHECK(!decoder.decode(samples) && samples.empty());

    // Each fixture cut short at many lengths, and with its bytes flipped,
    // either fails to open or decodes to an end; it never reads out of
    // bounds or loops.
    for (const char* name :
         {"noise-44100-2-24.flac",
          "sine-44100-2.mp3",
          "sine-22050-1.ogg",
          "sine-44100-6.ogg",
          "sine-48000-2.flac"}) {
        std::ifstream in(fixtures / name, std::ios::binary);
        const std::vector<char> whole(
            (std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>()
        );
        uint32_t state = 7;
        for (std::size_t cut = 0; cut < whole.size(); cut += 1 + cut / 3) {
            for (const bool flip : {false, true}) {
                std::vector<char> bytes(
                    whole.begin(), whole.begin() + static_cast<std::ptrdiff_t>(cut)
                );
                if (flip)
                    for (std::size_t i = 0; i < bytes.size(); i += 97) {
                        state = state * 1103515245U + 12345U;
                        bytes[i] = static_cast<char>(bytes[i] ^ static_cast<char>(state >> 24));
                    }
                const auto path = directory / "damaged.bin";
                std::ofstream(path, std::ios::binary)
                    .write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
                if (!decoder.open(path, error))
                    continue;
                samples.clear();
                std::size_t blocks = 0;
                while (decoder.decode(samples) && blocks < 10000)
                    ++blocks;
                CHECK(blocks < 10000);
            }
        }
    }

    // A chunk longer than any file ends the walk instead of seeking back.
    std::vector<uint8_t> huge{
        'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'j', 'u', 'n', 'k'
    };
    put_le(huge, 0xfffffff0U, 4);
    std::ofstream(directory / "huge.wav", std::ios::binary)
        .write(
            reinterpret_cast<const char*>(huge.data()), static_cast<std::streamsize>(huge.size())
        );
    CHECK(!decoder.open(directory / "huge.wav", error));

    // WAVE files the decoder refuses.
    const std::vector<uint8_t> frame{0, 0, 0, 0};
    write_wave(directory / "adpcm.wav", 2, 44100, 2, 4, frame);
    CHECK(!decoder.open(directory / "adpcm.wav", error));
    write_wave(directory / "nine.wav", 1, 44100, 9, 16, std::vector<uint8_t>(18));
    CHECK(!decoder.open(directory / "nine.wav", error));
    write_wave(directory / "slow.wav", 1, resampler_min_rate - 1, 1, 16, frame);
    CHECK(!decoder.open(directory / "slow.wav", error));
}

// Every music file of the installed game decodes to its end.
void installed_music(const std::filesystem::path& game_dir) {
    std::filesystem::path music;
    std::error_code error;
    for (std::filesystem::directory_iterator it(game_dir, error), end; !error && it != end;
         it.increment(error)) {
        std::string name = it->path().filename().string();
        std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        if (name == "music" && it->is_directory(error))
            music = it->path();
    }
    if (music.empty())
        oa::test::skip_test("the installed music", "the install has no music folder");
    int files = 0;
    for (const auto& entry : std::filesystem::directory_iterator(music)) {
        if (!entry.is_regular_file())
            continue;
        MusicDecoder decoder;
        const auto samples = decode_all(entry.path(), decoder);
        const double seconds = static_cast<double>(samples.size() / 2) / music_output_rate;
        std::printf(
            "%s: %u Hz, %u channels, %.2f s\n",
            entry.path().filename().string().c_str(),
            decoder.source_rate(),
            decoder.source_channels(),
            seconds
        );
        CHECK(seconds > 10.0);
        ++files;
    }
    CHECK(files > 0);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        installed_music(oa::test::require_game_directory("the installed music"));
        return 0;
    }
    resampler_limits();
    resampler_lengths_and_chunks();
    resampler_quality();
    wave_formats();
    wave_channel_mix();
    lossless_fixture();
    lossy_fixtures();
    damaged_files();
    std::filesystem::remove_all(scratch_directory());
    std::puts("music decoder: ok");
    return 0;
}
