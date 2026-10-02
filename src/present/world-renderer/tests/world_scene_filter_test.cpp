// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The area pass (scene_filter.hpp): its weights are exact and sum to one;
// its bytes equal a straightforward implementation that intersects every
// footprint with every scene pixel, and stay within half a level, plus the
// row averages' rounding, of the exact average in double precision; at a
// scale of one half it is the 2x2 box (a + b + c + d + 2) / 4, and at one it
// copies the scene; a line one scene pixel wide keeps its intensity wherever
// it falls; any split of the rows and pools of 1 to 8 threads give the same
// bytes, and a band writes only its own rows; malformed sizes are refused
// with nothing written, and rebuilding a plan no larger allocates nothing. A
// pinned FNV-1a digest of seeded scenes holds the bytes on every platform and
// build type: it moves only with a deliberate change to the pass, said in
// the commit that moves it. The time of a 1080p battlefield filtered from a
// scene twice its size, at one half and at two scales whose footprints cover
// three scene pixels, is logged, for information only.
#include "oa/platform/job_pool.hpp"
#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <type_traits>
#include <utility>
#include <vector>

namespace wr = oa::present::world_renderer;
namespace job_pool = oa::platform::job_pool;

namespace {

/// FNV-1a, 32 bits: the start of the hash, and the prime each byte multiplies by.
constexpr uint32_t fnv_offset_basis = 2166136261U;
constexpr uint32_t fnv_prime = 16777619U;

/// Seeds of the test's scenes.
constexpr uint32_t seed_slow = 0x6A09E667U;
constexpr uint32_t seed_reference = 0xBB67AE85U;
constexpr uint32_t seed_bands = 0x3C6EF372U;
constexpr uint32_t seed_digest = 0xA54FF53AU;
constexpr uint32_t seed_timing = 0x510E527FU;

/// Returns numerator / denominator in 16.16, rounded to the nearest.
///
/// @param numerator the fraction's numerator, at most 65535
/// @param denominator the fraction's denominator, above 0
/// @return the fraction in 16.16
constexpr uint32_t fixed_scale(uint32_t numerator, uint32_t denominator) {
    return (numerator * wr::area_fixed_one + denominator / 2U) / denominator;
}

/// Scales of the tests, 16.16: the ends of the range, the scales just inside
/// them and either side of two thirds, and simple fractions between.
constexpr uint32_t scale_half = wr::area_scale_min;
constexpr uint32_t scale_just_above_half = wr::area_scale_min + 1U;
/// A little above one half, at no simple fraction.
constexpr uint32_t scale_near_half = 33000;
constexpr uint32_t scale_eleven_twentieths = fixed_scale(11, 20);
constexpr uint32_t scale_three_fifths = fixed_scale(3, 5);
constexpr uint32_t scale_thirteen_twentieths = fixed_scale(13, 20);
constexpr uint32_t scale_two_thirds = fixed_scale(2, 3);
constexpr uint32_t scale_just_below_two_thirds = scale_two_thirds - 1U;
constexpr uint32_t scale_three_quarters = fixed_scale(3, 4);
constexpr uint32_t scale_nine_tenths = fixed_scale(9, 10);
constexpr uint32_t scale_just_below_one = wr::area_scale_max - 1U;
constexpr uint32_t scale_one = wr::area_scale_max;

/// Steps of the sweeps across the range of scales: primes, so the scales
/// swept fall at no regular fraction.
constexpr uint32_t weights_sweep_step = 211;
constexpr uint32_t slow_sweep_step = 997;

/// The pass's rounding, as its header gives it: 16.16 weights, row averages
/// kept 8 bits above a channel's, rounded halves up, then the sum of 16.16
/// weights times row averages rounded to the channel's 8 bits, halves up.
constexpr uint32_t weight_bits = 16;
constexpr uint32_t row_average_bits = 8;
constexpr uint64_t row_average_half = uint64_t{1} << (weight_bits - row_average_bits - 1U);
constexpr uint32_t result_bits = weight_bits + row_average_bits;
constexpr uint64_t result_half = uint64_t{1} << (result_bits - 1U);
static_assert(uint64_t{1} << weight_bits == wr::area_fixed_one);

/// The scales the thin line is followed at: 0.5, 0.55, 0.6, 0.65, 0.75 and 0.9.
constexpr uint32_t line_scales[] = {
    scale_half,
    scale_eleven_twentieths,
    scale_three_fifths,
    scale_thirteen_twentieths,
    scale_three_quarters,
    scale_nine_tenths,
};

/// Scales the digest is pinned at: one half, 0.6, two thirds, 0.75 and 0.9.
constexpr uint32_t digest_scales[] = {
    scale_half, scale_three_fifths, scale_two_thirds, scale_three_quarters, scale_nine_tenths
};

/// Scales the time is logged at: one half, whose footprints cover two scene
/// pixels a side, and two whose footprints cover up to three.
constexpr uint32_t timing_scales[] = {scale_half, scale_just_above_half, scale_two_thirds};

/// The picture the digest is pinned on, in pixels.
constexpr uint32_t digest_width = 333;
constexpr uint32_t digest_height = 177;

/// FNV-1a of the pictures at digest_scales, one after another.
constexpr uint32_t pinned_digest = 0x26B537ACU;

/// The battlefield of a 1920x1080 window, and the scene drawn at twice its
/// size with two spare columns and rows, rounded to even.
constexpr uint32_t timing_width = 1664;
constexpr uint32_t timing_height = 952;
constexpr uint32_t timing_scene_width = 3330;
constexpr uint32_t timing_scene_height = 1906;
/// Runs the time is the quickest of, and the threads of the pool timed.
constexpr int timing_runs = 5;
constexpr uint32_t timing_threads = 4;

/// Most a filtered level may differ from the exact average: half a level
/// for the last rounding, and half of 1/256 of a level for the row
/// averages', with room for the reference's own rounding.
constexpr double reference_tolerance = 0.5 + 1.0 / 256.0;

/// Byte a picture is filled with before a refused frame, to show it untouched.
constexpr uint8_t untouched_fill = 0xA5;

/// A 32-bit xorshift sequence, the same on every platform.
struct Random {
    uint32_t state{};

