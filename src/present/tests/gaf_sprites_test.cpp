// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// GAF relocation into presentation sprites: record layout, rejected files,
// and (with --data) every frame of the installed game drawn through
// draw_sprite against the GAF reader.

#include "oa/formats/gaf.hpp"
#include "oa/present/blit.hpp"
#include "oa/present/gaf_sprites.hpp"
#include "oa/present/surface.hpp"
#include "oa/test/game_assets.hpp"

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

using oa::Sprite;
using oa::present::GafSprites;
using oa::present::GafStatus;

int failures = 0;

#define CHECK(condition)                                                                           \
    do {                                                                                           \
        if (!(condition)) {                                                                        \
            std::fprintf(stderr, "%s:%d: CHECK(%s) failed\n", __FILE__, __LINE__, #condition);     \
            ++failures;                                                                            \
        }                                                                                          \
    } while (0)

void put_u16(std::vector<uint8_t>& bytes, size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8);
}

void put_u32(std::vector<uint8_t>& bytes, size_t at, uint32_t value) {
    for (size_t i = 0; i < 4; ++i) {
        bytes[at + i] = static_cast<uint8_t>(value >> (8 * i));
    }
}

void put_header(
    std::vector<uint8_t>& bytes,
    size_t at,
    uint16_t width,
    uint16_t height,
    int16_t origin_x,
    int16_t origin_y,
    uint8_t key,
    uint8_t encoding,
    uint8_t children,
    uint8_t child_draw_mode,
    uint32_t data
) {
    put_u16(bytes, at, width);
    put_u16(bytes, at + 2, height);
    put_u16(bytes, at + 4, static_cast<uint16_t>(origin_x));
    put_u16(bytes, at + 6, static_cast<uint16_t>(origin_y));
    bytes[at + 8] = key;
    bytes[at + 9] = encoding;
    bytes[at + 10] = children;
    bytes[at + 11] = child_draw_mode;
    put_u32(bytes, at + 0x10, data);
}

// Offsets inside the sample file.
constexpr size_t sequence_a = 0x14;
constexpr size_t sequence_b = 0x50;
constexpr size_t frame_raw = 0x80;
constexpr size_t frame_rle = 0x98;
constexpr size_t frame_composite = 0xB0;
constexpr size_t child_raw = 0xC8;
constexpr size_t child_rle = 0xE0;
constexpr size_t child_table = 0xF8;
constexpr size_t raw_pixels = 0x100;
constexpr size_t rle_stream = 0x108;
constexpr size_t child_raw_pixels = 0x118;
constexpr size_t child_rle_stream = 0x11C;
constexpr size_t sample_size = 0x124;

