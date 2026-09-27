// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/pcx.hpp"
#include "oa/test/game_assets.hpp"

#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

namespace {

int failures = 0;

#define CHECK(x)                                                                                   \
    do {                                                                                           \
        if (!(x)) {                                                                                \
            std::fprintf(stderr, "%s:%d: CHECK failed: %s\n", __FILE__, __LINE__, #x);             \
            ++failures;                                                                            \
        }                                                                                          \
    } while (false)

using namespace oa::present;
using oa::Palette;
using oa::PaletteEntry;
using oa::Surface;

std::vector<uint8_t> encode_run(uint8_t value, int32_t count, int32_t& written) {
    MemoryWriter writer;
    auto stream = memory_writer_stream(writer);
    written = encode_pcx_run(stream, value, count);
    return writer.bytes;
}

void test_encode_run() {
    int32_t written = 0;
    CHECK(encode_run(0x12, 1, written) == std::vector<uint8_t>({0x12}) && written == 1);
    CHECK(encode_run(0xC5, 1, written) == std::vector<uint8_t>({0xC1, 0xC5}) && written == 2);
    CHECK(encode_run(0x12, 2, written) == std::vector<uint8_t>({0xC2, 0x12}) && written == 2);
    CHECK(
        encode_run(0x40, 100, written) == std::vector<uint8_t>({0xFF, 0x40, 0xE5, 0x40}) &&
        written == 4
    );
    CHECK(encode_run(0x40, 0, written).empty() && written == 0);
    CHECK(encode_run(0x40, -3, written).empty() && written == 0);
}

void test_encode_row() {
    const uint8_t row[] = {1, 1, 1, 2, 0xD0, 3, 3};
    MemoryWriter writer;
    auto stream = memory_writer_stream(writer);
    const auto written = encode_pcx_row(stream, row, 7);
    CHECK(written == 7);
    CHECK(writer.bytes == std::vector<uint8_t>({0xC3, 1, 2, 0xC1, 0xD0, 0xC2, 3}));
}

void test_decode_row() {
    const uint8_t data[] = {0xC3, 7, 9, 0xC0, 5, 0xC4, 8};
    MemoryReader reader{data};
    auto stream = memory_reader_stream(reader);
    uint8_t row[6] = {};
    CHECK(decode_pcx_row(stream, row, 6));
    const uint8_t expected[6] = {7, 7, 7, 9, 8, 8};
    CHECK(std::memcmp(row, expected, sizeof row) == 0);
    CHECK(reader.position == sizeof data); // the 4-pixel run is clipped to 2

    const uint8_t truncated[] = {0xC3};
    MemoryReader short_reader{truncated};
    auto short_stream = memory_reader_stream(short_reader);
    CHECK(!decode_pcx_row(short_stream, row, 3));
}

void test_round_trip() {
    constexpr int32_t width = 37;
    constexpr int32_t height = 11;
    std::vector<uint8_t> pixels(width * height);
    uint32_t seed = 12345;
    for (auto& p : pixels) {
        seed = seed * 1103515245u + 12345u;
        p = static_cast<uint8_t>((seed >> 16) % 5 == 0 ? (seed >> 8) : 0xC7);
    }
    uint8_t color_map[pcx_color_map_size];
    for (int i = 0; i < pcx_color_map_size; ++i)
        color_map[i] = static_cast<uint8_t>(i * 7);
    MemoryWriter writer;
    auto out = memory_writer_stream(writer);
    CHECK(write_pcx_image(out, pixels.data(), width, height, color_map) == PcxStatus::ok);
    const auto& bytes = writer.bytes;
    CHECK(bytes.size() > static_cast<std::size_t>(pcx_header_size + pcx_color_map_size));
    CHECK(bytes[0] == 0x0A && bytes[1] == 5 && bytes[2] == 1 && bytes[3] == 8);
    CHECK(
        bytes[8] == width - 1 && bytes[10] == height - 1 && bytes[12] == width &&
        bytes[14] == height
    );
    CHECK(bytes[65] == 1 && bytes[66] == width);
    CHECK(std::memcmp(bytes.data() + 16, color_map, 48) == 0);
    CHECK(bytes[bytes.size() - pcx_color_map_size - 1] == pcx_palette_marker);

    MemoryReader reader{bytes};
    auto in = memory_reader_stream(reader);
    PcxImage image;
    CHECK(load_pcx_image(in, image) == PcxStatus::ok);
    CHECK(image.width == width && image.height == height);
    CHECK(image.pixels == pixels);
    CHECK(std::memcmp(image.color_map, color_map, sizeof color_map) == 0);

    MemoryReader surface_reader{bytes};
    auto surface_stream = memory_reader_stream(surface_reader);
    SurfaceBuffer surface;
    Palette palette{};
    CHECK(load_pcx_surface(surface_stream, surface, &palette) == PcxStatus::ok);
    CHECK(
        surface.surface.width == width && surface.surface.height == height &&
        surface.pixels == pixels
    );
    CHECK(
        palette.entries[2].r == 42 && palette.entries[2].g == 49 && palette.entries[2].b == 56 &&
        palette.entries[2].flags == 0
    );

    MemoryReader palette_reader{bytes};
    auto palette_stream = memory_reader_stream(palette_reader);
    Palette only{};
    CHECK(load_pcx_palette(palette_stream, only) == PcxStatus::ok);
    CHECK(std::memcmp(&only, &palette, sizeof only) == 0);

    DisplayContext display{};
    display.palette = palette;
    SurfaceBuffer saved = create_surface(width, height);
    saved.pixels = pixels;
    saved.surface.pixels = saved.pixels.data();
    MemoryWriter again;
    auto again_stream = memory_writer_stream(again);
    CHECK(save_pcx_surface(again_stream, display, saved.surface) == PcxStatus::ok);
    CHECK(again.bytes == bytes);
}

void test_rejects() {
    std::vector<uint8_t> header(pcx_header_size + pcx_color_map_size, 0);
    PcxImage image;
    {
        MemoryReader reader{std::span(header).first(10)};
        auto stream = memory_reader_stream(reader);
        CHECK(load_pcx_image(stream, image) == PcxStatus::short_header);
    }
    {
        MemoryReader reader{header};
        auto stream = memory_reader_stream(reader);
        CHECK(load_pcx_image(stream, image) == PcxStatus::bad_signature);
    }
    header[0] = pcx_manufacturer;
    header[1] = pcx_version;
    header[4] = 5; // x_min > x_max
    {
        MemoryReader reader{header};
        auto stream = memory_reader_stream(reader);
        CHECK(load_pcx_image(stream, image) == PcxStatus::bad_bounds);
    }
    header[4] = 0;
    header[8] = 0xFF;
    header[9] = 0xFF;
    header[10] = 0xFF;
    header[11] = 0xFF;
    {
        MemoryReader reader{header};
        auto stream = memory_reader_stream(reader);
        CHECK(load_pcx_image(stream, image) == PcxStatus::bad_bounds);
    }
    header[8] = header[9] = header[10] = header[11] = 3;
    header.resize(pcx_header_size + 10);
    {
        MemoryReader reader{header};
        auto stream = memory_reader_stream(reader);
        CHECK(load_pcx_image(stream, image) == PcxStatus::short_color_map);
    }
    header.resize(pcx_header_size + pcx_color_map_size, 0);
    header[9] = header[11] = 0;
    header[8] = header[10] = 40; // 41x41 pixels, but only the color map follows
    {
        MemoryReader reader{header};
        auto stream = memory_reader_stream(reader);
        CHECK(load_pcx_image(stream, image) == PcxStatus::truncated_pixels);
    }
}

void test_bmp_writer() {
    DisplayContext display{};
    display.palette.entries[1] = PaletteEntry{10, 20, 30, 0};
    SurfaceBuffer surface = create_surface(5, 4);
    for (std::size_t i = 0; i < surface.pixels.size(); ++i)
        surface.pixels[i] = static_cast<uint8_t>(i);
    // Pitch 8 so the 4-byte-aligned row (8 bytes) stays inside each row.
    std::vector<uint8_t> padded(8 * 4, 0);
    for (int y = 0; y < 4; ++y)
        std::memcpy(padded.data() + y * 8, surface.pixels.data() + y * 5, 5);
    Surface view = surface.surface;
    view.pitch = 8;
    view.pixels = padded.data();

    MemoryWriter writer;
    auto stream = memory_writer_stream(writer);
    BmpStripWriter bmp;
    bmp_strip_writer_init(bmp);
    CHECK(bmp.stream == nullptr);
    CHECK(!bmp_strip_writer_begin(bmp, nullptr, display, 5, 4));
    CHECK(bmp_strip_writer_begin(bmp, &stream, display, 5, 4));
    CHECK(bmp.data_offset == bmp_pixel_offset);
    CHECK(
        writer.bytes[0] == 'B' && writer.bytes[1] == 'M' && writer.bytes[10] == 0x36 &&
        writer.bytes[11] == 4
    );
    CHECK(
        writer.bytes[14] == 40 && writer.bytes[18] == 5 && writer.bytes[22] == 4 &&
        writer.bytes[28] == 8
    );
    const std::size_t quad = bmp_file_header_size + bmp_info_header_size + 4;
    CHECK(writer.bytes[quad] == 30 && writer.bytes[quad + 1] == 20 && writer.bytes[quad + 2] == 10);
    // Two strips: image rows 2..3 first, then 0..1.
    CHECK(bmp_strip_writer_write_rows(bmp, view, 2, 2, 2));
    CHECK(bmp_strip_writer_write_rows(bmp, view, 2, 0, 0));
    CHECK(writer.bytes.size() == static_cast<std::size_t>(bmp_pixel_offset + 8 * 4));
    for (int file_row = 0; file_row < 4; ++file_row) {
        const int image_row = 3 - file_row;
        CHECK(
            std::memcmp(
                writer.bytes.data() + bmp_pixel_offset + file_row * 8,
                padded.data() + image_row * 8,
                8
            ) == 0
        );
    }
    int closed = 0;
    stream.close = [](void* user) { ++*static_cast<int*>(user); };
    stream.user = &closed;
    bmp_strip_writer_close(bmp);
    CHECK(closed == 1 && bmp.stream == &stream);
}

// Every PCX the installed game provides, loose or archived.
void test_installed_sweep(const oa::AssetStore& assets) {
    int loaded = 0;
    int rejected = 0;
    for (const auto& name : assets.list_effective_recursive("", ".pcx")) {
        const auto bytes = oa::test::read_game_file(assets, name);
        MemoryReader reader{bytes};
        auto stream = memory_reader_stream(reader);
        PcxImage image;
        const PcxStatus status = load_pcx_image(stream, image);
        if (status != PcxStatus::ok) {
            ++rejected;
            CHECK(status == PcxStatus::bad_signature);
            continue;
        }
        ++loaded;
        // Every installed image ends exactly where its 0x0C marker and color map start.
        const bool clean_end = reader.position == bytes.size() - pcx_color_map_size - 1;
        if (!clean_end)
            std::fprintf(
                stderr,
                "%s: pixel data ends at %zu of %zu\n",
                name.c_str(),
                reader.position,
                bytes.size()
            );
        CHECK(clean_end);
        CHECK(bytes[bytes.size() - pcx_color_map_size - 1] == pcx_palette_marker);

        MemoryWriter writer;
        auto out = memory_writer_stream(writer);
        CHECK(
            write_pcx_image(out, image.pixels.data(), image.width, image.height, image.color_map) ==
            PcxStatus::ok
        );
        MemoryReader again{writer.bytes};
        auto in = memory_reader_stream(again);
        PcxImage copy;
        CHECK(load_pcx_image(in, copy) == PcxStatus::ok);
        CHECK(
            copy.pixels == image.pixels &&
            std::memcmp(copy.color_map, image.color_map, pcx_color_map_size) == 0
        );
    }
    std::printf(
        "installed PCX sweep: %d decoded and re-encoded, %d rejected by signature\n",
        loaded,
        rejected
    );
    CHECK(loaded > 0);
}

// The numbered writer takes the highest SHOTnnnn already listed, whatever
// the case of the extension, and writes one past it.
struct NumberedFiles {
    std::vector<std::string> listed;
    std::string pattern;
    std::string opened;
    MemoryWriter writer;
};

void test_numbered_pcx() {
    NumberedFiles files;
    files.listed = {"SHOT0003.pcx", "SHOT0012.PCX", "SHOTabc.pcx", "SHO.pcx", "SHOT0009.pcx"};
    NumberedFileHost host{};
    host.context = &files;
    host.list =
        [](void* context, const char* pattern, void (*visit)(void*, const char*), void* user) {
            auto& state = *static_cast<NumberedFiles*>(context);
            state.pattern = pattern;
            for (const auto& name : state.listed)
                visit(user, name.c_str());
        };
    host.open = [](void* context, const char* path, ByteStream* stream) {
        auto& state = *static_cast<NumberedFiles*>(context);
        state.opened = path;
        *stream = memory_writer_stream(state.writer);
        return true;
    };
    DisplayContext display{};
    for (int i = 0; i < OA_PALETTE_COLORS; ++i)
        display.palette.entries[i] = PaletteEntry{static_cast<uint8_t>(i), 1, 2, 0};
    SurfaceBuffer frame = create_surface(4, 3);
    for (std::size_t i = 0; i < frame.pixels.size(); ++i)
        frame.pixels[i] = static_cast<uint8_t>(i * 17);
    display.active_surface = &frame.surface;

    CHECK(!save_numbered_pcx(display, "OUT\\screenshots", "SHOT", host));
    CHECK(files.opened.empty() && files.pattern.empty());

    display.use_active_surface = 1;
    CHECK(save_numbered_pcx(display, "OUT\\screenshots", "SHOT", host));
    CHECK(files.pattern == "OUT\\screenshots\\SHOT*.pcx");
    CHECK(files.opened == "OUT\\screenshots\\SHOT0013.pcx");
    MemoryWriter expected;
    auto expected_stream = memory_writer_stream(expected);
    CHECK(save_pcx_surface(expected_stream, display, frame.surface) == PcxStatus::ok);
    CHECK(files.writer.bytes == expected.bytes);

    files.listed.clear();
    CHECK(save_numbered_pcx(display, "MOVIE001\\", "FRAM", host));
    CHECK(files.opened == "MOVIE001\\FRAM0001.pcx");
    CHECK(save_numbered_pcx(display, "", "FRAM", host));
    CHECK(files.opened == "FRAM0001.pcx");
}

} // namespace

int main(int argc, char** argv) {
    if (oa::test::game_data_requested(argc, argv)) {
        test_installed_sweep(oa::test::require_game_assets("the installed PCX sweep"));
    } else {
        test_encode_run();
        test_encode_row();
        test_decode_row();
        test_round_trip();
        test_rejects();
        test_bmp_writer();
        test_numbered_pcx();
    }
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return 1;
    }
    std::puts("pcx tests passed");
    return 0;
}
