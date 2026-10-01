// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The conversion of RGB frames into the window's XRGB8888 pixels: each pixel
// packed as 0xffRRGGBB through the gamma table, and the same bytes on pools
// of every size.
#include "xrgb_conversion.hpp"

#include <array>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <random>
#include <utility>
#include <vector>

namespace {

int failures = 0;

void check_at(bool condition, const char* expression, const char* file, int line) {
    if (condition)
        return;
    std::fprintf(stderr, "%s:%d: check failed: %s\n", file, line, expression);
    ++failures;
}

#define CHECK(condition) check_at((condition), #condition, __FILE__, __LINE__)

namespace job_pool = oa::platform::job_pool;

/// Thread counts the conversion runs on besides the calling thread alone.
constexpr uint32_t pool_sizes[] = {2, 3, 4, 8};
/// Seed of the test's frames.
constexpr uint32_t test_seed = 1001;
/// Bytes past each converted row that the conversion must leave alone.
constexpr std::size_t row_slack = 12;
/// Value of the bytes the conversion must leave alone.
constexpr uint8_t untouched = 0x5a;

/// Converts a frame into rows `row_slack` bytes wider than its pixels.
std::vector<uint8_t> convert(
    const std::vector<uint8_t>& rgb,
    uint32_t width,
    uint32_t height,
    const std::array<uint8_t, 256>* gamma,
    job_pool::Pool* pool
) {
    const std::size_t pitch = static_cast<std::size_t>(width) * 4U + row_slack;
    std::vector<uint8_t> pixels(pitch * height, untouched);
    oa::app::convert_rgb24_xrgb(rgb.data(), width, height, pixels.data(), pitch, gamma, pool);
    return pixels;
}

void conversion_packs_each_pixel() {
    std::mt19937 random(test_seed);
    std::array<uint8_t, 256> gamma{};
    for (std::size_t level = 0; level < gamma.size(); ++level)
        gamma[level] = static_cast<uint8_t>(255U - level / 2U);
    // Widths that end on and off the four-pixel step.
    for (const uint32_t width : {1U, 3U, 4U, 7U, 640U, 1001U}) {
        const uint32_t height = 77;
        std::vector<uint8_t> rgb(static_cast<std::size_t>(width) * height * 3U);
        for (auto& byte : rgb)
            byte = static_cast<uint8_t>(random());
        for (const bool corrected : {false, true}) {
            const auto pixels = convert(rgb, width, height, corrected ? &gamma : nullptr, nullptr);
            const std::size_t pitch = static_cast<std::size_t>(width) * 4U + row_slack;
            bool all = true;
            for (uint32_t y = 0; y < height; ++y) {
                for (uint32_t x = 0; x < width; ++x) {
                    const uint8_t* source = &rgb[(static_cast<std::size_t>(y) * width + x) * 3U];
                    const auto channel = [&](int index) -> uint32_t {
                        return corrected ? gamma[source[index]] : source[index];
                    };
                    const uint32_t expected =
                        0xff000000u | (channel(0) << 16) | (channel(1) << 8) | channel(2);
                    uint32_t pixel = 0;
                    std::memcpy(&pixel, &pixels[y * pitch + x * 4U], 4);
                    all = all && pixel == expected;
                }
                for (std::size_t spare = width * 4U; spare < pitch; ++spare)
                    all = all && pixels[y * pitch + spare] == untouched;
            }
            CHECK(all);
        }
    }
}

void conversion_is_the_same_on_every_pool() {
    std::mt19937 random(test_seed + 1);
    std::array<uint8_t, 256> gamma{};
    for (std::size_t level = 0; level < gamma.size(); ++level)
        gamma[level] = static_cast<uint8_t>((level * level) / 255U);
    std::vector<std::unique_ptr<job_pool::Pool>> pools;
    for (const uint32_t threads : pool_sizes)
        pools.push_back(std::make_unique<job_pool::Pool>(threads));
    // Frames of one band, of a part band and of many.
    for (const auto [width, height] : {std::pair{640U, 32U}, {1664U, 952U}, {333U, 101U}}) {
        std::vector<uint8_t> rgb(static_cast<std::size_t>(width) * height * 3U);
        for (auto& byte : rgb)
            byte = static_cast<uint8_t>(random());
        const std::array<uint8_t, 256>* const tables[] = {nullptr, &gamma};
        for (const auto* table : tables) {
            const auto alone = convert(rgb, width, height, table, nullptr);
            for (const auto& pool : pools)
                CHECK(convert(rgb, width, height, table, pool.get()) == alone);
        }
    }
}

} // namespace

int main() {
    conversion_packs_each_pixel();
    conversion_is_the_same_on_every_pool();
    if (failures != 0) {
        std::fprintf(stderr, "%d check(s) failed\n", failures);
        return EXIT_FAILURE;
    }
    std::puts("xrgb conversion: every pixel packed alike on every pool");
    return EXIT_SUCCESS;
}