// Two sequences: ALPHA with a raw 3x2 frame and a row-RLE 4x2 frame, BETA
// with one composite frame of a raw child and a flagged row-RLE child.
std::vector<uint8_t> sample_gaf() {
    std::vector<uint8_t> bytes(sample_size, 0);
    put_u32(bytes, 0, 0x00010100);
    put_u32(bytes, 4, 0x12340002); // only the low word counts
    put_u32(bytes, 8, 0x99);
    put_u32(bytes, 0x0C, sequence_a);
    put_u32(bytes, 0x10, sequence_b);

    put_u16(bytes, sequence_a, 2);
    put_u16(bytes, sequence_a + 2, 1);
    put_u32(bytes, sequence_a + 4, 0x55);
    std::memcpy(bytes.data() + sequence_a + 8, "ALPHA", 5);
    put_u32(bytes, sequence_a + 0x28, frame_raw);
    put_u32(bytes, sequence_a + 0x2C, 0xABCD0005);
    put_u32(bytes, sequence_a + 0x30, frame_rle);
    put_u32(bytes, sequence_a + 0x34, 7);

    put_u16(bytes, sequence_b, 1);
    std::memcpy(bytes.data() + sequence_b + 8, "BETA", 4);
    put_u32(bytes, sequence_b + 0x28, frame_composite);
    put_u32(bytes, sequence_b + 0x2C, 3);

    put_header(bytes, frame_raw, 3, 2, 1, -1, 9, OA_SPRITE_RAW, 0, 0, raw_pixels);
    put_u32(bytes, frame_raw + 0x0C, 0x11223344);
    put_u32(bytes, frame_raw + 0x14, 0x8000);
    const uint8_t raw[] = {1, 9, 2, 3, 4, 9};
    std::memcpy(bytes.data() + raw_pixels, raw, sizeof raw);

    put_header(bytes, frame_rle, 4, 2, 0, 0, 0, OA_SPRITE_ROW_RLE, 0, 0, rle_stream);
    // Row 0: skip 1, copy 3. Row 1: fill 4 with colour 7.
    const uint8_t rle[] = {5, 0, 0x03, 0x08, 21, 22, 23, 2, 0, 0x0E, 7};
    std::memcpy(bytes.data() + rle_stream, rle, sizeof rle);

    put_header(bytes, frame_composite, 8, 8, 4, 4, 0, OA_SPRITE_RAW, 2, 0, child_table);
    put_u32(bytes, child_table, child_raw);
    put_u32(bytes, child_table + 4, child_rle);
    put_header(bytes, child_raw, 2, 2, 1, 1, 0, OA_SPRITE_RAW, 0, 0, child_raw_pixels);
    const uint8_t child_pixels[] = {5, 0, 0, 6};
    std::memcpy(bytes.data() + child_raw_pixels, child_pixels, sizeof child_pixels);
    put_header(bytes, child_rle, 2, 1, 0, 0, 0, OA_SPRITE_ROW_RLE, 0, 1, child_rle_stream);
    const uint8_t child_stream[] = {2, 0, 0x06, 8};
    std::memcpy(bytes.data() + child_rle_stream, child_stream, sizeof child_stream);
    return bytes;
}

GafStatus relocate(const std::vector<uint8_t>& bytes, GafSprites& gaf) {
    return oa::present::relocate_gaf(std::span<const uint8_t>(bytes), gaf);
}

void test_sample_records() {
    GafSprites gaf;
    CHECK(relocate(sample_gaf(), gaf) == GafStatus::ok);
    CHECK(
        gaf.version == 0x00010100 && gaf.sequence_count == 0x12340002 &&
        gaf.reserved_after_sequence_count == 0x99
    );
    CHECK(gaf.sequences.size() == 2 && gaf.sprites.size() == 5 && gaf.children.size() == 2);
    const uint8_t* base = gaf.bytes.data();

    const auto& alpha = gaf.sequences[0];
    CHECK(std::strcmp(alpha.name, "ALPHA") == 0 && alpha.frame_count == 2);
    CHECK(alpha.repeat_flags == 1 && alpha.reserved_after_repeat_flags == 0x55);
    CHECK(alpha.frames[0].duration == 0xABCD0005 && alpha.frames[1].duration == 7);
    const Sprite* raw = alpha.frames[0].frame;
    CHECK(
        raw->width == 3 && raw->height == 2 && raw->origin_x == 1 && raw->origin_y == -1 &&
        raw->key == 9
    );
    CHECK(
        raw->encoding == OA_SPRITE_RAW && raw->child_count == 0 &&
        raw->reserved_after_child_draw_mode == 0x11223344
    );
    CHECK(raw->data == base + raw_pixels && raw->aux == nullptr);
    const Sprite* rle = alpha.frames[1].frame;
    CHECK(rle->encoding == OA_SPRITE_ROW_RLE && rle->data == base + rle_stream);

    const auto& beta = gaf.sequences[1];
    CHECK(
        std::strcmp(beta.name, "BETA") == 0 && beta.frame_count == 1 && beta.frames[0].duration == 3
    );
    const Sprite* composite = beta.frames[0].frame;
    CHECK(composite->child_count == 2 && composite->data == &gaf.children[0]);
    CHECK(
        gaf.children[0]->data == base + child_raw_pixels && gaf.children[0]->child_draw_mode == 0
    );
    CHECK(
        gaf.children[1]->data == base + child_rle_stream && gaf.children[1]->child_draw_mode == 1
    );

    CHECK(oa::present::gaf_frame(&alpha, 1) == rle);
    CHECK(oa::present::gaf_frame(&alpha, -1) == nullptr);
    CHECK(oa::present::gaf_frame(&alpha, 2) == nullptr);
    CHECK(oa::present::gaf_frame(nullptr, 0) == nullptr);
    CHECK(oa::present::find_gaf_sequence(gaf, "beta") == &beta);
    CHECK(oa::present::find_gaf_sequence(gaf, "Alpha") == &alpha);
    CHECK(oa::present::find_gaf_sequence(gaf, "ALPH") == nullptr);
    CHECK(oa::present::find_gaf_sequence(gaf, "ALPHAS") == nullptr);

    auto surface = oa::present::create_surface(6, 4);
    std::fill(surface.pixels.begin(), surface.pixels.end(), uint8_t{0xEE});
    oa::present::draw_sprite(&surface.surface, rle, 1, 1);
    const uint8_t expected[] = {
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE, //
        0xEE, 0xEE, 21,   22,   23,   0xEE, //
        0xEE, 7,    7,    7,    7,    0xEE, //
        0xEE, 0xEE, 0xEE, 0xEE, 0xEE, 0xEE,
    };
    CHECK(std::equal(surface.pixels.begin(), surface.pixels.end(), expected));

    GafSprites moved = std::move(gaf);
    CHECK(moved.sequences[1].frames[0].frame->data == &moved.children[0]);
}

