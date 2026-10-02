// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// The area pass: exact area weights in 16.16 fixed point, filtered in bands.
#include "oa/present/world_renderer/scene_filter.hpp"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>

namespace oa::present::world_renderer {
namespace {

/// Bits the row averages keep below a channel's 8: a 16.16 weighted sum of
/// 8-bit channels, shifted down by this, is an 8.8 row average.
constexpr uint32_t row_average_shift = 8;

/// Half a unit of the last bit a row average drops, added before the shift.
constexpr uint32_t row_average_half = 1U << (row_average_shift - 1U);

/// Bits a 16.16-weighted sum of 8.8 row averages holds below a channel's 8.
constexpr uint32_t result_shift = 24;

/// Half a channel level at result_shift bits, added before the shift.
constexpr uint32_t result_half = 1U << (result_shift - 1U);

/// Picture columns of each strip a band is filtered in: the row averages of
/// one strip stay on the stack.
constexpr uint32_t strip_columns = 256;

/// Scene rows of row averages a strip keeps: the at most area_taps_max rows
/// one picture row's footprint covers, and the one the next row may share.
constexpr uint32_t ring_rows = area_taps_max + 1U;

/// Row averages one ring row holds: every channel of a strip's columns.
constexpr uint32_t ring_row_values = strip_columns * area_pixel_bytes;

/// Marks a ring row that holds no scene row's averages yet.
constexpr uint32_t ring_row_empty = UINT32_MAX;

// The largest sums fit 32 bits: a row average of 255 is 255 x 256 at 8.8,
// and full weight on it is area_fixed_one.
static_assert(
    uint64_t{area_fixed_one} * 255U * area_fixed_one / (1U << row_average_shift) + result_half <=
    UINT32_MAX
);
// A row average of 255 at full weight, rounded, fits 16 bits.
static_assert(
    (uint64_t{area_fixed_one} * 255U + row_average_half) >> row_average_shift <= UINT16_MAX
);
// The footprint bounds of every picture column fit 32 bits.
static_assert(uint64_t{area_picture_edge_limit} * area_fixed_one <= UINT32_MAX);
// Every byte offset into a scene of the largest size fits 31 bits.
static_assert(
    uint64_t{area_picture_edge_limit} * 2U * area_stride_limit * area_pixel_bytes <= INT32_MAX
);

/// The first scene pixel a footprint covers, the last, and its bounds.
struct Footprint {
    uint32_t start{}; ///< first 16.16 screen unit the footprint covers
    uint32_t end{};   ///< 16.16 screen unit past the footprint
    uint32_t first{}; ///< first scene pixel it covers
    uint32_t last{};  ///< last scene pixel it covers
};

/// Returns the footprint of picture column (or row) `index`.
///
/// Positions are counted in 16.16 screen units: the picture pixel covers
/// [index x 65536, (index + 1) x 65536) and scene pixel i covers
/// [i x scale, (i + 1) x scale).
///
/// @param scale screen pixels per scene pixel, 16.16, from area_scale_min to area_scale_max
/// @param index picture column or row, below area_picture_edge_limit
/// @return the footprint's bounds and the scene pixels it covers
Footprint footprint_of(uint32_t scale, uint32_t index) noexcept {
    Footprint footprint;
    footprint.start = index * area_fixed_one;
    footprint.end = footprint.start + area_fixed_one;
    footprint.first = footprint.start / scale;
    footprint.last = (footprint.end - 1U) / scale;
    return footprint;
}

/// Returns the most scene pixels any footprint of an axis covers.
///
/// @param scale screen pixels per scene pixel, 16.16, from area_scale_min to area_scale_max
/// @param extent picture columns (or rows), 1 to area_picture_edge_limit
/// @return from 1 to area_taps_max
uint32_t taps_of_axis(uint32_t scale, uint32_t extent) noexcept {
    uint32_t taps = 1;
    for (uint32_t index = 0; index < extent; ++index) {
        const Footprint footprint = footprint_of(scale, index);
        taps = std::max(taps, footprint.last - footprint.first + 1U);
    }
    return taps;
}

/// Fills one axis's taps, each of `taps` entries lying inside the scene.
///
/// @param[out] axis one tap per picture column (or row)
/// @param scale screen pixels per scene pixel, 16.16, from area_scale_min to area_scale_max
/// @param extent picture columns (or rows), 1 to area_picture_edge_limit
/// @param taps entries of every tap, from taps_of_axis
void fill_axis(std::vector<AreaTap>& axis, uint32_t scale, uint32_t extent, uint32_t taps) {
    const uint32_t scene_extent = area_scene_extent(scale, extent);
    axis.resize(extent);
    for (uint32_t index = 0; index < extent; ++index) {
        const Footprint footprint = footprint_of(scale, index);
        AreaTap tap;
        // A footprint whose entries would run past the scene starts earlier,
        // with zero weights in front: never further back than its uncovered
        // entries, since its last pixel lies inside the scene.
        const uint32_t past_scene =
            footprint.first + taps > scene_extent ? footprint.first + taps - scene_extent : 0U;
        tap.first = footprint.first - past_scene;
        for (uint32_t pixel = footprint.first; pixel <= footprint.last; ++pixel) {
            const uint32_t covered_start = std::max(footprint.start, pixel * scale);
            const uint32_t covered_end = std::min(footprint.end, (pixel + 1U) * scale);
            tap.weights[pixel - tap.first] = covered_end - covered_start;
        }
        axis[index] = tap;
    }
}

/// Checks a frame against its plan before any pixel is written.
///
/// @param plan weights built by plan_area_filter
/// @param scene the scene
/// @param picture the picture
/// @return AreaError::none when the frame can be filtered
AreaError
check_frame(const AreaPlan& plan, const RgbSource& scene, const RgbTarget& picture) noexcept {
    if (plan.scale() == 0 || plan.columns().size() != plan.picture_width() ||
        plan.rows().size() != plan.picture_height())
        return AreaError::no_plan;
    if (scene.rgb == nullptr || picture.rgb == nullptr)
        return AreaError::missing_pixels;
    if (picture.width != plan.picture_width() || picture.height != plan.picture_height())
        return AreaError::picture_mismatch;
    if (scene.width < plan.scene_width() || scene.height < plan.scene_height())
        return AreaError::scene_too_small;
    if (scene.stride_pixels < scene.width || scene.stride_pixels > area_stride_limit ||
        picture.stride_pixels < picture.width || picture.stride_pixels > area_stride_limit)
        return AreaError::stride_out_of_range;
    return AreaError::none;
}

/// Averages one scene row across the footprints of a strip's columns.
///
/// Every column tap has ColumnTaps entries, so the columns are summed with
/// no branch.
///
/// @param columns the taps of the strip's first column and those after it
/// @param scene_row the scene row's first pixel
/// @param count columns of the strip
/// @param[out] averages each column's red, green and blue row averages, 8.8
template <uint32_t ColumnTaps>
void average_scene_row(
    const AreaTap* columns, const uint8_t* scene_row, uint32_t count, uint16_t* averages
) noexcept {
    for (uint32_t x = 0; x < count; ++x, averages += area_pixel_bytes) {
        const AreaTap& column_tap = columns[x];
        const uint8_t* pixel = scene_row + std::size_t{column_tap.first} * area_pixel_bytes;
        uint32_t red = 0;
        uint32_t green = 0;
        uint32_t blue = 0;
        for (uint32_t entry = 0; entry < ColumnTaps; ++entry, pixel += area_pixel_bytes) {
            const uint32_t weight = column_tap.weights[entry];
            red += weight * pixel[0];
            green += weight * pixel[1];
            blue += weight * pixel[2];
        }
        averages[0] = static_cast<uint16_t>((red + row_average_half) >> row_average_shift);
        averages[1] = static_cast<uint16_t>((green + row_average_half) >> row_average_shift);
        averages[2] = static_cast<uint16_t>((blue + row_average_half) >> row_average_shift);
    }
}

/// Sums the row averages of one picture row's footprint and rounds them to its channels.
///
/// @param averages the row averages of each of the footprint's Rows rows of nonzero weight
/// @param weights the weight of each of those rows, 16.16
/// @param values channels of the strip's columns
/// @param[out] out the strip's first channel in the picture row
template <uint32_t Rows>
void sum_row_averages(
    const std::array<const uint16_t*, area_taps_max>& averages,
    const std::array<uint32_t, area_taps_max>& weights,
    uint32_t values,
    uint8_t* out
) noexcept {
    // Copied out of the arrays, which a byte store could otherwise change.
    const uint16_t* const first = averages[0];
    const uint16_t* const second = averages[Rows > 1 ? 1 : 0];
    const uint16_t* const third = averages[Rows > 2 ? 2 : 0];
    const uint32_t first_weight = weights[0];
    const uint32_t second_weight = weights[1];
    const uint32_t third_weight = weights[2];
    for (uint32_t value = 0; value < values; ++value) {
        uint32_t sum = first_weight * first[value] + result_half;
        if constexpr (Rows > 1)
            sum += second_weight * second[value];
        if constexpr (Rows > 2)
            sum += third_weight * third[value];
        out[value] = static_cast<uint8_t>(sum >> result_shift);
    }
}

/// Filters picture rows [row_begin, row_end) of a checked frame.
///
/// The picture is filtered strip by strip of strip_columns columns. Within a
/// strip each scene row the rows need is averaged across the columns once,
/// into a ring of ring_rows rows on the stack, and each picture row then
/// sums the row averages of its footprint's rows of nonzero weight.
///
/// @param plan weights built by plan_area_filter, with ColumnTaps column entries
/// @param scene the scene, checked against the plan
/// @param[out] picture the picture, checked against the plan
/// @param row_begin first picture row to filter
/// @param row_end picture row past the last to filter
template <uint32_t ColumnTaps>
void filter_rows(
    const AreaPlan& plan,
    const RgbSource& scene,
    const RgbTarget& picture,
    uint32_t row_begin,
    uint32_t row_end
) noexcept {
    const std::size_t scene_row_bytes = std::size_t{scene.stride_pixels} * area_pixel_bytes;
    const std::size_t picture_row_bytes = std::size_t{picture.stride_pixels} * area_pixel_bytes;
    const AreaTap* const columns = plan.columns().data();
    const AreaTap* const rows = plan.rows().data();
    const uint32_t width = plan.picture_width();
    std::array<uint16_t, ring_rows * ring_row_values> ring{};
    for (uint32_t strip_begin = 0; strip_begin < width; strip_begin += strip_columns) {
        const uint32_t count = std::min(strip_columns, width - strip_begin);
        const uint32_t values = count * area_pixel_bytes;
        std::array<uint32_t, ring_rows> ring_scene_rows{};
        ring_scene_rows.fill(ring_row_empty);
        uint8_t* out = picture.rgb + row_begin * picture_row_bytes +
                       std::size_t{strip_begin} * area_pixel_bytes;
        for (uint32_t y = row_begin; y < row_end; ++y, out += picture_row_bytes) {
            const AreaTap& row_tap = rows[y];
            std::array<const uint16_t*, area_taps_max> averages{};
            std::array<uint32_t, area_taps_max> row_weights{};
            uint32_t row_count = 0;
            for (uint32_t entry = 0; entry < plan.row_taps(); ++entry) {
                if (row_tap.weights[entry] == 0)
                    continue;
                const uint32_t scene_row = row_tap.first + entry;
                const uint32_t slot = scene_row % ring_rows;
                uint16_t* const slot_values = ring.data() + std::size_t{slot} * ring_row_values;
                if (ring_scene_rows[slot] != scene_row) {
                    average_scene_row<ColumnTaps>(
                        columns + strip_begin,
                        scene.rgb + scene_row * scene_row_bytes,
                        count,
                        slot_values
                    );
                    ring_scene_rows[slot] = scene_row;
                }
                averages[row_count] = slot_values;
                row_weights[row_count] = row_tap.weights[entry];
                ++row_count;
            }
            switch (row_count) {
            case 1:
                sum_row_averages<1>(averages, row_weights, values, out);
                break;
            case 2:
                sum_row_averages<2>(averages, row_weights, values, out);
                break;
            default:
                sum_row_averages<area_taps_max>(averages, row_weights, values, out);
                break;
            }
        }
    }
}

/// Filters picture rows [row_begin, row_end) of a checked frame with the plan's column entries.
///
/// @param plan weights built by plan_area_filter
/// @param scene the scene, checked against the plan
/// @param[out] picture the picture, checked against the plan
/// @param row_begin first picture row to filter
/// @param row_end picture row past the last to filter
void filter_checked_rows(
    const AreaPlan& plan,
    const RgbSource& scene,
    const RgbTarget& picture,
    uint32_t row_begin,
    uint32_t row_end
) noexcept {
    switch (plan.column_taps()) {
    case 1:
        filter_rows<1>(plan, scene, picture, row_begin, row_end);
        break;
    case 2:
        filter_rows<2>(plan, scene, picture, row_begin, row_end);
        break;
    default:
        filter_rows<area_taps_max>(plan, scene, picture, row_begin, row_end);
        break;
    }
}

} // namespace

uint32_t area_scene_extent(uint32_t scale, uint32_t picture_extent) noexcept {
    if (scale < area_scale_min || scale > area_scale_max ||
        picture_extent > area_picture_edge_limit)
        return 0;
    const uint32_t span = picture_extent * area_fixed_one;
    return span / scale + (span % scale != 0 ? 1U : 0U);
}

AreaError
plan_area_filter(AreaPlan& plan, uint32_t scale, uint32_t picture_width, uint32_t picture_height) {
    plan.scale_ = 0;
    plan.picture_width_ = 0;
    plan.picture_height_ = 0;
    plan.scene_width_ = 0;
    plan.scene_height_ = 0;
    plan.column_taps_ = 0;
    plan.row_taps_ = 0;
    plan.columns_.clear();
    plan.rows_.clear();
    if (scale < area_scale_min || scale > area_scale_max)
        return AreaError::scale_out_of_range;
    if (picture_width == 0 || picture_height == 0)
        return AreaError::empty_picture;
    if (picture_width > area_picture_edge_limit || picture_height > area_picture_edge_limit)
        return AreaError::picture_too_large;
    const uint32_t column_taps = taps_of_axis(scale, picture_width);
    const uint32_t row_taps = taps_of_axis(scale, picture_height);
    fill_axis(plan.columns_, scale, picture_width, column_taps);
    fill_axis(plan.rows_, scale, picture_height, row_taps);
    plan.picture_width_ = picture_width;
    plan.picture_height_ = picture_height;
    plan.scene_width_ = area_scene_extent(scale, picture_width);
    plan.scene_height_ = area_scene_extent(scale, picture_height);
    plan.column_taps_ = column_taps;
    plan.row_taps_ = row_taps;
    plan.scale_ = scale;
    return AreaError::none;
}

AreaError area_filter_rgb24_rows(
    const AreaPlan& plan,
    const RgbSource& scene,
    const RgbTarget& picture,
    uint32_t row_begin,
    uint32_t row_end
) noexcept {
    const AreaError error = check_frame(plan, scene, picture);
    if (error != AreaError::none)
        return error;
    if (row_begin > row_end || row_end > picture.height)
        return AreaError::rows_out_of_range;
    filter_checked_rows(plan, scene, picture, row_begin, row_end);
    return AreaError::none;
}

AreaError area_filter_rgb24(
    const AreaPlan& plan,
    const RgbSource& scene,
    const RgbTarget& picture,
    platform::job_pool::Pool* pool
) {
    const AreaError error = check_frame(plan, scene, picture);
    if (error != AreaError::none)
        return error;
    const uint32_t height = picture.height;
    platform::job_pool::run_bands(
        pool, platform::job_pool::bands_of_rows(height, area_band_rows), [&](uint32_t band) {
            const uint32_t row_begin = band * area_band_rows;
            filter_checked_rows(
                plan, scene, picture, row_begin, std::min(height, row_begin + area_band_rows)
            );
        }
    );
    return AreaError::none;
}

} // namespace oa::present::world_renderer
