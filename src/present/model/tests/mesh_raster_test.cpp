// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#include "oa/present/model/mesh_raster.hpp"
#include "oa/present/display.hpp"
#include "oa/present/palette_tables.hpp"
#include "oa/present/surface.hpp"

#include <cstdio>
#include <cstdlib>
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

using namespace oa::present::model;
using oa::Sprite;
using oa::Surface;
using oa::present::DepthVertex;
using oa::present::PolygonVertex;
using oa::present::ShadedVertex;

// A raw texture whose texel is (x + 16 * y) & 0xff, padded for the samplers.
struct Texture {
    Sprite sprite{};
    std::vector<uint8_t> texels;
};

Texture make_texture(int width, int height) {
    Texture texture;
    texture.texels.assign(0x10000 + static_cast<std::size_t>(width * height), 0);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            texture.texels[static_cast<std::size_t>(y * width + x)] =
                static_cast<uint8_t>((x + 16 * y) & 0xff);
    texture.sprite.width = static_cast<uint16_t>(width);
    texture.sprite.height = static_cast<uint16_t>(height);
    texture.sprite.data = texture.texels.data();
    return texture;
}

struct Target {
    Sprite sprite{};
    std::vector<uint8_t> pixels;
    std::vector<uint8_t> depth;
};

Target make_target(int width, int height, bool with_depth, uint8_t fill) {
    Target target;
    target.pixels.assign(static_cast<std::size_t>(width * height), fill);
    target.sprite.width = static_cast<uint16_t>(width);
    target.sprite.height = static_cast<uint16_t>(height);
    target.sprite.data = target.pixels.data();
    if (with_depth) {
        target.depth.assign(target.pixels.size(), 0);
        target.sprite.aux = target.depth.data();
    }
    return target;
}

uint8_t at(const Target& t, int x, int y) {
    return t.pixels[static_cast<std::size_t>(y * t.sprite.width + x)];
}

uint8_t depth_at(const Target& t, int x, int y) {
    return t.depth[static_cast<std::size_t>(y * t.sprite.width + x)];
}

// An axis-aligned quad over the texture's corners maps texels one to one;
// the last row and column of the quad stay unfilled.
void test_depth_quad_maps_texels() {
    for (const int width : {16, 32, 64, 20}) {
        Texture texture = make_texture(width, 16);
        Target target = make_target(80, 24, true, 0xee);
        const int right = width - 1;
        const DepthVertex quad[] = {{0, 0, 7}, {right, 0, 7}, {right, 15, 7}, {0, 15, 7}};
        texture_depth_quad(&target.sprite, &texture.sprite, quad, nullptr);
        bool mapped = true;
        for (int y = 0; y < 15; ++y)
            for (int x = 0; x < right; ++x)
                mapped = mapped && at(target, x, y) == static_cast<uint8_t>((x + 16 * y) & 0xff) &&
                         depth_at(target, x, y) == 7;
        CHECK(mapped);
        CHECK(at(target, right, 3) == 0xee);
        CHECK(at(target, 3, 15) == 0xee);
    }
}

// Texels land only where the stored depth is <= the span depth.
void test_depth_quad_depth_test() {
    Texture texture = make_texture(16, 16);
    Target target = make_target(20, 20, true, 0xee);
    target.depth[static_cast<std::size_t>(2 * 20 + 3)] = 9;
    target.depth[static_cast<std::size_t>(4 * 20 + 5)] = 8;
    const DepthVertex quad[] = {{0, 0, 8}, {15, 0, 8}, {15, 15, 8}, {0, 15, 8}};
    texture_depth_quad(&target.sprite, &texture.sprite, quad, nullptr);
    CHECK(at(target, 3, 2) == 0xee);
    CHECK(depth_at(target, 3, 2) == 9);
    CHECK(at(target, 5, 4) == static_cast<uint8_t>(5 + 16 * 4));
    CHECK(at(target, 6, 6) == static_cast<uint8_t>(6 + 16 * 6));
}

// Without a depth plane every texel lands, and a quad partly off the
// sprite is clipped to it.
void test_depth_quad_without_depth_clips() {
    Texture texture = make_texture(16, 16);
    Target target = make_target(10, 10, false, 0xee);
    const DepthVertex quad[] = {{-5, -5, 0}, {10, -5, 0}, {10, 10, 0}, {-5, 10, 0}};
    texture_depth_quad(&target.sprite, &texture.sprite, quad, nullptr);
    CHECK(at(target, 0, 0) == static_cast<uint8_t>(5 + 16 * 5));
    CHECK(at(target, 8, 8) == static_cast<uint8_t>(13 + 16 * 13));
    CHECK(at(target, 9, 4) == 0xee);
    // Entirely outside: nothing drawn.
    Target clean = make_target(10, 10, false, 0xee);
    const DepthVertex away[] = {{20, 20, 0}, {30, 20, 0}, {30, 30, 0}, {20, 30, 0}};
    texture_depth_quad(&clean.sprite, &texture.sprite, away, nullptr);
    bool untouched = true;
    for (const uint8_t p : clean.pixels)
        untouched = untouched && p == 0xee;
    CHECK(untouched);
}

// Explicit texture coordinates select the sampled rectangle.
void test_depth_quad_uv() {
    Texture texture = make_texture(16, 16);
    Target target = make_target(20, 20, true, 0xee);
    const DepthVertex quad[] = {{0, 0, 1}, {4, 0, 1}, {4, 4, 1}, {0, 4, 1}};
    const TexturePoint uv[] = {{8, 8}, {12, 8}, {12, 12}, {8, 12}};
    texture_depth_quad(&target.sprite, &texture.sprite, quad, uv);
    CHECK(at(target, 0, 0) == static_cast<uint8_t>(8 + 16 * 8));
    CHECK(at(target, 3, 2) == static_cast<uint8_t>(11 + 16 * 10));
}

