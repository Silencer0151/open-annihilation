// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The offline mix: decoding and resampling, the decibel tables, voice gains,
// the game's voice policy, starts at exact samples, chunked rendering, the
// clip cache and the WAVE header.
//
// Pinned values: pinned_scene_digest is the SHA-256 of the 16-bit
// little-endian samples of the two-second synthetic scene built by
// build_scene. Every step of the mix is integer arithmetic (the gains go
// through the float placement levels, which use + - * / and sqrt only), so
// the digest is the same on every platform. It changes only when the mix is
// meant to sound different; say which step changed in the commit message.

#include "audio_test_support.hpp"
#include "oa/audio/offline_mix.hpp"
#include "oa/audio/spatial_gain.hpp"
#include "oa/base/sha256.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <map>
#include <string>
#include <vector>

using namespace oa::audio;
using namespace oa::audio::offline_mix;

namespace {

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

/// Ends the test with the failed expression's file and line when a condition is false.
void check(bool value, const char* expression, const char* file, int line) {
    if (!value) {
        std::fprintf(stderr, "%s:%d: FAILED: %s\n", file, line, expression);
        std::exit(1);
    }
}

constexpr int32_t unity = int32_t{1} << gain_bits;
constexpr uint32_t mix_rate = sample_rate;

/// The SHA-256 of the pinned scene's samples, as hexadecimal digits.
constexpr const char* pinned_scene_digest =
    "1b17ce15d159a1341a073d92f080005912efaae75b03d22962d23388d7985853";

/// Sound files held in memory, counting reads.
struct Files {
    std::map<std::string, std::vector<uint8_t>> files{};
    int32_t loads{};

    /// Returns the table that reads these files.
    ClipFileHooks hooks() { return ClipFileHooks{this, &Files::load}; }

