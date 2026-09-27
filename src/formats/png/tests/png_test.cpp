// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Round trips through the PNG writer and reader, the two row transforms the
// game's markup loader requests, and the reader's error and warning rules.

#include <cstdio>
#include <cstring>
#include <string>
#include <utility>
#include <vector>

#include <zlib.h>

#include "oa/formats/png.hpp"
#include "png_internal.hpp"

namespace {

using namespace oa::formats::png;

int g_failures = 0;

void check(bool condition, const char* what, int line) {
    if (!condition) {
        std::fprintf(stderr, "FAIL line %d: %s\n", line, what);
        ++g_failures;
    }
}

#define CHECK(cond) check((cond), #cond, __LINE__)

struct Log {
    std::vector<std::string> warnings;
    std::vector<std::string> errors;
};

void log_warning(void* user, const char* text) {
    static_cast<Log*>(user)->warnings.emplace_back(text);
}

void log_error(void* user, const char* text) {
    static_cast<Log*>(user)->errors.emplace_back(text);
}

Messages messages_for(Log* log) {
    return Messages{log, log_warning, log_error};
}

// Deterministic bytes that do not deflate well, so images span IDAT chunks.
std::vector<uint8_t> noise(std::size_t size, uint32_t seed) {
    std::vector<uint8_t> out(size);
    for (uint8_t& b : out) {
        seed = seed * 1103515245u + 12345u;
        b = static_cast<uint8_t>(seed >> 16);
    }
    return out;
}

// Clears the pad bits after the last pixel of each row.
void clear_row_padding(const Header& header, std::vector<uint8_t>& rows) {
    const std::size_t row = row_bytes(header, {});
    const uint64_t used_bits =
        static_cast<uint64_t>(header.width) * header.bit_depth * channel_count(header.color_type);
    const uint32_t spare = static_cast<uint32_t>(row * 8 - used_bits);
    if (spare == 0)
        return;
    const auto keep = static_cast<uint8_t>(0xffu << spare);
    for (uint32_t y = 0; y < header.height; ++y)
        rows[y * row + row - 1] &= keep;
}

std::vector<Rgb> grey_ramp(std::size_t entries) {
    std::vector<Rgb> palette(entries);
    for (std::size_t i = 0; i < entries; ++i)
        palette[i] = Rgb{static_cast<uint8_t>(i), static_cast<uint8_t>(i), static_cast<uint8_t>(i)};
    return palette;
}

std::vector<uint8_t> encode(
    const Header& header, const std::vector<uint8_t>& rows, const std::vector<Rgb>& palette = {}
) {
    std::vector<uint8_t> file;
    CHECK(write(Image{header, palette, rows}, &file));
    return file;
}

struct Decoded {
    bool info_ok = false;
    Info info;
    Progress progress = Progress::none;
    std::vector<uint8_t> rows;
};

Decoded decode(const std::vector<uint8_t>& file, const Transforms& transforms, Log* log) {
    Decoded out;
    const Messages messages = messages_for(log);
    out.info_ok = read_info(file, messages, &out.info);
    if (!out.info_ok)
        return out;
    out.rows.assign(row_bytes(out.info.header, transforms) * out.info.header.height, 0);
    out.progress = read_image(file, out.info, transforms, messages, out.rows);
    return out;
}

// ---- chunk surgery for malformed files ------------------------------------

struct RawChunk {
    std::string type;
    std::vector<uint8_t> data;
    bool keep_crc = true; // false writes a corrupt CRC
};

uint32_t be32(const uint8_t* p) {
    return (uint32_t{p[0]} << 24) | (uint32_t{p[1]} << 16) | (uint32_t{p[2]} << 8) | p[3];
}

void put_be32(std::vector<uint8_t>& out, uint32_t v) {
    for (int shift = 24; shift >= 0; shift -= 8)
        out.push_back(static_cast<uint8_t>(v >> shift));
}

std::vector<RawChunk> split(const std::vector<uint8_t>& file) {
    std::vector<RawChunk> chunks;
    std::size_t pos = k_signature_bytes;
    while (pos + 12 <= file.size()) {
        const uint32_t length = be32(file.data() + pos);
        RawChunk chunk;
        chunk.type.assign(reinterpret_cast<const char*>(file.data() + pos + 4), 4);
        chunk.data.assign(
            file.begin() + static_cast<std::ptrdiff_t>(pos + 8),
            file.begin() + static_cast<std::ptrdiff_t>(pos + 8 + length)
        );
        chunks.push_back(chunk);
        pos += 12 + length;
    }
    return chunks;
}

std::vector<uint8_t> join(const std::vector<RawChunk>& chunks) {
    std::vector<uint8_t> out(k_signature, k_signature + k_signature_bytes);
    for (const RawChunk& chunk : chunks) {
        put_be32(out, static_cast<uint32_t>(chunk.data.size()));
        std::vector<uint8_t> typed(chunk.type.begin(), chunk.type.end());
        typed.insert(typed.end(), chunk.data.begin(), chunk.data.end());
        out.insert(out.end(), typed.begin(), typed.end());
        uint32_t crc =
            static_cast<uint32_t>(crc32(0, typed.data(), static_cast<uInt>(typed.size())));
        if (!chunk.keep_crc)
            crc ^= 0xffffffffu;
        put_be32(out, crc);
    }
    return out;
}

std::size_t count_type(const std::vector<RawChunk>& chunks, const char* type) {
    std::size_t n = 0;
    for (const RawChunk& chunk : chunks)
        n += chunk.type == type ? 1 : 0;
    return n;
}

// Inserts `chunk` before the first chunk of type `before`.
void insert_before(std::vector<RawChunk>& chunks, const char* before, const RawChunk& chunk) {
    for (std::size_t i = 0; i < chunks.size(); ++i)
        if (chunks[i].type == before) {
            chunks.insert(chunks.begin() + static_cast<std::ptrdiff_t>(i), chunk);
            return;
        }
}

// ---- tests ----------------------------------------------------------------

void test_round_trip_every_format() {
    struct Format {
        ColorType color;
        uint8_t depth;
    };

    const Format formats[] = {
        {ColorType::grey, 1},
        {ColorType::grey, 2},
        {ColorType::grey, 4},
        {ColorType::grey, 8},
        {ColorType::grey, 16},
        {ColorType::rgb, 8},
        {ColorType::rgb, 16},
        {ColorType::palette, 1},
        {ColorType::palette, 2},
        {ColorType::palette, 4},
        {ColorType::palette, 8},
        {ColorType::grey_alpha, 8},
        {ColorType::grey_alpha, 16},
        {ColorType::rgb_alpha, 8},
        {ColorType::rgb_alpha, 16},
    };
    const uint32_t sizes[][2] = {{1, 1}, {3, 2}, {5, 9}, {13, 7}, {17, 17}};
    uint32_t seed = 1;
    for (const Format& format : formats)
        for (const auto& size : sizes)
            for (Interlace interlace : {Interlace::none, Interlace::adam7}) {
                const Header header{size[0], size[1], format.depth, format.color, interlace};
                std::vector<uint8_t> rows = noise(row_bytes(header, {}) * header.height, seed++);
                clear_row_padding(header, rows);
                const std::vector<Rgb> palette = format.color == ColorType::palette
                                                     ? grey_ramp(1u << format.depth)
                                                     : std::vector<Rgb>{};
                const auto file = encode(header, rows, palette);
                Log log;
                const Decoded d = decode(file, {}, &log);
                CHECK(d.info_ok && d.progress == Progress::end);
                CHECK(log.errors.empty() && log.warnings.empty());
                CHECK(d.info.header.width == header.width && d.info.header.height == header.height);
                CHECK(d.info.header.bit_depth == header.bit_depth);
                CHECK(d.info.header.color_type == header.color_type);
                CHECK(d.info.header.interlace == interlace);
                CHECK(d.rows == rows);
                CHECK(d.info.has_palette == (format.color == ColorType::palette));
            }
}

void test_multiple_idat_chunks() {
    const Header header{96, 64, 8, ColorType::rgb_alpha, Interlace::adam7};
    const auto rows = noise(row_bytes(header, {}) * header.height, 77);
    const auto file = encode(header, rows);
    CHECK(count_type(split(file), "IDAT") > 2);
    Log log;
    const Decoded d = decode(file, {}, &log);
    CHECK(d.progress == Progress::end && d.rows == rows);
}

void test_strip_16() {
    const Header header{3, 2, 16, ColorType::grey_alpha, Interlace::none};
    std::vector<uint8_t> rows(row_bytes(header, {}) * header.height);
    for (std::size_t i = 0; i < rows.size(); ++i)
        rows[i] = static_cast<uint8_t>(i * 7 + 1);
    Log log;
    const Decoded d = decode(encode(header, rows), Transforms{true, true}, &log);
    CHECK(row_bytes(header, Transforms{true, false}) == 6);
    CHECK(d.progress == Progress::end && d.rows.size() == 12);
    for (std::size_t i = 0; i < d.rows.size(); ++i)
        CHECK(d.rows[i] == rows[i * 2]);
}

void test_unpack_sub_byte_samples() {
    // 2-bit palette indices 0 1 2 3 3 on one row, then 3 2 1 0 0.
    const Header header{5, 2, 2, ColorType::palette, Interlace::none};
    const std::vector<uint8_t> rows = {0x1b, 0xc0, 0xe4, 0x00};
    Log log;
    const Decoded d = decode(encode(header, rows, grey_ramp(4)), Transforms{true, true}, &log);
    CHECK(row_bytes(header, Transforms{false, true}) == 5);
    CHECK((d.rows == std::vector<uint8_t>{0, 1, 2, 3, 3, 3, 2, 1, 0, 0}));

    // Interlaced 1-bit grey: unpacked values stay 0/1, not scaled to 255.
    const Header bits{9, 3, 1, ColorType::grey, Interlace::adam7};
    std::vector<uint8_t> packed = noise(row_bytes(bits, {}) * bits.height, 5);
    clear_row_padding(bits, packed);
    const Decoded u = decode(encode(bits, packed), Transforms{true, true}, &log);
    CHECK(u.rows.size() == 27);
    for (uint32_t y = 0; y < 3; ++y)
        for (uint32_t x = 0; x < 9; ++x) {
            const uint32_t bit = (packed[y * 2 + x / 8] >> (7 - x % 8)) & 1u;
            CHECK(u.rows[y * 9 + x] == bit);
        }
    CHECK(log.errors.empty());
}

// Reads the chunks before the image data, returning the one error (or "")
// and the warnings.
struct InfoResult {
    bool ok = false;
    Info info;
    std::string error;
    std::vector<std::string> warnings;
};

InfoResult info_of(const std::vector<RawChunk>& chunks) {
    Log log;
    InfoResult out;
    out.ok = read_info(join(chunks), messages_for(&log), &out.info);
    CHECK(log.errors.size() <= 1);
    out.error = log.errors.empty() ? "" : log.errors[0];
    out.warnings = log.warnings;
    return out;
}

std::vector<RawChunk>
with_header_byte(std::vector<RawChunk> chunks, std::size_t at, uint8_t value) {
    chunks[0].data[at] = value;
    return chunks;
}

void test_header_and_palette_errors() {
    const Header header{4, 4, 8, ColorType::palette, Interlace::none};
    const auto file = encode(header, noise(16, 3), grey_ramp(256));
    auto chunks = split(file);

    auto bad_crc = chunks;
    bad_crc[0].keep_crc = false;
    CHECK(info_of(bad_crc).error == "IHDR: CRC error");

    auto no_palette = chunks;
    no_palette.erase(no_palette.begin() + 1);
    CHECK(info_of(no_palette).error == "Missing PLTE before IDAT");

    auto odd_palette = chunks;
    odd_palette[1].data.pop_back();
    CHECK(info_of(odd_palette).error == "Invalid palette chunk");

    CHECK(info_of(with_header_byte(chunks, 8, 3)).error == "Invalid bit depth in IHDR");
    CHECK(info_of(with_header_byte(chunks, 9, 5)).error == "Invalid color type in IHDR");
    CHECK(info_of(with_header_byte(chunks, 3, 0)).error == "Invalid image size in IHDR");
    CHECK(info_of(with_header_byte(chunks, 10, 1)).error == "Unknown compression method in IHDR");
    CHECK(info_of(with_header_byte(chunks, 11, 1)).error == "Unknown filter method in IHDR");
    CHECK(info_of(with_header_byte(chunks, 12, 2)).error == "Unknown interlace method in IHDR");
    auto shallow_rgb = with_header_byte(chunks, 9, 2);
    shallow_rgb[0].data[8] = 4;
    CHECK(info_of(shallow_rgb).error == "Invalid color type/bit depth combination in IHDR");
    auto long_header = chunks;
    long_header[0].data.push_back(0);
    CHECK(info_of(long_header).error == "Invalid IHDR chunk");
    auto two_headers = chunks;
    two_headers.insert(two_headers.begin() + 1, chunks[0]);
    CHECK(info_of(two_headers).error == "Out of place IHDR");
    auto palette_first = chunks;
    std::swap(palette_first[0], palette_first[1]);
    CHECK(info_of(palette_first).error == "Missing IHDR before PLTE");
    std::vector<RawChunk> data_first = {chunks[2], chunks[0], chunks[1], chunks[3]};
    CHECK(info_of(data_first).error == "Missing IHDR before IDAT");

    // A 16-bit palette image passes.
    const InfoResult deep = info_of(with_header_byte(chunks, 8, 16));
    CHECK(deep.ok && deep.info.header.bit_depth == 16);

    // A PLTE on a grey image is kept; a ragged one is ignored with a
    // warning, yet still counts as the image's PLTE.
    const Header grey{2, 1, 8, ColorType::grey, Interlace::none};
    auto grey_chunks = split(encode(grey, {5, 6}));
    insert_before(grey_chunks, "IDAT", RawChunk{"PLTE", {0, 0, 0, 1, 2, 3}});
    InfoResult kept = info_of(grey_chunks);
    CHECK(kept.ok && kept.info.has_palette && kept.info.palette_entries == 2);
    CHECK(kept.info.palette[1].b == 3);
    grey_chunks = split(encode(grey, {5, 6}));
    insert_before(grey_chunks, "IDAT", RawChunk{"PLTE", {0, 0, 0, 1}});
    InfoResult ragged = info_of(grey_chunks);
    CHECK(ragged.ok && !ragged.info.has_palette);
    CHECK((ragged.warnings == std::vector<std::string>{"Invalid palette chunk"}));
    insert_before(grey_chunks, "IDAT", RawChunk{"PLTE", {0, 0, 0}});
    InfoResult second = info_of(grey_chunks);
    CHECK(!second.ok && second.error == "Duplicate PLTE chunk");

    // A tRNS read before PLTE is cut to the palette's size once it arrives.
    auto early_alpha = chunks;
    early_alpha[1].data.resize(2 * 3);
    early_alpha.insert(early_alpha.begin() + 1, RawChunk{"tRNS", {1, 2, 3, 4, 5}});
    const InfoResult truncated = info_of(early_alpha);
    CHECK(truncated.ok);
    CHECK(
        (truncated.warnings ==
         std::vector<std::string>{
             "Missing PLTE before tRNS", "Truncating incorrect tRNS chunk length"
         })
    );
}

// Rows too wide to size draw a warning when IHDR is read and another when
// the loader asks for the header.
void test_width_warnings() {
    const Header header{2, 1, 16, ColorType::rgb_alpha, Interlace::none};
    auto chunks = split(encode(header, noise(16, 4)));
    chunks[0].data[0] = 0x10; // width 2^28 + 2: eight bytes a pixel overflow 31 bits
    Log log;
    Info info;
    CHECK(read_info(join(chunks), messages_for(&log), &info));
    CHECK(
        (log.warnings ==
         std::vector<std::string>{"Width too large to process image data; rowbytes will overflow."})
    );
    const Header shown = header_of(info, messages_for(&log));
    CHECK(shown.width == 0x10000002u && log.warnings.size() == 2);
    CHECK(log.warnings[1] == "Width too large for libpng to process image data.");
    log = {};
    Info narrow;
    CHECK(read_info(encode(header, noise(16, 4)), messages_for(&log), &narrow));
    header_of(narrow, messages_for(&log));
    CHECK(log.warnings.empty());
}

void test_chunk_rules() {
    const Header header{4, 3, 8, ColorType::grey, Interlace::none};
    const auto rows = noise(12, 9);
    const auto chunks = split(encode(header, rows));

    // Ancillary CRC mismatch: warning, chunk dropped, image still decodes.
    auto ancillary = chunks;
    insert_before(ancillary, "IDAT", RawChunk{"gAMA", {0, 0, 0xaf, 0xc8}, false});
    Log log;
    Decoded d = decode(join(ancillary), {}, &log);
    CHECK(d.progress == Progress::end && d.rows == rows);
    CHECK((log.warnings == std::vector<std::string>{"gAMA: CRC error"}));

    // Unknown critical chunks stop reading; unknown ancillary ones are skipped.
    auto critical = chunks;
    insert_before(critical, "IDAT", RawChunk{"ABCD", {1, 2}});
    CHECK(info_of(critical).error == "ABCD: unknown critical chunk");
    auto private_chunk = chunks;
    insert_before(private_chunk, "IDAT", RawChunk{"prIv", {1, 2}});
    log = {};
    CHECK(decode(join(private_chunk), {}, &log).progress == Progress::end && log.errors.empty());

    // Type bytes from ')' up are accepted like letters; below that they are not.
    auto quirk = chunks;
    insert_before(quirk, "IDAT", RawChunk{"a)1b", {}});
    log = {};
    CHECK(decode(join(quirk), {}, &log).progress == Progress::end && log.errors.empty());
    auto invalid = chunks;
    insert_before(invalid, "IDAT", RawChunk{"a(1b", {}});
    CHECK(info_of(invalid).error == "a[28]1b: invalid chunk type");

    // Known ancillary chunks need IHDR first.
    auto early = chunks;
    early.insert(early.begin(), RawChunk{"tEXt", {'a', 0, 'b'}});
    CHECK(info_of(early).error == "Missing IHDR before tEXt");

    // Chunks after the image data.
    auto late = chunks;
    insert_before(late, "IEND", RawChunk{"gAMA", {0, 0, 0xaf, 0xc8}});
    insert_before(late, "IEND", RawChunk{"tEXt", {'a', 0, 'b'}});
    late.back().data = {0};
    log = {};
    d = decode(join(late), {}, &log);
    CHECK(d.progress == Progress::end && log.errors.empty());
    CHECK(
        (log.warnings ==
         std::vector<std::string>{"Invalid gAMA after IDAT", "Incorrect IEND chunk length"})
    );

    auto second_idat = chunks;
    insert_before(second_idat, "IEND", RawChunk{"IDAT", {}});
    log = {};
    d = decode(join(second_idat), {}, &log);
    CHECK(d.progress == Progress::rows && d.rows == rows);
    CHECK((log.errors == std::vector<std::string>{"Too many IDAT's found"}));

    auto no_end = chunks;
    no_end.pop_back();
    log = {};
    d = decode(join(no_end), {}, &log);
    CHECK(d.progress == Progress::rows && (log.errors == std::vector<std::string>{"Read Error"}));

    // IEND before any image data.
    std::vector<RawChunk> empty = {chunks.front(), chunks.back()};
    CHECK(info_of(empty).error == "No image in file");
}

// Warnings of the ancillary chunks before the image data.
void test_ancillary_rules() {
    const Header grey{2, 2, 8, ColorType::grey, Interlace::none};
    const Header indexed{2, 2, 8, ColorType::palette, Interlace::none};
    const Header rgba{2, 2, 8, ColorType::rgb_alpha, Interlace::none};
    const auto grey_chunks = split(encode(grey, noise(4, 1)));
    const auto palette_chunks = split(encode(indexed, noise(4, 2), grey_ramp(256)));
    const auto rgba_chunks = split(encode(rgba, noise(16, 3)));
    const std::vector<uint8_t> gamma_45455 = {0, 0, 0xb1, 0x8f};
    const std::vector<uint8_t> gamma_45000 = {0, 0, 0xaf, 0xc8};

    struct Case {
        const std::vector<RawChunk>* base;
        std::vector<RawChunk> before_data; // inserted before the first IDAT
        std::vector<std::string> warnings;
        const char* error; // "" when read_info succeeds
    };

    auto be32_bytes = [](uint32_t v) {
        return std::vector<uint8_t>{
            static_cast<uint8_t>(v >> 24),
            static_cast<uint8_t>(v >> 16),
            static_cast<uint8_t>(v >> 8),
            static_cast<uint8_t>(v)
        };
    };
    auto chrm = [&](uint32_t wx) {
        std::vector<uint8_t> out;
        for (uint32_t v : {wx, 32900u, 64000u, 33000u, 30000u, 60000u, 15000u, 6000u}) {
            const auto b = be32_bytes(v);
            out.insert(out.end(), b.begin(), b.end());
        }
        return out;
    };
    const std::vector<uint8_t> pcal_linear = {
        'p', 0, 0, 0, 0, 0, 0, 0, 0, 1, 0, 2, 'u', 0, '1', 0, '2'
    };
    std::vector<uint8_t> pcal_wrong_count = pcal_linear;
    pcal_wrong_count[11] = 3;
    std::vector<uint8_t> pcal_unknown_type = pcal_linear;
    pcal_unknown_type[10] = 7;
    std::vector<uint8_t> pcal_missing_parameter = pcal_linear;
    pcal_missing_parameter.resize(pcal_linear.size() - 2);

    const Case cases[] = {
        {&grey_chunks,
         {{"gAMA", gamma_45455}, {"gAMA", gamma_45455}},
         {"Duplicate gAMA chunk"},
         ""},
        {&grey_chunks, {{"gAMA", {0, 0, 1}}}, {"Incorrect gAMA chunk length"}, ""},
        {&palette_chunks, {{"gAMA", gamma_45455}}, {"Out of place gAMA chunk"}, ""},
        {&grey_chunks,
         {{"gAMA", gamma_45455}, {"sRGB", {0}}},
         {"Ignoring incorrect gAMA value when sRGB is also present"},
         ""},
        {&grey_chunks,
         {{"sRGB", {0}}, {"gAMA", gamma_45455}},
         {"Ignoring incorrect gAMA value when sRGB is also present"},
         ""},
        {&grey_chunks, {{"sRGB", {0}}, {"gAMA", gamma_45000}}, {}, ""},
        {&grey_chunks, {{"gAMA", gamma_45000}, {"sRGB", {0}}}, {}, ""},
        {&grey_chunks, {{"sRGB", {4}}}, {"Unknown sRGB intent"}, ""},
        {&grey_chunks, {{"sRGB", {0}}, {"sRGB", {0}}}, {"Duplicate sRGB chunk"}, ""},
        {&grey_chunks, {{"sRGB", {0, 0}}}, {"Incorrect sRGB chunk length"}, ""},
        {&palette_chunks, {{"sRGB", {0}}}, {"Out of place sRGB chunk"}, ""},
        {&grey_chunks, {{"cHRM", chrm(31270)}}, {}, ""},
        {&grey_chunks, {{"cHRM", chrm(90000)}}, {"Invalid cHRM white point"}, ""},
        {&grey_chunks,
         {{"cHRM", chrm(31270)}, {"cHRM", chrm(31270)}},
         {"Duplicate cHRM chunk"},
         ""},
        {&grey_chunks, {{"cHRM", {1, 2, 3}}}, {"Incorrect cHRM chunk length"}, ""},
        {&palette_chunks, {{"cHRM", chrm(31270)}}, {"Missing PLTE before cHRM"}, ""},
        {&grey_chunks,
         {{"cHRM", chrm(40000)}, {"sRGB", {1}}},
         {"Ignoring incorrect cHRM value when sRGB is also present"},
         ""},
        {&grey_chunks,
         {{"sRGB", {1}}, {"cHRM", chrm(40000)}},
         {"Ignoring incorrect cHRM value when sRGB is also present"},
         ""},
        {&grey_chunks, {{"sRGB", {1}}, {"cHRM", chrm(31270)}}, {}, ""},
        {&grey_chunks, {{"sBIT", {8, 8}}}, {"Incorrect sBIT chunk length"}, ""},
        {&grey_chunks, {{"sBIT", {8}}, {"sBIT", {8}}}, {"Duplicate sBIT chunk"}, ""},
        {&palette_chunks, {{"sBIT", {8, 8, 8}}}, {"Out of place sBIT chunk"}, ""},
        {&rgba_chunks, {{"tRNS", {0, 1}}}, {"tRNS chunk not allowed with alpha channel"}, ""},
        {&grey_chunks, {{"tRNS", {0, 1, 2}}}, {"Incorrect tRNS chunk length"}, ""},
        {&grey_chunks, {{"tRNS", {0, 1}}, {"tRNS", {0, 1}}}, {"Duplicate tRNS chunk"}, ""},
        {&palette_chunks, {{"tRNS", {}}}, {"Zero length tRNS chunk"}, ""},
        {&grey_chunks, {{"bKGD", {0}}}, {"Incorrect bKGD chunk length"}, ""},
        {&grey_chunks, {{"bKGD", {0, 1}}, {"bKGD", {0, 1}}}, {"Duplicate bKGD chunk"}, ""},
        {&grey_chunks, {{"hIST", {0, 1}}}, {"Missing PLTE before hIST"}, ""},
        {&palette_chunks, {{"hIST", {0, 1}}}, {"Incorrect hIST chunk length"}, ""},
        {&grey_chunks, {{"pHYs", {0, 0, 0, 1, 0, 0, 0, 1}}}, {"Incorrect pHYs chunk length"}, ""},
        {&grey_chunks,
         {{"pHYs", {0, 0, 0, 1, 0, 0, 0, 1, 0}}, {"pHYs", {0, 0, 0, 1, 0, 0, 0, 1, 0}}},
         {"Duplicate pHYS chunk"},
         ""},
        {&grey_chunks, {{"oFFs", {0}}}, {"Incorrect oFFs chunk length"}, ""},
        {&grey_chunks, {{"pCAL", pcal_linear}}, {}, ""},
        {&grey_chunks, {{"pCAL", {'p', 0, 0, 0}}}, {"Invalid pCAL data"}, ""},
        {&grey_chunks,
         {{"pCAL", pcal_wrong_count}},
         {"Invalid pCAL parameters for equation type"},
         ""},
        {&grey_chunks,
         {{"pCAL", pcal_unknown_type}},
         {"Unrecognized equation type for pCAL chunk"},
         ""},
        {&grey_chunks, {{"pCAL", pcal_missing_parameter}}, {"Invalid pCAL data"}, ""},
        {&grey_chunks,
         {{"pCAL", pcal_linear}, {"pCAL", pcal_linear}},
         {"Duplicate pCAL chunk"},
         ""},
        {&grey_chunks, {{"tIME", {7, 0xcf, 1, 1, 0, 0}}}, {"Incorrect tIME chunk length"}, ""},
        {&grey_chunks,
         {{"tIME", {7, 0xcf, 1, 1, 0, 0, 0}}, {"tIME", {7, 0xcf, 1, 1, 0, 0, 0}}},
         {"Duplicate tIME chunk"},
         ""},
        {&grey_chunks, {{"zTXt", {'k', 'e', 'y'}}}, {"Zero length zTXt chunk"}, ""},
        {&grey_chunks, {{"zTXt", {'k', 0, 1, 0x78}}}, {"Unknown zTXt compression type 1"}, ""},
        {&grey_chunks, {{"zTXt", {'k', 0, 0xff}}}, {"Unknown zTXt compression type -1"}, ""},
        {&grey_chunks, {{"zTXt", {'k', 0, 0, 0x78, 0x00}}}, {"incorrect header check"}, ""},
    };
    int index = 0;
    for (const Case& c : cases) {
        auto chunks = *c.base;
        for (const RawChunk& chunk : c.before_data)
            insert_before(chunks, "IDAT", chunk);
        const InfoResult result = info_of(chunks);
        if (result.warnings != c.warnings || result.error != c.error)
            std::fprintf(
                stderr,
                "ancillary case %d: %zu warnings, error '%s'\n",
                index,
                result.warnings.size(),
                result.error.c_str()
            );
        CHECK(result.warnings == c.warnings);
        CHECK(result.error == c.error);
        CHECK(result.ok == (c.error[0] == '\0'));
        ++index;
    }

    // Chunks that need IHDR first. cHRM's error reads as sBIT's.
    const std::pair<RawChunk, const char*> needs_header[] = {
        {{"gAMA", gamma_45000}, "Missing IHDR before gAMA"},
        {{"cHRM", chrm(31270)}, "Missing IHDR before sBIT"},
        {{"sBIT", {8}}, "Missing IHDR before sBIT"},
        {{"sRGB", {0}}, "Missing IHDR before sRGB"},
        {{"tRNS", {0, 1}}, "Missing IHDR before tRNS"},
        {{"bKGD", {0, 1}}, "Missing IHDR before bKGD"},
        {{"hIST", {0, 1}}, "Missing IHDR before hIST"},
        {{"pHYs", {0, 0, 0, 1, 0, 0, 0, 1, 0}}, "Missing IHDR before pHYS"},
        {{"oFFs", {0, 0, 0, 1, 0, 0, 0, 1, 0}}, "Missing IHDR before oFFs"},
        {{"pCAL", pcal_linear}, "Missing IHDR before pCAL"},
        {{"tIME", {7, 0xcf, 1, 1, 0, 0, 0}}, "Out of place tIME chunk"},
        {{"zTXt", {'k', 0, 0}}, "Missing IHDR before zTXt"},
    };
    for (const auto& [chunk, error] : needs_header) {
        auto chunks = grey_chunks;
        chunks.insert(chunks.begin(), chunk);
        CHECK(info_of(chunks).error == error);
    }

    // Ancillary chunks after the image data go to a fresh record: a second
    // tIME there is no duplicate of one before it.
    auto times = grey_chunks;
    const RawChunk time{"tIME", {7, 0xcf, 1, 1, 0, 0, 0}};
    insert_before(times, "IDAT", time);
    insert_before(times, "IEND", time);
    Log log;
    CHECK(decode(join(times), {}, &log).progress == Progress::end && log.warnings.empty());
    insert_before(times, "IEND", time);
    log = {};
    CHECK(decode(join(times), {}, &log).progress == Progress::end);
    CHECK((log.warnings == std::vector<std::string>{"Duplicate tIME chunk"}));
    auto after = grey_chunks;
    for (const char* type :
         {"PLTE", "sBIT", "cHRM", "sRGB", "tRNS", "bKGD", "hIST", "pHYs", "oFFs", "pCAL"})
        insert_before(after, "IEND", RawChunk{type, {0}});
    log = {};
    CHECK(decode(join(after), {}, &log).progress == Progress::end && log.errors.empty());
    CHECK(
        (log.warnings == std::vector<std::string>{
                             "Invalid PLTE after IDAT",
                             "Invalid sBIT after IDAT",
                             "Invalid cHRM after IDAT",
                             "Invalid sRGB after IDAT",
                             "Invalid tRNS after IDAT",
                             "Invalid bKGD after IDAT",
                             "Invalid hIST after IDAT",
                             "Invalid pHYS after IDAT",
                             "Invalid oFFs after IDAT",
                             "Invalid pCAL after IDAT"
                         })
    );
}

// Chunk messages name the chunk, escaping type bytes outside ')'..'Z' and
// 'a'..'z', and keep 63 characters of the text.
void test_chunk_message_format() {
    char out[oa::formats::png::detail::k_chunk_message_bytes];
    const uint8_t mixed[] = {'a', 0x28, '1', 'b'};
    oa::formats::png::detail::format_chunk_message(mixed, "invalid chunk type", out);
    CHECK(std::string(out) == "a[28]1b: invalid chunk type");
    const uint8_t edges[] = {0x29, 0x5b, 0x7a, 0x7b};
    oa::formats::png::detail::format_chunk_message(edges, "x", out);
    CHECK(std::string(out) == ")[5B]z[7B]: x");
    const std::string long_text(100, 'q');
    const uint8_t plain[] = {'t', 'E', 'X', 't'};
    oa::formats::png::detail::format_chunk_message(plain, long_text.c_str(), out);
    CHECK(std::string(out) == "tEXt: " + std::string(63, 'q'));
}

// Rows decoded before an error stay in the output.
void test_partial_image_data() {
    const Header header{64, 64, 8, ColorType::rgb_alpha, Interlace::none};
    const std::size_t row = row_bytes(header, {});
    const auto rows = noise(row * header.height, 11);
    const auto chunks = split(encode(header, rows));
    CHECK(count_type(chunks, "IDAT") >= 2);

    // Truncated after the first IDAT chunk.
    std::vector<RawChunk> truncated = {chunks[0], chunks[1], chunks.back()};
    Log log;
    Decoded d = decode(join(truncated), {}, &log);
    CHECK(d.info_ok && d.progress == Progress::none);
    CHECK((log.errors == std::vector<std::string>{"Not enough image data"}));
    CHECK(std::memcmp(d.rows.data(), rows.data(), row) == 0);
    CHECK(d.rows.back() == 0);

    // A bad CRC on the first IDAT is found once its rows are out.
    auto bad = chunks;
    bad[1].keep_crc = false;
    log = {};
    d = decode(join(bad), {}, &log);
    CHECK(d.progress == Progress::none);
    CHECK((log.errors == std::vector<std::string>{"IDAT: CRC error"}));
    CHECK(std::memcmp(d.rows.data(), rows.data(), row) == 0);

    // A bad CRC on the last IDAT is found after every row.
    auto bad_last = chunks;
    for (std::size_t i = bad_last.size(); i-- > 0;)
        if (bad_last[i].type == "IDAT") {
            bad_last[i].keep_crc = false;
            break;
        }
    log = {};
    d = decode(join(bad_last), {}, &log);
    CHECK(d.progress == Progress::rows && d.rows == rows);
    CHECK((log.errors == std::vector<std::string>{"IDAT: CRC error"}));

    // Bytes after the end of the zlib stream. When zlib meets the stream end
    // while filling the last row, that row is dropped with the error.
    auto trailing = chunks;
    for (std::size_t i = trailing.size(); i-- > 0;)
        if (trailing[i].type == "IDAT") {
            trailing[i].data.push_back(0);
            break;
        }
    log = {};
    d = decode(join(trailing), {}, &log);
    CHECK(d.progress == Progress::none);
    CHECK(std::memcmp(d.rows.data(), rows.data(), rows.size() - row) == 0);
    CHECK((log.errors == std::vector<std::string>{"Extra compressed data"}));

    // One IDAT holding the whole stream, cut inside its data: the header is
    // read, and the rows of every whole 8 KiB piece before the cut decode.
    const Header wide{128, 64, 8, ColorType::rgb_alpha, Interlace::none};
    const std::size_t wide_row = row_bytes(wide, {});
    const auto wide_rows = noise(wide_row * wide.height, 12);
    const auto wide_chunks = split(encode(wide, wide_rows));
    std::vector<RawChunk> single = {wide_chunks[0], RawChunk{"IDAT", {}}, wide_chunks.back()};
    for (const RawChunk& chunk : wide_chunks)
        if (chunk.type == "IDAT")
            single[1].data.insert(single[1].data.end(), chunk.data.begin(), chunk.data.end());
    CHECK(single[1].data.size() > 0x2000 * 3);
    std::vector<uint8_t> cut = join(single);
    const std::size_t data_at = k_signature_bytes + 12 + 13 + 8;
    cut.resize(data_at + 0x2000 * 2 + 100);
    log = {};
    d = decode(cut, {}, &log);
    CHECK(d.info_ok && d.info.header.width == 128 && d.info.header.height == 64);
    CHECK(d.info.image_data == data_at && d.info.image_data_bytes == single[1].data.size());
    CHECK(d.progress == Progress::none && (log.errors == std::vector<std::string>{"Read Error"}));
    std::size_t decoded = 0;
    while (decoded < wide.height &&
           std::memcmp(
               d.rows.data() + decoded * wide_row, wide_rows.data() + decoded * wide_row, wide_row
           ) == 0)
        ++decoded;
    // As many rows as the first two pieces inflate to.
    std::vector<uint8_t> inflated(wide_rows.size() + wide.height);
    z_stream z{};
    CHECK(inflateInit(&z) == Z_OK);
    z.next_in = single[1].data.data();
    z.avail_in = 0x2000 * 2;
    z.next_out = inflated.data();
    z.avail_out = static_cast<uInt>(inflated.size());
    inflate(&z, Z_SYNC_FLUSH);
    const std::size_t whole_rows = (inflated.size() - z.avail_out) / (wide_row + 1);
    inflateEnd(&z);
    CHECK(whole_rows > 0 && whole_rows < wide.height);
    CHECK(decoded == whole_rows);
    CHECK(d.rows.back() == 0);
}

void test_bad_filter_and_stream() {
    const Header header{2, 2, 8, ColorType::grey, Interlace::none};
    const uint8_t scanlines[] = {0, 1, 2, 5, 3, 4};
    std::vector<uint8_t> packed(64);
    uLongf size = static_cast<uLongf>(packed.size());
    CHECK(compress(packed.data(), &size, scanlines, sizeof scanlines) == Z_OK);
    packed.resize(size);
    auto chunks = split(encode(header, {1, 2, 3, 4}));
    auto set_data = [&](const std::vector<uint8_t>& data) {
        for (RawChunk& chunk : chunks)
            if (chunk.type == "IDAT")
                chunk.data = data;
    };
    set_data(packed);
    Log log;
    Decoded d = decode(join(chunks), {}, &log);
    CHECK(d.progress == Progress::none);
    CHECK((log.errors == std::vector<std::string>{"Bad adaptive filter type"}));
    CHECK(d.rows[0] == 1 && d.rows[1] == 2 && d.rows[2] == 0);

    // zlib's own message is shown; without one, the reader's.
    set_data({0x78, 0x9c, 0xff, 0xff});
    log = {};
    d = decode(join(chunks), {}, &log);
    CHECK(d.progress == Progress::none);
    CHECK((log.errors == std::vector<std::string>{"invalid block type"}));
    set_data({0x78, 0x20, 0, 0, 0, 1});
    log = {};
    d = decode(join(chunks), {}, &log);
    CHECK((log.errors == std::vector<std::string>{"Decompression error"}));
}

void test_writer_rejects_invalid_images() {
    std::vector<uint8_t> out;
    const std::vector<uint8_t> rows(8);
    CHECK(!write(Image{{2, 2, 16, ColorType::palette, Interlace::none}, {}, rows}, &out));
    CHECK(!write(Image{{2, 2, 4, ColorType::rgb, Interlace::none}, {}, rows}, &out));
    CHECK(!write(Image{{2, 2, 8, ColorType::palette, Interlace::none}, {}, rows}, &out));
    CHECK(!write(Image{{2, 2, 8, ColorType::grey, Interlace::none}, {}, rows}, &out));
    CHECK(!write(Image{{0, 2, 8, ColorType::grey, Interlace::none}, {}, rows}, &out));
    CHECK(write(Image{{2, 4, 8, ColorType::grey, Interlace::none}, {}, rows}, &out));
    CHECK(has_signature(out) && !has_signature(std::span<const uint8_t>(out.data(), 7)));
}

} // namespace

int main() {
    test_round_trip_every_format();
    test_multiple_idat_chunks();
    test_strip_16();
    test_unpack_sub_byte_samples();
    test_header_and_palette_errors();
    test_width_warnings();
    test_chunk_rules();
    test_ancillary_rules();
    test_chunk_message_format();
    test_partial_image_data();
    test_bad_filter_and_stream();
    test_writer_rejects_invalid_images();
    if (g_failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", g_failures);
        return 1;
    }
    std::puts("png-format: ok");
    return 0;
}