// A file handed over is kept where it is, not copied, and frames point into
// it; one that fails its checks is left with the caller.
void test_file_taken_over() {
    auto bytes = sample_gaf();
    const uint8_t* const held = bytes.data();
    GafSprites gaf;
    CHECK(oa::present::relocate_gaf(std::move(bytes), gaf) == GafStatus::ok);
    CHECK(gaf.bytes.data() == held);
    const Sprite* raw = oa::present::gaf_frame(&gaf.sequences[0], 0);
    CHECK(
        raw != nullptr && static_cast<const uint8_t*>(raw->data) >= held &&
        static_cast<const uint8_t*>(raw->data) < held + gaf.bytes.size()
    );
    std::vector<uint8_t> refused(8, 0);
    CHECK(oa::present::relocate_gaf(std::move(refused), gaf) == GafStatus::short_header);
    CHECK(refused.size() == 8);
}

void test_sequence_count_word() {
    auto bytes = sample_gaf();
    GafSprites gaf;
    put_u32(bytes, 4, 0x0000FFFF);
    CHECK(
        relocate(bytes, gaf) == GafStatus::ok && gaf.sequences.empty() &&
        gaf.sequence_count == 0xFFFF
    );
    put_u32(bytes, 4, 0x00018000);
    CHECK(relocate(bytes, gaf) == GafStatus::ok && gaf.sequences.empty());
    put_u32(bytes, 4, 0x7FFF);
    CHECK(
        relocate(bytes, gaf) == GafStatus::short_header && gaf.sequences.empty() &&
        gaf.bytes.empty()
    );
    put_u32(bytes, 4, 1);
    CHECK(
        relocate(bytes, gaf) == GafStatus::ok && gaf.sequences.size() == 1 &&
        gaf.sprites.size() == 2
    );
}