    /// Returns the next number of the sequence.
    ///
    /// @return 32 bits
    uint32_t next() {
        state ^= state << 13;
        state ^= state >> 17;
        state ^= state << 5;
        return state;
    }
};

/// RGB pixels the test owns, in rows stride pixels apart.
struct Picture {
    uint32_t width{};
    uint32_t height{};
    uint32_t stride{};
    std::vector<uint8_t> rgb;

    /// Returns the pixels as a scene the pass reads.
    ///
    /// @return a read-only view of the pixels
    [[nodiscard]] wr::RgbSource source() const { return {rgb.data(), width, height, stride}; }

    /// Returns the pixels as a picture the pass writes.
    ///
    /// @return a writable view of the pixels
    [[nodiscard]] wr::RgbTarget target() { return {rgb.data(), width, height, stride}; }

    /// Returns one channel of one pixel.
    ///
    /// @param x column
    /// @param y row
    /// @param channel 0 for red, 1 for green, 2 for blue
    /// @return the level
    [[nodiscard]] uint8_t at(uint32_t x, uint32_t y, uint32_t channel) const {
        return rgb[(static_cast<std::size_t>(y) * stride + x) * wr::area_pixel_bytes + channel];
    }
};

/// Returns a picture of `width` x `height` pixels in rows `stride` apart, every byte `fill`.
///
/// @param width pixels a row
/// @param height rows
/// @param stride pixels between row starts, at least width
/// @param fill every byte, padding included
/// @return the picture
Picture blank(uint32_t width, uint32_t height, uint32_t stride, uint8_t fill) {
    Picture picture{width, height, stride, {}};
    picture.rgb.assign(static_cast<std::size_t>(stride) * height * wr::area_pixel_bytes, fill);
    return picture;
}

/// Returns a picture of random bytes, padding included.
///
/// @param random the sequence the bytes are drawn from
/// @param width pixels a row
/// @param height rows
/// @param stride pixels between row starts, at least width
/// @return the picture
Picture random_scene(Random& random, uint32_t width, uint32_t height, uint32_t stride) {
    Picture picture = blank(width, height, stride, 0);
    for (auto& byte : picture.rgb)
        byte = static_cast<uint8_t>(random.next() >> 24);
    return picture;
}

/// Returns the length two half-open intervals share, 0 when they do not meet.
///
/// @param first_begin start of the first interval
/// @param first_end end of the first interval
/// @param second_begin start of the second interval
/// @param second_end end of the second interval
/// @return the shared length
uint64_t
overlap(uint64_t first_begin, uint64_t first_end, uint64_t second_begin, uint64_t second_end) {
    const uint64_t begin = std::max(first_begin, second_begin);
    const uint64_t end = std::min(first_end, second_end);
    return end > begin ? end - begin : 0;
}

/// Filters a picture the straightforward way: every scene pixel's share of
/// every footprint, from the footprint's and the pixel's bounds, rounded as
/// the pass rounds, with no plan.
///
/// @param scene the scene
/// @param scale screen pixels per scene pixel, 16.16
/// @param width picture columns
/// @param height picture rows
/// @return the picture, rows width pixels apart
Picture slow_area_filter(const Picture& scene, uint32_t scale, uint32_t width, uint32_t height) {
    Picture picture = blank(width, height, width, 0);
    for (uint32_t y = 0; y < height; ++y) {
        const uint64_t top = uint64_t{y} * wr::area_fixed_one;
        for (uint32_t x = 0; x < width; ++x) {
            const uint64_t left = uint64_t{x} * wr::area_fixed_one;
            for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel) {
                uint64_t total = 0;
                for (uint32_t row = 0; row < scene.height; ++row) {
                    const uint64_t row_weight = overlap(
                        top,
                        top + wr::area_fixed_one,
                        uint64_t{row} * scale,
                        uint64_t{row + 1U} * scale
                    );
                    if (row_weight == 0)
                        continue;
                    uint64_t row_sum = 0;
                    for (uint32_t column = 0; column < scene.width; ++column)
                        row_sum += overlap(
                                       left,
                                       left + wr::area_fixed_one,
                                       uint64_t{column} * scale,
                                       uint64_t{column + 1U} * scale
                                   ) *
                                   scene.at(column, row, channel);
                    total += row_weight * ((row_sum + row_average_half) >> row_average_bits);
                }
                picture.rgb
                    [(static_cast<std::size_t>(y) * width + x) * wr::area_pixel_bytes + channel] =
                    static_cast<uint8_t>((total + result_half) >> result_bits);
            }
        }
    }
    return picture;
}

