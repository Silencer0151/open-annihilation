// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// GUI art conversion for the gadget draw host: hand-encoded row runs of
// synthetic GAF frames, and (with the installed game) every frame of the GUI
// fonts and the common GUI art drawn back exactly over its covered pixels.
// Also the darkening under a shade_below dialog through a synthetic shade
// table.
#include "dialog_internal.hpp"

#include "oa/formats/gaf.hpp"
#include "oa/present/blit.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <initializer_list>
#include <memory>
#include <utility>
#include <vector>

namespace dialogs = oa::ui::frontend_dialogs;
namespace gaf = oa::formats::gaf;
namespace present = oa::present;

namespace {

int failures = 0;

void expect(bool condition, const char* what) {
    if (!condition) {
        std::fprintf(stderr, "FAILED: %s\n", what);
        ++failures;
    }
}

gaf::Frame compressed_frame(
    uint16_t width,
    uint16_t height,
    const std::vector<uint8_t>& pixels,
    const std::vector<uint8_t>& coverage
) {
    gaf::Frame frame;
    frame.width = width;
    frame.height = height;
    frame.transparency_index = 9;
    frame.compressed = true;
    frame.pixels = pixels;
    frame.coverage = coverage;
    return frame;
}

bool same_bytes(const std::vector<uint8_t>& actual, std::initializer_list<uint8_t> expected) {
    return actual == std::vector<uint8_t>(expected);
}

// Draws `sprite` with its top-left at the origin of a surface one row taller
// than the sprite, cleared to `fill`.
present::SurfaceBuffer draw_on(const present::SpriteBuffer& sprite, uint8_t fill) {
    auto surface = present::create_surface(sprite.sprite.width, sprite.sprite.height + 1);
    surface.pixels.assign(surface.pixels.size(), fill);
    present::draw_sprite(
        &surface.surface, &sprite.sprite, sprite.sprite.origin_x, sprite.sprite.origin_y
    );
    return surface;
}

// Rows ending in uncovered pixels carry their trailing skips, a blank row is
// all skips, and the rows decode without reading into their neighbours.
void test_row_runs() {
    const uint8_t n = 0; // uncovered pixel value
    const auto frame = compressed_frame(
        5,
        3,
        {5, 6, n, n, n, n, n, 9, n, n, n, n, n, n, n},
        {1, 1, 0, 0, 0, 0, 0, 1, 0, 0, 0, 0, 0, 0, 0}
    );
    present::SpriteBuffer sprite;
    expect(dialogs::convert_gaf_frame(frame, sprite), "a compressed frame converts");
    expect(sprite.sprite.encoding == OA_SPRITE_ROW_RLE, "a compressed frame becomes row runs");
    expect(sprite.sprite.key == 9, "the transparency index is the key");
    expect(
        same_bytes(
            sprite.pixels,
            {0x04, 0x00, 0x04, 5, 6, 0x07, 0x04, 0x00, 0x05, 0x00, 9, 0x05, 0x01, 0x00, 0x0B}
        ),
        "rows: literal 2 + skip 3; skip 2 + literal 1 + skip 2; skip 5"
    );
    expect(sprite.sprite.data == sprite.pixels.data(), "the sprite reads its own runs");
    const auto drawn = draw_on(sprite, 0xEE);
    const std::vector<uint8_t> expected{
        5,    6,    0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 9,    0xEE, 0xEE,
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    };
    expect(drawn.pixels == expected, "the runs draw exactly the covered pixels");
}

// Skips split at 0x7F pixels and literals at 0x40.
void test_long_runs() {
    constexpr uint16_t width = 220;
    std::vector<uint8_t> pixels(width, 0);
    std::vector<uint8_t> coverage(width, 0);
    for (size_t x = 150; x < width; ++x) {
        pixels[x] = static_cast<uint8_t>(x);
        coverage[x] = 1;
    }
    present::SpriteBuffer sprite;
    expect(
        dialogs::convert_gaf_frame(compressed_frame(width, 1, pixels, coverage), sprite),
        "a wide frame converts"
    );
    const auto& runs = sprite.pixels;
    expect(runs.size() == 2 + 74, "one row of 74 run bytes");
    expect(runs.size() > 71 && runs[0] == 74 && runs[1] == 0, "row byte count");
    expect(runs.size() > 71 && runs[2] == 0xFF && runs[3] == 0x2F, "skips of 127 and 23");
    expect(runs.size() > 71 && runs[4] == 0xFC && runs[5] == 150, "a literal of 64 from x=150");
    expect(runs.size() > 71 && runs[69] == 0x14 && runs[70] == 214, "a literal of 6 from x=214");
    const auto drawn = draw_on(sprite, 0xEE);
    bool exact = true;
    for (size_t x = 0; x < width; ++x)
        exact = exact && drawn.pixels[x] == (x < 150 ? 0xEE : static_cast<uint8_t>(x));
    expect(exact, "the wide row draws exactly its covered pixels");
}

void test_raw_frame() {
    gaf::Frame frame;
    frame.width = 2;
    frame.height = 1;
    frame.transparency_index = 7;
    frame.pixels = {7, 3};
    frame.coverage = {1, 1};
    present::SpriteBuffer sprite;
    expect(dialogs::convert_gaf_frame(frame, sprite), "a raw frame converts");
    expect(sprite.sprite.encoding == OA_SPRITE_RAW, "a raw frame stays raw");
    expect(same_bytes(sprite.pixels, {7, 3}), "raw pixels are kept");
    const auto drawn = draw_on(sprite, 0xEE);
    expect(drawn.pixels[0] == 0xEE && drawn.pixels[1] == 3, "the key pixel stays transparent");
}

// Every frame of the art the dialogs load, drawn over two different fills:
// the pixels a frame covers take its values and all others keep the fill.
void test_installed_art(oa::AssetStore& assets) {
    const auto* art = dialogs::dialog_art(assets);
    expect(art != nullptr, "the dialog art loads");
    if (art == nullptr)
        return;
    expect(art->files.size() >= 3, "both GUI fonts and the common GUI art are loaded");
    // The GAF directory takes one separator from the path setter, so the
    // names resolve inside archives as well as loose.
    expect(
        art->files.size() >= 3 && art->files[0]->path == "anims/hattfont12.gaf" &&
            art->files[1]->path == "anims/hattfont11.gaf" &&
            art->files[2]->path == "anims/commongui.gaf",
        "the GUI art is requested by its archive paths"
    );
    size_t checked = 0;
    for (const auto& file : art->files) {
        const auto parsed = gaf::parse(assets.read(file->path).bytes);
        expect(parsed.ok(), "a loaded GAF parses again");
        if (!parsed.ok())
            continue;
        for (size_t s = 0; s < file->sequences.size(); ++s) {
            const auto& source = parsed.archive->sequences[s];
            const auto& converted = file->sequences[s];
            for (size_t f = 0; f < converted.frames.size(); ++f) {
                if (converted.valid[f] == 0)
                    continue;
                const gaf::Frame& frame = source.frames[f];
                std::vector<uint8_t> pixels = frame.pixels;
                std::vector<uint8_t> coverage = frame.coverage;
                if (!frame.layers.empty()) {
                    const auto rendered = gaf::render_normal(frame);
                    pixels = rendered.frame->pixels;
                    coverage = rendered.frame->coverage;
                } else if (!frame.compressed) {
                    for (size_t at = 0; at < pixels.size(); ++at)
                        coverage[at] = pixels[at] != frame.transparency_index ? 1 : 0;
                }
                bool exact = true;
                for (const uint8_t fill : {uint8_t{0x00}, uint8_t{0xFF}}) {
                    const auto drawn = draw_on(converted.frames[f], fill);
                    for (size_t at = 0; at < drawn.pixels.size(); ++at) {
                        const bool inside = at < pixels.size() && coverage[at] != 0;
                        exact = exact && drawn.pixels[at] == (inside ? pixels[at] : fill);
                    }
                }
                if (!exact)
                    std::fprintf(
                        stderr, "%s %s frame %zu\n", file->path.c_str(), source.name.c_str(), f
                    );
                expect(exact, "a converted frame draws exactly its covered pixels");
                ++checked;
            }
        }
    }
    expect(checked > 200, "the fonts' glyphs and the common art are all checked");
}

// The bottom dialog opened with shade_below darkens the panel below through
// shade row 8 (level -0x18), entries 0x80 to 0xFF through the row before.
void test_shade_below() {
    auto& stack = dialogs::dialog_stack();
    auto art = std::make_unique<dialogs::DialogArt>();
    // Row 8 takes entry e to e + 1, row 7 to e + 2, every other row to 0.
    art->shade_table.assign(32U * 256U, 0);
    for (std::size_t entry = 0; entry < 256; ++entry) {
        art->shade_table[8U * 256U + entry] = static_cast<uint8_t>(entry + 1);
        art->shade_table[7U * 256U + entry] = static_cast<uint8_t>(entry + 2);
    }
    stack.art = std::move(art);
    stack.count = 1;
    stack.dialogs[0].flags = dialogs::panel_flag::shade_below;
    // Entry e is (e, 255 - e, 3): every colour is its own entry.
    oa::PaletteBytes palette{};
    for (std::size_t entry = 0; entry < oa::palette_color_count; ++entry) {
        palette[entry * oa::palette_entry_bytes] = static_cast<uint8_t>(entry);
        palette[entry * oa::palette_entry_bytes + 1] = static_cast<uint8_t>(255 - entry);
        palette[entry * oa::palette_entry_bytes + 2] = 3;
    }
    const std::vector<uint8_t> entries{0x20, 0x7f, 0x80, 0xc0};
    oa::ui::frontend_renderer::Surface frame;
    frame.width = static_cast<uint32_t>(entries.size());
    frame.height = 1;
    frame.rgb.resize(entries.size() * 3U);
    for (std::size_t pixel = 0; pixel < entries.size(); ++pixel)
        std::copy_n(&palette[entries[pixel] * oa::palette_entry_bytes], 3, &frame.rgb[pixel * 3U]);
    dialogs::dialog_shade_below(frame, 0, 0, 4, 1, palette);
    expect(frame.rgb[0] == 0x21, "entry 0x20 reads shade row 8");
    expect(frame.rgb[3] == 0x80, "entry 0x7f reads shade row 8");
    expect(frame.rgb[6] == 0x82, "entry 0x80 reads shade row 7");
    expect(frame.rgb[9] == 0xc2, "entry 0xc0 reads shade row 7");
    expect(frame.rgb[10] == 255 - 0xc2 && frame.rgb[11] == 3, "darkened through the palette");
    stack.dialogs[0].flags = 0;
    stack.count = 0;
    stack.art.reset();
}

} // namespace

// Without an argument the synthetic frames; with --data the installed game's
// art alone.
int main(int argc, char** argv) {
    const bool installed = oa::test::game_data_requested(argc, argv);
    if (installed) {
        auto assets = oa::test::require_game_assets("the installed dialog art");
        test_installed_art(assets);
    } else {
        test_row_runs();
        test_long_runs();
        test_raw_frame();
        test_shade_below();
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return 1;
    }
    std::puts(installed ? "installed dialog art: ok" : "dialog art: ok");
    return 0;
}