    /// Reads one file.
    static bool load(void* context, const char* resource, std::vector<uint8_t>* bytes) {
        auto& self = *static_cast<Files*>(context);
        ++self.loads;
        const auto found = self.files.find(resource);
        if (found == self.files.end())
            return false;
        *bytes = found->second;
        return true;
    }
};

/// Returns a 16-bit RIFF file of the given samples, interleaved when stereo.
std::vector<uint8_t>
riff16(uint32_t rate, uint16_t channel_count, const std::vector<int32_t>& values) {
    std::vector<uint8_t> bytes{};
    for (int32_t value : values) {
        const auto word = static_cast<uint16_t>(static_cast<int16_t>(value));
        bytes.push_back(static_cast<uint8_t>(word & 0xff));
        bytes.push_back(static_cast<uint8_t>(word >> 8));
    }
    return audio_test::make_riff(rate, 16, channel_count, bytes);
}

/// Returns a 48 kHz mono clip holding one value throughout.
std::vector<uint8_t> constant_clip(int32_t value, size_t length) {
    return riff16(mix_rate, 1, std::vector<int32_t>(length, value));
}

/// Returns a DIGI file of 8-bit samples with the given rate field.
std::vector<uint8_t> digi_file(uint32_t rate, const std::vector<uint8_t>& samples) {
    std::vector<uint8_t> digi(digi_samples_offset, 0);
    std::copy_n("DIGI", 4, digi.begin());
    std::copy_n("HSHD", 4, digi.begin() + 8);
    std::copy_n("SDAT", 4, digi.begin() + 0x20);
    for (uint32_t i = 0; i < 4; ++i)
        digi[digi_rate_offset + i] = static_cast<uint8_t>(rate >> (8 * i));
    digi.insert(digi.end(), samples.begin(), samples.end());
    return digi;
}

/// Decodes a file that must decode.
std::vector<int16_t> decoded(const std::vector<uint8_t>& file) {
    Clip clip{};
    std::string error{};
    CHECK(decode_clip(file, clip, error));
    CHECK(error.empty());
    return clip.samples;
}

/// Checks that a file does not decode, leaving the clip empty with a reason.
void check_rejected(const std::vector<uint8_t>& file, const char* what) {
    Clip clip{};
    clip.samples.assign(3, 1);
    std::string error{};
    const bool ok = decode_clip(file, clip, error);
    if (ok || !clip.samples.empty() || error.empty()) {
        std::fprintf(stderr, "FAILED: %s was not rejected\n", what);
        std::exit(1);
    }
}

/// Renders sample frames of a mix.
std::vector<int16_t> render(OfflineMix& mix, size_t frames) {
    std::vector<int16_t> out(frames * channels);
    mix.render(out);
    return out;
}

/// Returns the left sample of a frame.
int32_t left_of(const std::vector<int16_t>& pcm, size_t frame) {
    return pcm[frame * channels];
}

/// Returns a start at full scale with no placement.
ClipStart plain(std::string resource, uint64_t sample) {
    return ClipStart{std::move(resource), full_scale_volume, Spatial{}, sample};
}

void test_decode() {
    // 8-bit unsigned widens as (x - 128) * 256; 48 kHz copies exactly.
    CHECK(
        (decoded(audio_test::make_riff(mix_rate, 8, 1, {0, 128, 255})) ==
         std::vector<int16_t>{-32768, 0, 32512})
    );
    CHECK(
        (decoded(riff16(mix_rate, 1, {-32768, -1, 0, 1, 32767})) ==
         std::vector<int16_t>{-32768, -1, 0, 1, 32767})
    );

    // A ramp at 11025 Hz: n reads n * 11025 / 48000, interpolated toward the
    // next input sample, 0 past the end, rounding toward negative infinity.
    std::vector<int32_t> ramp{};
    for (int32_t i = 0; i < 8; ++i)
        ramp.push_back(i * 1000 - 4000);
    const std::vector<int16_t> ramp_expected{
        -4000, -3771, -3541, -3311, -3082, -2852, -2622, -2393, -2163, -1933, -1704, -1474,
        -1244, -1015, -785,  -555,  -325,  -96,   134,   364,   593,   823,   1053,  1282,
        1512,  1742,  1971,  2201,  2431,  2660,  2890,  2639,  1950,  1260,  571,
    };
    CHECK(decoded(riff16(default_sample_rate, 1, ramp)) == ramp_expected);

    // Stereo at 22254 Hz: the channels average first, rounding down.
    std::vector<int32_t> stereo{};
    for (int32_t i = 0; i < 6; ++i) {
        stereo.push_back(i * 300 - 1000);
        stereo.push_back(-i * 77 + 5);
    }
    const std::vector<int16_t> stereo_expected{
        -498,
        -447,
        -395,
        -343,
        -292,
        -240,
        -188,
        -136,
        -85,
        -33,
        19,
        54,
        26,
    };
    CHECK(decoded(riff16(22254, 2, stereo)) == stereo_expected);
    CHECK((decoded(riff16(mix_rate, 2, {-3, 0, 3, 0})) == std::vector<int16_t>{-2, 1}));

    // DIGI's 11000 means 11025 Hz; a headerless file is 8-bit 11025 Hz.
    const auto digi = decoded(digi_file(digi_nominal_rate, {128, 129, 130}));
    CHECK(
        (digi ==
         std::vector<int16_t>{0, 58, 117, 176, 235, 294, 352, 411, 470, 477, 360, 242, 124, 7})
    );
    const std::vector<uint8_t> raw{128, 129, 130};
    CHECK(decoded(raw) == digi);
    CHECK((decoded(audio_test::make_riff(96000, 8, 1, {138, 148})) == std::vector<int16_t>{2560}));
}

void test_malformed() {
    auto truncated = riff16(mix_rate, 1, {1, 2, 3, 4});
    truncated.resize(truncated.size() - 1);
    check_rejected(truncated, "a RIFF file whose data runs past its end");
    check_rejected(audio_test::make_riff(mix_rate, 16, 1, {}), "a RIFF file with no data");
    auto huge = riff16(mix_rate, 1, {1, 2});
    const size_t data_size_at = huge.size() - 4 - 4;
    for (size_t i = 0; i < 4; ++i)
        huge[data_size_at + i] = 0xf0;
    check_rejected(huge, "a RIFF data size of about four gigabytes");
    check_rejected(audio_test::make_riff(mix_rate, 16, 1, {7}), "a lone byte of a 16-bit frame");
    check_rejected(audio_test::make_riff(mix_rate, 24, 1, {1, 2, 3}), "24-bit samples");
    check_rejected(audio_test::make_riff(mix_rate, 8, 3, {1, 2, 3}), "three channels");
    check_rejected(audio_test::make_riff(3999, 8, 1, {1, 2, 3}), "3999 Hz");
    check_rejected(audio_test::make_riff(96001, 8, 1, {1, 2, 3}), "96001 Hz");
    check_rejected(audio_test::make_riff(0, 8, 1, {1, 2, 3}), "0 Hz");
    check_rejected({}, "an empty file");
    auto short_digi = digi_file(digi_nominal_rate, {});
    short_digi.resize(0x26);
    check_rejected(short_digi, "a DIGI file that ends inside its header");
    check_rejected(
        std::vector<uint8_t>(max_clip_file_bytes + 1, 128), "a file over max_clip_file_bytes"
    );
    // 16 MiB of 4000 Hz 8-bit samples would be 192 million samples at 48 kHz.
    check_rejected(
        digi_file(4000, std::vector<uint8_t>(max_clip_file_bytes - 64, 128)),
        "a clip longer than the mix plays"
    );
    // Stopping at the bound allocated nothing unbounded; a clip just inside it decodes.
    CHECK(decoded(digi_file(48000, std::vector<uint8_t>(1000, 128))).size() == 1000);
}

/// A volume ten decibels below full scale, whose gain is pinned.
constexpr int32_t ten_decibels_below{full_scale_volume - 1000};

void test_centibel_gain() {
    CHECK(centibel_gain(full_scale_volume) == unity);
    CHECK(centibel_gain(full_scale_volume + 1) == unity);
    CHECK(centibel_gain(0) == unity);
    CHECK(centibel_gain(INT32_MAX) == unity);
    CHECK(centibel_gain(INT32_MIN) == 0);
    CHECK(centibel_gain(ten_decibels_below) == 10362);
    CHECK(centibel_gain(volume_near) == 16708);
    // Both tables and the decades against a power function, to one step of
    // Q15, over six decades.
    int32_t previous = unity;
    for (int32_t below = 0; below <= 12000; ++below) {
        const int32_t gain = centibel_gain(full_scale_volume - below);
        const double exact = std::pow(10.0, -below / 2000.0) * unity;
        const auto expected = static_cast<int32_t>(std::floor(exact));
        if (std::abs(gain - expected) > 1) {
            std::fprintf(
                stderr, "centibel_gain %d below: %d, expected %d\n", below, gain, expected
            );
            std::exit(1);
        }
        CHECK(gain <= previous);
        previous = gain;
    }
}

void test_voice_gains() {
    const VoiceGains plain_gains = voice_gains(ten_decibels_below, Spatial{});
    CHECK(plain_gains.left == 10362 && plain_gains.right == 10362);
    const float min_distance = 100.0F;
    const float max_distance = 5000.0F;
    const VoicePosition right_side{200, 0, 0};
    const VoiceGains right =
        voice_gains(full_scale_volume, voice_spatial(1, min_distance, max_distance, &right_side));
    CHECK(right.left == 0 && right.right == unity / 2);
    const VoicePosition ahead{0, 0, 50};
    const VoiceGains near =
        voice_gains(ten_decibels_below, voice_spatial(1, min_distance, max_distance, &ahead));
    CHECK(near.left == 10362 && near.right == 10362);
    const VoicePosition left_side{-300, 0, 400};
    const Spatial placed = voice_spatial(1, min_distance, max_distance, &left_side);
    const StereoGain level = spatial_stereo_gain(placed);
    const VoiceGains left = voice_gains(full_scale_volume, placed);
    CHECK(left.left == static_cast<int32_t>(std::floor(level.left * unity)));
    CHECK(left.right == static_cast<int32_t>(std::floor(level.right * unity)));
    CHECK(left.left == 6553 && left.right == 2621);
    CHECK(default_master_gain == 13824);
}

// The ninth voice stops the oldest; a finished voice frees its place first.
void test_voice_limit() {
    Files files{};
    for (int32_t k = 0; k < 9; ++k)
        files.files["sounds/c" + std::to_string(k) + ".wav"] = constant_clip(1 << k, 1000);
    OfflineMix mix{files.hooks(), unity};
    for (uint64_t k = 0; k < 9; ++k)
        mix.start(plain("sounds/c" + std::to_string(k) + ".wav", k * 10));
    const auto pcm = render(mix, 200);
    CHECK(left_of(pcm, 5) == 1);
    CHECK(left_of(pcm, 75) == 255);
    CHECK(left_of(pcm, 85) == 510);
    CHECK(pcm[85 * channels + 1] == 510);
    CHECK(mix.voices_playing() == voice_limit);

    Files ending{};
    for (int32_t k = 0; k < 7; ++k)
        ending.files["sounds/c" + std::to_string(k) + ".wav"] = constant_clip(1 << k, 1000);
    ending.files["sounds/short.wav"] = constant_clip(128, 93);
    ending.files["sounds/late.wav"] = constant_clip(256, 1000);
    OfflineMix finished{ending.hooks(), unity};
    for (uint64_t k = 0; k < 7; ++k)
        finished.start(plain("sounds/c" + std::to_string(k) + ".wav", k));
    finished.start(plain("sounds/short.wav", 7));
    finished.start(plain("sounds/late.wav", 100));
    const auto kept = render(finished, 110);
    CHECK(left_of(kept, 99) == 255);
    CHECK(left_of(kept, 100) == 127 + 256);
    CHECK(finished.voices_playing() == 8);
}

// A fifth start of one clip restarts its furthest-played buffer, which then
// holds two voices until it stops, as the game's mixer counts it.
void test_restart() {
    Files files{};
    std::vector<int32_t> ramp{};
    for (int32_t p = 0; p < 1000; ++p)
        ramp.push_back(p + 1);
    files.files["sounds/ramp.wav"] = riff16(mix_rate, 1, ramp);
    files.files["sounds/c1.wav"] = constant_clip(2000, 1000);
    files.files["sounds/c2.wav"] = constant_clip(3000, 1000);
    files.files["sounds/c3.wav"] = constant_clip(5000, 1000);
    files.files["sounds/c4.wav"] = constant_clip(7000, 1000);
    files.files["sounds/c5.wav"] = constant_clip(100, 1000);
    OfflineMix mix{files.hooks(), unity};
    for (uint64_t k = 0; k < 5; ++k)
        mix.start(plain("sounds/ramp.wav", k * 10));
    auto pcm = render(mix, 46);
    // Buffers started at 10, 20 and 30 play on; the one from 0 restarted at 40.
    CHECK(left_of(pcm, 39) == 40 + 30 + 20 + 10);
    CHECK(left_of(pcm, 45) == 36 + 26 + 16 + 6);
    CHECK(mix.voices_playing() == 5);

    // Three more voices fill the eight; the ninth stops the oldest voice's
    // buffer, which is the restarted one, and its second voice is freed at
    // the next start.
    mix.start(plain("sounds/c1.wav", 50));
    mix.start(plain("sounds/c2.wav", 60));
    mix.start(plain("sounds/c3.wav", 70));
    mix.start(plain("sounds/c4.wav", 80));
    pcm = render(mix, 44);
    CHECK(left_of(pcm, 75 - 46) == 66 + 56 + 46 + 36 + 2000 + 3000 + 5000);
    CHECK(left_of(pcm, 85 - 46) == 76 + 66 + 56 + 2000 + 3000 + 5000 + 7000);
    CHECK(mix.voices_playing() == 7);
    mix.start(plain("sounds/c5.wav", 100));
    pcm = render(mix, 16);
    CHECK(left_of(pcm, 105 - 90) == 96 + 86 + 76 + 17000 + 100);
    CHECK(mix.voices_playing() == 8);

    // Once every buffer has played out, no voice is left.
    render(mix, 2000);
    CHECK(mix.voices_playing() == 0);
}

// Starts apply in sample order, then queue order; a late start applies at once.
void test_start_order() {
    Files files{};
    files.files["sounds/a.wav"] = constant_clip(10, 100);
    files.files["sounds/b.wav"] = constant_clip(1000, 100);
    OfflineMix mix{files.hooks(), unity};
    mix.start(plain("sounds/b.wav", 20));
    mix.start(plain("sounds/a.wav", 10));
    auto pcm = render(mix, 30);
    CHECK(left_of(pcm, 9) == 0 && left_of(pcm, 10) == 10 && left_of(pcm, 19) == 10);
    CHECK(left_of(pcm, 20) == 1010);
    CHECK(mix.position() == 30);
    mix.start(plain("sounds/a.wav", 5));
    pcm = render(mix, 1);
    CHECK(left_of(pcm, 0) == 1020);
    CHECK(mix.position() == 31);

    // Saturation at 16 bits, after the master gain.
    Files loud{};
    loud.files["sounds/loud.wav"] = constant_clip(30000, 10);
    OfflineMix saturated{loud.hooks(), unity};
    saturated.start(plain("sounds/loud.wav", 0));
    saturated.start(plain("sounds/loud.wav", 0));
    pcm = render(saturated, 1);
    CHECK(left_of(pcm, 0) == 32767);
    OfflineMix halved{loud.hooks(), unity / 2};
    halved.start(plain("sounds/loud.wav", 0));
    halved.start(plain("sounds/loud.wav", 0));
    pcm = render(halved, 1);
    CHECK(left_of(pcm, 0) == 30000);
    OfflineMix clamped{loud.hooks(), unity * 4};
    clamped.start(plain("sounds/loud.wav", 0));
    pcm = render(clamped, 1);
    CHECK(left_of(pcm, 0) == 30000);
}

// Clips that cannot be read or decoded play nothing and report once.
void test_errors() {
    Files files{};
    files.files["sounds/BAD.wav"] = audio_test::make_riff(mix_rate, 16, 1, {});
    OfflineMix mix{files.hooks(), unity};
    mix.start(plain("sounds/missing.wav", 0));
    mix.start(plain("sounds\\BAD.wav", 1));
    mix.start(plain("sounds/missing.wav", 2));
    mix.start(plain("SOUNDS/bad.wav", 3));
    const auto pcm = render(mix, 10);
    CHECK(std::all_of(pcm.begin(), pcm.end(), [](int16_t s) { return s == 0; }));
    CHECK(mix.errors().size() == 2);
    CHECK(mix.errors()[0].starts_with("sounds/missing.wav: "));
    CHECK(mix.errors()[1].starts_with("sounds/BAD.wav: "));
    CHECK(files.loads == 2);
    CHECK(mix.voices_playing() == 0);

    OfflineMix unread{ClipFileHooks{}, unity};
    unread.start(plain("sounds/a.wav", 0));
    render(unread, 1);
    CHECK(unread.errors().size() == 1);
}

// A full cache drops the least recently started clip.
void test_cache() {
    Files files{};
    const auto name = [](size_t k) { return "sounds/k" + std::to_string(k) + ".wav"; };
    for (size_t k = 0; k <= max_cached_clips; ++k)
        files.files[name(k)] = constant_clip(1, 10);
    OfflineMix mix{files.hooks(), unity};
    uint64_t at = 0;
    for (size_t k = 0; k <= max_cached_clips; ++k, at += 20)
        mix.start(plain(name(k), at));
    render(mix, static_cast<size_t>(at));
    CHECK(files.loads == static_cast<int32_t>(max_cached_clips) + 1);
    mix.start(plain(name(0), at));
    render(mix, 20);
    CHECK(files.loads == static_cast<int32_t>(max_cached_clips) + 2);
    mix.start(plain(name(max_cached_clips), at + 20));
    render(mix, 20);
    CHECK(files.loads == static_cast<int32_t>(max_cached_clips) + 2);
    mix.start(plain(name(1), at + 40));
    render(mix, 20);
    CHECK(files.loads == static_cast<int32_t>(max_cached_clips) + 3);
}

/// A 32-bit linear congruential sequence for the synthetic scene.
struct SceneRandom {
    uint32_t state{12345};