/// Plans and filters a whole picture on the calling thread.
///
/// @param scene the scene
/// @param scale screen pixels per scene pixel, 16.16
/// @param width picture columns
/// @param height picture rows
/// @return the picture, rows width pixels apart
Picture area_filter(const Picture& scene, uint32_t scale, uint32_t width, uint32_t height) {
    wr::AreaPlan plan;
    OA_CHECK(wr::plan_area_filter(plan, scale, width, height) == wr::AreaError::none);
    Picture picture = blank(width, height, width, 0);
    OA_CHECK(wr::area_filter_rgb24(plan, scene.source(), picture.target()) == wr::AreaError::none);
    return picture;
}

/// Returns the FNV-1a hash of bytes, continuing from `digest`.
///
/// @param digest the hash so far
/// @param bytes the bytes to add
/// @return the hash with the bytes added
uint32_t fnv1a(uint32_t digest, const std::vector<uint8_t>& bytes) {
    for (const uint8_t byte : bytes) {
        digest ^= byte;
        digest *= fnv_prime;
    }
    return digest;
}

/// Checks that each footprint's weights sum to one, lie inside the scene,
/// and give each scene pixel, over all the footprints it falls in, its
/// length in screen pixels exactly.
void test_weights_are_exact() {
    for (uint32_t scale = wr::area_scale_min; scale <= wr::area_scale_max;
         scale += weights_sweep_step) {
        for (const uint32_t extent : {1U, 2U, 3U, 7U, 64U, 501U}) {
            wr::AreaPlan plan;
            OA_CHECK(wr::plan_area_filter(plan, scale, extent, extent + 1U) == wr::AreaError::none);
            const uint32_t scene_columns = wr::area_scene_extent(scale, extent);
            OA_CHECK(plan.scene_width() == scene_columns);
            OA_CHECK(plan.column_taps() >= 1 && plan.column_taps() <= wr::area_taps_max);
            std::vector<uint64_t> received(scene_columns, 0);
            for (const wr::AreaTap& tap : plan.columns()) {
                OA_CHECK(tap.first + plan.column_taps() <= scene_columns);
                uint64_t sum = 0;
                for (uint32_t entry = 0; entry < plan.column_taps(); ++entry) {
                    sum += tap.weights[entry];
                    if (tap.first + entry < scene_columns)
                        received[tap.first + entry] += tap.weights[entry];
                }
                for (uint32_t entry = plan.column_taps(); entry < wr::area_taps_max; ++entry)
                    OA_CHECK(tap.weights[entry] == 0);
                OA_CHECK(sum == wr::area_fixed_one);
            }
            // Every scene pixel wholly inside the picture counts for exactly
            // its length; the last may be cut by the picture's edge.
            for (uint32_t column = 0; column < scene_columns; ++column) {
                if (uint64_t{column + 1U} * scale <= uint64_t{extent} * wr::area_fixed_one)
                    OA_CHECK(received[column] == scale);
            }
            for (const wr::AreaTap& tap : plan.rows()) {
                uint64_t sum = 0;
                for (uint32_t entry = 0; entry < plan.row_taps(); ++entry)
                    sum += tap.weights[entry];
                OA_CHECK(sum == wr::area_fixed_one);
                OA_CHECK(tap.first + plan.row_taps() <= plan.scene_height());
            }
        }
    }
    wr::AreaPlan plan;
    OA_CHECK(wr::plan_area_filter(plan, scale_half, 9, 5) == wr::AreaError::none);
    OA_CHECK(plan.column_taps() == 2 && plan.row_taps() == 2);
    OA_CHECK(plan.scene_width() == 18 && plan.scene_height() == 10);
    for (const wr::AreaTap& tap : plan.columns())
        OA_CHECK(tap.weights[0] == scale_half && tap.weights[1] == scale_half);
    OA_CHECK(wr::plan_area_filter(plan, scale_one, 9, 5) == wr::AreaError::none);
    OA_CHECK(plan.column_taps() == 1 && plan.row_taps() == 1);
    OA_CHECK(plan.scene_width() == 9 && plan.scene_height() == 5);
    // Just above one half, a footprint can cover three scene pixels.
    OA_CHECK(wr::plan_area_filter(plan, scale_just_above_half, 64, 64) == wr::AreaError::none);
    OA_CHECK(plan.column_taps() == 3 && plan.scene_width() == 128);
}

