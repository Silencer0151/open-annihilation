// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

// The area pass: reduces a scene drawn finer than the screen to the screen's
// picture, each screen pixel the average of the scene under its footprint
// weighted by the area it covers. It works in integers only: the weights are
// exact in 16.16 fixed point, and the bytes are the same on every platform,
// build type and number of threads. Beside it, the nearest resample turns a
// scene into the picture at any other scale, nearest-pixel, with the terrain
// fill's step.
//
// Beside them, the references of what the graphics card is asked to do with
// a scene, computed on the processor in double precision: its NEAREST and
// LINEAR scale modes, the sharp-bilinear filter (NEAREST into a prescale
// target, then LINEAR), its pixel-art filter and the overlay laid over the
// scaled picture; the exact footprint average they are measured against;
// the thickness of thin lines in a reduced scene; and the measures of how
// steady a moving picture stays. Tests and checks use the references; the
// game draws through the card.

#include "oa/platform/job_pool.hpp"

#include <array>
#include <cstdint>
#include <span>
#include <vector>

namespace oa::present::world_renderer {

/// One in the 16.16 fixed point of the area pass's scale and weights.
inline constexpr uint32_t area_fixed_one = 65536;

/// Smallest scale the area pass takes, 16.16: one screen pixel per two scene pixels.
inline constexpr uint32_t area_scale_min = area_fixed_one / 2;

/// Largest scale the area pass takes, 16.16: one screen pixel per scene pixel.
inline constexpr uint32_t area_scale_max = area_fixed_one;

/// Most scene pixels one screen pixel's footprint covers along either axis.
///
/// A footprint is one screen pixel long and a scene pixel at least half of
/// one, so it can touch the end of one scene pixel, the whole of a second and
/// the start of a third.
inline constexpr uint32_t area_taps_max = 3;

/// Most columns, and most rows, of the area pass's picture.
inline constexpr uint32_t area_picture_edge_limit = 8192;

/// Most pixels between the starts of two rows of a scene or picture.
///
/// Bounds every byte offset the pass computes, so none overflows a 32-bit
/// size: 2 x area_picture_edge_limit rows of this stride, 3 bytes a pixel,
/// stay under 2^31 bytes.
inline constexpr uint32_t area_stride_limit = 4 * area_picture_edge_limit;

/// Rows of the picture each band of area_filter_rgb24 filters.
inline constexpr uint32_t area_band_rows = 32;

/// Bytes of one pixel of a scene or picture: red, green, blue.
inline constexpr uint32_t area_pixel_bytes = 3;

/// The scene pixels one screen column (or row) averages, and their weights.
///
/// Every tap of a plan's axis has that axis's number of entries
/// (AreaPlan::column_taps or AreaPlan::row_taps). A footprint that covers
/// fewer scene pixels has zero weights for the rest; its first entry is moved
/// back where needed so that every entry lies inside the scene the plan
/// reads.
struct AreaTap {
    uint32_t first{}; ///< scene column (or row) of the first entry
    /// The part of the footprint each entry covers, 16.16; they sum to area_fixed_one.
    std::array<uint32_t, area_taps_max> weights{};
};

/// Why the area pass or the nearest resample refused a plan or a frame.
enum class AreaError {
    none,
    scale_out_of_range,  ///< scale below area_scale_min or above area_scale_max
    empty_picture,       ///< a picture with no columns or no rows
    picture_too_large,   ///< a picture wider or taller than area_picture_edge_limit
    no_plan,             ///< the plan was never built, or its build failed
    picture_mismatch,    ///< the picture's size is not the plan's
    scene_too_small,     ///< the scene is narrower or shorter than the plan reads
    stride_out_of_range, ///< a stride below its width or above area_stride_limit
    missing_pixels,      ///< a scene or picture with no storage
    rows_out_of_range,   ///< a band's rows are not within the picture
    phase_out_of_range,  ///< a phase above the scale: more than one scene pixel
};

/// Says what an error of the area pass or the nearest resample means, for messages.
///
/// @param error the error
/// @return static text; "none" for AreaError::none
[[nodiscard]] const char* area_error_text(AreaError error) noexcept;

class AreaPlan;

/// Builds the weights of the area pass for a scale, a picture size and the
/// place the picture starts in the scene.
///
/// Each weight is the length of the scene pixel the footprint covers, in
/// screen pixels, which at a 16.16 scale and a 16.16 phase is a whole number
/// of 16.16 units, so the weights of every footprint sum to exactly
/// area_fixed_one with no rounding. The plan's vectors keep their storage
/// between builds, so rebuilding at a size no larger allocates nothing.
///
/// A phase starts the picture that far into the scene, for a view drawn
/// between map pixels. A phase equal to the scale starts it one whole scene
/// pixel in: the picture is the one a phase of 0 gives of the scene without
/// its first column (or row).
///
/// @param[out] plan the weights; on failure, emptied, so that a frame refuses it
/// @param scale screen pixels per scene pixel, 16.16, within [area_scale_min, area_scale_max]
/// @param picture_width columns of the picture, 1 to area_picture_edge_limit
/// @param picture_height rows of the picture, 1 to area_picture_edge_limit
/// @param phase_x screen pixels the picture's first column starts past the
///        scene's left edge, 16.16, from 0 to the scale
/// @param phase_y screen pixels the picture's first row starts below the
///        scene's top edge, 16.16, from 0 to the scale
/// @return AreaError::none, or why the scale, size or phase was refused
[[nodiscard]] AreaError plan_area_filter(
    AreaPlan& plan,
    uint32_t scale,
    uint32_t picture_width,
    uint32_t picture_height,
    uint32_t phase_x = 0,
    uint32_t phase_y = 0
);

/// The weights of the area pass for one scale, picture size and phase,
/// built once and used for every frame drawn at them.
///
/// With no phase the scene starts at the picture's top-left corner: screen
/// column x covers scene columns [x / m, (x + 1) / m), where m = scale /
/// area_fixed_one, and rows alike. A phase p, in 16.16 screen pixels, moves
/// the picture into the scene: column x covers [(x + p) / m, (x + 1 + p) /
/// m). Only plan_area_filter fills a plan, so every tap of one lies inside
/// the scene it names; a plan never built, or whose build failed, is no
/// plan, and a frame refuses it.
class AreaPlan {
  public:

