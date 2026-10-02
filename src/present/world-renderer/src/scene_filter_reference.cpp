// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The references of the area pass and of what the graphics card is asked to
// do with a scene: the exact average under a footprint, from its geometry,
// the card's scale modes, the sharp-bilinear and pixel-art filters and the
// overlay, all in double precision; and the measures of a moving picture's
// steadiness. Tests and checks use them; the game does not.
#include "oa/present/world_renderer/scene_filter.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <cstdint>

namespace oa::present::world_renderer {

double area_sample_reference(
    const RgbSource& scene, double scale, uint32_t x, uint32_t y, uint32_t channel
) noexcept {
    if (!(scale > 0.0) || channel >= area_pixel_bytes || scene.rgb == nullptr)
        return 0.0;
    const double left = static_cast<double>(x) / scale;
    const double right = static_cast<double>(x + 1U) / scale;
    const double top = static_cast<double>(y) / scale;
    const double bottom = static_cast<double>(y + 1U) / scale;
    const double last_column = std::min(std::ceil(right), static_cast<double>(scene.width));
    const double last_row = std::min(std::ceil(bottom), static_cast<double>(scene.height));
    double sum = 0.0;
    for (double row = std::floor(top); row < last_row; row += 1.0) {
        const double covered_rows = std::min(bottom, row + 1.0) - std::max(top, row);
        const uint8_t* line =
            scene.rgb + static_cast<std::size_t>(row) * scene.stride_pixels * area_pixel_bytes;
        for (double column = std::floor(left); column < last_column; column += 1.0) {
            const double covered_columns = std::min(right, column + 1.0) - std::max(left, column);
            const uint8_t level =
                line[static_cast<std::size_t>(column) * area_pixel_bytes + channel];
            sum += covered_columns * covered_rows * static_cast<double>(level);
        }
    }
    return sum / ((right - left) * (bottom - top));
}

} // namespace oa::present::world_renderer