/// Checks that the pass gives the bytes of the straightforward implementation.
void test_matches_slow_implementation() {
    Random random{seed_slow};
    for (uint32_t scale = wr::area_scale_min; scale <= wr::area_scale_max;
         scale += slow_sweep_step) {
        const uint32_t width = 3U + random.next() % 21U;
        const uint32_t height = 2U + random.next() % 17U;
        // A scene larger than the plan reads, with padding: neither may
        // change the picture.
        const uint32_t scene_width = wr::area_scene_extent(scale, width) + random.next() % 3U;
        const uint32_t scene_height = wr::area_scene_extent(scale, height) + random.next() % 3U;
        const Picture scene =
            random_scene(random, scene_width, scene_height, scene_width + random.next() % 5U);
        const Picture fast = area_filter(scene, scale, width, height);
        const Picture slow = slow_area_filter(scene, scale, width, height);
        OA_CHECK(fast.rgb == slow.rgb);
        if (fast.rgb != slow.rgb)
            std::fprintf(stderr, "differs from the slow pass at scale %u\n", scale);
    }
    // The scales at the ends of the range, and either side of one half and
    // of two thirds.
    constexpr uint32_t width = 31;
    constexpr uint32_t height = 19;
    for (const uint32_t scale :
         {scale_half,
          scale_just_above_half,
          scale_just_below_two_thirds,
          scale_two_thirds,
          scale_just_below_one,
          scale_one}) {
        const Picture scene = random_scene(
            random,
            wr::area_scene_extent(scale, width),
            wr::area_scene_extent(scale, height),
            wr::area_scene_extent(scale, width)
        );
        OA_CHECK(
            area_filter(scene, scale, width, height).rgb ==
            slow_area_filter(scene, scale, width, height).rgb
        );
    }
}

/// Checks that every level is within reference_tolerance of the exact average.
void test_within_reference() {
    Random random{seed_reference};
    double largest = 0.0;
    for (const uint32_t scale :
         {scale_half,
          scale_near_half,
          scale_eleven_twentieths,
          scale_three_fifths,
          scale_two_thirds,
          scale_three_quarters,
          scale_nine_tenths,
          scale_just_below_one}) {
        const uint32_t width = 97;
        const uint32_t height = 61;
        const Picture scene = random_scene(
            random,
            wr::area_scene_extent(scale, width),
            wr::area_scene_extent(scale, height),
            wr::area_scene_extent(scale, width)
        );
        const Picture picture = area_filter(scene, scale, width, height);
        const double real_scale = static_cast<double>(scale) / wr::area_fixed_one;
        for (uint32_t y = 0; y < height; ++y) {
            for (uint32_t x = 0; x < width; ++x) {
                for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel) {
                    const double exact =
                        wr::area_sample_reference(scene.source(), real_scale, x, y, channel);
                    largest = std::max(largest, std::fabs(picture.at(x, y, channel) - exact));
                }
            }
        }
    }
    OA_CHECK(largest <= reference_tolerance);
    std::printf("largest difference from the exact average: %.4f of a level\n", largest);
}

/// Checks that one half is the 2x2 box (a + b + c + d + 2) / 4 and one is a copy.
void test_half_and_whole_scales() {
    Random random{seed_reference ^ seed_slow};
    const uint32_t width = 75;
    const uint32_t height = 44;
    const Picture scene = random_scene(random, width * 2U, height * 2U, width * 2U + 3U);
    const Picture half = area_filter(scene, scale_half, width, height);
    bool box = true;
    for (uint32_t y = 0; y < height; ++y) {
        for (uint32_t x = 0; x < width; ++x) {
            for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel) {
                const uint32_t sum =
                    scene.at(2 * x, 2 * y, channel) + scene.at(2 * x + 1, 2 * y, channel) +
                    scene.at(2 * x, 2 * y + 1, channel) + scene.at(2 * x + 1, 2 * y + 1, channel);
                box = box && half.at(x, y, channel) == (sum + 2U) / 4U;
            }
        }
    }
    OA_CHECK(box);
    const Picture whole = area_filter(scene, scale_one, width, height);
    bool copy = true;
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel)
                copy = copy && whole.at(x, y, channel) == scene.at(x, y, channel);
    OA_CHECK(copy);
}