    /// Returns the screen pixels per scene pixel.
    ///
    /// @return 16.16; 0 for no plan
    [[nodiscard]] uint32_t scale() const noexcept { return scale_; }

    /// Returns the columns of the picture.
    ///
    /// @return 1 to area_picture_edge_limit; 0 for no plan
    [[nodiscard]] uint32_t picture_width() const noexcept { return picture_width_; }

    /// Returns the rows of the picture.
    ///
    /// @return 1 to area_picture_edge_limit; 0 for no plan
    [[nodiscard]] uint32_t picture_height() const noexcept { return picture_height_; }

    /// Returns how far the picture's first column starts past the scene's left edge.
    ///
    /// @return 16.16 screen pixels, from 0 to the scale; 0 for no plan
    [[nodiscard]] uint32_t phase_x() const noexcept { return phase_x_; }

    /// Returns how far the picture's first row starts below the scene's top edge.
    ///
    /// @return 16.16 screen pixels, from 0 to the scale; 0 for no plan
    [[nodiscard]] uint32_t phase_y() const noexcept { return phase_y_; }

    /// Returns the scene columns the picture's footprints cover.
    ///
    /// @return area_scene_extent of the scale, the picture's width and the
    ///         column phase; 0 for no plan
    [[nodiscard]] uint32_t scene_width() const noexcept { return scene_width_; }

    /// Returns the scene rows the picture's footprints cover.
    ///
    /// @return area_scene_extent of the scale, the picture's height and the
    ///         row phase; 0 for no plan
    [[nodiscard]] uint32_t scene_height() const noexcept { return scene_height_; }

