// SPDX-FileCopyrightText: The Open Annihilation Authors; see COPYRIGHT
// SPDX-License-Identifier: GPL-3.0-only

#pragma once

#include "oa/formats/hpi.hpp"
#include "oa/formats/tnt.hpp"
#include "oa/platform/job_pool.hpp"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace oa::present::world_renderer {

// Pixel-space terrain viewport of the terrain view. source_x/source_y are the
// camera's map-pixel position (Game.camera_x, Game.camera_y); width/height
// are the destination viewport dimensions (Game.viewport_width,
// Game.viewport_height).
struct Viewport {
    uint32_t source_x = 0;
    uint32_t source_y = 0;
    uint32_t width = 0;
    uint32_t height = 0;
};

// Battlefield clip and camera state. The game initializes the destination at
// (128,32) and draws the TNT pixels there without an additional map
// projection. The camera may lie left of or above the map, where the view
// shows past its edges.
struct BattlefieldViewport {
    int32_t source_x = 0; ///< the camera's map column; below 0 left of the map
    int32_t source_y = 0; ///< the camera's map row; below 0 above the map
    int32_t destination_x = 128;
    int32_t destination_y = 32;
    uint32_t width = 0;
    uint32_t height = 0;
    uint32_t surface_width = 0;
    uint32_t surface_height = 0;
    float scale = 1.0F; // screen pixels per map pixel; 1 is the game's unscaled view
};

struct ScreenPoint {
    int32_t x = 0;
    int32_t y = 0;
};

// A map pixel; below 0 left of or above the map.
struct MapPixel {
    int32_t x = 0;
    int32_t y = 0;
};

/// Returns the game's battlefield rectangle on a surface.
///
/// The rectangle starts at (128,32), after the 128-pixel command panel and the
/// 32-pixel top bar, and leaves a 32-pixel bottom strip: 512x416 on a 640x480
/// surface.
///
/// @param source_x camera map-pixel X
/// @param source_y camera map-pixel Y
/// @param surface_width presentation surface width in pixels
/// @param surface_height presentation surface height in pixels
/// @return the viewport, or nullopt for a surface too small to hold the panels
[[nodiscard]] std::optional<BattlefieldViewport> game_battlefield_viewport(
    int32_t source_x, int32_t source_y, uint32_t surface_width, uint32_t surface_height
) noexcept;

struct Surface {
    uint32_t width = 0;
    uint32_t height = 0;
    std::vector<uint8_t> rgb;
};

enum class ErrorCode {
    none,
    invalid_map_model,
    viewport_out_of_bounds,
    output_limit,
};

struct Error {
    ErrorCode code = ErrorCode::none;
    std::string message;
};

struct RenderResult {
    std::optional<Surface> surface;
    std::optional<Error> error;

    /// Returns whether the result holds a surface.
    [[nodiscard]] bool ok() const noexcept { return surface.has_value(); }
};

/// Renders a camera crop of the TNT tile mosaic into RGB.
///
/// Gives the pixels the game's terrain view shows, partial edge tiles
/// included, without depending on SDL or a host graphics API.
///
/// @param map parsed TNT
/// @param game_palette the game palette
/// @param viewport crop in map pixels
/// @return the RGB surface, or an invalid-map, out-of-bounds or output-limit (64 Mi-pixel) error
[[nodiscard]] RenderResult render_viewport(
    const formats::tnt::Map& map, const PaletteBytes& game_palette, const Viewport& viewport
);