// The shaded quad remaps each texel through its shade-table row.
void test_shaded_quad() {
    std::vector<uint8_t> shade(oa::present::shade_table_size);
    for (int row = 0; row < 32; ++row)
        for (int value = 0; value < 256; ++value)
            shade[static_cast<std::size_t>(row * 256 + value)] = static_cast<uint8_t>(row);
    oa::present::DisplayContext display{};
    display.shade_table = shade.data();
    oa::present::DisplayContext* previous = oa::present::display_context();
    oa::present::bind_display(&display);
    Texture texture = make_texture(16, 16);
    Target target = make_target(20, 20, true, 0xee);
    const ShadedVertex quad[] = {{0, 0, 5, 9}, {15, 0, 5, 9}, {15, 15, 5, 9}, {0, 15, 5, 9}};
    texture_shaded_depth_quad(&target.sprite, &texture.sprite, quad, nullptr);
    CHECK(at(target, 4, 4) == 9);
    CHECK(depth_at(target, 4, 4) == 5);
    oa::present::bind_display(previous);
}

// The surface projector clips to the clip rectangle and leaves its last row
// and column unfilled.
void test_surface_quad() {
    Texture texture = make_texture(32, 32);
    auto surface = oa::present::create_surface(40, 40);
    for (auto& p : surface.pixels)
        p = 0xee;
    oa::present::set_surface_clip(surface.surface, {4, 4, 20, 20});
    const PolygonVertex quad[] = {{0, 0}, {31, 0}, {31, 31}, {0, 31}};
    texture_quad(&surface.surface, &texture.sprite, quad, nullptr);
    const auto pixel = [&](int x, int y) {
        return surface.pixels[static_cast<std::size_t>(y * 40 + x)];
    };
    CHECK(pixel(3, 10) == 0xee);
    CHECK(pixel(10, 3) == 0xee);
    CHECK(pixel(4, 4) == static_cast<uint8_t>((4 + 16 * 4) & 0xff));
    CHECK(pixel(19, 19) == static_cast<uint8_t>((19 + 16 * 19) & 0xff));
    CHECK(pixel(20, 10) == 0xee);
    CHECK(pixel(10, 20) == 0xee);
}

// The span sampler clips a row to the sprite, stepping the texture start.
void test_span_clip() {
    Texture texture = make_texture(16, 16);
    Target target = make_target(8, 4, true, 0xee);
    MeshSpanRow row{};
    row.left = -4;
    row.right = 12;
    row.u_left = 0;
    row.u_right = 16 << 16;
    row.v_left = 2 << 16;
    row.v_right = 2 << 16;
    row.depth_left = 3 << 16;
    row.depth_right = 3 << 16;
    sample_mesh_span(1, row, target.sprite, texture.sprite);
    CHECK(row.left == 0);
    CHECK(row.right == 7);
    CHECK(at(target, 0, 1) == static_cast<uint8_t>(4 + 16 * 2));
    CHECK(at(target, 6, 1) == static_cast<uint8_t>(10 + 16 * 2));
    CHECK(at(target, 7, 1) == 0xee);
}

// A seeded run of textured quads (convex and twisted, past the clip and the
// surface, textures of every sampled width) drawn on a whole surface and
// again band by band, each band drawing the whole run with its rows alone
// (surface_band), leaves the same pixels for every number of bands.
void test_surface_quad_bands() {
    constexpr int size = 72;
    uint32_t state = 0x9E3779B9u;
    const auto next = [&state](int bound) {
        state = state * 1664525U + 1013904223U;
        return static_cast<int>((state >> 8) % static_cast<uint32_t>(bound));
    };
    const Texture textures[] = {
        make_texture(16, 16),
        make_texture(32, 8),
        make_texture(64, 64),
        make_texture(128, 4),
        make_texture(24, 24)
    };

    struct Quad {
        alignas(4) oa::Rect32 clip{}; ///< packed, so aligned here
        PolygonVertex corners[4]{};
        int texture{};
    };

    std::vector<Quad> quads(300);
    for (Quad& quad : quads) {
        const int x1 = next(size);
        const int y1 = next(size);
        quad.clip = {x1, y1, x1 + next(size - x1), y1 + next(size - y1)};
        if (next(3) == 0)
            quad.clip = {0, 0, size - 1, size - 1};
        for (PolygonVertex& corner : quad.corners)
            corner = {next(size + 40) - 20, next(size + 40) - 20};
        quad.texture = next(5);
    }
    const auto draw = [&](Surface& surface) {
        for (const Quad& quad : quads) {
            oa::present::set_surface_clip(surface, quad.clip);
            texture_quad(&surface, &textures[quad.texture].sprite, quad.corners, nullptr);
        }
    };
    auto whole = oa::present::create_surface(size, size);
    draw(whole.surface);
    for (const int count : {2, 3, 5, 7}) {
        auto banded = oa::present::create_surface(size, size);
        for (int band = 0; band < count; ++band) {
            oa::present::set_surface_band(
                banded.surface, band * size / count, (band + 1) * size / count
            );
            draw(banded.surface);
        }
        CHECK(banded.pixels == whole.pixels);
    }
}

} // namespace

int main() {
    test_depth_quad_maps_texels();
    test_depth_quad_depth_test();
    test_depth_quad_without_depth_clips();
    test_depth_quad_uv();
    test_shaded_quad();
    test_surface_quad();
    test_span_clip();
    test_surface_quad_bands();
    if (failures != 0) {
        std::fprintf(stderr, "%d failure(s)\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("mesh raster tests passed");
    return EXIT_SUCCESS;
}