    /// Returns the entries of every column tap: the most any column's footprint covers.
    ///
    /// @return 1 to area_taps_max; 0 for no plan
    [[nodiscard]] uint32_t column_taps() const noexcept { return column_taps_; }

    /// Returns the entries of every row tap: the most any row's footprint covers.
    ///
    /// @return 1 to area_taps_max; 0 for no plan
    [[nodiscard]] uint32_t row_taps() const noexcept { return row_taps_; }

    /// Returns the taps of the picture's columns.
    ///
    /// @return one tap per picture column; empty for no plan
    [[nodiscard]] const std::vector<AreaTap>& columns() const noexcept { return columns_; }

    /// Returns the taps of the picture's rows.
    ///
    /// @return one tap per picture row; empty for no plan
    [[nodiscard]] const std::vector<AreaTap>& rows() const noexcept { return rows_; }

  private:

    /// Builds the plan, as declared above the class.
    ///
    /// @param[out] plan the weights
    /// @param scale screen pixels per scene pixel, 16.16
    /// @param picture_width columns of the picture
    /// @param picture_height rows of the picture
    /// @param phase_x screen pixels the first column starts into the scene, 16.16
    /// @param phase_y screen pixels the first row starts into the scene, 16.16
    /// @return AreaError::none, or why the scale, size or phase was refused
    friend AreaError plan_area_filter(
        AreaPlan& plan,
        uint32_t scale,
        uint32_t picture_width,
        uint32_t picture_height,
        uint32_t phase_x,
        uint32_t phase_y
    );