/// Renders a destination-sized, scaled sample of the TNT mosaic into RGB.
///
/// Destination pixel (x,y) reads map (source_x + x/scale, source_y + y/scale);
/// cost is dest_width*dest_height regardless of zoom, unlike render_viewport of
/// the visible map crop. Map pixels before or past the mosaic are black.
///
/// @param map parsed TNT
/// @param game_palette the game palette
/// @param source_x map-pixel X of the top-left sample; below 0 left of the map
/// @param source_y map-pixel Y of the top-left sample; below 0 above the map
/// @param dest_width output width in pixels
/// @param dest_height output height in pixels
/// @param scale screen pixels per map pixel; non-positive means 1
/// @return the RGB surface, or an invalid-map or output-limit error
[[nodiscard]] RenderResult render_scaled_viewport(
    const formats::tnt::Map& map,
    const PaletteBytes& game_palette,
    int32_t source_x,
    int32_t source_y,
    uint32_t dest_width,
    uint32_t dest_height,
    float scale
);

/// Rows of the destination each band of fill_scaled_viewport fills.
inline constexpr uint32_t terrain_band_rows = 32;

/// Writes a scaled sample of the TNT mosaic into caller storage without allocating.
///
/// Samples as render_scaled_viewport does, within the map the view shows:
/// the mosaic ends for the fill at shown_width across and shown_height
/// down, where the game never shows a map's last columns and rows, and the
/// pixels past them are black as those past the mosaic, and those left of
/// it and above it, are. The rows are
/// filled in bands of terrain_band_rows rows, on the pool's threads when one
/// is given; every row is the same whichever thread fills it. After an
/// error, which rows were written is not specified.
///
/// @param map parsed TNT
/// @param game_palette the game palette
/// @param source_x map-pixel X of the top-left sample; below 0 left of the map
/// @param source_y map-pixel Y of the top-left sample; below 0 above the map
/// @param shown_width map pixels across the view may show; the mosaic's width or fewer
/// @param shown_height map pixels down the view may show; the mosaic's height or fewer
/// @param dest_width output width in pixels
/// @param dest_height output height in pixels
/// @param scale screen pixels per map pixel; non-positive means 1
/// @param[out] dest_rgb RGB rows of dest_stride_pixels pixels
/// @param dest_stride_pixels destination row stride in pixels, at least dest_width
/// @param pool threads to fill the bands on; null fills them on the calling thread
/// @return an error for a missing destination, a malformed map or a missing tile; nullopt on success
[[nodiscard]] std::optional<Error> fill_scaled_viewport(
    const formats::tnt::Map& map,
    const PaletteBytes& game_palette,
    int32_t source_x,
    int32_t source_y,
    uint32_t shown_width,
    uint32_t shown_height,
    uint32_t dest_width,
    uint32_t dest_height,
    float scale,
    uint8_t* dest_rgb,
    uint32_t dest_stride_pixels,
    platform::job_pool::Pool* pool = nullptr
);

/// The destination pixels along one axis that show the map: from `first`
/// up to `end`; empty when `end` is not past `first`.
struct ShownSpan {
    int32_t first = 0;
    int32_t end = 0;
};

/// Returns the destination pixels along one axis that a scaled sample of
/// the map (fill_scaled_viewport) fills from the shown map, rather than
/// black past its edges.
///
/// Pixel d samples map pixel source + floor(d * 65536 / scale_fp), scale_fp
/// being the scale in 16.16 rounded to nearest, as the fill samples it; it
/// shows the map when that lies from 0 up to `shown`. The span is not cut
/// to any destination: a camera on the map puts `first` before pixel 0.
///
/// @param source map pixel of destination pixel 0; below 0 before the map
/// @param shown map pixels the view shows along the axis
/// @param scale screen pixels per map pixel; non-positive means 1
/// @return the pixels, clamped to the range of int32_t
[[nodiscard]] ShownSpan shown_map_span(int32_t source, uint32_t shown, float scale) noexcept;

/// Renders the battlefield crop into a full presentation surface.
///
/// The crop lands at the same destination origin unit projection and
/// screen/map conversion use; the rest of the surface is black.
///
/// @param map parsed TNT
/// @param palette the game palette
/// @param viewport battlefield rectangle, camera and surface size; the camera on the map
/// @return the RGB surface, or an out-of-bounds (a camera before the map
///         among them), output-limit or render_viewport error
[[nodiscard]] RenderResult render_battlefield_viewport(
    const formats::tnt::Map& map, const PaletteBytes& palette, const BattlefieldViewport& viewport
);

