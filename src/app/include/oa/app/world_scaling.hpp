// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

// How a match frame draws the battlefield: the draw scale, the scene pixels
// per map pixel, apart from the zoom, the screen pixels per map pixel, and
// the size of the scene the terrain, the draws and the fog go into; the
// draw scale and method the accelerated tier draws it at, within its scene
// budget; and how far between map pixels that tier draws the view as it
// scrolls and zooms, while the camera itself stays on whole map pixels.
#pragma once

#include "oa/app/render_policy.hpp"

#include <cstdint>
#include <optional>

namespace oa::app {

/// How a scene drawn apart from the world layer becomes the battlefield's picture.
enum class SceneMethod : uint8_t {
    none,    ///< the scene is the world layer, drawn at the zoom
    nearest, ///< resampled nearest into the world layer at the zoom
    area,    ///< averaged into the world layer by the exact area pass, below zoom 1
    /// Resampled nearest into the world layer, the base the painters after
    /// the fog paint over; the graphics card magnifies the scene itself, above
    /// zoom 1, and lays what the painters changed over it.
    magnify,
};

/// How a frame draws the battlefield: its draw scale and the scene it draws into.
///
/// The scene starts at the camera's map pixel, at its own top-left corner,
/// as the battlefield does.
struct WorldScaling {
    /// Scene columns and rows past the battlefield's right and bottom edges,
    /// at least, when the scene is drawn at another scale than the zoom.
    static constexpr int32_t margin = 2;