/// Checks that a line one scene pixel wide, at full level on black, keeps its
/// intensity wherever it falls: each picture row (or column) it crosses sums
/// to the level times the scale, give or take one level for the rounding of
/// the at most two pixels it touches.
void test_thin_line_keeps_its_intensity() {
    constexpr uint32_t width = 48;
    constexpr uint32_t height = 6;
    constexpr uint32_t places = 40;
    constexpr uint8_t level = 255;
    constexpr double tolerance = 1.0 + 1.0 / 128.0;
    for (const uint32_t scale : line_scales) {
        const double ideal = level * static_cast<double>(scale) / wr::area_fixed_one;
        double lowest = 2.0;
        double highest = 0.0;
        for (uint32_t place = 0; place < places; ++place) {
            const uint32_t scene_width = wr::area_scene_extent(scale, width);
            const uint32_t scene_height = wr::area_scene_extent(scale, height);
            // A vertical line, then the same line turned to run across.
            Picture down = blank(scene_width, scene_height, scene_width, 0);
            Picture across = blank(scene_height, scene_width, scene_height, 0);
            for (uint32_t row = 0; row < scene_height; ++row) {
                for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel) {
                    down.rgb[(row * scene_width + 4U + place) * wr::area_pixel_bytes + channel] =
                        level;
                    across
                        .rgb[((4U + place) * scene_height + row) * wr::area_pixel_bytes + channel] =
                        level;
                }
            }
            const Picture down_picture = area_filter(down, scale, width, height);
            const Picture across_picture = area_filter(across, scale, height, width);
            for (uint32_t row = 0; row < height; ++row) {
                for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel) {
                    uint32_t down_sum = 0;
                    uint32_t across_sum = 0;
                    for (uint32_t column = 0; column < width; ++column) {
                        down_sum += down_picture.at(column, row, channel);
                        across_sum += across_picture.at(row, column, channel);
                    }
                    OA_CHECK(std::fabs(down_sum - ideal) <= tolerance);
                    OA_CHECK(std::fabs(across_sum - ideal) <= tolerance);
                    lowest = std::min({lowest, down_sum / ideal, across_sum / ideal});
                    highest = std::max({highest, down_sum / ideal, across_sum / ideal});
                }
            }
        }
        std::printf(
            "thin line at scale %.4f: %.3f to %.3f of its intensity over %u places\n",
            static_cast<double>(scale) / wr::area_fixed_one,
            lowest,
            highest,
            places
        );
    }
}

/// Returns whether rows [begin, end) of two pictures of one size hold the same bytes, padding included.
///
/// @param first one picture
/// @param second the other, of the same size and stride
/// @param begin first row compared
/// @param end row past the last compared
/// @return true when the rows match
bool rows_equal(const Picture& first, const Picture& second, uint32_t begin, uint32_t end) {
    const auto row_bytes = static_cast<std::ptrdiff_t>(first.stride) * wr::area_pixel_bytes;
    return std::equal(
        first.rgb.begin() + begin * row_bytes,
        first.rgb.begin() + end * row_bytes,
        second.rgb.begin() + begin * row_bytes
    );
}

/// Checks that any split of the rows, and pools of 1 to 8 threads, give the
/// bytes of the whole picture filtered at once, and that the padding past
/// each row is never written.
void test_bands_and_pools_agree() {
    Random random{seed_bands};
    const uint32_t width = 1000;
    const uint32_t height = 700;
    for (const uint32_t scale : {scale_half, scale_three_fifths, scale_three_quarters, scale_one}) {
        const Picture scene = random_scene(
            random,
            wr::area_scene_extent(scale, width),
            wr::area_scene_extent(scale, height),
            wr::area_scene_extent(scale, width) + 1U
        );
        wr::AreaPlan plan;
        OA_CHECK(wr::plan_area_filter(plan, scale, width, height) == wr::AreaError::none);
        Picture whole = blank(width, height, width + 2U, untouched_fill);
        OA_CHECK(
            wr::area_filter_rgb24_rows(plan, scene.source(), whole.target(), 0, height) ==
            wr::AreaError::none
        );
        for (uint32_t threads = 1; threads <= 8; ++threads) {
            job_pool::Pool pool(threads);
            Picture pooled = blank(width, height, width + 2U, untouched_fill);
            OA_CHECK(
                wr::area_filter_rgb24(plan, scene.source(), pooled.target(), &pool) ==
                wr::AreaError::none
            );
            OA_CHECK(pooled.rgb == whole.rgb);
        }
        Picture unpooled = blank(width, height, width + 2U, untouched_fill);
        OA_CHECK(
            wr::area_filter_rgb24(plan, scene.source(), unpooled.target()) == wr::AreaError::none
        );
        OA_CHECK(unpooled.rgb == whole.rgb);
        // Bands filtered last to first.
        for (const uint32_t band_rows : {1U, 7U, 33U, 350U}) {
            Picture banded = blank(width, height, width + 2U, untouched_fill);
            for (uint32_t band = job_pool::bands_of_rows(height, band_rows); band-- > 0;) {
                const uint32_t begin = band * band_rows;
                OA_CHECK(
                    wr::area_filter_rgb24_rows(
                        plan,
                        scene.source(),
                        banded.target(),
                        begin,
                        std::min(height, begin + band_rows)
                    ) == wr::AreaError::none
                );
            }
            OA_CHECK(banded.rgb == whole.rgb);
        }
        bool padding_kept = true;
        for (uint32_t row = 0; row < height; ++row)
            for (uint32_t byte = width * wr::area_pixel_bytes;
                 byte < whole.stride * wr::area_pixel_bytes;
                 ++byte)
                padding_kept =
                    padding_kept &&
                    whole.rgb[row * whole.stride * wr::area_pixel_bytes + byte] == untouched_fill;
        OA_CHECK(padding_kept);
    }
}

