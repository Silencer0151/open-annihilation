// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// Views in whole numbers. A planned view keeps its centre in half map
// pixels and its height in map pixels, and is kept on the map by the same
// rules as clamp_view (director.hpp), worked out exactly: a view the
// planner writes is one clamp_view leaves unchanged, so compiling a
// planned script gives no clamping warning. Points are framed within a part
// of the view, and views compared by how much they overlap.

#include "oa/media/director.hpp"
#include "planner_internal.hpp"
#include "planner_tuning.hpp"

#include <algorithm>

namespace oa::media::director::planning {
namespace {

/// The smallest view height, in whole map pixels.
constexpr int64_t smallest_height{static_cast<int64_t>(min_view_height)};
static_assert(static_cast<double>(smallest_height) == min_view_height);
/// Percent: the whole of something.
constexpr int64_t percent{100};
/// Tenths in a half: a half pixel written with one decimal place.
constexpr int64_t half_in_tenths{5};

/// Returns a value held within [low, high].
///
/// @param value the value
/// @param low the smallest result
/// @param high the largest result, at least low
/// @return the held value
int64_t held(int64_t value, int64_t low, int64_t high) noexcept {
    return std::min(std::max(value, low), high);
}

/// Returns a centre held within [low, high], in half pixels, then moved by
/// half a pixel when that keeps it within them and makes it whole.
///
/// @param value the centre, in half pixels
/// @param low the smallest centre
/// @param high the largest centre, at least low
/// @return the held centre
int64_t held_centre(int64_t value, int64_t low, int64_t high) noexcept {
    const int64_t kept{held(value, low, high)};
    if (kept % 2 == 0)
        return kept;
    if (kept - 1 >= low)
        return kept - 1;
    if (kept + 1 <= high)
        return kept + 1;
    return kept;
}

/// Returns a centre coordinate as a script decimal.
///
/// @param doubled the coordinate, in half pixels
/// @return a whole number, or a half with one decimal place
oa::formats::oascript::Decimal decimal_of_half(int64_t doubled) noexcept {
    if (doubled % 2 == 0)
        return oa::formats::oascript::Decimal{doubled / 2, 0};
    return oa::formats::oascript::Decimal{doubled * half_in_tenths, 1};
}

} // namespace

Point ground_of(WorldPoint point) noexcept {
    return Point{point.x, ground_row(point)};
}

int64_t floor_div(int64_t numerator, int64_t denominator) noexcept {
    const int64_t quotient{numerator / denominator};
    return (numerator % denominator != 0 && numerator < 0) ? quotient - 1 : quotient;
}

int64_t ceil_div(int64_t numerator, int64_t denominator) noexcept {
    return -floor_div(-numerator, denominator);
}

uint64_t integer_sqrt(uint64_t value) noexcept {
    uint64_t root{};
    uint64_t bit{uint64_t{1} << 62};
    while (bit > value)
        bit >>= 2;
    uint64_t rest{value};
    while (bit != 0) {
        if (rest >= root + bit) {
            rest -= root + bit;
            root = (root >> 1) + bit;
        } else {
            root >>= 1;
        }
        bit >>= 2;
    }
    return root;
}

int64_t distance_squared(Point a, Point b) noexcept {
    const int64_t dx{int64_t{a.x} - b.x};
    const int64_t drow{int64_t{a.row} - b.row};
    return dx * dx + drow * drow;
}

int64_t distance(Point a, Point b) noexcept {
    return static_cast<int64_t>(integer_sqrt(static_cast<uint64_t>(distance_squared(a, b))));
}

Stage make_stage(const TimelineHeader& header, const PlannerSettings& settings) noexcept {
    Stage stage{};
    stage.output_width = std::max<int64_t>(1, settings.width);
    stage.output_height = std::max<int64_t>(1, settings.height);
    stage.map_width = std::max<int64_t>(0, header.map_width);
    stage.map_height = std::max<int64_t>(0, header.map_height);
    const int64_t across{stage.map_width * stage.output_height / stage.output_width};
    stage.fit = std::max(smallest_height, std::min(stage.map_height, across));
    return stage;
}

int64_t view_width(const Stage& stage, int64_t height) noexcept {
    return height * stage.output_width / stage.output_height;
}

PlannedView clamp_planned(const Stage& stage, PlannedView view) noexcept {
    PlannedView kept{};
    kept.height = held(view.height, smallest_height, stage.fit);
    const int64_t width_scaled{kept.height * stage.output_width};
    if (width_scaled >= stage.map_width * stage.output_height) {
        kept.x2 = stage.map_width;
    } else {
        const int64_t low{ceil_div(width_scaled, stage.output_height)};
        kept.x2 = held_centre(view.x2, low, 2 * stage.map_width - low);
    }
    if (kept.height >= stage.map_height)
        kept.z2 = stage.map_height;
    else
        kept.z2 = held_centre(view.z2, kept.height, 2 * stage.map_height - kept.height);
    return kept;
}

PlannedView whole_map(const Stage& stage) noexcept {
    return clamp_planned(stage, PlannedView{stage.map_width, stage.map_height, stage.fit});
}

namespace {

/// A bounding box of points.
struct Bounds {
    int32_t left{};
    int32_t right{};
    int32_t top{};
    int32_t bottom{};
};

/// Returns the bounding box of points.
///
/// @param points the points; at least one
/// @return their smallest and largest column and row
Bounds bounds_of(std::span<const Point> points) noexcept {
    Bounds box{points.front().x, points.front().x, points.front().row, points.front().row};
    for (const Point& point : points) {
        box.left = std::min(box.left, point.x);
        box.right = std::max(box.right, point.x);
        box.top = std::min(box.top, point.row);
        box.bottom = std::max(box.bottom, point.row);
    }
    return box;
}

/// Returns the height a view needs to hold points within a part of it.
///
/// @param stage the stage
/// @param points the points; at least one
/// @param area_percent the part of each side, 1 to 100
/// @return the larger of the rows and the columns the points span, each
///         scaled to fill area_percent of the view, rounded up
int64_t
height_within(const Stage& stage, std::span<const Point> points, int64_t area_percent) noexcept {
    const Bounds box{bounds_of(points)};
    const int64_t columns{int64_t{box.right} - box.left};
    const int64_t rows{int64_t{box.bottom} - box.top};
    const int64_t by_rows{ceil_div(rows * percent, area_percent)};
    const int64_t by_columns{
        ceil_div(columns * stage.output_height * percent, stage.output_width * area_percent)
    };
    return std::max(by_rows, by_columns);
}

} // namespace

int64_t framed_height(const Stage& stage, std::span<const Point> points) noexcept {
    if (points.empty())
        return 0;
    return height_within(stage, points, safe_area_percent);
}

std::optional<PlannedView> frame_points_within(
    const Stage& stage,
    std::span<const Point> points,
    int64_t area_percent,
    int64_t min_height,
    int64_t max_height
) {
    if (points.empty())
        return std::nullopt;
    const Bounds box{bounds_of(points)};
    PlannedView view{};
    view.x2 = 2 * floor_div(int64_t{box.left} + box.right, 2);
    view.z2 = 2 * floor_div(int64_t{box.top} + box.bottom, 2);
    view.height = held(
        height_within(stage, points, std::clamp<int64_t>(area_percent, 1, percent)),
        min_height,
        std::max(min_height, max_height)
    );
    return clamp_planned(stage, view);
}

std::optional<PlannedView> frame_points(
    const Stage& stage, std::span<const Point> points, int64_t min_height, int64_t max_height
) {
    return frame_points_within(stage, points, safe_area_percent, min_height, max_height);
}

PlannedView limit_zoom(
    const Stage& stage, const PlannedView& from, const PlannedView& to, uint32_t ticks
) noexcept {
    const int64_t factor{
        percent + int64_t{max_zoom_change_percent} * ticks / tuning::ticks_per_second
    };
    const int64_t highest{from.height * factor / percent};
    const int64_t lowest{ceil_div(from.height * percent, factor)};
    PlannedView limited{to};
    limited.height = held(to.height, lowest, std::max(lowest, highest));
    return clamp_planned(stage, limited);
}

bool within_part(
    const Stage& stage, const PlannedView& view, Point point, int64_t area_percent
) noexcept {
    const int64_t across{2 * int64_t{point.x} - view.x2};
    const int64_t down{2 * int64_t{point.row} - view.z2};
    return (across < 0 ? -across : across) * percent <=
               view_width(stage, view.height) * area_percent &&
           (down < 0 ? -down : down) * percent <= view.height * area_percent;
}

bool in_safe_area(const Stage& stage, const PlannedView& view, Point point) noexcept {
    return within_part(stage, view, point, safe_area_percent);
}

bool near_enough_to_pan(
    const Stage& stage, const PlannedView& from, const PlannedView& to
) noexcept {
    const int64_t across{to.x2 - from.x2};
    const int64_t down{to.z2 - from.z2};
    const int64_t apart{across * across + down * down};
    const int64_t reach{2 * view_width(stage, from.height) * pan_reach_percent};
    if (apart * percent * percent > reach * reach)
        return false;
    const int64_t low{std::min(from.height, to.height)};
    const int64_t high{std::max(from.height, to.height)};
    return high <= low * tuning::pan_height_ratio;
}

int64_t overlap_percent(const Stage& stage, const PlannedView& a, const PlannedView& b) noexcept {
    // Edges in half pixels: a view spans its width and height about its
    // doubled centre.
    const int64_t a_width{view_width(stage, a.height)};
    const int64_t b_width{view_width(stage, b.height)};
    const int64_t left{std::max(a.x2 - a_width, b.x2 - b_width)};
    const int64_t right{std::min(a.x2 + a_width, b.x2 + b_width)};
    const int64_t top{std::max(a.z2 - a.height, b.z2 - b.height)};
    const int64_t bottom{std::min(a.z2 + a.height, b.z2 + b.height)};
    if (right <= left || bottom <= top)
        return 0;
    const int64_t shared{(right - left) * (bottom - top)};
    const int64_t either{4 * a_width * a.height + 4 * b_width * b.height - shared};
    return either > 0 ? shared * percent / either : 0;
}

int64_t water_percent(const Analysis& analysis, const PlannedView& view) noexcept {
    const int64_t columns{analysis.water_columns};
    const int64_t rows{analysis.water_rows};
    if (columns <= 0 || rows <= 0)
        return 0;
    // The squares the view covers: its edges in map pixels, then squares.
    const int64_t width{view_width(analysis.stage, view.height)};
    const int64_t square{water_square_pixels};
    const int64_t first_column{
        std::clamp<int64_t>(floor_div(view.x2 - width, 2 * square), 0, columns - 1)
    };
    const int64_t last_column{
        std::clamp<int64_t>(floor_div(view.x2 + width - 1, 2 * square), 0, columns - 1)
    };
    const int64_t first_row{
        std::clamp<int64_t>(floor_div(view.z2 - view.height, 2 * square), 0, rows - 1)
    };
    const int64_t last_row{
        std::clamp<int64_t>(floor_div(view.z2 + view.height - 1, 2 * square), 0, rows - 1)
    };
    const auto sum_at{[&](int64_t column, int64_t row) {
        return analysis.water_sums
            [static_cast<size_t>(row) * static_cast<size_t>(columns + 1) +
             static_cast<size_t>(column)];
    }};
    const int64_t total{
        sum_at(last_column + 1, last_row + 1) - sum_at(first_column, last_row + 1) -
        sum_at(last_column + 1, first_row) + sum_at(first_column, first_row)
    };
    const int64_t count{(last_column - first_column + 1) * (last_row - first_row + 1)};
    return total / count;
}

int64_t water_at(const Analysis& analysis, Point at) noexcept {
    const int64_t column{floor_div(at.x, water_square_pixels)};
    const int64_t row{floor_div(at.row, water_square_pixels)};
    if (column < 0 || row < 0 || column >= analysis.water_columns || row >= analysis.water_rows)
        return 0;
    const auto sum_at{[&](int64_t x, int64_t z) {
        return analysis.water_sums
            [static_cast<size_t>(z) * static_cast<size_t>(analysis.water_columns + 1) +
             static_cast<size_t>(x)];
    }};
    return sum_at(column + 1, row + 1) - sum_at(column, row + 1) - sum_at(column + 1, row) +
           sum_at(column, row);
}

PlannedView settle_on_land(
    const Analysis& analysis,
    std::span<const Point> points,
    const PlannedView& view,
    int64_t keep_percent
) {
    const Stage& stage{analysis.stage};
    if (points.empty() || analysis.water_columns <= 0)
        return view;
    const Bounds box{bounds_of(points)};
    // The centres, doubled, that keep the points within the kept part: a
    // point may lie half that part of the view from the centre.
    const int64_t keep{std::clamp<int64_t>(keep_percent, 0, safe_area_percent)};
    const int64_t reach_x{view_width(stage, view.height) * keep / percent};
    const int64_t reach_z{view.height * keep / percent};
    const int64_t low_x{2 * int64_t{box.right} - reach_x};
    const int64_t high_x{2 * int64_t{box.left} + reach_x};
    const int64_t low_z{2 * int64_t{box.bottom} - reach_z};
    const int64_t high_z{2 * int64_t{box.top} + reach_z};
    if (low_x > high_x || low_z > high_z)
        return view;
    const int64_t steps{tuning::water_search_steps};
    PlannedView best{view};
    int64_t best_water{water_percent(analysis, view)};
    int64_t best_distance{0};
    for (int64_t row{}; row < steps; ++row)
        for (int64_t column{}; column < steps; ++column) {
            PlannedView tried{view};
            tried.x2 = 2 * floor_div(low_x + (high_x - low_x) * column / (steps - 1), 2);
            tried.z2 = 2 * floor_div(low_z + (high_z - low_z) * row / (steps - 1), 2);
            const PlannedView kept{clamp_planned(stage, tried)};
            // Keeping the view on the map may move the points out of it.
            if (kept != tried && !std::all_of(points.begin(), points.end(), [&](Point point) {
                    return within_part(stage, kept, point, keep);
                }))
                continue;
            tried = kept;
            const int64_t water{water_percent(analysis, tried)};
            const int64_t moved{
                (tried.x2 > view.x2 ? tried.x2 - view.x2 : view.x2 - tried.x2) +
                (tried.z2 > view.z2 ? tried.z2 - view.z2 : view.z2 - tried.z2)
            };
            if (water < best_water || (water == best_water && moved < best_distance)) {
                best = tried;
                best_water = water;
                best_distance = moved;
            }
        }
    return best;
}

oa::formats::oascript::CameraState camera_of(const PlannedView& view) {
    oa::formats::oascript::CameraState camera{};
    camera.position.x = decimal_of_half(view.x2);
    camera.position.y = oa::formats::oascript::Decimal{view.height, 0};
    camera.position.z = decimal_of_half(view.z2);
    return camera;
}

} // namespace oa::media::director::planning