    /// Returns the next value, 0 to 32767.
    uint32_t next() {
        state = state * 1103515245U + 12345U;
        return state >> 16 & 0x7fff;
    }
};

/// Starts in the scene's burst.
constexpr uint64_t scene_burst_starts = 12;
/// The sample frame the scene's burst begins on.
constexpr uint64_t scene_burst_sample = 48000;

/// Adds the synthetic scene's clips to a file table and returns its starts.
std::vector<ClipStart> build_scene(Files& files) {
    SceneRandom random{};
    std::vector<int32_t> triangle{};
    for (int32_t i = 0; i < 2000; ++i)
        triangle.push_back((i % 200 < 100 ? i % 100 : 100 - i % 100) * 400 - 20000);
    files.files["sounds/triangle.wav"] = riff16(22050, 1, triangle);
    std::vector<uint8_t> noise{};
    for (int32_t i = 0; i < 3000; ++i)
        noise.push_back(static_cast<uint8_t>(random.next() & 0xff));
    files.files["sounds/noise.wav"] = audio_test::make_riff(default_sample_rate, 8, 1, noise);
    std::vector<int32_t> square{};
    for (int32_t i = 0; i < 4410; ++i) {
        square.push_back(i % 100 < 50 ? 12000 : -12000);
        square.push_back(i % 60 < 30 ? -9000 : 9000);
    }
    files.files["sounds/square.wav"] = riff16(44100, 2, square);
    std::vector<uint8_t> saw{};
    for (int32_t i = 0; i < 1500; ++i)
        saw.push_back(static_cast<uint8_t>(i * 3 & 0xff));
    files.files["sounds/SAW.wav"] = digi_file(digi_nominal_rate, saw);

    const std::array<const char*, 5> names{
        "sounds/triangle.wav",
        "sounds/noise.wav",
        "sounds/square.wav",
        "sounds\\SAW.wav",
        "sounds/absent.wav",
    };
    std::vector<ClipStart> starts{};
    uint64_t at = 0;
    for (int32_t i = 0; i < 60; ++i) {
        // Bursts of starts on one sample, and gaps.
        if (random.next() % 3 != 0)
            at += random.next() % 4000;
        ClipStart start{};
        start.resource = names[random.next() % names.size()];
        start.volume = volume_far - 500 + static_cast<int32_t>(random.next() % 1600);
        start.start_sample = at;
        if (random.next() % 2 == 0) {
            const VoicePosition position{
                static_cast<int32_t>(random.next() % 2000) - 1000,
                static_cast<int32_t>(random.next() % 200) - 100,
                static_cast<int32_t>(random.next() % 2000) - 1000,
            };
            start.spatial = voice_spatial(1, 300.0F, 4000.0F, &position);
        }
        starts.push_back(start);
    }
    // A burst that fills every voice and starts one clip six times.
    for (uint64_t i = 0; i < scene_burst_starts; ++i)
        starts.push_back(plain(names[i % 2], scene_burst_sample + i * 7));
    std::stable_sort(starts.begin(), starts.end(), [](const ClipStart& a, const ClipStart& b) {
        return a.start_sample < b.start_sample;
    });
    return starts;
}

/// Returns the SHA-256 of 16-bit samples written little-endian, as hexadecimal digits.
std::string digest_hex(const std::vector<int16_t>& pcm) {
    std::vector<uint8_t> bytes{};
    for (int16_t sample : pcm) {
        const auto word = static_cast<uint16_t>(sample);
        bytes.push_back(static_cast<uint8_t>(word & 0xff));
        bytes.push_back(static_cast<uint8_t>(word >> 8));
    }
    const auto digest = oa::base::sha256::digest_of(bytes);
    std::string hex{};
    for (uint8_t byte : digest) {
        char pair[3]{};
        std::snprintf(pair, sizeof(pair), "%02x", byte);
        hex += pair;
    }
    return hex;
}

// Rendering in pieces, with starts queued ahead or just in time, equals one
// render; the whole scene's samples are pinned.
void test_chunks_and_pin() {
    constexpr size_t scene_frames = 2 * mix_rate;
    Files files{};
    const std::vector<ClipStart> starts = build_scene(files);

    OfflineMix whole{files.hooks()};
    for (const ClipStart& start : starts)
        whole.start(start);
    const auto expected = render(whole, scene_frames);
    CHECK(whole.position() == scene_frames);
    CHECK(whole.errors().size() == 1);

    OfflineMix burst{files.hooks()};
    for (const ClipStart& start : starts)
        burst.start(start);
    const auto before_burst = render(burst, scene_burst_sample + scene_burst_starts * 7);
    CHECK(burst.voices_playing() == voice_limit);
    CHECK(std::equal(before_burst.begin(), before_burst.end(), expected.begin()));

    const std::array<size_t, 7> pieces{1, 799, 1600, 7, 4800, 1, 12345};
    OfflineMix ahead{files.hooks()};
    for (const ClipStart& start : starts)
        ahead.start(start);
    OfflineMix in_time{files.hooks()};
    std::vector<int16_t> ahead_pcm{};
    std::vector<int16_t> in_time_pcm{};
    size_t next_start = 0;
    size_t done = 0;
    for (size_t p = 0; done < scene_frames; ++p) {
        const size_t count = std::min(pieces[p % pieces.size()], scene_frames - done);
        while (next_start < starts.size() && starts[next_start].start_sample < done + count)
            in_time.start(starts[next_start++]);
        const auto a = render(ahead, count);
        const auto b = render(in_time, count);
        ahead_pcm.insert(ahead_pcm.end(), a.begin(), a.end());
        in_time_pcm.insert(in_time_pcm.end(), b.begin(), b.end());
        done += count;
    }
    CHECK(ahead_pcm == expected);
    CHECK(in_time_pcm == expected);

    size_t silent = 0;
    int32_t peak = 0;
    for (int16_t sample : expected) {
        silent += sample == 0 ? 1 : 0;
        peak = std::max(peak, std::abs(int32_t{sample}));
    }
    CHECK(silent < expected.size() / 20);
    CHECK(peak > 20000);
    const std::string digest = digest_hex(expected);
    if (digest != pinned_scene_digest) {
        std::fprintf(stderr, "scene digest %s, pinned %s\n", digest.c_str(), pinned_scene_digest);
        std::exit(1);
    }
}

void test_wave_header() {
    const std::array<uint8_t, wave_header_bytes> expected{
        'R',  'I',  'F', 'F', 0x24, 0xee, 0x02, 0x00, 'W', 'A',  'V',  'E',  'f',  'm',  't',
        ' ',  16,   0,   0,   0,    1,    0,    2,    0,   0x80, 0xbb, 0x00, 0x00, 0x00, 0xee,
        0x02, 0x00, 4,   0,   16,   0,    'd',  'a',  't', 'a',  0x00, 0xee, 0x02, 0x00,
    };
    CHECK(wave_header(mix_rate) == expected);
    const auto empty = wave_header(0);
    CHECK(empty[4] == 36 && empty[40] == 0 && empty[43] == 0);
    // 2^30 frames are 4 GiB of samples: both sizes are all ones.
    const auto oversized = wave_header(uint64_t{1} << 30);
    for (size_t i = 0; i < 4; ++i)
        CHECK(oversized[4 + i] == 0xff && oversized[40 + i] == 0xff);
    // Samples that fit when the RIFF size does not.
    const auto edge = wave_header((uint64_t{0xffffffff} - 20) / 4);
    CHECK(edge[4] == 0xff && edge[7] == 0xff && edge[43] == 0xff && edge[40] == 0xe8);
}

// Every sound file of the installed game decodes.
void test_installed(const oa::AssetStore& assets) {
    int32_t files = 0;
    int32_t empty = 0;
    uint64_t samples = 0;
    for (const auto& name : assets.list_effective_recursive("", ".wav")) {
        const auto bytes = oa::test::read_game_file(assets, name);
        if (bytes.empty()) {
            ++empty;
            continue;
        }
        Clip clip{};
        std::string error{};
        if (!decode_clip(bytes, clip, error)) {
            std::fprintf(stderr, "FAILED: %s: %s\n", name.c_str(), error.c_str());
            std::exit(1);
        }
        CHECK(!clip.samples.empty());
        ++files;
        samples += clip.samples.size();
    }
    std::printf(
        "installed sounds: %d decoded, %d empty, %llu samples at 48 kHz\n",
        files,
        empty,
        static_cast<unsigned long long>(samples)
    );
    CHECK(files >= 500);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        test_installed(oa::test::require_game_assets("the installed sound decode"));
        return 0;
    }
    test_decode();
    test_malformed();
    test_centibel_gain();
    test_voice_gains();
    test_voice_limit();
    test_restart();
    test_start_order();
    test_errors();
    test_cache();
    test_wave_header();
    test_chunks_and_pin();
    return 0;
}
