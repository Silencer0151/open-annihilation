// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/formats/gaf.hpp"

#include <cstdint>
#include <exception>
#include <iostream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace gaf = oa::formats::gaf;

namespace {

void require(bool condition, std::string_view message) {
    if (!condition)
        throw std::runtime_error(std::string(message));
}

void put16(std::vector<uint8_t>& bytes, std::size_t at, uint16_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
}

void put32(std::vector<uint8_t>& bytes, std::size_t at, uint32_t value) {
    bytes[at] = static_cast<uint8_t>(value);
    bytes[at + 1] = static_cast<uint8_t>(value >> 8U);
    bytes[at + 2] = static_cast<uint8_t>(value >> 16U);
    bytes[at + 3] = static_cast<uint8_t>(value >> 24U);
}

void frame_header(
    std::vector<uint8_t>& bytes,
    std::size_t at,
    uint16_t width,
    uint16_t height,
    int16_t origin_x,
    int16_t origin_y,
    uint8_t transparency,
    bool compressed,
    uint8_t layers,
    uint8_t special,
    uint32_t data
) {
    put16(bytes, at, width);
    put16(bytes, at + 2, height);
    put16(bytes, at + 4, static_cast<uint16_t>(origin_x));
    put16(bytes, at + 6, static_cast<uint16_t>(origin_y));
    bytes[at + 8] = transparency;
    bytes[at + 9] = compressed ? 1 : 0;
    bytes[at + 10] = layers;
    bytes[at + 11] = special;
    put32(bytes, at + 16, data);
}

std::vector<uint8_t> one_frame_file(std::size_t frame_at = 64) {
    std::vector<uint8_t> bytes(256);
    put32(bytes, 0, 0x00010100U);
    // The high 16 bits are nonzero on purpose: only the signed low 16 bits count.
    put32(bytes, 4, 0xabcd0001U);
    put32(bytes, 12, 16);
    put16(bytes, 16, 1);
    constexpr std::string_view name = "TEST";
    for (std::size_t i = 0; i < name.size(); ++i)
        bytes[24 + i] = static_cast<uint8_t>(name[i]);
    put32(bytes, 56, static_cast<uint32_t>(frame_at));
    // High half is ignored by the duration lookup.
    put32(bytes, 60, 0xbeef0007U);
    return bytes;
}

void raw_fields_and_low_words() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 3, 2, -2, 4, 9, false, 0, 1, 88);
    bytes[88] = 1;
    bytes[89] = 9;
    bytes[90] = 2;
    bytes[91] = 3;
    bytes[92] = 4;
    bytes[93] = 5;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "raw GAF did not parse");
    require(
        result.archive->raw_sequence_count == 0xabcd0001U &&
            result.archive->header_sequence_count == 1 && result.archive->sequences.size() == 1,
        "signed-low16 sequence count was not preserved"
    );
    const auto& frame = result.archive->sequences[0].frames[0];
    require(
        result.archive->sequences[0].name == "TEST" && frame.duration == 7 &&
            frame.origin_x == -2 && frame.origin_y == 4,
        "sequence name, duration low word, or signed origins disagreed"
    );
    require(
        frame.layer_count == 0 && frame.special_render_flag == 1 &&
            frame.pixels == std::vector<uint8_t>({1, 9, 2, 3, 4, 5}),
        "separate layer-count and special-render bytes or raw pixels disagreed"
    );
    const auto rendered = gaf::render_normal(frame);
    require(
        rendered.ok() && rendered.frame->pixels == std::vector<uint8_t>({1, 9, 2, 3, 4, 5}),
        "top-level normal render incorrectly routed its own special flag"
    );
}

