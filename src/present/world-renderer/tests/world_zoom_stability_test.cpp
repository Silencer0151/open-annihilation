// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How steady the zoomed battlefield stays as the view moves
// (scene_filter.hpp). A seeded line one map pixel wide, on a scene drawn at
// one pixel per map pixel, moved one map pixel a frame for 64 frames and in
// eighth-pixel steps, at zooms 0.5 to 0.95: the area pass keeps its light
// in every row within one level per pixel it touches; today's point
// sampling loses it on some frames; a line drawn with the thin-line
// rule keeps at least two thirds of a screen pixel's light; and the card's
// two-level reduction, the Full tier's zoomed-out world target, keeps the
// line's light within 0.85 and 1.16 of the ideal, exactly at 0.5, and
// swings no wider than plain bilinear reduction. On a seeded
// map of fine detail, frames of the area pass shimmer less than point
// sampling's at 0.5, 0.6 and 0.75, and the sharp-bilinear and pixel-art
// references less than NEAREST at 1.37 and 2, the scene moving in eighths
// of a map pixel.
#include "oa/present/world_renderer.hpp"
#include "oa/present/world_renderer/scene_filter.hpp"
#include "oa/test/check.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <vector>

namespace wr = oa::present::world_renderer;

namespace {

/// Seeds of the line and of the map of fine detail.
constexpr uint32_t seed_line = 0x1F83D9ABU;
constexpr uint32_t seed_map = 0x5BE0CD19U;

/// Frames the line is followed for, one map pixel apart, and the steps of an
/// eighth of a map pixel it is also followed through.
constexpr uint32_t line_frames = 64;
constexpr uint32_t eighth_steps = 8;

/// The zoomed-out scales the line is followed at, in hundredths.
constexpr std::array<uint32_t, 10> zoom_out_hundredths{50, 55, 60, 65, 70, 75, 80, 85, 90, 95};

/// The scene the line is drawn on, in pixels: wide enough for every frame,
/// and a few rows.
constexpr uint32_t line_scene_width = 192;
constexpr uint32_t line_scene_height = 6;
/// Column of the line at the first frame; it moves one column left a frame.
constexpr uint32_t line_first_column = 150;

/// The bounds of a thin line's light through the two-level reduction, as a
/// share of the ideal, across the zooms it is followed at: the model's
/// figures, 0.86 to 1.15 at the widest, zoom 0.6, with a hundredth of room.
constexpr double two_level_least = 0.85;
constexpr double two_level_most = 1.16;
/// The pixels of a row the two-level reduction may spread the line over,
/// each rounded by up to half a level.
constexpr double two_level_pixels_lit = 4.0;

/// The least light a thin line keeps, in screen pixels of its own level.
constexpr double thin_line_least = 2.0 / 3.0;
/// The most a thin line drawn two scene pixels thick gives below zoom 2/3.
constexpr double thin_line_most = 4.0 / 3.0;

/// The map of fine detail the shimmer is measured on, and the frames the
/// camera sweeps it through.
constexpr uint32_t map_width = 160;
constexpr uint32_t map_height = 24;
constexpr uint32_t sweep_frames = 32;

/// The zoomed-out and magnified scales the shimmer is compared at.
constexpr std::array<double, 3> shimmer_zoom_out{0.5, 0.6, 0.75};
constexpr std::array<double, 2> shimmer_magnify{1.37, 2.0};

/// The green channel, which the light and the shimmer are measured on.
constexpr uint32_t green = 1;

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

/// RGB pixels the test owns, rows one after another.
struct Picture {
    uint32_t width{};
    uint32_t height{};
    std::vector<uint8_t> rgb;

    /// Returns the pixels as a scene.
    ///
    /// @return a read-only view
    [[nodiscard]] wr::RgbSource source() const { return {rgb.data(), width, height, width}; }

    /// Returns the pixels as a picture.
    ///
    /// @return a writable view
    [[nodiscard]] wr::RgbTarget target() { return {rgb.data(), width, height, width}; }