/// Checks that a band writes its own rows as the whole picture has them and
/// leaves every other row as it was.
void test_band_writes_only_its_rows() {
    Random random{seed_bands ^ seed_digest};
    const uint32_t width = 61;
    const uint32_t height = 40;
    for (const uint32_t scale : {scale_half, scale_two_thirds, scale_one}) {
        const Picture scene = random_scene(
            random,
            wr::area_scene_extent(scale, width),
            wr::area_scene_extent(scale, height),
            wr::area_scene_extent(scale, width)
        );
        wr::AreaPlan plan;
        OA_CHECK(wr::plan_area_filter(plan, scale, width, height) == wr::AreaError::none);
        Picture whole = blank(width, height, width + 1U, untouched_fill);
        OA_CHECK(
            wr::area_filter_rgb24(plan, scene.source(), whole.target()) == wr::AreaError::none
        );
        const Picture untouched = blank(width, height, width + 1U, untouched_fill);
        for (uint32_t begin = 0; begin < height; ++begin) {
            for (uint32_t end = begin; end <= height; end += 1U + end / 4U) {
                Picture band = untouched;
                OA_CHECK(
                    wr::area_filter_rgb24_rows(plan, scene.source(), band.target(), begin, end) ==
                    wr::AreaError::none
                );
                OA_CHECK(rows_equal(band, untouched, 0, begin));
                OA_CHECK(rows_equal(band, whole, begin, end));
                OA_CHECK(rows_equal(band, untouched, end, height));
            }
        }
    }
}

/// Checks that a malformed scale or size is refused and leaves the plan empty.
void test_malformed_plans() {
    wr::AreaPlan plan;
    OA_CHECK(wr::plan_area_filter(plan, scale_half, 10, 10) == wr::AreaError::none);
    OA_CHECK(
        wr::plan_area_filter(plan, wr::area_scale_min - 1U, 10, 10) ==
        wr::AreaError::scale_out_of_range
    );
    OA_CHECK(plan.scale() == 0 && plan.columns().empty() && plan.rows().empty());
    OA_CHECK(
        wr::plan_area_filter(plan, wr::area_scale_max + 1U, 10, 10) ==
        wr::AreaError::scale_out_of_range
    );
    OA_CHECK(wr::plan_area_filter(plan, 0, 10, 10) == wr::AreaError::scale_out_of_range);
    OA_CHECK(wr::plan_area_filter(plan, scale_half, 0, 10) == wr::AreaError::empty_picture);
    OA_CHECK(wr::plan_area_filter(plan, scale_half, 10, 0) == wr::AreaError::empty_picture);
    OA_CHECK(
        wr::plan_area_filter(plan, scale_half, wr::area_picture_edge_limit + 1U, 10) ==
        wr::AreaError::picture_too_large
    );
    OA_CHECK(
        wr::plan_area_filter(plan, scale_half, 10, wr::area_picture_edge_limit + 1U) ==
        wr::AreaError::picture_too_large
    );
    OA_CHECK(plan.scale() == 0 && plan.picture_width() == 0 && plan.scene_width() == 0);
    OA_CHECK(
        wr::plan_area_filter(
            plan, scale_half, wr::area_picture_edge_limit, wr::area_picture_edge_limit
        ) == wr::AreaError::none
    );
    OA_CHECK(plan.scene_width() == 2U * wr::area_picture_edge_limit);
    OA_CHECK(wr::area_scene_extent(scale_half, 10) == 20);
    OA_CHECK(wr::area_scene_extent(scale_three_fifths, 10) == 17);
    OA_CHECK(wr::area_scene_extent(wr::area_scale_min - 1U, 10) == 0);
    OA_CHECK(wr::area_scene_extent(scale_half, wr::area_picture_edge_limit + 1U) == 0);
}