void test_rejected_files() {
    GafSprites gaf;
    CHECK(relocate(std::vector<uint8_t>(8, 0), gaf) == GafStatus::short_header);

    auto bytes = sample_gaf();
    put_u32(bytes, 0x10, sample_size - 0x20);
    CHECK(relocate(bytes, gaf) == GafStatus::sequence_outside);

    bytes = sample_gaf();
    put_u16(bytes, sequence_b, 0xFFFF);
    CHECK(relocate(bytes, gaf) == GafStatus::sequence_outside);

    bytes = sample_gaf();
    put_u32(bytes, sequence_a + 0x30, sample_size - 4);
    CHECK(relocate(bytes, gaf) == GafStatus::header_outside);

    bytes = sample_gaf();
    put_u32(bytes, sequence_b + 0x28, frame_raw);
    CHECK(relocate(bytes, gaf) == GafStatus::shared_header);

    bytes = sample_gaf();
    put_u32(bytes, child_table + 4, child_raw);
    CHECK(relocate(bytes, gaf) == GafStatus::shared_header);

    bytes = sample_gaf();
    bytes[child_raw + 10] = 1;
    CHECK(relocate(bytes, gaf) == GafStatus::nested_children);

    bytes = sample_gaf();
    put_u32(bytes, frame_composite + 0x10, sample_size - 4);
    CHECK(relocate(bytes, gaf) == GafStatus::children_outside);

    bytes = sample_gaf();
    put_u32(bytes, frame_raw + 0x10, sample_size - 5);
    CHECK(relocate(bytes, gaf) == GafStatus::pixels_outside);

    // A literal run reaching past its row's bytes.
    bytes = sample_gaf();
    bytes[rle_stream] = 4;
    CHECK(relocate(bytes, gaf) == GafStatus::malformed_rows);

    // A row whose commands stop short of the frame width.
    bytes = sample_gaf();
    bytes[rle_stream + 3] = 0x04; // copy 2 instead of 3
    bytes[rle_stream] = 4;
    CHECK(relocate(bytes, gaf) == GafStatus::malformed_rows);

    // A row length past the end of the file.
    bytes = sample_gaf();
    put_u16(bytes, child_rle_stream, 0x100);
    CHECK(relocate(bytes, gaf) == GafStatus::malformed_rows);

    // A zero-length row draws nothing and needs no commands.
    bytes = sample_gaf();
    put_u16(bytes, child_rle_stream, 0);
    CHECK(relocate(bytes, gaf) == GafStatus::ok);

    std::vector<uint8_t> huge(oa::present::gaf_max_bytes + 1, 0);
    CHECK(relocate(huge, gaf) == GafStatus::too_large);
    CHECK(
        std::strcmp(
            oa::present::gaf_status_text(GafStatus::shared_header),
            "GAF frame header is referenced more than once"
        ) == 0
    );
}

struct SweepCounts {
    int files = 0;
    int frames = 0;
    int special = 0;
    int mismatched = 0;
};

// Draws `sprite` at its origin on a frame-sized surface filled with `fill`.
std::vector<uint8_t> draw_over(
    const Sprite* sprite,
    uint16_t width,
    uint16_t height,
    int16_t origin_x,
    int16_t origin_y,
    uint8_t fill
) {
    auto surface = oa::present::create_surface(width, height);
    std::fill(surface.pixels.begin(), surface.pixels.end(), fill);
    oa::present::draw_sprite(&surface.surface, sprite, origin_x, origin_y);
    return surface.pixels;
}

void compare_frame(
    const Sprite* sprite, const oa::formats::gaf::Frame& model, SweepCounts& counts
) {
    ++counts.frames;
    CHECK(sprite->width == model.width && sprite->height == model.height);
    CHECK(sprite->origin_x == model.origin_x && sprite->origin_y == model.origin_y);
    CHECK(sprite->key == model.transparency_index && sprite->child_count == model.layer_count);
    for (uint32_t i = 0; i < sprite->child_count; ++i) {
        const Sprite* child = static_cast<Sprite* const*>(sprite->data)[i];
        CHECK(child->child_draw_mode == model.layers[i].special_render_flag);
        CHECK(child->width == model.layers[i].width && child->origin_x == model.layers[i].origin_x);
    }
    const auto rendered = oa::formats::gaf::render_normal(model);
    if (!rendered.ok()) {
        ++counts.special;
        return;
    }
    if (sprite->width == 0 || sprite->height == 0) {
        return;
    }
    // A pixel is drawn when both fills end with the same value.
    const auto low =
        draw_over(sprite, sprite->width, sprite->height, sprite->origin_x, sprite->origin_y, 0x00);
    const auto high =
        draw_over(sprite, sprite->width, sprite->height, sprite->origin_x, sprite->origin_y, 0xFF);
    const auto& frame = *rendered.frame;
    for (size_t i = 0; i < low.size(); ++i) {
        const bool drawn = low[i] == high[i];
        if (drawn != (frame.coverage[i] != 0) || (drawn && low[i] != frame.pixels[i])) {
            ++counts.mismatched;
            return;
        }
    }
}