namespace oa::present::world_renderer {

namespace {

/// A scene's level, its coordinates clamped to its edges.
///
/// @param scene the scene, at least one pixel
/// @param column scene column, clamped
/// @param row scene row, clamped
/// @param channel 0 for red, 1 for green, 2 for blue
/// @return the level
[[nodiscard]] double
clamped_level(const RgbSource& scene, int64_t column, int64_t row, uint32_t channel) noexcept {
    const auto x = static_cast<std::size_t>(std::clamp<int64_t>(column, 0, scene.width - 1));
    const auto y = static_cast<std::size_t>(std::clamp<int64_t>(row, 0, scene.height - 1));
    return static_cast<double>(
        scene.rgb[(y * scene.stride_pixels + x) * area_pixel_bytes + channel]
    );
}

/// Returns a level rounded to the nearest whole level, halves up, within 0 to 255.
///
/// @param level the unrounded level
/// @return the byte
[[nodiscard]] uint8_t rounded_level(double level) noexcept {
    return static_cast<uint8_t>(std::clamp(std::floor(level + 0.5), 0.0, 255.0));
}

/// Says whether a scene and a picture can be drawn: both hold pixels, and
/// the placement's scales are above 0.
///
/// @param scene the scene
/// @param placement where it lands
/// @param picture the picture
/// @return true when every reference may draw
[[nodiscard]] bool drawable(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept {
    return scene.rgb != nullptr && scene.width != 0 && scene.height != 0 &&
           picture.rgb != nullptr && placement.scale_x > 0.0 && placement.scale_y > 0.0;
}

/// One axis of a linear sample: the first of the two pixels it lies
/// between, and the weight of the second.
struct LinearTap {
    int64_t first{}; ///< the first pixel, unclamped
    double weight{}; ///< the second pixel's weight, from 0 to 1
};

/// Returns the linear tap of a coordinate whose pixel centres lie at whole numbers plus one half.
///
/// @param coordinate the sample's coordinate in pixels
/// @return the tap
[[nodiscard]] LinearTap linear_tap(double coordinate) noexcept {
    const double centred = coordinate - 0.5;
    const double first = std::floor(centred);
    return {static_cast<int64_t>(first), centred - first};
}

/// Returns the tap of the pixel-art filter along one axis.
///
/// @param coordinate the picture pixel centre's scene coordinate
/// @param scale picture pixels per scene pixel, above 0
/// @return the tap: the pixel floor(t - b/2) and the smoothstep weight of the next
[[nodiscard]] LinearTap pixelart_tap(double coordinate, double scale) noexcept {
    const double span = std::clamp(1.0 / scale, 1.0e-5, 1.0);
    const double shifted = coordinate - 0.5 * span;
    const double first = std::floor(shifted);
    const double fraction = shifted - first;
    const double ramp = std::clamp((fraction - (1.0 - span)) / span, 0.0, 1.0);
    return {static_cast<int64_t>(first), ramp * ramp * (3.0 - 2.0 * ramp)};
}

/// Writes a picture pixel from two taps over the scene, interpolating between the four pixels.
///
/// @param scene the scene
/// @param across the tap across
/// @param down the tap down
/// @param[out] out the pixel's three bytes
void write_interpolated(
    const RgbSource& scene, const LinearTap& across, const LinearTap& down, uint8_t* out
) noexcept {
    for (uint32_t channel = 0; channel < area_pixel_bytes; ++channel) {
        const double top =
            clamped_level(scene, across.first, down.first, channel) * (1.0 - across.weight) +
            clamped_level(scene, across.first + 1, down.first, channel) * across.weight;
        const double bottom =
            clamped_level(scene, across.first, down.first + 1, channel) * (1.0 - across.weight) +
            clamped_level(scene, across.first + 1, down.first + 1, channel) * across.weight;
        out[channel] = rounded_level(top * (1.0 - down.weight) + bottom * down.weight);
    }
}

/// Returns a picture pixel's byte offset.
///
/// @param picture the picture
/// @param x column
/// @param y row
/// @return the offset of its first byte
[[nodiscard]] std::size_t
picture_offset(const RgbTarget& picture, uint32_t x, uint32_t y) noexcept {
    return (static_cast<std::size_t>(y) * picture.stride_pixels + x) * area_pixel_bytes;
}

} // namespace

void nearest_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept {
    if (!drawable(scene, placement, picture))
        return;
    for (uint32_t y = 0; y < picture.height; ++y) {
        const double v = (static_cast<double>(y) + 0.5 - placement.offset_y) / placement.scale_y;
        for (uint32_t x = 0; x < picture.width; ++x) {
            const double u =
                (static_cast<double>(x) + 0.5 - placement.offset_x) / placement.scale_x;
            auto* out = picture.rgb + picture_offset(picture, x, y);
            for (uint32_t channel = 0; channel < area_pixel_bytes; ++channel)
                out[channel] = static_cast<uint8_t>(clamped_level(
                    scene,
                    static_cast<int64_t>(std::floor(u)),
                    static_cast<int64_t>(std::floor(v)),
                    channel
                ));
        }
    }
}

void bilinear_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept {
    sharp_bilinear_rgb24(scene, placement, 1, picture);
}

void sharp_bilinear_rgb24(
    const RgbSource& scene,
    const ScenePlacement& placement,
    uint32_t factor,
    const RgbTarget& picture
) noexcept {
    if (!drawable(scene, placement, picture) || factor == 0)
        return;
    const auto n = static_cast<double>(factor);
    // A tap on the prescale target, whose pixel p shows scene pixel p / n.
    const auto scene_tap = [n](double coordinate) {
        const auto tap = linear_tap(coordinate * n);
        const auto first = static_cast<int64_t>(std::floor(static_cast<double>(tap.first) / n));
        const auto second =
            static_cast<int64_t>(std::floor(static_cast<double>(tap.first + 1) / n));
        // The two target pixels show one scene pixel, or two neighbours.
        return LinearTap{first, second == first ? 0.0 : tap.weight};
    };
    for (uint32_t y = 0; y < picture.height; ++y) {
        const double v = (static_cast<double>(y) + 0.5 - placement.offset_y) / placement.scale_y;
        // Within the prescale target its own edge clamps, which is the scene's.
        const auto down =
            scene_tap(std::clamp(v, 0.5 / n, static_cast<double>(scene.height) - 0.5 / n));
        for (uint32_t x = 0; x < picture.width; ++x) {
            const double u =
                (static_cast<double>(x) + 0.5 - placement.offset_x) / placement.scale_x;
            const auto across =
                scene_tap(std::clamp(u, 0.5 / n, static_cast<double>(scene.width) - 0.5 / n));
            write_interpolated(scene, across, down, picture.rgb + picture_offset(picture, x, y));
        }
    }
}

void pixelart_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept {
    if (!drawable(scene, placement, picture))
        return;
    for (uint32_t y = 0; y < picture.height; ++y) {
        const double v = (static_cast<double>(y) + 0.5 - placement.offset_y) / placement.scale_y;
        const auto down = pixelart_tap(v, placement.scale_y);
        for (uint32_t x = 0; x < picture.width; ++x) {
            const double u =
                (static_cast<double>(x) + 0.5 - placement.offset_x) / placement.scale_x;
            const auto across = pixelart_tap(u, placement.scale_x);
            write_interpolated(scene, across, down, picture.rgb + picture_offset(picture, x, y));
        }
    }
}

void overlay_rgb24(
    const RgbTarget& picture, const uint32_t* overlay, uint32_t overlay_stride_pixels
) noexcept {
    constexpr uint32_t opaque = 0xFF000000U;
    if (picture.rgb == nullptr || overlay == nullptr)
        return;
    for (uint32_t y = 0; y < picture.height; ++y)
        for (uint32_t x = 0; x < picture.width; ++x) {
            const uint32_t word = overlay[static_cast<std::size_t>(y) * overlay_stride_pixels + x];
            if ((word & opaque) != opaque)
                continue;
            auto* out = picture.rgb + picture_offset(picture, x, y);
            out[0] = static_cast<uint8_t>(word >> 16);
            out[1] = static_cast<uint8_t>(word >> 8);
            out[2] = static_cast<uint8_t>(word);
        }
}

double footprint_sample_reference(
    const RgbSource& scene,
    const ScenePlacement& placement,
    uint32_t x,
    uint32_t y,
    uint32_t channel
) noexcept {
    if (scene.rgb == nullptr || scene.width == 0 || scene.height == 0 ||
        !(placement.scale_x > 0.0) || !(placement.scale_y > 0.0) || channel >= area_pixel_bytes)
        return 0.0;
    const double left = (static_cast<double>(x) - placement.offset_x) / placement.scale_x;
    const double right = (static_cast<double>(x) + 1.0 - placement.offset_x) / placement.scale_x;
    const double top = (static_cast<double>(y) - placement.offset_y) / placement.scale_y;
    const double bottom = (static_cast<double>(y) + 1.0 - placement.offset_y) / placement.scale_y;
    double sum = 0.0;
    for (double row = std::floor(top); row < bottom; row += 1.0) {
        const double covered_rows = std::min(bottom, row + 1.0) - std::max(top, row);
        for (double column = std::floor(left); column < right; column += 1.0) {
            const double covered_columns = std::min(right, column + 1.0) - std::max(left, column);
            sum += covered_columns * covered_rows *
                   clamped_level(
                       scene, static_cast<int64_t>(column), static_cast<int64_t>(row), channel
                   );
        }
    }
    return sum / ((right - left) * (bottom - top));
}

double line_energy(const RgbSource& picture, uint32_t channel) noexcept {
    if (picture.rgb == nullptr || channel >= area_pixel_bytes)
        return 0.0;
    double sum = 0.0;
    for (uint32_t y = 0; y < picture.height; ++y)
        for (uint32_t x = 0; x < picture.width; ++x)
            sum +=
                picture.rgb
                    [(static_cast<std::size_t>(y) * picture.stride_pixels + x) * area_pixel_bytes +
                     channel];
    return sum / 255.0;
}

ExactChannel exact_channel(
    const RgbSource& scene,
    const ScenePlacement& placement,
    uint32_t width,
    uint32_t height,
    uint32_t channel
) {
    ExactChannel exact;
    exact.width = width;
    exact.height = height;
    exact.levels.resize(static_cast<std::size_t>(width) * height);
    for (uint32_t y = 0; y < height; ++y)
        for (uint32_t x = 0; x < width; ++x)
            exact.levels[static_cast<std::size_t>(y) * width + x] =
                footprint_sample_reference(scene, placement, x, y, channel);
    return exact;
}

double shimmer(
    std::span<const RgbSource> frames, std::span<const ExactChannel> exact, uint32_t channel
) noexcept {
    if (frames.size() < 2 || frames.size() != exact.size() || channel >= area_pixel_bytes)
        return 0.0;
    const uint32_t width = frames.front().width;
    const uint32_t height = frames.front().height;
    for (std::size_t index = 0; index < frames.size(); ++index)
        if (frames[index].rgb == nullptr || frames[index].width != width ||
            frames[index].height != height || exact[index].width != width ||
            exact[index].height != height ||
            exact[index].levels.size() != static_cast<std::size_t>(width) * height)
            return 0.0;
    const auto level = [channel](const RgbSource& frame, uint32_t x, uint32_t y) {
        return static_cast<double>(
            frame.rgb
                [(static_cast<std::size_t>(y) * frame.stride_pixels + x) * area_pixel_bytes +
                 channel]
        );
    };
    double sum = 0.0;
    for (std::size_t index = 1; index < frames.size(); ++index)
        for (uint32_t y = 0; y < height; ++y)
            for (uint32_t x = 0; x < width; ++x) {
                const auto at = static_cast<std::size_t>(y) * width + x;
                const double change = level(frames[index], x, y) - level(frames[index - 1], x, y);
                const double exact_change = exact[index].levels[at] - exact[index - 1].levels[at];
                sum += std::abs(change - exact_change);
            }
    return sum / (static_cast<double>(frames.size() - 1) * width * height);
}

} // namespace oa::present::world_renderer