    uint32_t scale_{};             ///< screen pixels per scene pixel, 16.16; 0 for no plan
    uint32_t picture_width_{};     ///< columns of the picture
    uint32_t picture_height_{};    ///< rows of the picture
    uint32_t phase_x_{};           ///< 16.16 screen pixels the first column starts into the scene
    uint32_t phase_y_{};           ///< 16.16 screen pixels the first row starts into the scene
    uint32_t scene_width_{};       ///< scene columns the picture's footprints cover
    uint32_t scene_height_{};      ///< scene rows the picture's footprints cover
    uint32_t column_taps_{};       ///< entries of every column tap
    uint32_t row_taps_{};          ///< entries of every row tap
    std::vector<AreaTap> columns_; ///< one tap per picture column
    std::vector<AreaTap> rows_;    ///< one tap per picture row
};

/// Read-only RGB pixels, area_pixel_bytes a pixel, in rows stride_pixels apart.
struct RgbSource {
    const uint8_t* rgb{};     ///< the first row's first pixel
    uint32_t width{};         ///< pixels a row
    uint32_t height{};        ///< rows
    uint32_t stride_pixels{}; ///< pixels from one row's start to the next's
};

/// Writable RGB pixels, area_pixel_bytes a pixel, in rows stride_pixels apart.
struct RgbTarget {
    uint8_t* rgb{};           ///< the first row's first pixel
    uint32_t width{};         ///< pixels a row
    uint32_t height{};        ///< rows
    uint32_t stride_pixels{}; ///< pixels from one row's start to the next's
};

/// Returns the scene columns (or rows) the footprints of a picture's columns (or rows) cover.
///
/// @param scale screen pixels per scene pixel, 16.16, within [area_scale_min, area_scale_max]
/// @param picture_extent columns (or rows) of the picture, at most area_picture_edge_limit
/// @param phase screen pixels the picture starts into the scene, 16.16, from 0 to the scale
/// @return (picture_extent x area_fixed_one + phase) / scale, rounded up; 0 for an
///         argument out of range
[[nodiscard]] uint32_t
area_scene_extent(uint32_t scale, uint32_t picture_extent, uint32_t phase = 0) noexcept;

/// Filters rows [row_begin, row_end) of the picture from the scene.
///
/// Each channel of each picture pixel is the area-weighted average of the
/// scene under its footprint. The pass is separable: each scene row under
/// the footprint is first averaged across the footprint's columns and kept
/// at 16 bits, 8 above the channel's, rounded to the nearest with halves up;
/// those row averages are then averaged across the footprint's rows and
/// rounded once more to the channel's 8 bits, to the nearest with halves up.
/// A pixel depends only on the plan and the scene, never on which rows are
/// filtered with it, so any split of the picture's rows gives the same
/// bytes. Each scene row the named rows need is averaged across the
/// columns once, in strips of columns whose row averages stay on the stack,
/// so nothing is allocated. Reads only the plan's scene_width x scene_height
/// corner of the scene; writes only the named rows of the picture, which
/// must not share storage with the scene.
///
/// @param plan weights built by plan_area_filter
/// @param scene the scene, at least the plan's scene_width x scene_height
/// @param[out] picture the picture, exactly the plan's picture_width x picture_height
/// @param row_begin first picture row to filter
/// @param row_end picture row past the last to filter, at most the picture's height
/// @return AreaError::none, or why the frame was refused, in which case nothing was written
[[nodiscard]] AreaError area_filter_rgb24_rows(
    const AreaPlan& plan,
    const RgbSource& scene,
    const RgbTarget& picture,
    uint32_t row_begin,
    uint32_t row_end
) noexcept;

/// Filters the whole picture from the scene, in bands of area_band_rows rows.
///
/// The bands run on the pool's threads when one is given, and in order on
/// the calling thread otherwise; the bytes are those area_filter_rgb24_rows
/// gives for the whole picture, whatever the number of threads.
///
/// @param plan weights built by plan_area_filter
/// @param scene the scene, at least the plan's scene_width x scene_height
/// @param[out] picture the picture, exactly the plan's picture_width x picture_height
/// @param pool threads to filter the bands on; null filters them on the calling thread
/// @return AreaError::none, or why the frame was refused, in which case nothing was written
[[nodiscard]] AreaError area_filter_rgb24(
    const AreaPlan& plan,
    const RgbSource& scene,
    const RgbTarget& picture,
    platform::job_pool::Pool* pool = nullptr
);

/// Rows of the picture each band of resample_nearest_rgb24 fills.
inline constexpr uint32_t resample_band_rows = 32;

/// Returns the scene columns (or rows) resample_nearest_rgb24 reads for a picture's columns (or
/// rows).
///
/// The last picture column x = picture_extent - 1 reads scene column
/// floor(x * area_fixed_one / scale_fp), where scale_fp is the scale in
/// 16.16, rounded to the nearest and at least 1; the scene must be wider
/// than that column.
///
/// @param scale picture pixels per scene pixel; non-positive means 1
/// @param picture_extent picture columns (or rows)
/// @return the last scene column read plus one; 0 for an empty picture
[[nodiscard]] uint64_t resample_scene_extent(float scale, uint32_t picture_extent) noexcept;

/// Writes the picture of a scene at another scale, nearest-pixel.
///
/// The scene starts at the picture's top-left corner. Picture pixel (x, y)
/// shows scene pixel (floor(x * area_fixed_one / scale_fp),
/// floor(y * area_fixed_one / scale_fp)), where scale_fp is the scale in
/// 16.16, rounded to the nearest and at least 1: the step fill_scaled_viewport
/// samples the map with, so the picture of a scene of the mosaic drawn at one
/// pixel per map pixel is the terrain fill_scaled_viewport draws at that
/// scale, and at scale 1 the picture is a copy of the scene's corner. The
/// rows are filled in bands of resample_band_rows rows, on the pool's
/// threads when one is given; every row is the same whichever thread fills
/// it. Reads only the scene pixels resample_scene_extent names; writes
/// nothing past each picture row's width. The picture must not share
/// storage with the scene. An empty picture is filled with nothing.
///
/// @param scene the scene, at least resample_scene_extent of the scale and the picture's width
///        by that of the scale and its height
/// @param scale picture pixels per scene pixel; non-positive means 1
/// @param[out] picture the picture, of any size
/// @param pool threads to fill the bands on; null fills them on the calling thread
/// @return AreaError::none, or why the frame was refused, in which case nothing was written:
///         missing_pixels, stride_out_of_range for a stride below its width, or
///         scene_too_small
[[nodiscard]] AreaError resample_nearest_rgb24(
    const RgbSource& scene,
    float scale,
    const RgbTarget& picture,
    platform::job_pool::Pool* pool = nullptr
);

/// Returns the exact area-weighted average of one channel of the scene under a screen pixel.
///
/// The reference the area pass is held to, computed in double precision
/// from the footprint's geometry rather than from a plan: screen pixel
/// (x, y) covers scene columns [x / scale, (x + 1) / scale) and rows alike,
/// and each scene pixel counts by the area of it the footprint covers.
/// Scene pixels outside the scene count as black.
///
/// @param scene the scene
/// @param scale screen pixels per scene pixel, above 0 and at most 1
/// @param x picture column
/// @param y picture row
/// @param channel 0 for red, 1 for green, 2 for blue
/// @return the average, from 0 to 255, unrounded
[[nodiscard]] double area_sample_reference(
    const RgbSource& scene, double scale, uint32_t x, uint32_t y, uint32_t channel
) noexcept;

/// Where a scene lands on a picture: picture point (offset_x + u * scale_x,
/// offset_y + v * scale_y) shows scene point (u, v). Pixel (x, y) of either
/// covers [x, x + 1) by [y, y + 1), its centre at (x + 0.5, y + 0.5).
struct ScenePlacement {
    double scale_x{1.0};  ///< picture pixels per scene pixel across, above 0
    double scale_y{1.0};  ///< picture pixels per scene pixel down, above 0
    double offset_x{0.0}; ///< picture column the scene's left edge lands on
    double offset_y{0.0}; ///< picture row the scene's top edge lands on
};

/// Draws the scene into the picture as the card's NEAREST scale mode does.
///
/// Each picture pixel shows the scene pixel under its centre, clamped to the
/// scene's edge.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands
/// @param[out] picture every pixel written
void nearest_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept;

/// Draws the scene into the picture as the card's LINEAR scale mode does.
///
/// Each picture pixel is the scene interpolated linearly between the
/// centres of the four scene pixels around its centre, clamped to the
/// scene's edge, rounded to the nearest level with halves up.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands
/// @param[out] picture every pixel written
void bilinear_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept;

/// Draws the scene into the picture by the sharp-bilinear filter.
///
/// The scene is drawn NEAREST into a prescale target `factor` times its
/// size, which is then drawn LINEAR into the picture: each scene pixel
/// becomes a solid block with a blended edge one picture pixel wide. A
/// factor of 1 is plain LINEAR; at a whole-number scale equal to the
/// factor, with the scene's edges on pixel edges, the picture is nearest
/// replication.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands
/// @param factor the prescale target's pixels per scene pixel, at least 1
/// @param[out] picture every pixel written
void sharp_bilinear_rgb24(
    const RgbSource& scene,
    const ScenePlacement& placement,
    uint32_t factor,
    const RgbTarget& picture
) noexcept;

/// Draws the scene into the picture as the card's pixel-art scale mode does.
///
/// For each axis the picture pixel's centre falls at scene coordinate t;
/// with b, the scene pixels a picture pixel spans, at most 1, the sample is
/// taken between scene pixels floor(t - b/2) and the next, at the weight
/// smoothstep(1 - b, 1, fract(t - b/2)) toward the next; scene pixels
/// beyond the edge are the edge's. Rounded to the nearest level with halves
/// up. At a whole-number scale with the scene's edges on pixel edges it is
/// nearest replication.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands
/// @param[out] picture every pixel written
void pixelart_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept;

/// Draws the scene into the picture as the card's two-level reduction does,
/// which reduces the Full tier's world target when the view is zoomed out:
/// the scene halved, each pixel of the half the mean of the four scene
/// pixels under it, drawn LINEAR at twice the scale, and the scene drawn
/// LINEAR over it at alpha 1 - t, t = log2(1 / scale) by the scale across,
/// which runs from one half to 1; both in exact arithmetic, the mix
/// rounded once to the nearest level with halves up. At one half the
/// picture is the half alone, a box of four scene pixels a picture pixel;
/// at 1 it is the scene itself; between, the weights change evenly with
/// the scale, so nothing pops as it eases. Scene pixels beyond the edge
/// are the edge's.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands, its scale across from one half to 1
/// @param[out] picture every pixel written
void two_level_rgb24(
    const RgbSource& scene, const ScenePlacement& placement, const RgbTarget& picture
) noexcept;

/// Lays an overlay over a picture: where an overlay pixel is opaque (alpha
/// 255) its colour replaces the picture's, and elsewhere the picture shows.
///
/// @param[in,out] picture the picture
/// @param overlay 0xAARRGGBB words, rows overlay_stride_pixels apart, at
///        least the picture's size
/// @param overlay_stride_pixels words from one overlay row's start to the next's
void overlay_rgb24(
    const RgbTarget& picture, const uint32_t* overlay, uint32_t overlay_stride_pixels
) noexcept;

/// Returns the exact average of one channel of the scene under a picture pixel's footprint.
///
/// The footprint is the scene's part the picture pixel covers; each scene
/// pixel counts by the area of it the footprint covers, and scene pixels
/// beyond the scene's edge are the edge's.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands
/// @param x picture column
/// @param y picture row
/// @param channel 0 for red, 1 for green, 2 for blue
/// @return the average, from 0 to 255, unrounded
[[nodiscard]] double footprint_sample_reference(
    const RgbSource& scene,
    const ScenePlacement& placement,
    uint32_t x,
    uint32_t y,
    uint32_t channel
) noexcept;

/// Returns how thick a line is drawn into a scene so that it stays about
/// one screen pixel thick once the scene is reduced to the screen: the
/// draw scale over the zoom, rounded to the nearest, at least 1; 1 where
/// the scene is not reduced.
///
/// @param draw_scale scene pixels per map pixel
/// @param zoom screen pixels per map pixel
/// @return the line's thickness in scene pixels
[[nodiscard]] int32_t scene_line_thickness(float draw_scale, float zoom) noexcept;

/// Returns the light of one channel of a picture: the sum of its levels in
/// units of a full-brightness pixel, which a thin line's picture keeps
/// constant wherever it falls when nothing of it is lost or doubled.
///
/// @param picture the picture
/// @param channel 0 for red, 1 for green, 2 for blue
/// @return the sum of the levels over 255
[[nodiscard]] double line_energy(const RgbSource& picture, uint32_t channel) noexcept;

/// The exact values of one channel of a picture, row after row.
struct ExactChannel {
    std::vector<double> levels; ///< width x height levels from 0 to 255, unrounded
    uint32_t width{};           ///< columns
    uint32_t height{};          ///< rows
};

/// Returns the exact picture of one channel of a scene: footprint_sample_reference at every pixel.
///
/// @param scene the scene, at least one pixel
/// @param placement where the scene lands
/// @param width the picture's columns
/// @param height the picture's rows
/// @param channel 0 for red, 1 for green, 2 for blue
/// @return the exact levels
[[nodiscard]] ExactChannel exact_channel(
    const RgbSource& scene,
    const ScenePlacement& placement,
    uint32_t width,
    uint32_t height,
    uint32_t channel
);

/// Returns how much a run of frames shimmers: the mean, over each pair of
/// consecutive frames and each pixel, of how far the pixel's change from
/// one frame to the next departs from the change of the exact pictures of
/// the same moments. A filter whose frames follow the exact pictures'
/// changes, as the view moves, does not shimmer, whatever its constant
/// blur.
///
/// @param frames the frames, in order, all one size
/// @param exact the exact picture of each frame's moment, of the same size
/// @param channel 0 for red, 1 for green, 2 for blue
/// @return the mean departure in levels; 0 for fewer than two frames or
///     frames and exact pictures that do not pair up
[[nodiscard]] double shimmer(
    std::span<const RgbSource> frames, std::span<const ExactChannel> exact, uint32_t channel
) noexcept;

} // namespace oa::present::world_renderer