void compressed_commands() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 6, 1, 0, 0, 0, true, 0, 0, 88);
    // literal(2): 10,11; transparent(1); repeat(3): 7, covering six pixels.
    put16(bytes, 88, 6);
    bytes[90] = 4;
    bytes[91] = 10;
    bytes[92] = 11;
    bytes[93] = 3;
    bytes[94] = 10;
    bytes[95] = 7;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "compressed GAF did not parse");
    require(
        result.archive->sequences[0].frames[0].pixels == std::vector<uint8_t>({10, 11, 0, 7, 7, 7}),
        "row command decoding disagreed"
    );

    auto short_row = one_frame_file();
    frame_header(short_row, 64, 4, 1, 0, 0, 9, true, 0, 0, 88);
    put16(short_row, 88, 0); // fully-transparent-row encoding
    const auto short_result = gaf::parse(short_row);
    require(
        short_result.ok() && short_result.archive->sequences[0].frames[0].pixels ==
                                 std::vector<uint8_t>({9, 9, 9, 9}),
        "zero-length compressed row was not fully transparent"
    );

    auto clipped_run = one_frame_file();
    frame_header(clipped_run, 64, 2, 1, 0, 0, 0, true, 0, 0, 88);
    put16(clipped_run, 88, 2);
    clipped_run[90] = 14; // repeat four, clipped to the two-pixel destination
    clipped_run[91] = 5;
    const auto clipped_result = gaf::parse(clipped_run);
    require(
        clipped_result.ok() &&
            clipped_result.archive->sequences[0].frames[0].pixels == std::vector<uint8_t>({5, 5}),
        "compressed run was not clipped to the frame width"
    );
}

void layered_origins_clipping_and_special_route() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 4, 3, 2, 1, 0, false, 2, 0, 88);
    put32(bytes, 88, 96);
    put32(bytes, 92, 128);
    // Child 0 lands at (-1,0), so its first column clips. Index 0 is transparent.
    frame_header(bytes, 96, 3, 2, 3, 1, 0, false, 0, 0, 160);
    bytes[160] = 1;
    bytes[161] = 2;
    bytes[162] = 3;
    bytes[163] = 4;
    bytes[164] = 0;
    bytes[165] = 6;
    // Child 1 lands at (2,1) over child 0.
    frame_header(bytes, 128, 2, 2, 0, 0, 9, false, 0, 0, 166);
    bytes[166] = 7;
    bytes[167] = 9;
    bytes[168] = 8;
    bytes[169] = 5;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "layered GAF did not parse");
    const auto& frame = result.archive->sequences[0].frames[0];
    require(
        frame.layer_count == 2 && frame.special_render_flag == 0 && frame.layers.size() == 2,
        "byte-sized layer model disagreed"
    );
    const auto rendered = gaf::render_normal(frame);
    require(rendered.ok(), rendered.error ? rendered.error->message : "layer render failed");
    require(
        rendered.frame->pixels == std::vector<uint8_t>({2, 3, 0, 0, 0, 6, 7, 0, 0, 0, 8, 5}),
        "layer origin subtraction, transparency, or clipping disagreed"
    );

    auto special = frame;
    special.layers[1].special_render_flag = 1;
    const auto rejected = gaf::render_normal(special);
    require(
        !rejected.ok() && rejected.error->code == gaf::ErrorCode::unsupported_special_render,
        "special-render child route was not explicit"
    );
}

void compressed_literal_transparency_index_is_still_written() {
    auto bytes = one_frame_file();
    frame_header(bytes, 64, 3, 1, 0, 0, 0, false, 2, 0, 88);
    put32(bytes, 88, 96);
    put32(bytes, 92, 128);
    frame_header(bytes, 96, 3, 1, 0, 0, 0, false, 0, 0, 152);
    bytes[152] = 7;
    bytes[153] = 7;
    bytes[154] = 7;
    frame_header(bytes, 128, 3, 1, 0, 0, 9, true, 0, 0, 155);
    put16(bytes, 155, 5);
    bytes[157] = 0; // one literal pixel
    bytes[158] = 9; // equal to metadata transparency index, but still written
    bytes[159] = 3; // skip one, retaining the lower layer
    bytes[160] = 6; // repeat two (clipped to one remaining destination pixel)
    bytes[161] = 9;
    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "coverage GAF did not parse");
    const auto& literal = result.archive->sequences[0].frames[0].layers[1];
    require(
        literal.pixels == std::vector<uint8_t>({9, 9, 9}) &&
            literal.coverage == std::vector<uint8_t>({1, 0, 1}),
        "compressed literal/repeat/skip coverage was lost"
    );
    const auto rendered = gaf::render_normal(result.archive->sequences[0].frames[0]);
    require(
        rendered.ok() && rendered.frame->pixels == std::vector<uint8_t>({9, 7, 9}),
        "compressed transparency-index writes or skip did not preserve lower layer"
    );
    require(
        rendered.frame->coverage == std::vector<uint8_t>({1, 1, 1}),
        "render output discarded compressed literal transparency coverage"
    );

    const auto isolated = gaf::render_normal(literal);
    require(
        isolated.ok() && isolated.frame->pixels == std::vector<uint8_t>({9, 9, 9}) &&
            isolated.frame->coverage == std::vector<uint8_t>({1, 0, 1}),
        "render output cannot distinguish literal transparency-index writes from skips"
    );
}