// Every GAF the installed game provides, loose or archived.
void test_installed_sweep(const oa::AssetStore& assets) {
    SweepCounts counts;
    for (const auto& name : assets.list_effective_recursive("", ".gaf")) {
        const auto bytes = oa::test::read_game_file(assets, name);
        GafSprites gaf;
        const GafStatus status = relocate(bytes, gaf);
        const auto parsed = oa::formats::gaf::parse(bytes);
        if (status != GafStatus::ok || !parsed.ok()) {
            std::fprintf(stderr, "%s: %s\n", name.c_str(), oa::present::gaf_status_text(status));
            CHECK(false);
            continue;
        }
        ++counts.files;
        const auto& archive = *parsed.archive;
        CHECK(gaf.sequences.size() == archive.sequences.size());
        for (size_t s = 0; s < gaf.sequences.size() && s < archive.sequences.size(); ++s) {
            const auto& sequence = gaf.sequences[s];
            const auto& model = archive.sequences[s];
            CHECK(
                sequence.frame_count == static_cast<uint16_t>(model.frames.size()) &&
                sequence.name == model.name
            );
            for (int32_t k = 0; k < sequence.frame_count; ++k) {
                CHECK(
                    static_cast<uint16_t>(sequence.frames[k].duration) == model.frames[k].duration
                );
                compare_frame(oa::present::gaf_frame(&sequence, k), model.frames[k], counts);
            }
        }
    }
    std::printf(
        "installed GAF sweep: %d files, %d frames drawn, %d through the blended child path, %d "
        "mismatched\n",
        counts.files,
        counts.frames,
        counts.special,
        counts.mismatched
    );
    CHECK(counts.files > 0 && counts.mismatched == 0);
}

// The radar's FX.GAF sequences, found by name as a game load looks them up.
void test_radar_logos(const oa::AssetStore& assets) {
    const auto bytes = oa::test::read_game_file(assets, "anims/fx.gaf");
    CHECK(!bytes.empty());
    GafSprites fx;
    CHECK(relocate(bytes, fx) == GafStatus::ok);
    const auto* units = oa::present::find_gaf_sequence(fx, "RADLOGO");
    const auto* cursor = oa::present::find_gaf_sequence(fx, "radlogohigh");
    const auto* weapons = oa::present::find_gaf_sequence(fx, "nuclogo");
    CHECK(units != nullptr && units->frame_count == 10 && units != cursor);
    CHECK(cursor != nullptr && cursor->frame_count == 1);
    CHECK(weapons != nullptr && weapons->frame_count == 10);
    if (units == nullptr || cursor == nullptr || weapons == nullptr) {
        return;
    }
    const Sprite* blip = oa::present::gaf_frame(units, 9);
    CHECK(
        blip != nullptr && blip->width == 4 && blip->height == 4 && blip->origin_x == 1 &&
        blip->origin_y == 1
    );
    const Sprite* marker = oa::present::gaf_frame(cursor, 0);
    CHECK(marker != nullptr && marker->width == 6 && marker->origin_x == 2);
    const Sprite* icon = oa::present::gaf_frame(weapons, 0);
    CHECK(icon != nullptr && icon->width == 7 && icon->origin_x == 3);
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        const auto assets = oa::test::require_game_assets("the installed GAF sweep");
        test_installed_sweep(assets);
        test_radar_logos(assets);
    } else {
        test_sample_records();
        test_file_taken_over();
        test_sequence_count_word();
        test_rejected_files();
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("gaf sprite tests passed");
    return 0;
}