    /// Scene pixels per map pixel; the zoom when the scene is the battlefield.
    float draw_scale{1.0F};
    int32_t scene_width{};  ///< scene columns
    int32_t scene_height{}; ///< scene rows
    /// The scene is drawn apart from the world layer and becomes it by the
    /// method; otherwise it is the world layer.
    bool apart{};
    SceneMethod method{SceneMethod::none}; ///< none exactly when the scene is not apart
};

/// Returns how a frame draws the battlefield at a zoom.
///
/// Without a draw scale the frame draws at the zoom into the world layer
/// itself, and the scene is the battlefield. A draw scale draws the scene
/// apart from the world layer, resampled nearest into it. At the zoom that
/// scene is the battlefield's size; at another scale it covers the
/// battlefield's map pixels at the draw scale with WorldScaling::margin more
/// columns and rows, rounded up to an even number of each:
/// even(ceil(battlefield * draw_scale / zoom) + margin) in each axis. A draw
/// scale that is not above 0 counts as none, and a zoom that is not above 0
/// gives the battlefield's size.
///
/// @param zoom screen pixels per map pixel
/// @param battlefield_width battlefield columns in screen pixels
/// @param battlefield_height battlefield rows in screen pixels
/// @param draw_scale scene pixels per map pixel to draw the scene at apart from the world layer;
///        none draws at the zoom
/// @return the draw scale and the scene, with SceneMethod::nearest when apart
[[nodiscard]] WorldScaling world_scaling(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    std::optional<float> draw_scale
) noexcept;

/// Scene pixels per battlefield pixel the full scene budget allows, k: the
/// draw scale is at most the zoom times its square root. A provisional
/// figure, chosen without a measurement of the accelerated tier's frames.
inline constexpr double full_budget_scene_ratio = 4.0;
/// Most scene pixels the full scene budget allows, P. A provisional figure.
inline constexpr uint64_t full_budget_scene_pixels = uint64_t{1} << 23;
/// Scene pixels per battlefield pixel the reduced scene budget allows. A provisional figure.
inline constexpr double reduced_budget_scene_ratio = 2.25;
/// Most scene pixels the reduced scene budget allows. A provisional figure.
inline constexpr uint64_t reduced_budget_scene_pixels = uint64_t{1} << 22;
/// The zoom over the draw scale above which the area pass is skipped and
/// the scene drawn at the zoom: the pass would change almost nothing. A
/// provisional figure.
inline constexpr double area_cut_off = 0.9;

/// Returns the draw scale the accelerated tier draws a zoomed-out battlefield at.
///
/// The highest scale within the budget, from the zoom to 1: the zoom times
/// the square root of the scene pixels per battlefield pixel the budget
/// allows, and of its most scene pixels over the battlefield's pixels, at
/// most 1 and at most twice the zoom. The zoom under budget none, and at
/// zoom 1 and above.
///
/// @param zoom screen pixels per map pixel
/// @param battlefield_width battlefield columns in screen pixels
/// @param battlefield_height battlefield rows in screen pixels
/// @param budget the scene budget
/// @return scene pixels per map pixel
[[nodiscard]] float accelerated_draw_scale(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    render_policy::SceneBudget budget
) noexcept;

/// Returns how the accelerated tier draws the battlefield at a zoom.
///
/// Below zoom 1 the scene is drawn at accelerated_draw_scale and averaged
/// into the world layer by the area pass (SceneMethod::area), unless the
/// draw scale is the zoom or the zoom over it is above area_cut_off: the
/// frame then draws at the zoom, as the standard tier does. At zoom 1 the
/// frame draws as the standard tier does. Above zoom 1 with magnify on, the
/// scene is drawn at 1 (SceneMethod::magnify); with it off, at the zoom. A
/// scene drawn apart has world_scaling's size at its draw scale.
///
/// @param zoom screen pixels per map pixel
/// @param battlefield_width battlefield columns in screen pixels
/// @param battlefield_height battlefield rows in screen pixels
/// @param budget the scene budget
/// @param magnify the graphics card magnifies the scene above zoom 1;
///        false is the step-down's magnify-off rung
/// @return the draw scale, the scene and its method
[[nodiscard]] WorldScaling accelerated_world_scaling(
    float zoom,
    int32_t battlefield_width,
    int32_t battlefield_height,
    render_policy::SceneBudget budget,
    bool magnify
) noexcept;

/// Returns the scale the graphics card draws a frame's battlefield at on
/// the display: display pixels per pixel of the picture it draws.
///
/// The draw scale, the scene and its budget come from the layout alone,
/// which on a window at native density is in window points, so they are the
/// same at every density. A magnified scene is drawn at the zoom times the
/// density over its draw scale; the world layer, 1:1 in layout pixels, at
/// the density. The whole-number test that keeps NEAREST
/// (render_policy::chrome_filter) is made on this scale.
///
/// @param scaling how the frame draws the battlefield (accelerated_world_scaling)
/// @param zoom layout pixels per map pixel
/// @param density display pixels per layout pixel: 1 on a window at the
///        window system's density
/// @return display pixels per pixel drawn
[[nodiscard]] double
world_display_scale(const WorldScaling& scaling, float zoom, double density) noexcept;

/// The size of a scene.
struct SceneExtent {
    int32_t width{};  ///< scene columns
    int32_t height{}; ///< scene rows
};

/// Returns the largest scene accelerated_world_scaling gives a magnified
/// frame of a battlefield, over every zoom above 1, which it gives at a zoom
/// just above 1: the battlefield with WorldScaling::margin more columns and
/// rows, rounded up to even sizes.
///
/// @param battlefield_width battlefield columns in screen pixels
/// @param battlefield_height battlefield rows in screen pixels
/// @return the scene's size
[[nodiscard]] SceneExtent
largest_magnified_scene(int32_t battlefield_width, int32_t battlefield_height) noexcept;

/// Returns the scale the area pass reduces a scene drawn at a draw scale by
/// to the battlefield at a zoom: the zoom over the draw scale in 16.16 fixed
/// point, rounded up, so that the scene columns and rows the pass reads
/// never exceed the map pixels the battlefield shows at the draw scale.
///
/// @param zoom screen pixels per map pixel, from half the draw scale to it
/// @param draw_scale scene pixels per map pixel, above 0
/// @return screen pixels per scene pixel, 16.16, from one half to one
[[nodiscard]] uint32_t area_scale(float zoom, float draw_scale) noexcept;

/// Returns how far into a scene the area pass starts the battlefield's
/// picture for a view drawn between map pixels: the view's offset in scene
/// pixels, the offset times the draw scale, in the pass's 16.16 screen
/// pixels, rounded to the nearest and held from 0 to the scale.
///
/// @param offset map pixels the view lies past the camera's map pixel, from 0 to 1
/// @param draw_scale scene pixels per map pixel, above 0
/// @param scale screen pixels per scene pixel, 16.16 (area_scale)
/// @return the phase, 16.16 screen pixels, from 0 to the scale
[[nodiscard]] uint32_t area_phase(double offset, float draw_scale, uint32_t scale) noexcept;

/// Where a magnified frame draws its scene's corner along one axis.
struct MagnifiedSpan {
    uint32_t corner{}; ///< scene columns (or rows) drawn, from the scene's first
    /// Screen pixels from the battlefield's edge the corner lands at: 0, or
    /// below 0, before the edge, for a view drawn between map pixels.
    double start{};
    double extent{}; ///< screen pixels the corner covers
};

/// Returns where a magnified frame draws its scene's corner along one axis.
///
/// A view on its camera's map pixel draws the map pixels the battlefield
/// shows, rounded up, within the scene, landing on the battlefield's edge
/// and covering their screen pixels at the zoom, rounded to the nearest. A
/// view `offset` map pixels past the camera's draws one more column (or
/// row), at the same screen pixels per scene pixel, landing `offset` scene
/// pixels before the edge, which the battlefield's clip cuts off: the same
/// picture moved, whatever the offset, and still reaching past the far edge.
///
/// @param battlefield the battlefield's columns (or rows), in screen pixels
/// @param zoom screen pixels per map pixel, above 1
/// @param scene the scene's columns (or rows), at least one more than the
///        battlefield shows
/// @param offset map pixels the view lies past the camera's map pixel, from 0 to 1
/// @return the corner and where it lands
[[nodiscard]] MagnifiedSpan
magnified_span(uint32_t battlefield, float zoom, uint32_t scene, double offset) noexcept;

/// Returns the most a view drawn between map pixels may lie past its
/// camera's map pixel along one axis: one map pixel, or what is left before
/// the camera's farthest place on the map, so that the view never shows
/// more past the map's far edge than the camera alone shows there.
///
/// @param camera the camera's map pixel along the axis, on the map
/// @param farthest the camera's farthest place on the map along the axis
/// @return map pixels, from 0 to 1
[[nodiscard]] double most_view_offset(int32_t camera, int32_t farthest) noexcept;

/// Returns a view's offset along one axis after a frame's scroll: the
/// offset moved by the scroll's exact travel, less the whole map pixels the
/// camera stepped, held from 0 to the most the view may lie past the
/// camera. The view so follows the scroll smoothly within the camera's map
/// pixel; scrolling toward the start of the map, or held at the edge of
/// the range, it waits there for the camera's next step and never jumps.
///
/// The camera steps on the scroll's carry, which every axis the scroll
/// moves shares, so the scroll's place within the camera's map pixel is the
/// carry past it toward the map's end, and one map pixel less the carry
/// toward its start. A view that trails that place, as an axis's does when
/// it joins a scroll already moving the other, makes up the difference
/// over the frames left before the camera's next step, an even share each
/// frame on top of the travel, and lands on the place at that step instead
/// of jumping there; from then on it moves by the travel alone. A view on
/// the place, as every scroll's that starts from no carry is, never trails
/// it.
///
/// @param offset map pixels the view lay past the camera before the frame, from 0 to 1
/// @param travel map pixels the scroll moved the view in the frame; negative
///        toward the map's start
/// @param carry the fraction of a map pixel the scroll had moved toward the
///        camera's next step before the frame, from 0 to 1
/// @param camera_step whole map pixels the camera moved in the frame
/// @param most the most the view may lie past the camera after the frame (most_view_offset)
/// @return map pixels the view lies past the camera after the frame, from 0 to most
[[nodiscard]] double scrolled_view_offset(
    double offset, double travel, double carry, int32_t camera_step, double most
) noexcept;

/// Returns the offset of a view whose exact place is known, as a zoom
/// eased about an anchor knows it: the exact place less the camera's map
/// pixel, held from 0 to the most the view may lie past the camera. A view
/// before its camera's map pixel is drawn on it.
///
/// @param exact the view's exact place along the axis, in map pixels
/// @param camera the camera's map pixel along the axis
/// @param most the most the view may lie past the camera (most_view_offset)
/// @return map pixels the view lies past the camera, from 0 to most
[[nodiscard]] double view_offset_at(double exact, int32_t camera, double most) noexcept;

} // namespace oa::app