    /// Returns one level.
    ///
    /// @param x column
    /// @param y row
    /// @param channel 0 for red, 1 for green, 2 for blue
    /// @return the level
    [[nodiscard]] uint8_t at(uint32_t x, uint32_t y, uint32_t channel) const {
        return rgb[(static_cast<std::size_t>(y) * width + x) * wr::area_pixel_bytes + channel];
    }
};

/// Returns a black picture.
///
/// @param width columns
/// @param height rows
/// @return the picture
Picture black(uint32_t width, uint32_t height) {
    return {width, height, std::vector<uint8_t>(std::size_t{width} * height * 3U, 0)};
}

/// Draws a vertical line into a scene: `columns` whole columns at `column`
/// at the level, then a column at the level times `eighths` / 8 to its
/// left and the rest of the level's eighths to its right, so its light is
/// the level times `columns` whatever its fraction.
///
/// @param[in,out] scene the scene
/// @param column the line's first whole column
/// @param eighths eighths of a column the line lies left of `column`, 0 to 7
/// @param columns whole columns of its thickness
/// @param level the line's level in every channel, a multiple of 8
void draw_line(Picture& scene, uint32_t column, uint32_t eighths, uint32_t columns, uint8_t level) {
    const auto put = [&](uint32_t x, uint32_t value) {
        if (x >= scene.width)
            return;
        for (uint32_t y = 0; y < scene.height; ++y)
            for (uint32_t channel = 0; channel < wr::area_pixel_bytes; ++channel) {
                auto& byte =
                    scene.rgb[(static_cast<std::size_t>(y) * scene.width + x) * 3U + channel];
                byte = static_cast<uint8_t>(std::min<uint32_t>(255, byte + value));
            }
    };
    const uint32_t eighth = level / eighth_steps;
    put(column - 1, eighth * eighths);
    for (uint32_t index = 0; index + 1 < columns; ++index)
        put(column + index, level);
    put(column + columns - 1, level - eighth * eighths);
}

/// Filters a scene by the area pass at a scale in hundredths, into a picture
/// as wide as the scene's footprints allow.
///
/// @param scene the scene
/// @param hundredths screen pixels per scene pixel, in hundredths
/// @return the picture
Picture area_picture(const Picture& scene, uint32_t hundredths) {
    const auto scale =
        static_cast<uint32_t>((uint64_t{hundredths} * wr::area_fixed_one + 99U) / 100U);
    const auto width = static_cast<uint32_t>((scene.width - 2) * hundredths / 100U);
    const auto height = static_cast<uint32_t>((scene.height - 2) * hundredths / 100U);
    wr::AreaPlan plan;
    OA_CHECK(wr::plan_area_filter(plan, scale, width, height) == wr::AreaError::none);
    Picture picture = black(width, height);
    OA_CHECK(wr::area_filter_rgb24(plan, scene.source(), picture.target()) == wr::AreaError::none);
    return picture;
}

/// Returns the light of a picture's row, in levels.
///
/// @param picture the picture
/// @param row the row
/// @param[out] touched the pixels of the row the line lit
/// @return the sum of the row's green levels
uint32_t row_light(const Picture& picture, uint32_t row, uint32_t& touched) {
    uint32_t sum = 0;
    touched = 0;
    for (uint32_t x = 0; x < picture.width; ++x) {
        const uint8_t level = picture.at(x, row, green);
        sum += level;
        touched += level != 0 ? 1U : 0U;
    }
    return sum;
}

/// Follows a line one map pixel wide through 64 frames and eighth-pixel
/// steps: the area pass keeps each row's light at the zoom times the
/// line's, within one level per pixel the line lit.
void test_area_keeps_a_thin_line() {
    Random random{seed_line};
    const auto level = static_cast<uint8_t>(64U + 8U * (random.next() % 24U));
    for (const uint32_t hundredths : zoom_out_hundredths) {
        const double ideal = static_cast<double>(level) * hundredths / 100.0;
        bool steady = true;
        for (uint32_t frame = 0; frame < line_frames; ++frame)
            for (uint32_t eighths = 0; eighths < eighth_steps; ++eighths) {
                Picture scene = black(line_scene_width, line_scene_height);
                draw_line(scene, line_first_column - frame, eighths, 1, level);
                const Picture picture = area_picture(scene, hundredths);
                for (uint32_t row = 0; row < picture.height; ++row) {
                    uint32_t touched = 0;
                    const uint32_t light = row_light(picture, row, touched);
                    steady = steady && std::abs(static_cast<double>(light) - ideal) <=
                                           static_cast<double>(touched);
                }
            }
        OA_CHECK(steady);
        if (!steady)
            std::fprintf(stderr, "the area pass lost a thin line at zoom 0.%u\n", hundredths);
    }
}

/// Follows the line through today's point sampling at zoom 0.5: on some
/// frames none of it shows.
void test_point_sampling_loses_a_thin_line() {
    Random random{seed_line};
    const auto level = static_cast<uint8_t>(64U + 8U * (random.next() % 24U));
    uint32_t lost = 0;
    uint32_t shown = 0;
    for (uint32_t frame = 0; frame < line_frames; ++frame) {
        Picture scene = black(line_scene_width, line_scene_height);
        draw_line(scene, line_first_column - frame, 0, 1, level);
        Picture picture = black((line_scene_width - 2) / 2, (line_scene_height - 2) / 2);
        OA_CHECK(
            wr::resample_nearest_rgb24(
                wr::RgbSource{scene.rgb.data(), scene.width, scene.height, scene.width},
                0.5F,
                wr::RgbTarget{picture.rgb.data(), picture.width, picture.height, picture.width}
            ) == wr::AreaError::none
        );
        uint32_t touched = 0;
        if (row_light(picture, 0, touched) == 0)
            ++lost;
        else
            ++shown;
    }
    OA_CHECK(lost != 0 && shown != 0);
}

/// Follows a line drawn as thick as the thin-line rule says: through the
/// area pass it keeps at least two thirds of a screen pixel's light in every
/// row, and at most four thirds.
void test_thin_line_rule_keeps_two_thirds() {
    Random random{seed_line};
    const auto level = static_cast<uint8_t>(64U + 8U * (random.next() % 24U));
    for (const uint32_t hundredths : zoom_out_hundredths) {
        const float zoom = static_cast<float>(hundredths) / 100.0F;
        const auto thickness = static_cast<uint32_t>(wr::scene_line_thickness(1.0F, zoom));
        OA_CHECK(thickness == (hundredths < 67 ? 2U : 1U));
        double least = thin_line_most;
        double most = 0.0;
        for (uint32_t frame = 0; frame < line_frames; ++frame) {
            Picture scene = black(line_scene_width, line_scene_height);
            draw_line(scene, line_first_column - frame, 0, thickness, level);
            const Picture picture = area_picture(scene, hundredths);
            for (uint32_t row = 0; row < picture.height; ++row) {
                uint32_t touched = 0;
                const double light = static_cast<double>(row_light(picture, row, touched)) /
                                     static_cast<double>(level);
                least = std::min(least, light);
                most = std::max(most, light);
            }
        }
        // Rounding may take up to a level from each of the three pixels lit.
        const double rounding = 3.0 / static_cast<double>(level);
        OA_CHECK(least >= thin_line_least - rounding);
        OA_CHECK(most <= thin_line_most + rounding);
    }
    // The rule leaves a scene that is not reduced at one pixel.
    OA_CHECK(wr::scene_line_thickness(1.0F, 1.0F) == 1);
    OA_CHECK(wr::scene_line_thickness(1.0F, 2.0F) == 1);
    OA_CHECK(wr::scene_line_thickness(0.75F, 0.5F) == 2);
}

/// Follows the line one map pixel wide through the two-level reduction, a
/// map pixel a frame: each row's light stays within two_level_least and
/// two_level_most of the ideal at every zoom, is the ideal at zoom 0.5,
/// where the reduction is the box of four, and swings no wider than plain
/// bilinear reduction's across the frames.
void test_two_level_reduction_keeps_a_thin_line() {
    Random random{seed_line};
    const auto level = static_cast<uint8_t>(64U + 8U * (random.next() % 24U));
    for (const uint32_t hundredths : zoom_out_hundredths) {
        const double zoom = static_cast<double>(hundredths) / 100.0;
        const double ideal = static_cast<double>(level) * zoom;
        const auto width = static_cast<uint32_t>(line_scene_width * hundredths / 100U);
        const auto height = static_cast<uint32_t>(line_scene_height * hundredths / 100U);
        double least = two_level_most;
        double most = 0.0;
        double plain_least = 10.0;
        double plain_most = 0.0;
        for (uint32_t frame = 0; frame < line_frames; ++frame) {
            Picture scene = black(line_scene_width, line_scene_height);
            draw_line(scene, line_first_column - frame, 0, 1, level);
            Picture reduced = black(width, height);
            wr::two_level_rgb24(scene.source(), {zoom, zoom, 0.0, 0.0}, reduced.target());
            Picture plain = black(width, height);
            wr::bilinear_rgb24(scene.source(), {zoom, zoom, 0.0, 0.0}, plain.target());
            for (uint32_t row = 0; row < height; ++row) {
                uint32_t touched = 0;
                const double light = static_cast<double>(row_light(reduced, row, touched)) / ideal;
                least = std::min(least, light);
                most = std::max(most, light);
                const double plain_light =
                    static_cast<double>(row_light(plain, row, touched)) / ideal;
                plain_least = std::min(plain_least, plain_light);
                plain_most = std::max(plain_most, plain_light);
            }
        }
        std::printf(
            "a thin line at zoom %.2f: the two-level reduction keeps %.2f to %.2f of its light, "
            "plain bilinear %.2f to %.2f\n",
            zoom,
            least,
            most,
            plain_least,
            plain_most
        );
        // Rounding may take up to half a level from each pixel the line lights.
        const double rounding = two_level_pixels_lit * 0.5 / ideal;
        OA_CHECK(least >= two_level_least - rounding);
        OA_CHECK(most <= two_level_most + rounding);
        if (hundredths == 50) {
            OA_CHECK(std::abs(least - 1.0) <= rounding);
            OA_CHECK(std::abs(most - 1.0) <= rounding);
        }
        OA_CHECK(most - least <= plain_most - plain_least + 2.0 * rounding);
    }
}

/// Returns a seeded map of fine detail: every pixel a random level.
///
/// @return the map
Picture fine_map() {
    Random random{seed_map};
    Picture map = black(map_width, map_height);
    for (auto& byte : map.rgb)
        byte = static_cast<uint8_t>(random.next() >> 24);
    return map;
}

/// Returns the part of the map a camera at a column shows: the map's
/// columns from it on.
///
/// @param map the map
/// @param camera the first column
/// @param width columns of the part
/// @return the scene
Picture scene_at(const Picture& map, uint32_t camera, uint32_t width) {
    Picture scene = black(width, map.height);
    for (uint32_t y = 0; y < map.height; ++y)
        std::copy_n(
            map.rgb.begin() + static_cast<std::ptrdiff_t>(
                                  (static_cast<std::size_t>(y) * map.width + camera) * 3U
                              ),
            width * 3U,
            scene.rgb.begin() +
                static_cast<std::ptrdiff_t>(static_cast<std::size_t>(y) * width * 3U)
        );
    return scene;
}

/// Sweeps the camera across the map a map pixel a frame at zoomed-out
/// scales: the area pass's frames shimmer less than today's point
/// sampling's.
void test_area_shimmers_less_than_point_sampling() {
    const Picture map = fine_map();
    for (const double zoom : shimmer_zoom_out) {
        const auto hundredths = static_cast<uint32_t>(std::lround(zoom * 100.0));
        const uint32_t scene_width = map_width - sweep_frames;
        std::vector<Picture> area;
        std::vector<Picture> nearest;
        std::vector<wr::ExactChannel> exact;
        for (uint32_t frame = 0; frame < sweep_frames; ++frame) {
            const Picture scene = scene_at(map, frame, scene_width);
            area.push_back(area_picture(scene, hundredths));
            Picture point = black(area.back().width, area.back().height);
            OA_CHECK(
                wr::resample_nearest_rgb24(
                    wr::RgbSource{scene.rgb.data(), scene.width, scene.height, scene.width},
                    static_cast<float>(zoom),
                    wr::RgbTarget{point.rgb.data(), point.width, point.height, point.width}
                ) == wr::AreaError::none
            );
            nearest.push_back(std::move(point));
            exact.push_back(
                wr::exact_channel(
                    scene.source(),
                    {zoom, zoom, 0.0, 0.0},
                    area.back().width,
                    area.back().height,
                    green
                )
            );
        }
        std::vector<wr::RgbSource> area_frames;
        std::vector<wr::RgbSource> nearest_frames;
        for (uint32_t frame = 0; frame < sweep_frames; ++frame) {
            area_frames.push_back(area[frame].source());
            nearest_frames.push_back(nearest[frame].source());
        }
        const double area_shimmer = wr::shimmer(area_frames, exact, green);
        const double nearest_shimmer = wr::shimmer(nearest_frames, exact, green);
        std::printf(
            "shimmer at zoom %.2f: area %.2f, point sampling %.2f levels\n",
            zoom,
            area_shimmer,
            nearest_shimmer
        );
        OA_CHECK(area_shimmer < nearest_shimmer);
        OA_CHECK(area_shimmer < 1.0);
    }
}

/// Moves the scene in eighths of a map pixel under magnifying scales: the
/// sharp-bilinear and pixel-art references shimmer less than NEAREST.
void test_card_filters_shimmer_less_than_nearest() {
    const Picture map = fine_map();
    const Picture scene = scene_at(map, 0, map_width / 2);
    for (const double zoom : shimmer_magnify) {
        const auto factor = static_cast<uint32_t>(std::ceil(zoom));
        const auto width = static_cast<uint32_t>(std::floor((scene.width - 4) * zoom));
        const auto height = static_cast<uint32_t>(std::floor((scene.height - 4) * zoom));
        std::vector<Picture> nearest;
        std::vector<Picture> sharp;
        std::vector<Picture> pixelart;
        std::vector<wr::ExactChannel> exact;
        for (uint32_t step = 0; step < sweep_frames; ++step) {
            // The scene's corner a step of an eighth of a map pixel further left.
            const wr::ScenePlacement placement{
                zoom, zoom, -static_cast<double>(step) * zoom / eighth_steps, 0.0
            };
            nearest.push_back(black(width, height));
            wr::nearest_rgb24(scene.source(), placement, nearest.back().target());
            sharp.push_back(black(width, height));
            wr::sharp_bilinear_rgb24(scene.source(), placement, factor, sharp.back().target());
            pixelart.push_back(black(width, height));
            wr::pixelart_rgb24(scene.source(), placement, pixelart.back().target());
            exact.push_back(wr::exact_channel(scene.source(), placement, width, height, green));
        }
        const auto frames_of = [](const std::vector<Picture>& pictures) {
            std::vector<wr::RgbSource> frames;
            for (const auto& picture : pictures)
                frames.push_back(picture.source());
            return frames;
        };
        const double nearest_shimmer = wr::shimmer(frames_of(nearest), exact, green);
        const double sharp_shimmer = wr::shimmer(frames_of(sharp), exact, green);
        const double pixelart_shimmer = wr::shimmer(frames_of(pixelart), exact, green);
        std::printf(
            "shimmer at zoom %.2f: nearest %.2f, sharp-bilinear %.2f, pixel art %.2f levels\n",
            zoom,
            nearest_shimmer,
            sharp_shimmer,
            pixelart_shimmer
        );
        OA_CHECK(sharp_shimmer < nearest_shimmer);
        OA_CHECK(pixelart_shimmer < nearest_shimmer);
    }
}

} // namespace

int main() {
    test_area_keeps_a_thin_line();
    test_point_sampling_loses_a_thin_line();
    test_thin_line_rule_keeps_two_thirds();
    test_two_level_reduction_keeps_a_thin_line();
    test_area_shimmers_less_than_point_sampling();
    test_card_filters_shimmer_less_than_nearest();
    return oa::test::check_exit_status();
}