/// Checks that rebuilding a plan at a size no larger, at another scale or
/// after a failed build, keeps the plan's storage and allocates nothing.
void test_rebuild_keeps_storage() {
    constexpr uint32_t large_width = 900;
    constexpr uint32_t large_height = 700;
    constexpr uint32_t small_width = 333;
    constexpr uint32_t small_height = 177;
    wr::AreaPlan plan;
    OA_CHECK(
        wr::plan_area_filter(plan, scale_half, large_width, large_height) == wr::AreaError::none
    );
    const wr::AreaTap* const columns = plan.columns().data();
    const wr::AreaTap* const rows = plan.rows().data();
    const std::size_t column_capacity = plan.columns().capacity();
    const std::size_t row_capacity = plan.rows().capacity();
    const auto storage_kept = [&] {
        return plan.columns().data() == columns && plan.rows().data() == rows &&
               plan.columns().capacity() == column_capacity &&
               plan.rows().capacity() == row_capacity;
    };
    OA_CHECK(
        wr::plan_area_filter(plan, scale_two_thirds, small_width, small_height) ==
        wr::AreaError::none
    );
    OA_CHECK(plan.columns().size() == small_width && plan.rows().size() == small_height);
    OA_CHECK(storage_kept());
    OA_CHECK(wr::plan_area_filter(plan, 0, small_width, small_height) != wr::AreaError::none);
    OA_CHECK(storage_kept());
    OA_CHECK(
        wr::plan_area_filter(plan, scale_just_above_half, large_width, large_height) ==
        wr::AreaError::none
    );
    OA_CHECK(storage_kept());
}

/// Checks that a malformed frame is refused with no byte of the picture written.
void test_malformed_frames() {
    constexpr uint32_t width = 20;
    constexpr uint32_t height = 12;
    constexpr uint32_t scale = scale_three_fifths;
    Random random{seed_slow ^ seed_bands};
    const uint32_t scene_width = wr::area_scene_extent(scale, width);
    const uint32_t scene_height = wr::area_scene_extent(scale, height);
    const Picture scene = random_scene(random, scene_width, scene_height, scene_width);
    wr::AreaPlan plan;
    OA_CHECK(wr::plan_area_filter(plan, scale, width, height) == wr::AreaError::none);
    Picture picture = blank(width, height, width, untouched_fill);
    const std::vector<uint8_t> untouched = picture.rgb;

    // Refused with `expected` whole and as one band, with nothing written.
    const auto refused = [&](wr::AreaError expected,
                             const wr::AreaPlan& with_plan,
                             wr::RgbSource source,
                             wr::RgbTarget target) {
        const bool whole_refused = wr::area_filter_rgb24(with_plan, source, target) == expected;
        const bool band_refused =
            wr::area_filter_rgb24_rows(with_plan, source, target, 0, height) == expected;
        return whole_refused && band_refused && picture.rgb == untouched;
    };

    OA_CHECK(refused(wr::AreaError::no_plan, wr::AreaPlan{}, scene.source(), picture.target()));
    wr::RgbSource source = scene.source();
    wr::RgbTarget target = picture.target();
    source.rgb = nullptr;
    OA_CHECK(refused(wr::AreaError::missing_pixels, plan, source, picture.target()));
    target.rgb = nullptr;
    OA_CHECK(refused(wr::AreaError::missing_pixels, plan, scene.source(), target));
    target = picture.target();
    target.width = width - 1U;
    OA_CHECK(refused(wr::AreaError::picture_mismatch, plan, scene.source(), target));
    target = picture.target();
    target.height = height + 1U;
    OA_CHECK(refused(wr::AreaError::picture_mismatch, plan, scene.source(), target));
    source = scene.source();
    source.width = scene_width - 1U;
    OA_CHECK(refused(wr::AreaError::scene_too_small, plan, source, picture.target()));
    source = scene.source();
    source.height = scene_height - 1U;
    OA_CHECK(refused(wr::AreaError::scene_too_small, plan, source, picture.target()));
    source = scene.source();
    source.stride_pixels = scene_width - 1U;
    OA_CHECK(refused(wr::AreaError::stride_out_of_range, plan, source, picture.target()));
    source.stride_pixels = wr::area_stride_limit + 1U;
    OA_CHECK(refused(wr::AreaError::stride_out_of_range, plan, source, picture.target()));
    target = picture.target();
    target.stride_pixels = width - 1U;
    OA_CHECK(refused(wr::AreaError::stride_out_of_range, plan, scene.source(), target));
    target.stride_pixels = wr::area_stride_limit + 1U;
    OA_CHECK(refused(wr::AreaError::stride_out_of_range, plan, scene.source(), target));
    OA_CHECK(
        wr::area_filter_rgb24_rows(plan, scene.source(), picture.target(), 5, 4) ==
        wr::AreaError::rows_out_of_range
    );
    OA_CHECK(
        wr::area_filter_rgb24_rows(plan, scene.source(), picture.target(), 0, height + 1U) ==
        wr::AreaError::rows_out_of_range
    );
    OA_CHECK(picture.rgb == untouched);
    // An empty band is a band with nothing to filter.
    OA_CHECK(
        wr::area_filter_rgb24_rows(plan, scene.source(), picture.target(), 4, 4) ==
        wr::AreaError::none
    );
    OA_CHECK(picture.rgb == untouched);
    // A plan whose build failed is no plan.
    wr::AreaPlan failed;
    OA_CHECK(wr::plan_area_filter(failed, 0, width, height) == wr::AreaError::scale_out_of_range);
    OA_CHECK(refused(wr::AreaError::no_plan, failed, scene.source(), picture.target()));
    // Only plan_area_filter fills a plan, so its taps cannot be edited to
    // read outside the scene; a plan moved from keeps its size but no taps,
    // and is no plan.
    static_assert(!std::is_aggregate_v<wr::AreaPlan>);
    wr::AreaPlan moved_from;
    OA_CHECK(wr::plan_area_filter(moved_from, scale, width, height) == wr::AreaError::none);
    const wr::AreaPlan moved_to = std::move(moved_from);
    OA_CHECK(moved_to.columns().size() == width);
    OA_CHECK(refused(wr::AreaError::no_plan, moved_from, scene.source(), picture.target()));
}

