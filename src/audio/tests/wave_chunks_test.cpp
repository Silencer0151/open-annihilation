// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "audio_test_support.hpp"
#include "oa/audio/wave_chunks.hpp"
#include "oa/test/game_assets.hpp"

#include <cstdint>
#include <string>

using namespace oa::audio;
using audio_test::require;

namespace {

WaveCursor cursor_of(const std::vector<uint8_t>& bytes) {
    return wave_cursor(bytes.data(), static_cast<uint32_t>(bytes.size()));
}

void synthetic_layouts() {
    const std::vector<uint8_t> samples{1, 2, 3, 4, 5, 6};
    auto riff = audio_test::make_riff(22050, 16, 2, samples, {9, 9, 9});
    auto cursor = cursor_of(riff);
    require(detect_wave_container(cursor) == WaveContainer::riff, "riff detected");
    WaveLayout layout{};
    require(describe_wave(cursor, layout), "riff described");
    require(
        layout.format.sample_rate == 22050 && layout.format.bits == 16 &&
            layout.format.channels == 2,
        "riff format"
    );
    require(layout.data_bytes == 6 && riff[layout.data_offset] == 1, "riff data after odd chunk");
    require(cursor.position == layout.data_offset, "cursor left at samples");

    char tag[4] = {'d', 'a', 't', 'a'};
    auto missing = cursor_of(riff);
    require(find_wave_chunk(missing, "cue ") == 0, "absent chunk returns zero");
    require(find_wave_chunk(missing, tag) == 6, "chunk size returned");

    // A four-byte "fmt " payload is rejected.
    auto short_format = riff;
    const auto fmt_at = static_cast<std::size_t>(12 + 8 + 3 + 4);
    short_format[fmt_at] = 4;
    auto short_cursor = cursor_of(short_format);
    require(!describe_wave(short_cursor, layout), "short fmt rejected");

    // An empty data chunk is rejected.
    auto empty = audio_test::make_riff(11025, 8, 1, {});
    auto empty_cursor = cursor_of(empty);
    require(!describe_wave(empty_cursor, layout), "empty data rejected");

    std::vector<uint8_t> digi(0x28, 0);
    std::copy_n("DIGI", 4, digi.begin());
    std::copy_n("HSHD", 4, digi.begin() + 8);
    std::copy_n("SDAT", 4, digi.begin() + 0x20);
    digi[0x16] = 0xf8; // 11000
    digi[0x17] = 0x2a;
    digi.insert(digi.end(), {7, 8, 9});
    auto digi_cursor = cursor_of(digi);
    require(describe_wave(digi_cursor, layout), "digi described");
    require(
        layout.container == WaveContainer::digi && layout.format.sample_rate == 11025 &&
            layout.format.bits == 8 && layout.format.channels == 1,
        "digi 11000 maps to 11025, 8-bit mono"
    );
    require(layout.data_offset == 0x28 && layout.data_bytes == 3, "digi samples at 0x28");

    // DIGI with a broken second tag falls back to the RIFF test on that tag.
    auto broken = digi;
    std::copy_n("RIFF", 4, broken.begin() + 8);
    auto broken_cursor = cursor_of(broken);
    require(detect_wave_container(broken_cursor) == WaveContainer::raw, "last tag reused");

    const std::vector<uint8_t> raw{0x80, 0x81, 0x82};
    auto raw_cursor = cursor_of(raw);
    require(describe_wave(raw_cursor, layout), "raw described");
    require(
        layout.container == WaveContainer::raw && layout.data_offset == 0 &&
            layout.data_bytes == 3 && layout.format.sample_rate == 11025,
        "raw is the whole file at 11025 Hz"
    );

    const std::vector<uint8_t> tiny{'R', 'I'};
    auto tiny_cursor = cursor_of(tiny);
    require(detect_wave_container(tiny_cursor) == WaveContainer::raw, "short file is raw");
}

// Every WAV the installed game provides, loose or archived. A listed sound
// that reads as empty has no layout to check; it is counted and reported.
void installed_sweep(const oa::AssetStore& assets) {
    int counts[3]{};
    int files = 0;
    int empty = 0;
    for (const auto& name : assets.list_effective_recursive("", ".wav")) {
        const auto bytes = oa::test::read_game_file(assets, name);
        if (bytes.empty()) {
            std::printf("installed sound %s is empty\n", name.c_str());
            ++empty;
            continue;
        }
        auto cursor = cursor_of(bytes);
        WaveLayout layout{};
        require(describe_wave(cursor, layout), name.c_str());
        ++counts[static_cast<int>(layout.container)];
        ++files;
        require(layout.format.bits == 8 || layout.format.bits == 16, name.c_str());
        require(layout.format.channels == 1 || layout.format.channels == 2, name.c_str());
        require(
            layout.format.sample_rate >= 5000 && layout.format.sample_rate <= 48000, name.c_str()
        );
        require(layout.data_offset <= bytes.size(), name.c_str());
        if (layout.container != WaveContainer::riff)
            require(layout.data_offset + layout.data_bytes == bytes.size(), name.c_str());
    }
    std::printf(
        "installed sounds: %d files, raw %d, digi %d, riff %d, %d empty\n",
        files,
        counts[0],
        counts[1],
        counts[2],
        empty
    );
    require(files >= 500, "the installed sounds are present");
    require(counts[1] >= 1 && counts[0] >= 1, "the installed sounds exercise every container");
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        installed_sweep(oa::test::require_game_assets("the installed sound sweep"));
        return 0;
    }
    synthetic_layouts();
    return 0;
}