/// Converts a map pixel to a screen point through the battlefield viewport.
///
/// @param viewport battlefield rectangle, camera and scale
/// @param map_pixel map-pixel position
/// @return the screen point, rounded and clamped to int32
[[nodiscard]] ScreenPoint
map_pixel_to_screen(const BattlefieldViewport& viewport, MapPixel map_pixel) noexcept;
/// Converts a screen point inside the battlefield rectangle to a map pixel.
///
/// @param viewport battlefield rectangle, camera and scale
/// @param screen screen point
/// @return the map pixel, or nullopt outside the battlefield rectangle or
///         beyond a signed 32-bit map pixel
[[nodiscard]] std::optional<MapPixel>
screen_to_map_pixel(const BattlefieldViewport& viewport, ScreenPoint screen) noexcept;

/// How far past its camera's map pixel (BattlefieldViewport::source_x and
/// source_y) a view drawn between map pixels lies, in map pixels. A view
/// with no offset is drawn on the camera's map pixel, as the game always
/// draws it.
struct ViewOffset {
    double x = 0.0; ///< map pixels past the camera's column, from 0 to 1
    double y = 0.0; ///< map pixels past the camera's row, from 0 to 1
};

/// Converts a screen point inside the battlefield rectangle to the map pixel
/// a view drawn between map pixels shows there.
///
/// Screen column x shows map column source_x + offset.x + (x - destination_x)
/// / scale, and rows alike; that is rounded to the nearest whole map pixel,
/// halves away from zero, as screen_to_map_pixel rounds. With no offset the
/// map pixel is the one screen_to_map_pixel gives.
///
/// @param viewport battlefield rectangle, camera and scale
/// @param screen screen point
/// @param offset how far past the camera's map pixel the view is drawn, each from 0 to 1
/// @return the map pixel, or nullopt outside the battlefield rectangle or
///         beyond a signed 32-bit map pixel
[[nodiscard]] std::optional<MapPixel> screen_to_map_pixel(
    const BattlefieldViewport& viewport, ScreenPoint screen, ViewOffset offset
) noexcept;

/// Returns how many screen pixels a run of map pixels spans at the viewport's scale.
///
/// Something drawn a fixed number of battlefield pixels wide, such as the
/// square of a pixel particle, spans this many screen pixels in a zoomed view.
///
/// @param viewport battlefield viewport giving the scale; a zero scale is 1
/// @param map_pixels run length in map pixels
/// @return the run times the scale, rounded to the nearest pixel and at least 1
[[nodiscard]] int32_t screen_span(const BattlefieldViewport& viewport, int32_t map_pixels) noexcept;

/// RadarShare::share_flags bit set while the owner shares radar (ShareRadar).
inline constexpr uint8_t share_flag_share_radar = 0x40;

// Radar sharing between the viewing player and a unit's owner (Unit.owner).
// alliance_byte is the owner's Player.alliance entry for the viewer's
// Player.index (written when an alliance changes). share_flags is the owner's
// PlayerSetupInfo.role, reached through Player.info. share_flag_share_radar
// is toggled by the share-radar setting; the other bits are not this test's.
// Zero alliance or a clear ShareRadar bit hides the contact.
struct RadarShare {
    uint8_t alliance_byte = 0;
    uint8_t share_flags = 0;
};

/// Tests whether an allied player's radar contact is shared with the viewer.
///
/// @param share the owner's alliance byte for the viewer and share flags
/// @return true when the alliance byte is nonzero and share_flag_share_radar is set
[[nodiscard]] bool shared_radar_contact(const RadarShare& share) noexcept;

} // namespace oa::present::world_renderer