/// Checks the pinned digest of seeded scenes filtered at digest_scales.
void test_pinned_digest() {
    Random random{seed_digest};
    uint32_t digest = fnv_offset_basis;
    for (const uint32_t scale : digest_scales) {
        const uint32_t scene_width = wr::area_scene_extent(scale, digest_width) + 2U;
        const uint32_t scene_height = wr::area_scene_extent(scale, digest_height) + 2U;
        const Picture scene = random_scene(random, scene_width, scene_height, scene_width);
        digest = fnv1a(digest, area_filter(scene, scale, digest_width, digest_height).rgb);
    }
    if (digest != pinned_digest)
        std::fprintf(stderr, "area pass digest 0x%08X, pinned 0x%08X\n", digest, pinned_digest);
    OA_CHECK(digest == pinned_digest);
}

/// Logs the quickest of timing_runs filters of a 1080p battlefield from a
/// scene twice its size, at each of timing_scales, on the calling thread and
/// on a pool; for information, never checked.
void log_time_at_1080p() {
    Random random{seed_timing};
    const Picture scene =
        random_scene(random, timing_scene_width, timing_scene_height, timing_scene_width);
    Picture picture = blank(timing_width, timing_height, timing_width, 0);
    job_pool::Pool pool(timing_threads);
    wr::AreaPlan plan;
    const auto quickest = [&](job_pool::Pool* with_pool) {
        auto best = std::chrono::steady_clock::duration::max();
        for (int run = 0; run < timing_runs; ++run) {
            const auto start = std::chrono::steady_clock::now();
            OA_CHECK(
                wr::area_filter_rgb24(plan, scene.source(), picture.target(), with_pool) ==
                wr::AreaError::none
            );
            best = std::min(best, std::chrono::steady_clock::now() - start);
        }
        return std::chrono::duration<double, std::milli>(best).count();
    };
    for (const uint32_t scale : timing_scales) {
        OA_CHECK(
            wr::plan_area_filter(plan, scale, timing_width, timing_height) == wr::AreaError::none
        );
        OA_CHECK(plan.scene_width() <= timing_scene_width);
        OA_CHECK(plan.scene_height() <= timing_scene_height);
        const double alone = quickest(nullptr);
        const double pooled = quickest(&pool);
        std::printf(
            "area pass, %ux%u picture from a %ux%u scene at scale %.5f (%u x %u taps): %.2f ms on "
            "the calling thread, %.2f ms on %u threads\n",
            timing_width,
            timing_height,
            plan.scene_width(),
            plan.scene_height(),
            static_cast<double>(scale) / wr::area_fixed_one,
            plan.column_taps(),
            plan.row_taps(),
            alone,
            pooled,
            pool.threads()
        );
    }
}

} // namespace

int main() {
    test_weights_are_exact();
    test_matches_slow_implementation();
    test_within_reference();
    test_half_and_whole_scales();
    test_thin_line_keeps_its_intensity();
    test_bands_and_pools_agree();
    test_band_writes_only_its_rows();
    test_malformed_plans();
    test_rebuild_keeps_storage();
    test_malformed_frames();
    test_pinned_digest();
    log_time_at_1080p();
    return oa::test::check_exit_status();
}