void frame_at_rejects_out_of_range_indices() {
    std::vector<uint8_t> bytes(160);
    put32(bytes, 0, 0x00010100U);
    put32(bytes, 4, 1);
    put32(bytes, 12, 16);
    put16(bytes, 16, 2);
    put32(bytes, 56, 80);
    put32(bytes, 60, 3);
    put32(bytes, 64, 120);
    put32(bytes, 68, 9);
    frame_header(bytes, 80, 1, 1, 0, 0, 0, false, 0, 0, 104);
    bytes[104] = 11;
    frame_header(bytes, 120, 1, 1, 1, 2, 4, false, 0, 0, 144);
    bytes[144] = 22;

    const auto result = gaf::parse(bytes);
    require(result.ok(), result.error ? result.error->message : "two-frame GAF did not parse");
    const auto* sequence = &result.archive->sequences[0];
    require(
        gaf::frame_at(nullptr, -1) == nullptr, "negative index on a null sequence was not null"
    );
    require(
        gaf::frame_at(nullptr, 0) == nullptr, "nonnegative index on a null sequence was not null"
    );
    require(gaf::frame_at(sequence, -1) == nullptr, "negative frame_at index was accepted");
    require(gaf::frame_at(sequence, 2) == nullptr, "index equal to the frame count was accepted");
    require(gaf::frame_at(sequence, 0x7fffffff) == nullptr, "large frame_at index was accepted");
    const auto* first = gaf::frame_at(sequence, 0);
    const auto* second = gaf::frame_at(sequence, 1);
    require(
        first == &sequence->frames[0] && first->duration == 3 &&
            first->pixels == std::vector<uint8_t>({11}),
        "frame_at did not return the first frame-list entry"
    );
    require(
        second == &sequence->frames[1] && second->origin_x == 1 && second->origin_y == 2 &&
            second->pixels == std::vector<uint8_t>({22}),
        "frame_at did not return the second frame-list entry"
    );

    auto empty_bytes = one_frame_file();
    put16(empty_bytes, 16, 0);
    const auto empty = gaf::parse(empty_bytes);
    require(
        empty.ok() && gaf::frame_at(&empty.archive->sequences[0], 0) == nullptr,
        "zero frame count did not reject index 0"
    );
}

void malformed_and_bounded_inputs() {
    require(!gaf::parse(std::vector<uint8_t>(11)).ok(), "truncated header was accepted");

    auto cycle = one_frame_file();
    frame_header(cycle, 64, 1, 1, 0, 0, 0, false, 1, 0, 88);
    put32(cycle, 88, 64);
    const auto cycle_result = gaf::parse(cycle);
    require(
        !cycle_result.ok() && cycle_result.error->code == gaf::ErrorCode::pointer_cycle,
        "layer pointer cycle was not rejected"
    );

    auto too_large = one_frame_file();
    frame_header(too_large, 64, 65535, 65535, 0, 0, 0, false, 0, 0, 88);
    const auto large_result = gaf::parse(too_large);
    require(
        !large_result.ok() && large_result.error->code == gaf::ErrorCode::pixel_limit,
        "pathological frame allocation was not bounded"
    );

    auto bad_run = one_frame_file();
    frame_header(bad_run, 64, 2, 1, 0, 0, 0, true, 0, 0, 88);
    put16(bad_run, 88, 1);
    bad_run[90] = 14; // repeat command without its required value byte
    const auto run_result = gaf::parse(bad_run);
    require(
        !run_result.ok() && run_result.error->code == gaf::ErrorCode::malformed_compression,
        "oversized compressed run was accepted"
    );

    auto negative_count = std::vector<uint8_t>(12);
    put32(negative_count, 4, 0x0000ffffU);
    const auto negative = gaf::parse(negative_count);
    require(
        negative.ok() && negative.archive->header_sequence_count == -1 &&
            negative.archive->sequences.empty(),
        "signed sequence-count loop behavior disagreed"
    );
}

