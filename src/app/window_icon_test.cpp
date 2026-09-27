// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The embedded window icon decodes to 256 by 256 RGBA pixels, transparent
// in the corners and opaque in the middle, and files that are not such an
// image are refused with a reason.
#include "oa/app/window_icon.hpp"
#include "oa/formats/png.hpp"

#include <cstdint>
#include <cstdio>
#include <string>
#include <vector>

namespace {

int failures = 0;

void check(bool condition, const char* expression, const char* file, int line) {
    if (!condition) {
        std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
        ++failures;
    }
}

#define CHECK(condition) check((condition), #condition, __FILE__, __LINE__)

using namespace oa::app;

// Offset of the alpha byte in a pixel.
constexpr uint32_t alpha_byte = 3;

uint8_t alpha_at(const WindowIcon& icon, uint32_t x, uint32_t y) {
    return icon.pixels
        [(static_cast<std::size_t>(y) * icon.width + x) * window_icon_pixel_bytes + alpha_byte];
}

std::vector<uint8_t> encoded(oa::formats::png::ColorType color_type, uint32_t size) {
    oa::formats::png::Header header;
    header.width = size;
    header.height = size;
    header.bit_depth = 8;
    header.color_type = color_type;
    const std::vector<uint8_t> rows(oa::formats::png::row_bytes(header, {}) * size, 0x80);
    std::vector<uint8_t> file;
    CHECK(oa::formats::png::write(oa::formats::png::Image{header, {}, rows}, &file));
    return file;
}

void test_embedded_icon() {
    WindowIcon icon;
    std::string error;
    CHECK(decode_window_icon(window_icon_png(), icon, error));
    CHECK(error.empty());
    CHECK(icon.width == window_icon_size && icon.height == window_icon_size);
    CHECK(
        icon.pixels.size() ==
        std::size_t{window_icon_size} * window_icon_size * window_icon_pixel_bytes
    );
    if (icon.pixels.size() !=
        std::size_t{window_icon_size} * window_icon_size * window_icon_pixel_bytes)
        return;
    const uint32_t last = window_icon_size - 1;
    CHECK(alpha_at(icon, 0, 0) == 0 && alpha_at(icon, last, 0) == 0);
    CHECK(alpha_at(icon, 0, last) == 0 && alpha_at(icon, last, last) == 0);
    CHECK(alpha_at(icon, window_icon_size / 2, window_icon_size / 2) == 0xff);
}

void test_refused_files() {
    WindowIcon icon;
    std::string error;
    const std::vector<uint8_t> not_png = {'n', 'o', 't', ' ', 'a', ' ', 'p', 'n', 'g'};
    CHECK(
        !decode_window_icon(not_png, icon, error) && error == "not a PNG file" &&
        icon.pixels.empty()
    );

    const auto embedded = window_icon_png();
    const std::vector<uint8_t> cut(embedded.begin(), embedded.begin() + embedded.size() / 2);
    CHECK(!decode_window_icon(cut, icon, error) && !error.empty() && icon.pixels.empty());

    const auto rgb = encoded(oa::formats::png::ColorType::rgb, window_icon_size);
    CHECK(!decode_window_icon(rgb, icon, error) && error == "not an 8-bit RGBA image");

    const auto small = encoded(oa::formats::png::ColorType::rgb_alpha, 16);
    CHECK(!decode_window_icon(small, icon, error) && error == "not 256 by 256 pixels");

    const auto square = encoded(oa::formats::png::ColorType::rgb_alpha, window_icon_size);
    CHECK(decode_window_icon(square, icon, error) && error.empty());
    CHECK(
        icon.pixels.size() ==
        std::size_t{window_icon_size} * window_icon_size * window_icon_pixel_bytes
    );
}

} // namespace

int main() {
    test_embedded_icon();
    test_refused_files();
    if (failures != 0) {
        std::fprintf(stderr, "window icon: %d check(s) failed\n", failures);
        return 1;
    }
    std::printf("window icon: the embedded icon decodes, and other files are refused\n");
    return 0;
}
