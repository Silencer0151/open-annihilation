// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/fnt.hpp"
#include <cassert>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace {
void put16(std::vector<uint8_t>& b, std::size_t o, uint16_t v) {
    b[o] = static_cast<uint8_t>(v);
    b[o + 1] = static_cast<uint8_t>(v >> 8U);
}

std::vector<uint8_t> fnt_with_width(uint8_t width) {
    const std::size_t packed = (static_cast<std::size_t>(width) + 7U) / 8U;
    std::vector<uint8_t> bytes(516 + 1 + packed);
    bytes[0] = 1;
    constexpr std::size_t glyph = 516;
    bytes[4 + static_cast<std::size_t>('A') * 2] = static_cast<uint8_t>(glyph);
    bytes[5 + static_cast<std::size_t>('A') * 2] = static_cast<uint8_t>(glyph >> 8U);
    bytes[glyph] = width;
    return bytes;
}

void write_bytes(const std::filesystem::path& path, std::span<const uint8_t> bytes) {
    std::filesystem::create_directories(path.parent_path());
    std::ofstream out(path, std::ios::binary);
    assert(out);
    out.write(
        reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size())
    );
    assert(out);
}

uint32_t named_width(oa::AssetStore& assets, std::string_view name, std::string_view language) {
    const auto font = oa::formats::fnt::load_named_fnt(assets, name, language);
    return oa::formats::fnt::measure_text(font, "A");
}
} // namespace

int main() {
    std::vector<uint8_t> b(522);
    put16(b, 0, 2);
    put16(b, 2, 0x1234);
    put16(b, 4 + static_cast<std::size_t>(' ') * 2, 516);
    put16(b, 4 + static_cast<std::size_t>('A') * 2, 519);
    b[516] = 3;
    b[519] = 3;
    b[520] = 0xA8; // continuous bits: 101 / 010
    const auto f = oa::formats::fnt::parse_fnt(b);
    assert(f.word_after_height == 0x1234);
    assert(oa::formats::fnt::measure_text(f, " A\nA") == 9);
    assert(oa::formats::fnt::line_height(f) == 4);
    std::vector<uint8_t> pixels(12, 7);
    std::vector<uint8_t> coverage(12);
    assert(oa::formats::fnt::raster_text({4, 3, 4, pixels, coverage}, f, " A", 0, 1) == 6);
    assert(pixels[7] == 255);
    assert(coverage[7] == 1);
    assert(oa::formats::fnt::raster_text({4, 3, 4, pixels, coverage}, f, "A", -2, 0) == 1);
    bool rejected = false;
    b.resize(520);
    try {
        (void)oa::formats::fnt::parse_fnt(b);
    } catch (const std::runtime_error&) {
        rejected = true;
    }
    assert(rejected);

    const auto root = std::filesystem::temp_directory_path() / "oa-font-format-named-fnt";
    std::filesystem::remove_all(root);
    write_bytes(root / "fonts" / "smlfont.FNT", fnt_with_width(3));
    oa::AssetStore assets(root);
    assert(named_width(assets, "smlfont.old", "") == 3);
    assert(named_width(assets, "smlfont", "english") == 3);
    write_bytes(root / "fonts-english" / "smlfont.FNT", fnt_with_width(5));
    // The store lists each loose folder once; a rescan picks up the new folder.
    assets.mark_loose_shadows();
    assert(named_width(assets, "smlfont", "english") == 5);
    assert(named_width(assets, "smlfont", "") == 3);
    write_bytes(root / "fonts-en.FNT", fnt_with_width(7));
    write_bytes(root / "fonts-en.gb" / "smlfont.FNT", fnt_with_width(9));
    assets.mark_loose_shadows();
    assert(named_width(assets, "smlfont", "en.gb") == 7);
    write_bytes(root / "fonts-english" / "blank.FNT", {});
    write_bytes(root / "fonts" / "blank.FNT", fnt_with_width(4));
    assets.mark_loose_shadows();
    bool missing = false;
    try {
        (void)oa::formats::fnt::load_named_fnt(assets, "blank", "english");
    } catch (const std::runtime_error& error) {
        missing = std::string_view(error.what()) == "missing font file: fonts-english\\blank.FNT";
    }
    assert(missing);
    write_bytes(root / "fonts-english" / "bad.FNT", std::vector<uint8_t>{1, 2, 3, 4});
    write_bytes(root / "fonts" / "bad.FNT", fnt_with_width(4));
    assets.mark_loose_shadows();
    bool rejected_language_file = false;
    try {
        (void)oa::formats::fnt::load_named_fnt(assets, "bad", "english");
    } catch (const std::runtime_error& error) {
        rejected_language_file =
            std::string_view(error.what()) == "FNT is shorter than its 516-byte header";
    }
    assert(rejected_language_file);
    bool absent = false;
    try {
        (void)oa::formats::fnt::load_named_fnt(assets, "missing", "english");
    } catch (const std::runtime_error& error) {
        absent = std::string_view(error.what()) == "missing font file: fonts\\missing.FNT";
    }
    assert(absent);
    std::filesystem::remove_all(root);
}