// A frame's size is refused before anything of it is decoded: past 4096 by
// 4096 pixels, or past the side a caller allows; a frame whose rows are not
// in the file fails before its buffers are made.
void frame_bounds_come_before_decoding() {
    auto wide = one_frame_file();
    frame_header(wide, 64, 4097, 4096, 0, 0, 0, true, 0, 0, 88);
    const auto wide_result = gaf::parse(wide);
    require(
        !wide_result.ok() && wide_result.error->code == gaf::ErrorCode::pixel_limit,
        "a frame over 4096 by 4096 pixels was accepted"
    );

    auto glyph = one_frame_file();
    glyph.resize(88 + 129);
    frame_header(glyph, 64, 129, 1, 0, 0, 0, false, 0, 0, 88);
    const auto glyph_result = gaf::parse(glyph, gaf::PixelData::decoded, 128);
    require(
        !glyph_result.ok() && glyph_result.error->code == gaf::ErrorCode::side_limit &&
            glyph_result.error->offset == 64,
        "a frame wider than the caller allows was accepted"
    );
    frame_header(glyph, 64, 128, 1, 0, 0, 0, false, 0, 0, 88);
    require(
        gaf::parse(glyph, gaf::PixelData::decoded, 128).ok(), "a frame within the side was refused"
    );

    // 4096 rows need 8192 bytes of row lengths; the file holds 168.
    auto rowless = one_frame_file();
    frame_header(rowless, 64, 4096, 4096, 0, 0, 0, true, 0, 0, 88);
    const auto rowless_result = gaf::parse(rowless);
    require(
        !rowless_result.ok() && rowless_result.error->code == gaf::ErrorCode::truncated,
        "a frame whose rows lie past the file was accepted"
    );
}

// Nine 4096 x 4096 frames decode to 288 MiB of pixels and coverage, past
// what a parse may keep; a checked parse keeps none of them and loads the file.
void checked_parse_keeps_no_decoded_total() {
    constexpr std::size_t frames = 9;
    constexpr uint16_t side = 4096;
    constexpr std::size_t frame_at = 56 + frames * 8;
    constexpr std::size_t rows_at = frame_at + 24;
    auto bytes = one_frame_file();
    bytes.resize(rows_at + std::size_t{side} * 2);
    put16(bytes, 16, frames);
    for (std::size_t index = 0; index < frames; ++index) {
        put32(bytes, 56 + index * 8, static_cast<uint32_t>(frame_at));
        put32(bytes, 60 + index * 8, 1);
    }
    // Every row is empty, which leaves it transparent.
    frame_header(bytes, frame_at, side, side, 0, 0, 0, true, 0, 0, rows_at);
    const auto checked = gaf::parse(bytes, gaf::PixelData::checked);
    require(
        checked.ok() && checked.archive->sequences[0].frames.size() == frames &&
            checked.archive->sequences[0].frames[0].pixels.empty(),
        checked.error ? checked.error->message : "a checked parse kept pixels"
    );
}

} // namespace

int main() {
    try {
        raw_fields_and_low_words();
        compressed_commands();
        layered_origins_clipping_and_special_route();
        compressed_literal_transparency_index_is_still_written();
        frame_at_rejects_out_of_range_indices();
        malformed_and_bounded_inputs();
        frame_bounds_come_before_decoding();
        checked_parse_keeps_no_decoded_total();
    } catch (const std::exception& error) {
        std::cerr << "sprite-format test failure: " << error.what() << '\n';
        return 1;
    }
    std::cout << "sprite-format tests passed\n";
    return 0;
}
