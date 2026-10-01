// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Decodes every frame of the installed game's movies, Data/1.zrb to
// Data/5.zrb, as the intro player shows and plays them: each frame as RGB
// through its palette, and the first audio track as interleaved signed
// 16-bit samples. For the movies of the 3.1c release, known by their file
// digests, the frame and sample-block counts and two digests are pinned:
// the SHA-256 of every frame's checksum in order, and the SHA-256 of all the
// samples in order. A frame's checksum is the 64-bit FNV-1a of its RGB bytes
// read as little-endian 64-bit words, which keeps the test quick. A movie
// whose file differs is decoded through and its digests printed, without
// comparison.
// The movies are optional content: an installation without them skips.
#include "oa/base/sha256.hpp"
#include "oa/formats/smacker.hpp"
#include "oa/formats/smacker/decoder.hpp"
#include "oa/test/game_data.hpp"

#include <algorithm>
#include <array>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <optional>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

namespace {

namespace sha256 = oa::base::sha256;
namespace smacker = oa::formats::smacker;

constexpr int kInstalledMovieCount = 5;
constexpr std::size_t kRgbBytesPerPixel = 3;
constexpr std::size_t kFileReadBytes = 1U << 20;
constexpr uint64_t kFnvOffsetBasis = 0xcbf29ce484222325ULL;
constexpr uint64_t kFnvPrime = 0x100000001b3ULL;
constexpr std::size_t kWordBytes = 8;

/// What decoding one movie of the 3.1c release gives.
struct KnownMovie {
    std::string_view file_digest;
    uint32_t frames{};
    uint32_t audio_blocks{};        ///< audio chunks that held samples
    uint64_t audio_bytes{};         ///< bytes of 16-bit samples
    std::string_view frames_digest; ///< SHA-256 of the frame checksums
    std::string_view audio_digest;  ///< SHA-256 of the samples
};

constexpr std::array<KnownMovie, kInstalledMovieCount> kKnownMovies = {{
    {"a4231eda0e5d65848264fe6c24904f9f3b3e1ca2017287e84b6a11c2d861669f",
     599,
     569,
     1760824,
     "0e05902882445b664ac13335d3f50e3a40650060f1b14c8aaae39f00c3931eb7",
     "c39bd7445bf17e111a5d7dda1ba3b52f8a10fd04413b8f5b3df7d129c9352107"},
    {"523a5dbfeb9f775881ff12bd7f0fe240d0ace1f69dfe3c17d1c7a7d2c3f2d14f",
     4058,
     4028,
     11929316,
     "74ecb12770b1d6191a5a7adb4ea404aec77736eff21ac5504d7baf52bfe17dda",
     "b41c245b8d46164b80fbc39f5b534b9814757d95d9725346fcfd7f8f0aa21a58"},
    {"2f641407c59b566d21d57922f368b012807ad4e4c6718843409ef7a2c41cdcb1",
     889,
     859,
     2613368,
     "5dc7392cebff7273ac3ba6bc3f09aa794edf3aa37aca5de3e9a6221e86b4e1fa",
     "2150a800554900cb80b7725dc871e8528f048230d936efbcd0ef42dfa837efba"},
    {"4fa980819dc933ab9635256a8ba627625dbef91df75d2486aa130f7ede37ceab",
     1841,
     1811,
     5411952,
     "52ed365ce0b837494276e52a621b37ce8b22f66e85fea49d17e7ddd88209dad3",
     "e2fa9d0e9fb0cc33cfd8de77bb23274de2e06616314f05326b31c5dfd476d2f7"},
    {"d1363691fe62e85f5f2b7047392a37dbd9b20b673fd52cd5834bcc730ad99458",
     4020,
     3990,
     11817564,
     "5c73a1eedff9fba41c5cf84cbd627441e975611fed272244460a9e680f63fbf7",
     "6cbd8e4c51f78dc9bf9b47b224b5c438a801a7bb5800400d6d9dcd0cb7c7f25a"},
}};

int failures = 0;

/// Reports a failed check and counts it.
///
/// @param condition the check
/// @param what the failure, printed after "FAIL: " when `condition` is false
/// @return `condition`
bool check(bool condition, const std::string& what) {
    if (!condition) {
        std::cerr << "FAIL: " << what << '\n';
        ++failures;
    }
    return condition;
}

/// Finds a directory entry by name ignoring ASCII case.
///
/// @param directory the folder searched
/// @param name the entry's name in any case
/// @return the entry, or nullopt when the folder holds none of that name
std::optional<std::filesystem::path>
entry_ignoring_case(const std::filesystem::path& directory, const std::string& name) {
    const auto upper = [](char c) {
        return c >= 'a' && c <= 'z' ? static_cast<char>(c - 'a' + 'A') : c;
    };
    std::error_code error;
    for (const auto& entry : std::filesystem::directory_iterator(directory, error)) {
        const auto candidate = entry.path().filename().string();
        if (std::equal(
                candidate.begin(), candidate.end(), name.begin(), name.end(), [&](char a, char b) {
                    return upper(a) == upper(b);
                }
            ))
            return entry.path();
    }
    return std::nullopt;
}

/// Returns a digest as lower-case hexadecimal.
///
/// @param digest the digest
/// @return its text
std::string hex(const sha256::Digest& digest) {
    const auto text = sha256::to_hex(digest);
    return std::string(text.begin(), text.end());
}

/// Returns a frame's checksum: 64-bit FNV-1a over little-endian 64-bit words.
///
/// @param bytes the frame's RGB bytes; a trailing partial word is left out
/// @return the checksum
uint64_t frame_checksum(std::span<const uint8_t> bytes) {
    uint64_t checksum = kFnvOffsetBasis;
    for (std::size_t at = 0; at + kWordBytes <= bytes.size(); at += kWordBytes) {
        uint64_t word = 0;
        for (std::size_t byte = kWordBytes; byte-- > 0;)
            word = word << 8 | bytes[at + byte];
        checksum = (checksum ^ word) * kFnvPrime;
    }
    return checksum;
}

/// Returns the digest of a whole file.
///
/// @param path the file
/// @return its digest, or nullopt when it cannot be read
std::optional<std::string> file_digest(const std::filesystem::path& path) {
    std::ifstream file(path, std::ios::binary);
    if (!file)
        return std::nullopt;
    sha256::Hasher hasher;
    std::vector<char> buffer(kFileReadBytes);
    while (file) {
        file.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        const auto read = static_cast<std::size_t>(file.gcount());
        sha256::update(hasher, std::span(reinterpret_cast<const uint8_t*>(buffer.data()), read));
    }
    return hex(sha256::finish(hasher));
}

/// What decoding a movie gave.
struct Decoded {
    uint32_t frames{};
    uint32_t audio_blocks{};
    uint64_t audio_bytes{};
    std::string frames_digest;
    std::string audio_digest;
};

/// Decodes every frame of a movie and digests its pictures and samples.
///
/// @param path the movie
/// @param name the name failures are reported under
/// @return what was decoded, or nullopt after reporting a failure
std::optional<Decoded> decode_movie(const std::filesystem::path& path, const std::string& name) {
    const auto opened = smacker::SmackerReader::open(path);
    if (!check(static_cast<bool>(opened), name + " does not open: " + opened.error))
        return std::nullopt;
    const auto& reader = *opened.reader;
    const auto& header = reader.header();
    std::string error;
    std::vector<uint8_t> trees;
    if (!check(reader.read_huffman_trees(trees, error), name + ": trees " + error))
        return std::nullopt;
    auto video = smacker::VideoDecoder::create(header, trees, error);
    if (!check(video.has_value(), name + ": tables " + error))
        return std::nullopt;
    std::optional<std::size_t> track;
    for (std::size_t index = 0; index < header.audio.size() && !track; ++index) {
        if (header.audio[index].sample_rate() != 0)
            track = index;
    }
    const auto format =
        track ? smacker::audio_format(header.audio[*track]) : smacker::AudioFormat{};
    std::ifstream file(path, std::ios::binary);
    smacker::Palette palette{};
    std::vector<uint8_t> payload;
    std::vector<uint8_t> rgb(
        static_cast<std::size_t>(header.width) * header.height * kRgbBytesPerPixel
    );
    std::vector<int16_t> samples;
    sha256::Hasher frames_hasher;
    sha256::Hasher audio_hasher;
    Decoded decoded;
    for (uint32_t index = 0; index < header.frame_count; ++index) {
        const auto frame = reader.frame(index);
        const auto where = name + " frame " + std::to_string(index) + ": ";
        payload.resize(frame->compressed_size);
        file.seekg(static_cast<std::streamoff>(frame->file_offset));
        if (!check(
                static_cast<bool>(file.read(
                    reinterpret_cast<char*>(payload.data()),
                    static_cast<std::streamsize>(payload.size())
                )),
                where + "payload is truncated"
            ))
            return std::nullopt;
        smacker::FrameChunks chunks;
        if (!check(
                smacker::split_frame(payload, frame->type, header, chunks, error), where + error
            ))
            return std::nullopt;
        if (!chunks.palette.empty() &&
            !check(smacker::update_palette(chunks.palette, palette, error), where + error))
            return std::nullopt;
        if (track && format.coding != smacker::AudioCoding::none && !chunks.audio[*track].empty()) {
            if (!check(
                    smacker::decode_audio(chunks.audio[*track], format, samples, error),
                    where + error
                ))
                return std::nullopt;
            if (!samples.empty()) {
                ++decoded.audio_blocks;
                const auto bytes = std::span(
                    reinterpret_cast<const uint8_t*>(samples.data()),
                    samples.size() * sizeof(int16_t)
                );
                decoded.audio_bytes += bytes.size();
                sha256::update(audio_hasher, bytes);
            }
        }
        if (!check(video->decode(chunks.video, error), where + error))
            return std::nullopt;
        const auto pixels = video->pixels();
        for (std::size_t pixel = 0; pixel < pixels.size(); ++pixel)
            std::memcpy(
                &rgb[pixel * kRgbBytesPerPixel],
                &palette[static_cast<std::size_t>(pixels[pixel]) * kRgbBytesPerPixel],
                kRgbBytesPerPixel
            );
        const auto checksum = frame_checksum(rgb);
        std::array<uint8_t, kWordBytes> checksum_bytes{};
        for (std::size_t byte = 0; byte < kWordBytes; ++byte)
            checksum_bytes[byte] = static_cast<uint8_t>(checksum >> (8 * byte));
        sha256::update(frames_hasher, checksum_bytes);
        ++decoded.frames;
    }
    decoded.frames_digest = hex(sha256::finish(frames_hasher));
    decoded.audio_digest = hex(sha256::finish(audio_hasher));
    return decoded;
}

/// Decodes each installed movie and checks it against the release's.
///
/// @param root the installation
void check_installed_movies(const std::filesystem::path& root) {
    const auto data = entry_ignoring_case(root, "Data");
    if (!data)
        oa::test::skip_test("decoding the installed movies", "the install has no Data folder");
    int present = 0;
    for (int movie = 1; movie <= kInstalledMovieCount; ++movie) {
        const std::string name = "Data/" + std::to_string(movie) + ".zrb";
        const auto path = entry_ignoring_case(*data, std::to_string(movie) + ".zrb");
        if (!path) {
            std::cout << "note: the install holds no " << name << '\n';
            continue;
        }
        ++present;
        const auto digest = file_digest(*path);
        if (!check(digest.has_value(), name + " cannot be read"))
            continue;
        const auto decoded = decode_movie(*path, name);
        if (!decoded)
            continue;
        const auto known =
            std::find_if(kKnownMovies.begin(), kKnownMovies.end(), [&](const KnownMovie& entry) {
                return entry.file_digest == *digest;
            });
        if (known == kKnownMovies.end()) {
            std::cout << "note: " << name << " is not the 3.1c release's; decoded "
                      << decoded->frames << " frames " << decoded->frames_digest << ", "
                      << decoded->audio_blocks << " audio blocks of " << decoded->audio_bytes
                      << " bytes " << decoded->audio_digest << '\n';
            continue;
        }
        check(decoded->frames == known->frames, name + ": frame count");
        check(decoded->audio_blocks == known->audio_blocks, name + ": audio block count");
        check(decoded->audio_bytes == known->audio_bytes, name + ": audio bytes");
        check(
            decoded->frames_digest == known->frames_digest,
            name + ": frames digest " + decoded->frames_digest
        );
        check(
            decoded->audio_digest == known->audio_digest,
            name + ": audio digest " + decoded->audio_digest
        );
    }
    if (present == 0)
        oa::test::skip_test(
            "decoding the installed movies", "the install's Data folder holds no movie"
        );
}

} // namespace

int main() {
    check_installed_movies(oa::test::require_game_directory("decoding the installed movies"));
    if (failures != 0)
        return 1;
    std::cout << "the installed movies decode to the release's pictures and sound\n";
    return 0;
}
